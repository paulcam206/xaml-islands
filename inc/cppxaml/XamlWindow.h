#pragma once
#include <winrt/base.h>
#include <winrt/Microsoft.Toolkit.Win32.UI.XamlHost.h>
#include <winrt/Windows.UI.Xaml.h>
#include <Windows.UI.Xaml.Hosting.DesktopWindowXamlSource.h>
#include <mutex>

namespace cppxaml {
    struct XamlWindow;

    struct AppController {
    public:
        void (*OnUICreated)(winrt::Windows::UI::Xaml::UIElement content, XamlWindow* xw) { nullptr };
        LRESULT(*WndProc)(HWND, INT, WPARAM, LPARAM, XamlWindow*) { nullptr };

        AppController(HINSTANCE hInst, winrt::Windows::UI::Xaml::Application xapp) {
            m_hInstance = hInst;
            m_xapp = xapp;
            m_winxamlmanager = winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        }

        HINSTANCE HInstance() const {
            return m_hInstance;
        }

    private:
        winrt::Windows::UI::Xaml::Application m_xapp{ nullptr };
        HINSTANCE m_hInstance{ nullptr };
        winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager m_winxamlmanager{ nullptr };
    };

    struct XamlWindow {
    private:
        XamlWindow(PCWSTR id) : m_Id(id) {}
        winrt::Windows::UI::Xaml::UIElement(*getUI) (const XamlWindow& xw) { nullptr };

        // This DesktopWindowXamlSource is the object that enables a non-UWP desktop application 
        // to host WinRT XAML controls in any UI element that is associated with a window handle (HWND).
        winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource desktopXamlSource{ nullptr };
        HWND hWnd{ nullptr };
        HACCEL hAccelTable{ nullptr };

        AppController* controller{ nullptr };
        std::wstring m_Id;
        std::wstring m_markup;

        static inline std::unordered_map<std::wstring, XamlWindow> s_windows{};
    public:
        XamlWindow(const XamlWindow&) = delete;
        XamlWindow(XamlWindow&& other) noexcept :
            m_Id(std::move(other.m_Id)),
            getUI(std::move(other.getUI)),
            m_markup(std::move(other.m_markup)),
            desktopXamlSource(std::move(other.desktopXamlSource)),
            hWnd(std::move(other.hWnd)),
            hAccelTable(std::move(hAccelTable)),
            controller(std::move(other.controller))
        {}

        HWND hwnd() const {
            return hWnd;
        }

        /// <summary>
        /// Creates a window that hosts a XAML UI element. The template parameter is the XAML type to instantiate, usually a Page.
        /// </summary>
        /// <typeparam name="TUIElement">The XAML class to host - usually a Page.</typeparam>
        /// <param name="id"></param>
        /// <param name="controller"></param>
        /// <returns></returns>
        template<typename TUIElement>
        static XamlWindow& Make(PCWSTR id, AppController* controller = nullptr) {
            XamlWindow xw(id);
            xw.controller = controller;
            xw.getUI = [](const XamlWindow&) { return TUIElement().as<winrt::Windows::UI::Xaml::UIElement>(); };
            const auto& entry = s_windows.emplace(id, std::move(xw));
            return entry.first->second;
        }

        /// <summary>
        /// Creates a window that hosts XAML UI, based on some markup provided as an input parameter.
        /// </summary>
        /// <remarks>
        /// <example>
        ///   <c>auto& xw = cppxaml::XamlWindow::Make(L"MyPage", L"&lt;StackPanel&gt;&lt;TextBlock&gt;Hello&lt;/TextBlock&gt;&lt;/StackPanel&gt;", c);</c>
        /// </example>
        /// </remarks>
        /// 
        /// <param name="id"></param>
        /// <param name="markup">
        /// XAML markup string to use to create the UI.
        /// </param>
        /// <param name="c"></param>
        /// <returns></returns>
        static XamlWindow& Make(PCWSTR id, std::wstring_view markup, AppController* c = nullptr) {
            XamlWindow xw(id);
            xw.controller = c;
            xw.m_markup = markup;
            xw.getUI = [](const XamlWindow& xw) {
                return winrt::Windows::UI::Xaml::Markup::XamlReader::Load(xw.m_markup)
                    .as<winrt::Windows::UI::Xaml::UIElement>();
            };
            const auto& entry = s_windows.emplace(id, std::move(xw));
            return entry.first->second;
        }

        /// <summary>
        /// Creates a window that hosts XAML UI. The UI will be provided by the app.
        /// </summary>
        /// <param name="id"></param>
        /// <param name="getUI">
        /// App-provided function to create UI. This can e.g. instantiate XAML types programmatically.
        /// <example><c>auto& xw = cppxaml::XamlWindow::Make(L"Foo", [](auto&amp;...) { return winrt::Windows::UI::Xaml::Controls::Button(); });</c></example>
        /// </param>
        /// <param name="c"></param>
        /// <returns></returns>
        static XamlWindow& Make(PCWSTR id, winrt::Windows::UI::Xaml::UIElement(*getUI)(const XamlWindow&), AppController* c = nullptr) {
            XamlWindow xw(id);
            xw.controller = c;
            xw.getUI = getUI;
            const auto& entry = s_windows.emplace(id, std::move(xw));
            return entry.first->second;
        }

        std::wstring_view Id() const {
            return m_Id;
        }
        static XamlWindow& Get(PCWSTR id) {
            return s_windows.at(id);
        }
        static constexpr PCWSTR WindowClass() {
            return L"XamlWindow";
        }

        HWND Create(PCWSTR szTitle, DWORD style, HWND parent = nullptr, int width = CW_USEDEFAULT, int height = CW_USEDEFAULT, int nCmdShow = SW_NORMAL) {
            static std::once_flag once;

            std::call_once(once, [ctl = this->controller]() {
                WNDCLASSEXW wcex{};
                wcex.cbSize = sizeof(wcex);

                wcex.style = CS_HREDRAW | CS_VREDRAW;
                wcex.lpfnWndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT
                {
                    // Get handle to the core window.
                    auto xw = reinterpret_cast<XamlWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
                    if (xw == nullptr && message == WM_CREATE) {
                        auto createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);

                        xw = reinterpret_cast<XamlWindow*>(createStruct->lpCreateParams);
                        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(xw));
                    }


                    return xw ? xw->WndProc(hWnd, message, wParam, lParam) : DefWindowProc(hWnd, message, wParam, lParam);
                };

                wcex.cbClsExtra = 0;
                wcex.cbWndExtra = sizeof(XamlWindow*);
                wcex.hInstance = ctl->HInstance();
                wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
                wcex.hbrBackground = 0;
                wcex.lpszMenuName = nullptr;
                wcex.lpszClassName = WindowClass();
                winrt::check_bool(RegisterClassExW(&wcex) != 0);
            });

            desktopXamlSource = winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource();
            auto hWnd = CreateWindowW(WindowClass(), szTitle, style,
                CW_USEDEFAULT, CW_USEDEFAULT, width, height, parent, nullptr, controller->HInstance(), this);
            if (hWnd) {
                ShowWindow(hWnd, nCmdShow);
                UpdateWindow(hWnd);
            }
            return hWnd;
        }

        void SetAcceleratorTable(HACCEL acc) {
            hAccelTable = acc;
        }

        AppController* Controller() const {
            return controller;
        }

        int RunLoop() {
            MSG msg;

            // Main message loop:
            while (GetMessage(&msg, nullptr, 0, 0))
            {
                if (auto xamlSourceNative2 = desktopXamlSource.as<IDesktopWindowXamlSourceNative2>()) {
                    BOOL xamlSourceProcessedMessage = FALSE;
                    winrt::check_hresult(xamlSourceNative2->PreTranslateMessage(&msg, &xamlSourceProcessedMessage));
                    if (xamlSourceProcessedMessage) {
                        continue;
                    }
                }

                if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
            return (int)msg.wParam;
        }

        HWND GetBridgeWindow() const {
            auto interop = desktopXamlSource.as<IDesktopWindowXamlSourceNative>();
            HWND hWndXamlIsland = nullptr;
            winrt::check_hresult(interop->get_WindowHandle(&hWndXamlIsland));
            return hWndXamlIsland;
        }

        LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
            auto interop = desktopXamlSource.as<IDesktopWindowXamlSourceNative>();

            switch (message)
            {
            case WM_CREATE: {
                this->hWnd = hwnd;
                auto createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);

                // Parent the DesktopWindowXamlSource object to the current window.
                winrt::check_hresult(interop->AttachToWindow(hWnd));  // This fails due to access violation!

                auto hWndXamlIsland = GetBridgeWindow();
                SetWindowPos(hWndXamlIsland, nullptr, 0, 0, createStruct->cx, createStruct->cy,
                    SWP_SHOWWINDOW | SWP_NOACTIVATE);

                auto mainPage = this->getUI(*this);
                if (controller && controller->OnUICreated) {
                    controller->OnUICreated(mainPage, this);
                }
                desktopXamlSource.Content(mainPage);
                break;
            }
            case WM_SIZE:
            {
                HWND hWndXamlIsland = nullptr;
                winrt::check_hresult(interop->get_WindowHandle(&hWndXamlIsland));

                SetWindowPos(hWndXamlIsland, nullptr, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_SHOWWINDOW);

                break;
            }
            }
            if (controller && controller->WndProc) {
                return controller->WndProc(hWnd, message, wParam, lParam, this);
            }
            else return DefWindowProc(hWnd, message, wParam, lParam);

        }
    };
}
