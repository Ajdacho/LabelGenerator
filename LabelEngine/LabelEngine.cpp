#include "LabelEngine.h"
#include "json.hpp"
#include <windows.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <set>

#pragma comment(lib, "gdiplus.lib")

namespace fs = std::filesystem;
using json = nlohmann::json;

static constexpr float MM_TO_PT = 2.83464567f;
static float MmToPt(float mm) { return mm * MM_TO_PT; }

struct CustomerStyleConfig {
    std::string bgColor = "#FFFFFF";
    std::string textColor = "#000000";
    std::string logoPosition = "top";
    std::string carePrefix = "care_";
    bool showLogo = true;
    bool showStyle = true;
    bool showProduct = true;
    bool showSize = true;
    bool showCare = true;
    bool showComposition = true;
    bool showLining = true;
};

struct PdfConfig {
    std::vector<std::string> bottomLogos;
    std::vector<std::string> premiumKeywords;
    std::vector<std::string> ignored;
    std::unordered_map<std::string, CustomerStyleConfig> customerStyles;
    std::unordered_map<std::string, std::string> translations;
    std::unordered_map<std::string, std::string> styleTranslations;
    std::unordered_map<std::string, std::string> materialTranslations;
    std::unordered_map<std::string, std::string> excelColumnMapping;
    json layout;
};

struct ExcelRowData {
    std::string productCode, colour, composition, lining, lining2, style, customer, careSymbols, sizes;
};

struct PdfImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> jpegData;
    bool IsValid() const { return !jpegData.empty() && width > 0 && height > 0; }
};

static std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size);
    return wstr;
}

static std::string ConvertUtf8ToCp1250(const std::string& utf8) {
    if (utf8.empty()) return "";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wlen);
    int clen = WideCharToMultiByte(1250, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string cstr(clen, 0);
    WideCharToMultiByte(1250, 0, wstr.c_str(), -1, &cstr[0], clen, NULL, NULL);
    if (!cstr.empty() && cstr.back() == '\0') cstr.pop_back();
    return cstr;
}

static std::string ToUpper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });
    return str;
}

static std::string Trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n\"");
    auto end = str.find_last_not_of(" \t\r\n\"");
    return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
}

static std::string SanitizeFileName(std::string str) {
    std::string invalidChars = "\\/:*?\"<>|";
    for (char& c : str) {
        if (invalidChars.find(c) != std::string::npos) c = '_';
    }
    return str;
}

static std::string EscapePdfString(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '(' || c == ')' || c == '\\') result += '\\';
        result += c;
    }
    return result;
}

static void HexToRgb(const std::string& hex, float& r, float& g, float& b) {
    std::string cleanHex = hex;
    if (!cleanHex.empty() && cleanHex[0] == '#') cleanHex = cleanHex.substr(1);
    if (cleanHex.length() == 6) {
        unsigned int intVal = std::stoul(cleanHex, nullptr, 16);
        r = ((intVal >> 16) & 0xFF) / 255.0f;
        g = ((intVal >> 8) & 0xFF) / 255.0f;
        b = (intVal & 0xFF) / 255.0f;
    }
    else {
        r = 0.0f; g = 0.0f; b = 0.0f;
    }
}

static float GetTextWidth(const std::string& text, int fontSize, bool isBold) {
    static const float helveticaRegular[128] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0.278f, 0.278f, 0.355f, 0.556f, 0.556f, 0.889f, 0.667f, 0.191f, 0.333f, 0.333f, 0.389f, 0.584f, 0.278f, 0.333f, 0.278f, 0.278f,
        0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.278f, 0.278f, 0.584f, 0.584f, 0.584f, 0.556f,
        0.1015f, 0.667f, 0.667f, 0.722f, 0.722f, 0.667f, 0.611f, 0.778f, 0.722f, 0.278f, 0.500f, 0.667f, 0.556f, 0.833f, 0.722f, 0.778f,
        0.667f, 0.778f, 0.722f, 0.667f, 0.611f, 0.722f, 0.667f, 0.944f, 0.667f, 0.667f, 0.611f, 0.278f, 0.278f, 0.278f, 0.469f, 0.556f,
        0.333f, 0.556f, 0.556f, 0.500f, 0.556f, 0.556f, 0.278f, 0.556f, 0.556f, 0.222f, 0.222f, 0.500f, 0.222f, 0.833f, 0.556f, 0.556f,
        0.556f, 0.556f, 0.333f, 0.500f, 0.278f, 0.556f, 0.500f, 0.722f, 0.500f, 0.500f, 0.500f, 0.334f, 0.260f, 0.334f, 0.584f, 0
    };

    static const float helveticaBold[128] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0.278f, 0.333f, 0.474f, 0.556f, 0.556f, 0.889f, 0.722f, 0.238f, 0.333f, 0.333f, 0.389f, 0.584f, 0.278f, 0.333f, 0.278f, 0.278f,
        0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.556f, 0.333f, 0.333f, 0.584f, 0.584f, 0.584f, 0.611f,
        0.975f, 0.722f, 0.722f, 0.722f, 0.722f, 0.667f, 0.611f, 0.778f, 0.722f, 0.278f, 0.556f, 0.722f, 0.611f, 0.833f, 0.722f, 0.778f,
        0.667f, 0.778f, 0.722f, 0.667f, 0.611f, 0.722f, 0.667f, 0.944f, 0.667f, 0.667f, 0.611f, 0.333f, 0.278f, 0.333f, 0.584f, 0.556f,
        0.333f, 0.556f, 0.611f, 0.556f, 0.611f, 0.556f, 0.333f, 0.611f, 0.611f, 0.278f, 0.278f, 0.556f, 0.278f, 0.889f, 0.611f, 0.611f,
        0.611f, 0.611f, 0.389f, 0.556f, 0.333f, 0.611f, 0.556f, 0.778f, 0.556f, 0.556f, 0.500f, 0.389f, 0.280f, 0.389f, 0.584f, 0
    };

    float width = 0.0f;
    for (unsigned char c : text) {
        float charW = 0.55f;
        if (c < 128) {
            charW = isBold ? helveticaBold[c] : helveticaRegular[c];
        }
        else {
            unsigned char mapped = c;
            switch (c) {
            case 140: mapped = 'S'; break;
            case 143: mapped = 'Z'; break;
            case 156: mapped = 's'; break;
            case 159: mapped = 'z'; break;
            case 163: mapped = 'L'; break;
            case 165: mapped = 'A'; break;
            case 175: mapped = 'Z'; break;
            case 179: mapped = 'l'; break;
            case 185: mapped = 'a'; break;
            case 191: mapped = 'z'; break;
            case 198: mapped = 'C'; break;
            case 202: mapped = 'E'; break;
            case 209: mapped = 'N'; break;
            case 211: mapped = 'O'; break;
            case 230: mapped = 'c'; break;
            case 234: mapped = 'e'; break;
            case 241: mapped = 'n'; break;
            case 243: mapped = 'o'; break;
            }
            if (mapped < 128) {
                charW = isBold ? helveticaBold[mapped] : helveticaRegular[mapped];
            }
        }
        width += charW;
    }
    return width * (float)fontSize;
}

static std::vector<std::string> SplitAndFormatMaterials(const std::string& input, const std::unordered_map<std::string, std::string>& customMaterialMap) {
    std::vector<std::string> result;
    std::stringstream ss(input);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = Trim(item);
        if (item.empty()) continue;

        std::string upperItem = ToUpper(item);
        size_t pos = upperItem.find("%");

        std::string pct = "";
        if (pos != std::string::npos) {
            pct = Trim(item.substr(0, pos)) + "%";
        }

        std::string codePart = pos != std::string::npos ? Trim(upperItem.substr(pos + 1)) : upperItem;
        std::string translated = "";

        auto it = customMaterialMap.find(codePart);
        if (it != customMaterialMap.end()) {
            translated = pct.empty() ? it->second : (pct + " " + it->second);
        }
        else {
            bool found = false;
            for (const auto& [key, val] : customMaterialMap) {
                if (!key.empty() && codePart.find(key) != std::string::npos) {
                    translated = pct.empty() ? val : (pct + " " + val);
                    found = true;
                    break;
                }
            }
            if (!found) {
                translated = item;
            }
        }
        result.push_back(Trim(translated));
    }

    if (result.size() > 1) {
        std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
            auto getPct = [](const std::string& s) {
                size_t p = s.find('%');
                if (p != std::string::npos) {
                    try {
                        return std::stof(s.substr(0, p));
                    }
                    catch (...) {}
                }
                return 0.0f;
                };
            return getPct(a) > getPct(b);
            });
    }

    if (result.empty() && !input.empty()) result.push_back(input);
    return result;
}

static std::vector<std::string> SplitSizes(const std::string& input) {
    std::vector<std::string> result;
    std::stringstream ss(input);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = Trim(token);
        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}

static PdfImage LoadImageAsJpeg(const std::wstring& filePath, bool forceWhiteBackground = false) {
    PdfImage img;
    Gdiplus::Bitmap bitmap(filePath.c_str());
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return img;
    img.width = bitmap.GetWidth();
    img.height = bitmap.GetHeight();

    Gdiplus::Bitmap opaqueBitmap(img.width, img.height, PixelFormat24bppRGB);
    Gdiplus::Graphics g(&opaqueBitmap);
    g.Clear(forceWhiteBackground ? Gdiplus::Color(255, 255, 255) : Gdiplus::Color(0, 0, 0));
    g.DrawImage(&bitmap, 0, 0, img.width, img.height);

    CLSID jpgClsid;
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return img;
    std::vector<BYTE> memory(size);
    auto pImageCodecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(memory.data());
    Gdiplus::GetImageEncoders(num, size, pImageCodecs);
    bool found = false;
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecs[j].MimeType, L"image/jpeg") == 0) {
            jpgClsid = pImageCodecs[j].Clsid;
            found = true;
            break;
        }
    }
    if (!found) return img;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(NULL, TRUE, &stream) == S_OK) {
        if (opaqueBitmap.Save(stream, &jpgClsid, NULL) == S_OK) {
            STATSTG statstg;
            stream->Stat(&statstg, STATFLAG_NONAME);
            ULONG streamSize = static_cast<ULONG>(statstg.cbSize.QuadPart);
            img.jpegData.resize(streamSize);
            LARGE_INTEGER li = { 0 };
            stream->Seek(li, STREAM_SEEK_SET, NULL);
            ULONG bytesRead = 0;
            stream->Read(img.jpegData.data(), streamSize, &bytesRead);
        }
        stream->Release();
    }
    return img;
}

static std::string ConvertXlsxToTabCsv(const std::string& inputPath) {
    fs::path p(Utf8ToWstring(inputPath));
    if (ToUpper(p.extension().string()) != ".XLSX") return inputPath;

    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    fs::path tempCsvPath = fs::path(tempDir) / L"label_generator_temp.txt";

    if (fs::exists(tempCsvPath)) {
        fs::remove(tempCsvPath);
    }

    std::wostringstream cmdStream;
    cmdStream << L"powershell -NoProfile -ExecutionPolicy Bypass -Command \""
        << L"try { "
        << L"$excel = New-Object -ComObject Excel.Application; $excel.Visible = $false; $excel.DisplayAlerts = $false; "
        << L"$wb = $excel.Workbooks.Open('" << p.wstring() << L"'); "
        << L"$wb.SaveAs('" << tempCsvPath.wstring() << L"', -4158); "
        << L"$wb.Close($false); $excel.Quit(); "
        << L"} catch { exit 1; }\"";

    std::wstring command = cmdStream.str();

    STARTUPINFOEXW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    std::vector<wchar_t> cmdBuffer(command.begin(), command.end());
    cmdBuffer.push_back(L'\0');

    DWORD exitCode = 0;
    if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si.StartupInfo, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (exitCode != 0 || !fs::exists(tempCsvPath)) {
        return "";
    }

    return tempCsvPath.string();
}

static std::vector<ExcelRowData> ReadExcelCSV(const std::string& filePath, const std::unordered_map<std::string, std::string>& colMap) {
    std::vector<ExcelRowData> rows;
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return rows;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    if (content.empty()) return rows;

    char delimiter = '\t';
    {
        int tabs = 0, semicolons = 0, commas = 0;
        bool q = false;
        for (char c : content) {
            if (c == '\r' || c == '\n') { if (!q) break; }
            else if (c == '"') q = !q;
            else if (!q) {
                if (c == '\t') tabs++;
                else if (c == ';') semicolons++;
                else if (c == ',') commas++;
            }
        }
        if (tabs >= semicolons && tabs >= commas) delimiter = '\t';
        else if (semicolons >= commas) delimiter = ';';
        else delimiter = ',';
    }

    std::vector<std::vector<std::string>> allRows;
    std::vector<std::string> currentRow;
    std::string currentField;
    bool inQuotes = false;
    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        if (c == '"') {
            if (inQuotes && i + 1 < content.size() && content[i + 1] == '"') {
                currentField += '"';
                i++;
            }
            else {
                inQuotes = !inQuotes;
            }
        }
        else if (c == delimiter && !inQuotes) {
            currentRow.push_back(Trim(currentField));
            currentField.clear();
        }
        else if ((c == '\r' || c == '\n') && !inQuotes) {
            if (c == '\r' && i + 1 < content.size() && content[i + 1] == '\n') i++;
            currentRow.push_back(Trim(currentField));
            currentField.clear();
            if (!currentRow.empty() && !(currentRow.size() == 1 && currentRow[0].empty())) allRows.push_back(currentRow);
            currentRow.clear();
        }
        else {
            if (inQuotes && (c == '\r' || c == '\n')) {
                if (currentField.empty() || currentField.back() != ' ') currentField += ' ';
            }
            else {
                currentField += c;
            }
        }
    }
    if (!currentField.empty() || !currentRow.empty()) {
        currentRow.push_back(Trim(currentField));
        if (!currentRow.empty() && !(currentRow.size() == 1 && currentRow[0].empty())) allRows.push_back(currentRow);
    }

    if (allRows.empty()) return rows;

    int colProduct = -1, colColor = -1, colComp = -1, colLining = -1, colLining2 = -1, colStyle = -1, colCustomer = -1, colSizes = -1;

    std::string targetProd = colMap.count("product_code") ? ToUpper(colMap.at("product_code")) : "";
    std::string targetCol = colMap.count("colour") ? ToUpper(colMap.at("colour")) : "";
    std::string targetComp = colMap.count("composition") ? ToUpper(colMap.at("composition")) : "";
    std::string targetLin = colMap.count("lining") ? ToUpper(colMap.at("lining")) : "";
    std::string targetLin2 = colMap.count("lining2") ? ToUpper(colMap.at("lining2")) : "";
    std::string targetStyle = colMap.count("style") ? ToUpper(colMap.at("style")) : "";
    std::string targetCust = colMap.count("customer") ? ToUpper(colMap.at("customer")) : "";
    std::string targetSizes = colMap.count("sizes") ? ToUpper(colMap.at("sizes")) : "";

    const auto& headerCols = allRows[0];
    for (size_t c = 0; c < headerCols.size(); ++c) {
        std::string up = ToUpper(Trim(headerCols[c]));
        if (!targetProd.empty() && up == targetProd) colProduct = (int)c;
        else if (!targetCol.empty() && up == targetCol) colColor = (int)c;
        else if (!targetComp.empty() && up == targetComp) colComp = (int)c;
        else if (!targetLin2.empty() && up == targetLin2) colLining2 = (int)c;
        else if (!targetLin.empty() && up == targetLin) colLining = (int)c;
        else if (!targetStyle.empty() && up == targetStyle) colStyle = (int)c;
        else if (!targetCust.empty() && up == targetCust) colCustomer = (int)c;
        else if (!targetSizes.empty() && up == targetSizes) colSizes = (int)c;
    }

    for (size_t c = 0; c < headerCols.size(); ++c) {
        std::string up = ToUpper(Trim(headerCols[c]));
        if (colProduct == -1 && (up.find("PRODUCT") != std::string::npos || up.find("NAME") != std::string::npos)) colProduct = (int)c;
        else if (colColor == -1 && (up == "COLOUR" || up == "COLOR")) colColor = (int)c;
        else if (colComp == -1 && up == "COMPOSITION") colComp = (int)c;
        else if (colLining2 == -1 && (up == "LINING 2" || up == "LINING2")) colLining2 = (int)c;
        else if (colLining == -1 && up == "LINING") colLining = (int)c;
        else if (colStyle == -1 && up == "STYLE") colStyle = (int)c;
        else if (colCustomer == -1 && (up == "CUSTOMER" || up == "BRAND")) colCustomer = (int)c;
        else if (colSizes == -1 && (up == "SIZES" || up == "SIZE")) colSizes = (int)c;
    }

    if (colProduct == -1) colProduct = 0;
    if (colColor == -1) colColor = 1;
    if (colComp == -1) colComp = 2;
    if (colLining == -1) colLining = 3;
    if (colStyle == -1) colStyle = 4;
    if (colCustomer == -1) colCustomer = 5;

    for (size_t i = 1; i < allRows.size(); ++i) {
        const auto& cols = allRows[i];
        bool hasContent = false;
        for (const auto& col : cols) {
            if (!Trim(col).empty()) {
                hasContent = true;
                break;
            }
        }
        if (!hasContent) continue;

        ExcelRowData row;
        row.productCode = (colProduct >= 0 && colProduct < (int)cols.size()) ? cols[colProduct] : "";
        row.colour = (colColor >= 0 && colColor < (int)cols.size()) ? cols[colColor] : "";
        row.composition = (colComp >= 0 && colComp < (int)cols.size()) ? cols[colComp] : "";
        row.lining = (colLining >= 0 && colLining < (int)cols.size()) ? cols[colLining] : "";
        row.lining2 = (colLining2 >= 0 && colLining2 < (int)cols.size()) ? cols[colLining2] : "";
        row.style = (colStyle >= 0 && colStyle < (int)cols.size()) ? cols[colStyle] : "";
        row.customer = (colCustomer >= 0 && colCustomer < (int)cols.size()) ? cols[colCustomer] : "";
        row.sizes = (colSizes >= 0 && colSizes < (int)cols.size()) ? cols[colSizes] : "";

        rows.push_back(row);
    }
    return rows;
}

static bool CreateLabelPDF(const std::wstring& filePath, const ExcelRowData& data, const std::string& currentSize, const std::string& logosDir, const std::string& iconsDir, const PdfConfig& config, std::set<std::string>& warnings) {
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    float pageW = MmToPt(210.0f);
    float pageH = MmToPt(297.0f);

    std::string upperCustomer = Trim(ToUpper(data.customer));
    std::string upperStyle = Trim(ToUpper(data.style));

    std::vector<std::string> searchKeys = {
        upperCustomer + " AND " + upperStyle,
        upperStyle + " AND " + upperCustomer,
        upperCustomer,
        upperStyle,
        "DEFAULT"
    };

    CustomerStyleConfig styleCfg;
    for (const auto& key : searchKeys) {
        if (config.customerStyles.find(key) != config.customerStyles.end()) {
            styleCfg = config.customerStyles.at(key);
            break;
        }
    }

    bool isBottomLogo = (ToUpper(styleCfg.logoPosition) == "BOTTOM");
    for (const auto& bl : config.bottomLogos) {
        if (!bl.empty() && upperCustomer.find(bl) != std::string::npos) {
            isBottomLogo = true;
            break;
        }
    }

    bool forceWhiteBg = (ToUpper(styleCfg.bgColor) == "#FFFFFF" || styleCfg.bgColor == "white");

    PdfImage logoImg;
    if (styleCfg.showLogo && !logosDir.empty() && !data.customer.empty()) {
        fs::path logoPath = fs::path(Utf8ToWstring(logosDir)) / Utf8ToWstring(data.customer + ".png");
        if (!fs::exists(logoPath)) logoPath = fs::path(Utf8ToWstring(logosDir)) / Utf8ToWstring(data.customer + ".jpg");

        if (fs::exists(logoPath)) {
            logoImg = LoadImageAsJpeg(logoPath.wstring(), forceWhiteBg);
        }
        else {
            warnings.insert("- Brak pliku logotypu klienta: " + data.customer);
        }
    }

    std::vector<PdfImage> careImages;
    if (styleCfg.showCare && !iconsDir.empty()) {
        std::string prefix = styleCfg.carePrefix;
        if (prefix.empty()) prefix = "care_";

        for (int i = 1; i <= 5; ++i) {
            std::string iconName = prefix + std::to_string(i);
            fs::path iconPath = fs::path(Utf8ToWstring(iconsDir)) / Utf8ToWstring(iconName + ".png");
            if (!fs::exists(iconPath)) iconPath = fs::path(Utf8ToWstring(iconsDir)) / Utf8ToWstring(iconName + ".jpg");

            if (fs::exists(iconPath)) {
                PdfImage iconImg = LoadImageAsJpeg(iconPath.wstring(), forceWhiteBg);
                if (iconImg.IsValid()) careImages.push_back(iconImg);
            }
            else {
                warnings.insert("- Brak ikony prania: " + iconName);
            }
        }
    }

    float globalLogoScale = 0.75f;
    for (const auto& block : config.layout) {
        if (block.value("type", "") == "logo") {
            globalLogoScale = block.value("size", 75.0f) / 100.0f;
            break;
        }
    }

    std::stringstream stream;

    float bgR = 1.0f, bgG = 1.0f, bgB = 1.0f;
    HexToRgb(styleCfg.bgColor, bgR, bgG, bgB);
    stream << bgR << " " << bgG << " " << bgB << " rg\n";
    stream << "0 0 " << pageW << " " << pageH << " re f\n";

    float textR = 0.0f, textG = 0.0f, textB = 0.0f;
    HexToRgb(styleCfg.textColor, textR, textG, textB);

    float centerX = pageW / 2.0f;
    float currentY = pageH - MmToPt(30.0f);

    auto AddCenteredText = [&](const std::string& text, int fontSize, float& yPos, bool isBold = false) {
        std::string cp1250Text = ConvertUtf8ToCp1250(text);
        float textW = GetTextWidth(cp1250Text, fontSize, isBold);
        std::string font = isBold ? "/F2" : "/F1";

        stream << textR << " " << textG << " " << textB << " RG " << textR << " " << textG << " " << textB << " rg ";
        stream << "BT " << font << " " << fontSize << " Tf " << (centerX - textW / 2.0f) << " " << yPos << " Td (" << EscapePdfString(cp1250Text) << ") Tj ET\n";
        yPos -= MmToPt(fontSize * 0.35f + 1.5f);
        };

    auto AddMultilineText = [&](const std::string& text, int fontSize, float& yPos, bool isBold = false) {
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line, '\n')) {
            std::string trimmedLine = Trim(line);
            if (!trimmedLine.empty()) {
                AddCenteredText(trimmedLine, fontSize, yPos, isBold);
            }
        }
        };

    for (const auto& block : config.layout) {
        std::string type = block.value("type", "");
        float spacing = block.value("spacing", 4.0f);
        int size = block.value("size", 10);
        bool bold = block.value("bold", false);

        if (type == "logo") {
            if (styleCfg.showLogo && logoImg.IsValid() && !isBottomLogo) {
                float maxLogoW = MmToPt(80.0f * globalLogoScale);
                float maxLogoH = MmToPt(40.0f * globalLogoScale);
                float aspect = (float)logoImg.width / (float)logoImg.height;
                float drawW = maxLogoW;
                float drawH = drawW / aspect;
                if (drawH > maxLogoH) { drawH = maxLogoH; drawW = drawH * aspect; }
                stream << "q " << drawW << " 0 0 " << drawH << " " << (centerX - drawW / 2.0f) << " " << (currentY - drawH) << " cm /ImLogo Do Q\n";
                currentY -= (drawH + MmToPt(8.0f));
            }
            else {
                currentY -= MmToPt(10.0f);
            }
        }
        else if (type == "style") {
            if (styleCfg.showStyle) {
                std::string upperStyleKey = Trim(ToUpper(data.style));
                std::string baseStyleText;
                if (config.styleTranslations.find(upperStyleKey) != config.styleTranslations.end()) {
                    baseStyleText = config.styleTranslations.at(upperStyleKey);
                }
                else if (!data.style.empty()) {
                    baseStyleText = data.style;
                }
                if (!baseStyleText.empty()) {
                    AddMultilineText(baseStyleText, size, currentY, bold);
                }
                currentY -= MmToPt(spacing);
            }
        }
        else if (type == "product") {
            if (styleCfg.showProduct) {
                std::string pref = config.translations.count("series_label") ? config.translations.at("series_label") : "Seria/Series";
                int prefSize = block.value("prefix_size", size);
                if (!pref.empty()) AddCenteredText(pref, prefSize, currentY, bold);

                std::string fullCode = data.productCode;
                if (!data.colour.empty() && fullCode.find(data.colour) == std::string::npos) {
                    fullCode += " " + data.colour;
                }
                AddCenteredText(fullCode, size, currentY, bold);
                currentY -= MmToPt(spacing);
            }
        }
        else if (type == "size") {
            if (styleCfg.showSize && !currentSize.empty()) {
                std::string pref = config.translations.count("size_label") ? config.translations.at("size_label") : "Rozmiar/Size";
                int prefSize = block.value("prefix_size", size);
                if (!pref.empty()) AddCenteredText(pref, prefSize, currentY, bold);
                AddCenteredText(currentSize, size, currentY, bold);
                currentY -= MmToPt(spacing);
            }
        }
        else if (type == "care_header") {
            if (styleCfg.showCare) {
                std::string careText = config.translations.count("care_label") ? config.translations.at("care_label") : "Przepis konserwacji:\nCare instruction:\nPokyny na ošetrovanie:";
                AddMultilineText(careText, size, currentY, bold);
                currentY -= MmToPt(spacing);
            }
        }
        else if (type == "care_symbols") {
            if (styleCfg.showCare && !careImages.empty()) {
                float iconSize = MmToPt(block.value("size", 6.0f));
                float gap = MmToPt(block.value("gap", 2.5f));
                float totalWidth = (careImages.size() * iconSize) + ((careImages.size() - 1) * gap);
                float startX = centerX - (totalWidth / 2.0f);
                currentY -= MmToPt(0.0f);
                for (size_t i = 0; i < careImages.size(); ++i) {
                    stream << "q " << iconSize << " 0 0 " << iconSize << " " << (startX + i * (iconSize + gap)) << " " << (currentY - iconSize) << " cm /ImCare" << i << " Do Q\n";
                }
                currentY -= iconSize;
                currentY -= MmToPt(spacing);
            }
            else {
                currentY -= MmToPt(spacing);
            }
        }
        else if (type == "composition") {
            if (styleCfg.showComposition) {
                std::string pref = config.translations.count("composition_label") ? config.translations.at("composition_label") : "Tkanina/Fabric/Materiál:";
                if (!pref.empty()) AddCenteredText(pref, size, currentY, bold);
                std::vector<std::string> materials = SplitAndFormatMaterials(data.composition, config.materialTranslations);
                for (const auto& mat : materials) {
                    AddCenteredText(mat, size, currentY, bold);
                }
                currentY -= MmToPt(spacing);
            }
        }
        else if (type == "lining") {
            if (styleCfg.showLining) {
                if (!data.lining2.empty()) {
                    std::string pref1 = config.translations.count("lining_1_label") ? config.translations.at("lining_1_label") : "Podszewka 1/Lining 1/Podšívka 1:";
                    if (!pref1.empty()) AddCenteredText(pref1, size, currentY, bold);
                    std::vector<std::string> linings1 = SplitAndFormatMaterials(data.lining, config.materialTranslations);
                    for (const auto& lin : linings1) AddCenteredText(lin, size, currentY, bold);
                    currentY -= MmToPt(spacing);

                    std::string pref2 = config.translations.count("lining_2_label") ? config.translations.at("lining_2_label") : "Podszewka 2/Lining 2/Podšívka 2:";
                    if (!pref2.empty()) AddCenteredText(pref2, size, currentY, bold);
                    std::vector<std::string> linings2 = SplitAndFormatMaterials(data.lining2, config.materialTranslations);
                    for (const auto& lin : linings2) AddCenteredText(lin, size, currentY, bold);
                    currentY -= MmToPt(spacing);
                }
                else if (!data.lining.empty()) {
                    std::string pref = config.translations.count("lining_label") ? config.translations.at("lining_label") : "Podszewka/Lining/Podšívka:";
                    if (!pref.empty()) AddCenteredText(pref, size, currentY, bold);
                    std::vector<std::string> linings = SplitAndFormatMaterials(data.lining, config.materialTranslations);
                    for (const auto& lin : linings) AddCenteredText(lin, size, currentY, bold);
                    currentY -= MmToPt(spacing);
                }
            }
        }
    }

    if (styleCfg.showLogo && logoImg.IsValid() && isBottomLogo) {
        float maxLogoW = MmToPt(80.0f * globalLogoScale);
        float maxLogoH = MmToPt(40.0f * globalLogoScale);
        float aspect = (float)logoImg.width / (float)logoImg.height;
        float drawW = maxLogoW;
        float drawH = drawW / aspect;
        if (drawH > maxLogoH) { drawH = maxLogoH; drawW = drawH * aspect; }

        currentY -= MmToPt(4.0f);
        currentY -= drawH;

        stream << "q " << drawW << " 0 0 " << drawH << " " << (centerX - drawW / 2.0f) << " " << currentY << " cm /ImLogo Do Q\n";
    }

    std::string streamStr = stream.str();
    std::vector<size_t> offsets;
    auto AddObject = [&](std::stringstream& ss) { offsets.push_back((size_t)ss.tellp()); };
    std::stringstream pdf;

    pdf << "%PDF-1.4\n";
    AddObject(pdf);
    pdf << "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    AddObject(pdf);
    pdf << "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";

    int nextObj = 6;
    int logoObjNum = 0;
    if (styleCfg.showLogo && logoImg.IsValid()) logoObjNum = nextObj++;

    std::vector<int> careObjNums;
    for (size_t i = 0; i < careImages.size(); ++i) careObjNums.push_back(nextObj++);

    AddObject(pdf);
    pdf << "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << pageW << " " << pageH << "] " << "/Resources << /Font << /F1 4 0 R /F2 5 0 R >> /XObject << ";
    if (logoObjNum > 0) pdf << "/ImLogo " << logoObjNum << " 0 R ";
    for (size_t i = 0; i < careObjNums.size(); ++i) pdf << "/ImCare" << i << " " << careObjNums[i] << " 0 R ";
    pdf << ">> >> /Contents " << nextObj << " 0 R >>\nendobj\n";

    std::string cp1250Encoding = "<< /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [ 128 /euro 130 /quotesinglbase /florin /quotedblbase /ellipsis /dagger /daggerdbl /circumflex /perthousand /Scaron /guilsinglleft /Sacute /Tcaron /Zcaron /Zacute 145 /quoteleft /quoteright /quotedblleft /quotedblright /bullet /endash /emdash /tilde /trademark /scaron /guilsinglright /sacute /tcaron /zcaron /zacute 161 /caron /breve /Lslash /currency /Aogonek /brokenbar /section /dieresis /copyright /Scommaaccent /guillemotleft /logicalnot /sfthyphen /registered /Zdotaccent 176 /degree /plusminus /ogonek /lslash /acute /mu /paragraph /periodcentered /cedilla /aogonek /scommaaccent /guillemotright /Lcaron /lcaron /zdotaccent 192 /Racute /Aacute /Acircumflex /Abreve /Adieresis /Lacute /Cacute /Ccedilla /Ccaron /Eacute /Eogonek /Edieresis /Ecaron /Iacute /Icircumflex /Dcaron 208 /Dcroat /Nacute /Ncaron /Oacute /Ocircumflex /Ohungarumlaut /Odieresis /multiply /Rcaron /Uring /Uacute /Uhungarumlaut /Udieresis /Yacute /Tcommaaccent /germandbls 224 /racute /aacute /acircumflex /abreve /adieresis /lacute /cacute /ccedilla /ccaron /eacute /eogonek /edieresis /ecaron /iacute /icircumflex /dcaron 240 /dcroat /nacute /ncaron /oacute /ocircumflex /ohungarumlaut /odieresis /divide /rcaron /uring /uacute /uhungarumlaut /udieresis /yacute /tcommaaccent /dotaccent ] >>";
    AddObject(pdf);
    pdf << "4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding " << cp1250Encoding << " >>\nendobj\n";
    AddObject(pdf);
    pdf << "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding " << cp1250Encoding << " >>\nendobj\n";

    if (styleCfg.showLogo && logoImg.IsValid()) {
        AddObject(pdf);
        pdf << logoObjNum << " 0 obj\n<< /Type /XObject /Subtype /Image /Width " << logoImg.width << " /Height " << logoImg.height << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " << logoImg.jpegData.size() << " >>\nstream\n";
        pdf.write(reinterpret_cast<const char*>(logoImg.jpegData.data()), logoImg.jpegData.size());
        pdf << "\nendstream\nendobj\n";
    }

    for (size_t i = 0; i < careImages.size(); ++i) {
        AddObject(pdf);
        pdf << careObjNums[i] << " 0 obj\n<< /Type /XObject /Subtype /Image /Width " << careImages[i].width << " /Height " << careImages[i].height << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " << careImages[i].jpegData.size() << " >>\nstream\n";
        pdf.write(reinterpret_cast<const char*>(careImages[i].jpegData.data()), careImages[i].jpegData.size());
        pdf << "\nendstream\nendobj\n";
    }

    AddObject(pdf);
    pdf << nextObj << " 0 obj\n<< /Length " << streamStr.length() << " >>\nstream\n" << streamStr << "\nendstream\nendobj\n";

    size_t startXref = (size_t)pdf.tellp();
    pdf << "xref\n0 " << (offsets.size() + 1) << "\n0000000000 65535 f \n";
    for (size_t off : offsets) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%010zx 00000 n \n", off);
        pdf << buf;
    }
    pdf << "trailer\n<< /Size " << (offsets.size() + 1) << " /Root 1 0 R >>\nstartxref\n" << startXref << "\n%%EOF\n";

    file << pdf.str();
    file.close();
    return true;
}

extern "C" {
    DLL_API bool GenerateLabels(const char* inputFilePath, const char* logosDir, const char* iconsDir, const char* outputDir, const char* configJsonPath) {
        if (!inputFilePath || !outputDir) return false;

        std::string effectiveCsvPath = ConvertXlsxToTabCsv(inputFilePath);
        if (effectiveCsvPath.empty()) return false;

        PdfConfig pdfConfig;
        std::set<std::string> warnings;

        if (configJsonPath && fs::exists(configJsonPath)) {
            try {
                std::ifstream cFile(configJsonPath);
                json j;
                cFile >> j;
                if (j.contains("excel_column_mapping")) {
                    for (auto& [key, val] : j["excel_column_mapping"].items()) {
                        pdfConfig.excelColumnMapping[key] = val.get<std::string>();
                    }
                }
                if (j.contains("bottom_logos")) {
                    for (auto& item : j["bottom_logos"]) {
                        pdfConfig.bottomLogos.push_back(ToUpper(item.get<std::string>()));
                    }
                }
                if (j.contains("premium_keywords")) {
                    for (auto& item : j["premium_keywords"]) {
                        pdfConfig.premiumKeywords.push_back(ToUpper(item.get<std::string>()));
                    }
                }
                if (j.contains("ignored")) {
                    for (auto& item : j["ignored"]) {
                        pdfConfig.ignored.push_back(ToUpper(item.get<std::string>()));
                    }
                }
                if (j.contains("customer_styles")) {
                    for (auto& [key, val] : j["customer_styles"].items()) {
                        CustomerStyleConfig cs;
                        if (val.contains("bg_color")) cs.bgColor = val["bg_color"].get<std::string>();
                        if (val.contains("text_color")) cs.textColor = val["text_color"].get<std::string>();
                        if (val.contains("logo_position")) cs.logoPosition = val["logo_position"].get<std::string>();
                        if (val.contains("care_prefix")) cs.carePrefix = val["care_prefix"].get<std::string>();
                        if (val.contains("show_logo")) cs.showLogo = val["show_logo"].get<bool>();
                        if (val.contains("show_style")) cs.showStyle = val["show_style"].get<bool>();
                        if (val.contains("show_product")) cs.showProduct = val["show_product"].get<bool>();
                        if (val.contains("show_size")) cs.showSize = val["show_size"].get<bool>();
                        if (val.contains("show_care")) cs.showCare = val["show_care"].get<bool>();
                        if (val.contains("show_composition")) cs.showComposition = val["show_composition"].get<bool>();
                        if (val.contains("show_lining")) cs.showLining = val["show_lining"].get<bool>();
                        pdfConfig.customerStyles[ToUpper(key)] = cs;
                    }
                }
                if (j.contains("translations")) {
                    for (auto& [key, val] : j["translations"].items()) {
                        pdfConfig.translations[key] = val.get<std::string>();
                    }
                }
                if (j.contains("style_translations")) {
                    for (auto& [key, val] : j["style_translations"].items()) {
                        pdfConfig.styleTranslations[ToUpper(key)] = val.get<std::string>();
                    }
                }
                if (j.contains("material_translations")) {
                    for (auto& [key, val] : j["material_translations"].items()) {
                        pdfConfig.materialTranslations[ToUpper(key)] = val.get<std::string>();
                    }
                }
                if (j.contains("layout")) {
                    pdfConfig.layout = j["layout"];
                }
            }
            catch (...) {}
        }

        if (pdfConfig.layout.empty()) {
            pdfConfig.layout = json::parse(R"([
                {"type": "logo", "size": 75.0},
                {"type": "style", "bold": true, "size": 13, "spacing": 4.0},
                {"type": "product", "bold": true, "size": 13, "spacing": 6.0},
                {"type": "size", "bold": true, "size": 13, "spacing": 6.0},
                {"type": "care_header", "bold": false, "size": 10, "spacing": 1.0},
                {"type": "care_symbols", "size": 6.0, "gap": 2.5, "spacing": 6.0},
                {"type": "composition", "bold": false, "size": 10, "spacing": 3.0},
                {"type": "lining", "bold": false, "size": 10, "spacing": 6.0}
            ])");
        }

        ULONG_PTR gdiplusToken;
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        std::vector<ExcelRowData> rows = ReadExcelCSV(effectiveCsvPath, pdfConfig.excelColumnMapping);
        if (rows.empty()) {
            Gdiplus::GdiplusShutdown(gdiplusToken);
            if (effectiveCsvPath != inputFilePath) fs::remove(Utf8ToWstring(effectiveCsvPath));
            return false;
        }

        if (configJsonPath && fs::exists(configJsonPath)) {
            try {
                std::ifstream cFile(configJsonPath);
                json j;
                cFile >> j;
                if (j.contains("logo_mapping")) {
                    auto mapping = j["logo_mapping"];
                    for (auto& row : rows) {
                        std::string upperCust = ToUpper(row.customer);
                        for (auto it = mapping.begin(); it != mapping.end(); ++it) {
                            if (ToUpper(it.key()) == upperCust) {
                                row.customer = it.value().get<std::string>();
                                break;
                            }
                        }
                    }
                }
                if (j.contains("style_mapping")) {
                    auto mapping = j["style_mapping"];
                    for (auto& row : rows) {
                        std::string upperStyle = ToUpper(row.style);
                        for (auto it = mapping.begin(); it != mapping.end(); ++it) {
                            if (ToUpper(it.key()) == upperStyle) {
                                row.style = it.value().get<std::string>();
                                break;
                            }
                        }
                    }
                }
            }
            catch (...) {}
        }

        fs::path baseOutputDir(Utf8ToWstring(outputDir));
        fs::path genLabelsDir = baseOutputDir / L"generated_labels";
        fs::path genLabelsSortedDir = baseOutputDir / L"generated_labels_sorted";
        fs::create_directories(genLabelsDir);
        fs::create_directories(genLabelsSortedDir);

        int generatedCount = 0;
        int globalPdfCounter = 1;

        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            int excelRowNumber = static_cast<int>(i) + 2;

            std::vector<std::string> skipReasons;
            std::string upperStyle = ToUpper(row.style);
            std::string upperCust = ToUpper(row.customer);
            std::string upperProd = ToUpper(row.productCode);
            std::string upperCol = ToUpper(row.colour);

            if (upperStyle == "TOTAL" || upperStyle == "FABRIC") {
                skipReasons.push_back("Niewłaściwy styl ('" + row.style + "')");
            }
            else {
                for (const auto& ign : pdfConfig.ignored) {
                    if (!ign.empty() && (
                        upperStyle.find(ign) != std::string::npos ||
                        upperCust.find(ign) != std::string::npos ||
                        upperProd.find(ign) != std::string::npos ||
                        upperCol.find(ign) != std::string::npos)) {
                        skipReasons.push_back("Pasuje do reguły 'ignored' ('" + ign + "')");
                        break;
                    }
                }
            }

            if (row.productCode.empty()) {
                skipReasons.push_back("Brak kodu produktu (NAME)");
            }
            if (row.customer.empty()) {
                skipReasons.push_back("Brak nazwy klienta (CUSTOMER)");
            }

            if (!skipReasons.empty()) {
                std::string reasonStr;
                for (size_t j = 0; j < skipReasons.size(); ++j) {
                    reasonStr += skipReasons[j];
                    if (j < skipReasons.size() - 1) reasonStr += ", ";
                }
                std::string prod = row.productCode.empty() ? "N/A" : row.productCode;
                std::string cust = row.customer.empty() ? "N/A" : row.customer;

                warnings.insert("[WIERSZ " + std::to_string(excelRowNumber) + "] Pominięto etykietę (Produkt: " + prod + ", Klient: " + cust + "). Powód: " + reasonStr);
                continue;
            }

            std::vector<std::string> sizeList = SplitSizes(row.sizes);
            if (sizeList.empty()) {
                sizeList.push_back("");
            }

            for (const auto& currentSize : sizeList) {
                std::string customerFolderName = row.customer.empty() ? "INNI_KLIENCI" : ToUpper(row.customer);
                customerFolderName = SanitizeFileName(customerFolderName);

                std::string labelFileName = row.productCode;
                if (!row.colour.empty()) labelFileName += "_" + row.colour;
                if (!currentSize.empty()) labelFileName += "-size-" + currentSize;
                labelFileName = SanitizeFileName(labelFileName);

                fs::path clientFolder = genLabelsDir / Utf8ToWstring(customerFolderName);
                fs::create_directories(clientFolder);

                fs::path pdfPath1 = clientFolder / Utf8ToWstring(labelFileName + ".pdf");
                if (CreateLabelPDF(pdfPath1.wstring(), row, currentSize, logosDir, iconsDir, pdfConfig, warnings)) {
                    generatedCount++;
                }
                else {
                    warnings.insert("[ZAPIS] Błąd zapisu pliku PDF: " + labelFileName + " (Klient: " + customerFolderName + ")");
                }

                std::stringstream ssSorted;
                ssSorted << std::setw(4) << std::setfill('0') << globalPdfCounter++ << "_" << labelFileName << ".pdf";
                fs::path pdfPath2 = genLabelsSortedDir / Utf8ToWstring(ssSorted.str());

                if (!CreateLabelPDF(pdfPath2.wstring(), row, currentSize, logosDir, iconsDir, pdfConfig, warnings)) {
                    warnings.insert("[ZAPIS] Błąd zapisu posortowanego pliku PDF: " + ssSorted.str());
                }
            }
        }

        fs::path reportPath = baseOutputDir / L"log.txt";
        if (!warnings.empty()) {
            std::ofstream reportFile(reportPath, std::ios::binary);
            if (reportFile.is_open()) {
                std::string header = "--- RAPORT OSTRZEŻEŃ ---\r\n";
                reportFile.write(header.c_str(), header.size());
                for (const auto& w : warnings) {
                    std::string line = w + "\r\n";
                    reportFile.write(line.c_str(), line.size());
                }
                reportFile.close();
            }
        }
        else {
            if (fs::exists(reportPath)) {
                fs::remove(reportPath);
            }
        }

        Gdiplus::GdiplusShutdown(gdiplusToken);
        if (effectiveCsvPath != inputFilePath) fs::remove(Utf8ToWstring(effectiveCsvPath));
        return generatedCount > 0;
    }

    DLL_API bool GenerateLabelsFromCSV(const char* csvPath, const char* configJsonPath, const char* outputDir) {
        return GenerateLabels(csvPath, "", "", outputDir, configJsonPath);
    }
}