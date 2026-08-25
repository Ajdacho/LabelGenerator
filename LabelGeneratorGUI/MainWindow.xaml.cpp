#include "pch.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Interop.h>

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "LabelEngine.h"

#pragma warning(push)
#pragma warning(disable: 26819)
#include "json.hpp"
#pragma warning(pop)

#include <shobjidl.h>
#include <shellapi.h>
#include <microsoft.ui.xaml.window.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "shell32.lib")

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;
using json = nlohmann::json;

static std::filesystem::path GetExeDirPath()
{
    char exePathBuf[MAX_PATH];
    GetModuleFileNameA(NULL, exePathBuf, MAX_PATH);
    return std::filesystem::path(exePathBuf).parent_path();
}

static std::filesystem::path GetSettingsFilePath()
{
    return GetExeDirPath() / "settings.json";
}

static void SaveSettingKey(const std::string& key, const std::string& value)
{
    try
    {
        auto path = GetSettingsFilePath();
        json j;
        std::ifstream inFile(path);
        if (inFile.is_open())
        {
            inFile >> j;
            inFile.close();
        }
        j[key] = value;
        std::ofstream outFile(path);
        outFile << j;
    }
    catch (...) {}
}

static std::string FloatToStrClean(float v) {
    std::string s = std::to_string(v);
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

namespace winrt::LabelGeneratorGUI::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        this->ExtendsContentIntoTitleBar(true);
        this->SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());

        int width = 800;
        int height = 680;

        auto appWindow = this->AppWindow();
        auto displayArea = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
            appWindow.Id(), Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);

        if (displayArea)
        {
            auto workArea = displayArea.WorkArea();
            int x = workArea.X + (workArea.Width - width) / 2;
            int y = workArea.Y + (workArea.Height - height) / 2;
            appWindow.MoveAndResize({ x, y, width, height });
        }
        else
        {
            appWindow.Resize({ width, height });
        }

        std::wstring iconPath = (GetExeDirPath() / L"Assets" / L"app.ico").wstring();
        try
        {
            this->AppWindow().SetIcon(iconPath);
        }
        catch (...) {}

        LoadSavedSettings();

        if (auto rootElement = this->Content().try_as<Microsoft::UI::Xaml::FrameworkElement>())
        {
            rootElement.Loaded([this](IInspectable const&, RoutedEventArgs const&)
                {
                    CheckFirstRunAsync();
                });
        }
    }

    HWND MainWindow::GetWindowHandle()
    {
        auto windowNative = this->try_as<::IWindowNative>();
        HWND hwnd{ nullptr };
        if (windowNative) windowNative->get_WindowHandle(&hwnd);
        return hwnd;
    }

    void MainWindow::LoadSavedSettings()
    {
        try
        {
            std::ifstream file(GetSettingsFilePath());
            if (file.is_open())
            {
                json j;
                file >> j;

                if (j.contains("DataFilePath"))
                {
                    m_dataFilePath = winrt::to_hstring(j["DataFilePath"].get<std::string>());
                    dataFileTextBox().Text(m_dataFilePath);
                }
                if (j.contains("LogosFolderPath"))
                {
                    m_logosFolderPath = winrt::to_hstring(j["LogosFolderPath"].get<std::string>());
                    logosFolderTextBox().Text(m_logosFolderPath);
                }
                if (j.contains("IconsFolderPath"))
                {
                    m_iconsFolderPath = winrt::to_hstring(j["IconsFolderPath"].get<std::string>());
                    iconsFolderTextBox().Text(m_iconsFolderPath);
                }
                if (j.contains("OutputFolderPath"))
                {
                    m_outputFolderPath = winrt::to_hstring(j["OutputFolderPath"].get<std::string>());
                    outputFolderTextBox().Text(m_outputFolderPath);
                }
                if (j.contains("AutoUpdateEnabled"))
                {
                    bool enabled = j["AutoUpdateEnabled"].get<bool>();
                    AutoUpdateCheckBox().IsChecked(enabled);
                    if (enabled)
                    {
                        updateCheckmarkBadge().Visibility(Visibility::Visible);
                    }
                }
            }
        }
        catch (...) {}

        UpdateGenerateButtonState();
    }

    winrt::fire_and_forget MainWindow::CheckFirstRunAsync()
    {
        try
        {
            bool asked = false;
            std::ifstream file(GetSettingsFilePath());
            if (file.is_open())
            {
                json j;
                file >> j;
                if (j.contains("AutoUpdateAsked"))
                {
                    asked = j["AutoUpdateAsked"].get<bool>();
                }
            }

            if (!asked)
            {
                auto xamlRoot = this->Content().XamlRoot();
                if (!xamlRoot) co_return;

                FirstRunDialog().XamlRoot(xamlRoot);
                auto result = co_await FirstRunDialog().ShowAsync();
                bool enable = (result == winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary);

                auto path = GetSettingsFilePath();
                json j;
                std::ifstream inFile(path);
                if (inFile.is_open())
                {
                    inFile >> j;
                    inFile.close();
                }
                j["AutoUpdateAsked"] = true;
                j["AutoUpdateEnabled"] = enable;
                std::ofstream outFile(path);
                outFile << j;

                AutoUpdateCheckBox().IsChecked(enable);
                if (enable)
                {
                    updateCheckmarkBadge().Visibility(Visibility::Visible);
                }
            }
        }
        catch (...) {}
    }

    void MainWindow::UpdateGenerateButtonState()
    {
        bool ready = !m_dataFilePath.empty() &&
            !m_logosFolderPath.empty() &&
            !m_iconsFolderPath.empty() &&
            !m_outputFolderPath.empty();

        generateButton().IsEnabled(ready);
    }

    Windows::Foundation::IAsyncAction MainWindow::selectDataFileButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        FileOpenPicker picker;
        picker.as<IInitializeWithWindow>()->Initialize(GetWindowHandle());
        picker.FileTypeFilter().Append(L".xlsx");
        picker.FileTypeFilter().Append(L".csv");

        StorageFile file = co_await picker.PickSingleFileAsync();
        if (file)
        {
            m_dataFilePath = file.Path();
            dataFileTextBox().Text(m_dataFilePath);
            SaveSettingKey("DataFilePath", winrt::to_string(m_dataFilePath));
            UpdateGenerateButtonState();
        }
    }

    Windows::Foundation::IAsyncAction MainWindow::selectLogosFolderButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        FolderPicker picker;
        picker.as<IInitializeWithWindow>()->Initialize(GetWindowHandle());
        picker.FileTypeFilter().Append(L"*");

        StorageFolder folder = co_await picker.PickSingleFolderAsync();
        if (folder)
        {
            m_logosFolderPath = folder.Path();
            logosFolderTextBox().Text(m_logosFolderPath);
            SaveSettingKey("LogosFolderPath", winrt::to_string(m_logosFolderPath));
            UpdateGenerateButtonState();
        }
    }

    Windows::Foundation::IAsyncAction MainWindow::selectIconsFolderButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        FolderPicker picker;
        picker.as<IInitializeWithWindow>()->Initialize(GetWindowHandle());
        picker.FileTypeFilter().Append(L"*");

        StorageFolder folder = co_await picker.PickSingleFolderAsync();
        if (folder)
        {
            m_iconsFolderPath = folder.Path();
            iconsFolderTextBox().Text(m_iconsFolderPath);
            SaveSettingKey("IconsFolderPath", winrt::to_string(m_iconsFolderPath));
            UpdateGenerateButtonState();
        }
    }

    Windows::Foundation::IAsyncAction MainWindow::selectOutputFolderButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        FolderPicker picker;
        picker.as<IInitializeWithWindow>()->Initialize(GetWindowHandle());
        picker.FileTypeFilter().Append(L"*");

        StorageFolder folder = co_await picker.PickSingleFolderAsync();
        if (folder)
        {
            m_outputFolderPath = folder.Path();
            outputFolderTextBox().Text(m_outputFolderPath);
            SaveSettingKey("OutputFolderPath", winrt::to_string(m_outputFolderPath));
            UpdateGenerateButtonState();
        }
    }

    winrt::fire_and_forget MainWindow::configButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto xamlRoot = this->Content().XamlRoot();
        if (!xamlRoot) co_return;

        std::string configPath = (GetExeDirPath() / "config.json").string();

        LogoSizeTextBox().Text(L"75");

        try {
            std::ifstream cFile(configPath);
            json j;
            if (cFile.is_open()) {
                cFile >> j;

                if (j.contains("ignored")) {
                    std::string ignored;
                    for (auto& item : j["ignored"]) {
                        if (!ignored.empty()) ignored += ", ";
                        ignored += item.get<std::string>();
                    }
                    IgnoredStylesTextBox().Text(winrt::to_hstring(ignored));
                }

                if (j.contains("layout")) {
                    for (auto& block : j["layout"]) {
                        std::string type = block.value("type", "");
                        if (type == "care_symbols" && block.contains("size")) {
                            CareSymbolsSizeTextBox().Text(winrt::to_hstring(FloatToStrClean(block["size"].get<float>())));
                        }
                        else if (type == "product" && block.contains("size")) {
                            ProductTextSizeTextBox().Text(winrt::to_hstring(std::to_string(block["size"].get<int>())));
                        }
                        else if (type == "composition" && block.contains("size")) {
                            CompositionTextSizeTextBox().Text(winrt::to_hstring(std::to_string(block["size"].get<int>())));
                        }
                        else if (type == "logo" && block.contains("size")) {
                            LogoSizeTextBox().Text(winrt::to_hstring(FloatToStrClean(block["size"].get<float>())));
                        }
                    }
                }
            }
        }
        catch (...) {}

        ConfigDialog().XamlRoot(xamlRoot);
        co_await ConfigDialog().ShowAsync();
    }

    void MainWindow::OpenRawConfigButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::wstring configPath = (GetExeDirPath() / "config.json").wstring();
        ShellExecuteW(NULL, L"open", configPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }

    void MainWindow::ConfigDialog_PrimaryButtonClick(Microsoft::UI::Xaml::Controls::ContentDialog const&, Microsoft::UI::Xaml::Controls::ContentDialogButtonClickEventArgs const&)
    {
        std::string configPath = (GetExeDirPath() / "config.json").string();

        try {
            std::ifstream cFile(configPath);
            json j;
            if (cFile.is_open()) {
                cFile >> j;
                cFile.close();
            }

            std::string ignoredStr = winrt::to_string(IgnoredStylesTextBox().Text());
            std::vector<std::string> ignoredList;
            std::stringstream ss(ignoredStr);
            std::string item;
            while (std::getline(ss, item, ',')) {
                auto start = item.find_first_not_of(" \t");
                auto end = item.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    ignoredList.push_back(item.substr(start, end - start + 1));
                }
            }
            j["ignored"] = ignoredList;

            if (j.contains("layout")) {
                for (auto& block : j["layout"]) {
                    std::string type = block.value("type", "");
                    try {
                        if (type == "care_symbols" && block.contains("size")) {
                            block["size"] = std::stof(winrt::to_string(CareSymbolsSizeTextBox().Text()));
                        }
                        else if (type == "product" && block.contains("size")) {
                            block["size"] = std::stoi(winrt::to_string(ProductTextSizeTextBox().Text()));
                        }
                        else if (type == "composition" && block.contains("size")) {
                            block["size"] = std::stoi(winrt::to_string(CompositionTextSizeTextBox().Text()));
                        }
                        else if (type == "logo") {
                            block["size"] = std::stof(winrt::to_string(LogoSizeTextBox().Text()));
                        }
                    }
                    catch (...) {}
                }
            }

            std::ofstream oFile(configPath);
            oFile << std::setw(2) << j << std::endl;
        }
        catch (...) {}

        try
        {
            auto path = GetSettingsFilePath();
            json j;
            std::ifstream inFile(path);
            if (inFile.is_open())
            {
                inFile >> j;
                inFile.close();
            }
            bool enabled = AutoUpdateCheckBox().IsChecked().Value();
            j["AutoUpdateEnabled"] = enabled;
            std::ofstream outFile(path);
            outFile << j;

            if (enabled)
            {
                updateCheckmarkBadge().Visibility(Visibility::Visible);
            }
            else
            {
                updateCheckmarkBadge().Visibility(Visibility::Collapsed);
            }
        }
        catch (...) {}
    }

    void MainWindow::OpenFolderButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_outputFolderPath.empty())
        {
            ShellExecuteW(NULL, L"open", m_outputFolderPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }

    winrt::fire_and_forget MainWindow::checkForUpdatesButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ResultInfoBar().IsOpen(true);
        ResultInfoBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Informational);
        ResultInfoBar().Title(L"Sprawdzanie aktualizacji");
        ResultInfoBar().Message(L"Posiadasz najnowszą wersję programu.");
        ShowReportButton().Visibility(Visibility::Collapsed);
        OpenFolderButton().Visibility(Visibility::Collapsed);

        updateCheckmarkBadge().Visibility(Visibility::Visible);

        co_return;
    }

    winrt::fire_and_forget MainWindow::generateButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        generationProgressBar().Visibility(Visibility::Visible);
        generateButton().IsEnabled(false);
        ResultInfoBar().IsOpen(false);

        std::string dataPath = winrt::to_string(m_dataFilePath);
        std::string logosPath = winrt::to_string(m_logosFolderPath);
        std::string iconsPath = winrt::to_string(m_iconsFolderPath);
        std::string outputPath = winrt::to_string(m_outputFolderPath);

        std::string configPath = (GetExeDirPath() / "config.json").string();

        co_await winrt::resume_background();

        bool success = GenerateLabels(
            dataPath.c_str(),
            logosPath.c_str(),
            iconsPath.c_str(),
            outputPath.c_str(),
            configPath.c_str()
        );

        std::filesystem::path reportPath = std::filesystem::path(m_outputFolderPath.c_str()) / L"log.txt";
        std::wstring reportContent = L"";
        bool hasWarnings = false;

        if (std::filesystem::exists(reportPath)) {
            std::ifstream file(reportPath);
            if (file.is_open()) {
                std::string str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                reportContent = winrt::to_hstring(str).c_str();
                if (!reportContent.empty()) {
                    hasWarnings = true;
                }
            }
        }

        this->DispatcherQueue().TryEnqueue([this, success, hasWarnings, reportContent]()
            {
                generationProgressBar().Visibility(Visibility::Collapsed);
                generateButton().IsEnabled(true);
                m_lastReportContent = reportContent;

                ResultInfoBar().IsOpen(true);
                OpenFolderButton().Visibility(Visibility::Visible);

                if (!success && !hasWarnings) {
                    ResultInfoBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
                    ResultInfoBar().Title(L"Błąd generowania");
                    ResultInfoBar().Message(L"Nie udało się otworzyć pliku. Upewnij się, że plik nie jest używany. Jeśli nie masz zainstalowanego programu Excel, ręcznie zapisz listę jako plik .csv i użyj go jako źródła danych.");
                    ShowReportButton().Visibility(Visibility::Collapsed);
                }
                else if (success && hasWarnings) {
                    ResultInfoBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Warning);
                    ResultInfoBar().Title(L"Ostrzeżenia");
                    ResultInfoBar().Message(L"Wygenerowano metki, ale wykryto ostrzeżenia. W folderze docelowym utworzono plik log.txt.");
                    ShowReportButton().Visibility(Visibility::Visible);
                }
                else {
                    ResultInfoBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Success);
                    ResultInfoBar().Title(L"Sukces");
                    ResultInfoBar().Message(L"Pomyślnie wygenerowano wszystkie metki.");
                    ShowReportButton().Visibility(Visibility::Collapsed);
                }
            });
    }

    winrt::fire_and_forget MainWindow::ShowReportButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto xamlRoot = this->Content().XamlRoot();
        if (!xamlRoot) co_return;

        Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(xamlRoot);
        dialog.Title(box_value(L"Szczegółowy raport ostrzeżeń"));

        Microsoft::UI::Xaml::Controls::TextBlock textBlock;
        textBlock.Text(m_lastReportContent.c_str());
        textBlock.TextWrapping(Microsoft::UI::Xaml::TextWrapping::Wrap);
        textBlock.FontFamily(Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));

        Microsoft::UI::Xaml::Controls::ScrollViewer scrollViewer;
        scrollViewer.VerticalScrollBarVisibility(Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Auto);
        scrollViewer.HorizontalScrollBarVisibility(Microsoft::UI::Xaml::Controls::ScrollBarVisibility::Disabled);
        scrollViewer.MaxHeight(450);
        scrollViewer.Content(textBlock);

        dialog.Content(scrollViewer);
        dialog.CloseButtonText(L"Zamknij");
        co_await dialog.ShowAsync();
    }
}