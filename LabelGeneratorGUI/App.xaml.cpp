/* ==============================================================================
 * Author: Filip Cieśla
 *
 * Description:
 * LabelGeneratorGUI is a WinUI 3 desktop application designed to automate the
 * generation of print-ready PDF product and care labels. It parses bulk data
 * directly from Excel spreadsheets and applies dynamic styling based on a
 * flexible JSON configuration. The software allows for advanced layout
 * customization and style overrides per customer and product type.
 *
 * Required External Libraries:
 * - nlohmann/json (for parsing config.json)
 * - OpenXLSX (for reading .xlsx spreadsheet data)
 * - libharu (for rendering and exporting PDF files)
 * ============================================================================== */

#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::LabelGeneratorGUI::implementation
{
    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
            {
                if (IsDebuggerPresent())
                {
                    auto errorMessage = e.Message();
                    __debugbreak();
                }
            });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        window = make<MainWindow>();
        window.Activate();
    }
}