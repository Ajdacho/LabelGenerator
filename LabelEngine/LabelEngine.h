#pragma once
#include <string>

#ifdef _WIN32
#define DLL_API __declspec(dllexport)
#else
#define DLL_API
#endif

extern "C" {
    DLL_API bool GenerateLabels(const char* inputFilePath, const char* logosDir, const char* iconsDir, const char* outputDir, const char* configJsonPath);
    DLL_API bool GenerateLabelsFromCSV(const char* csvPath, const char* configJsonPath, const char* outputDir);
}