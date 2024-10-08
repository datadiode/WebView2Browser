// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "BrowserWindow.h"
#include "shlobj.h"
#include <WebView2EnvironmentOptions.h>
#include <Urlmon.h>
#pragma comment (lib, "Urlmon.lib")
#include <wincrypt.h>

using namespace Microsoft::WRL;

template<typename pfn> struct DllImport {
    FARPROC const p;
    operator pfn() const { return reinterpret_cast<pfn>(p); }
};

static struct user32_dll {
    HMODULE dll;
    DllImport<UINT(WINAPI*)(HWND)> GetDpiForWindow;
} const user32 = {
    GetModuleHandleW(L"user32"),
    GetProcAddress(user32.dll, "GetDpiForWindow"),
};

WCHAR BrowserWindow::s_windowClass[] = { 0 };
WCHAR BrowserWindow::s_title[] = { 0 };

// encoding function
static std::wstring_convert<std::codecvt_utf8<wchar_t>> utf8;
static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> utf8_utf16; 

static std::string to_utf8(std::wstring& wide_string) { return utf8.to_bytes(wide_string); }
static std::wstring to_wstring(std::string &string) { return utf8_utf16.from_bytes(string); }

static BYTE const *MD5(BYTE const *data, DWORD len, BYTE *hash = static_cast<BYTE *>(_alloca(17)))
{
    DWORD cbHash = 17;
    HCRYPTPROV hProv = 0;
    if (CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        HCRYPTPROV hHash = 0;
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
        {
            CryptHashData(hHash, data, len, 0);
            CryptGetHashParam(hHash, HP_HASHVAL, hash, &cbHash, 0);
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    return cbHash == 16 ? hash : nullptr;
}

//
//  FUNCTION: RegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM BrowserWindow::RegisterClass(HINSTANCE hInstance, COPYDATASTRUCT const &cds)
{
    // Initialize window class string
    if (int const len = LoadStringW(hInstance, IDC_WEBVIEWBROWSERAPP, s_windowClass, MAX_LOADSTRING - 40))
    {
        if (BYTE const *const hash = cds.cbData ? MD5(static_cast<BYTE *>(cds.lpData), cds.cbData) : nullptr)
        {
            StringFromGUID2(*reinterpret_cast<GUID const *>(hash), s_windowClass + len, 40);
        }
    }

    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProcStatic;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WEBVIEWBROWSERAPP));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_WEBVIEWBROWSERAPP);
    wcex.lpszClassName = s_windowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//  FUNCTION: WndProcStatic(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Redirect messages to approriate instance or call default proc
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK BrowserWindow::WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Get the ptr to the BrowserWindow instance who created this hWnd.
    // The pointer was set when the hWnd was created during InitInstance.
    BrowserWindow* browser_window = reinterpret_cast<BrowserWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (browser_window != nullptr)
    {
        return browser_window->WndProc(hWnd, message, wParam, lParam);  // Forward message to instance-aware WndProc
    }
    else
    {
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for each browser window instance.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK BrowserWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{

    switch (message)
    {
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* minmax = reinterpret_cast<MINMAXINFO*>(lParam);
        minmax->ptMinTrackSize.x = m_minWindowWidth;
        minmax->ptMinTrackSize.y = m_minWindowHeight;
    }
    break;
    case WM_DPICHANGED:
    {
        UpdateMinWindowSize();
    }
    case WM_SIZE:
    {
        ResizeUIWebViews();
        if (m_tabs.find(m_activeTabId) != m_tabs.end())
        {
            m_tabs.at(m_activeTabId)->ResizeWebView();
        }
    }
    break;
    case WM_CLOSE:
    {
        nlohmann::json jsonObj;
        jsonObj["message"] = MG_CLOSE_WINDOW;
        jsonObj["args"] = nullptr;

        CheckFailure(PostJsonToWebView(jsonObj, m_controlsWebView.Get()), L"Try again.");
    }
    break;
    case WM_NCDESTROY:
    {
        SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
        delete this;
        PostQuitMessage(0);
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_COPYDATA:
    {
        COPYDATASTRUCT *const pcds = reinterpret_cast<COPYDATASTRUCT *>(lParam);
        if (pcds->dwData == 1)
        {
            if (m_tabs.count(m_activeTabId) == 0)
                return IDRETRY;
            m_tabs.at(m_activeTabId)->m_contentWebView->Navigate(static_cast<LPWSTR>(pcds->lpData));
            return IDOK;
        }
    }
    break;
    default:
    {
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    break;
    }
    return 0;
}


BOOL BrowserWindow::LaunchWindow(_In_ HINSTANCE hInstance, LPCWSTR lpCmdLine, _In_ int nCmdShow)
{
    // BrowserWindow keeps a reference to itself in its host window and will
    // delete itself when the window is destroyed.
    BrowserWindow* window = new BrowserWindow();
    if (!window->InitInstance(hInstance, lpCmdLine, nCmdShow))
    {
        delete window;
        return FALSE;
    }
    return TRUE;
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL BrowserWindow::InitInstance(HINSTANCE hInstance, LPCWSTR lpCmdLine, int nCmdShow)
{
    m_hInst = hInstance; // Store app instance handle
    m_lpCmdLine = lpCmdLine;
    LoadStringW(m_hInst, IDS_APP_TITLE, s_title, MAX_LOADSTRING);

    SetUIMessageBroker();

    m_hWnd = CreateWindowW(s_windowClass, s_title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, m_hInst, nullptr);

    if (!m_hWnd)
    {
        return FALSE;
    }

    // Make the BrowserWindow instance ptr available through the hWnd
    SetWindowLongPtr(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    UpdateMinWindowSize();
    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);

    // Get directory for user data. This will be kept separated from the
    // directory for the browser UI data.
    std::wstring userDataDirectory = GetAppDataDirectory();
    userDataDirectory.append(L"\\User Data");

    WCHAR executingFile[MAX_PATH];
    WCHAR executingFileFull[MAX_PATH];
    LPWSTR executingFileName = nullptr;

    WCHAR browserExecutableFolder[MAX_PATH] = { 0 };
    WCHAR additionalBrowserArguments[4096] = { 0 };

    GetModuleFileNameW(hInstance, executingFile, _countof(executingFile));
    GetFullPathNameW(executingFile, _countof(executingFileFull), executingFileFull, &executingFileName);

    PathRenameExtensionW(executingFile, L".ini");
    GetPrivateProfileStringW(executingFileName, L"BrowserExecutableFolder", nullptr,
        browserExecutableFolder, _countof(browserExecutableFolder), executingFile);
    GetPrivateProfileStringW(executingFileName, L"AdditionalBrowserArguments", nullptr,
        additionalBrowserArguments, _countof(additionalBrowserArguments), executingFile);

    if (*browserExecutableFolder && PathIsRelativeW(browserExecutableFolder))
    {
        *executingFileName = L'\0';
        PathCombineW(browserExecutableFolder, executingFileFull, browserExecutableFolder);
    }

    auto environmentOptions = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    environmentOptions->put_AdditionalBrowserArguments(additionalBrowserArguments);

    // Create WebView environment for web content requested by the user. All
    // tabs will be created from this environment and kept isolated from the
    // browser UI. This enviroment is created first so the UI can request new
    // tabs when it's ready.
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(browserExecutableFolder,
        userDataDirectory.c_str(), environmentOptions.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, browserExecutableFolder](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
    {
        RETURN_IF_FAILED(result);

        m_contentEnv = env;
        HRESULT hr = InitUIWebViews(browserExecutableFolder);

        if (!SUCCEEDED(hr))
        {
            OutputDebugString(L"UI WebViews environment creation failed\n");
        }

        return hr;
    }).Get());

    if (!SUCCEEDED(hr))
    {
        OutputDebugString(L"Content WebViews environment creation failed\n");
        return FALSE;
    }

    return TRUE;
}

HRESULT BrowserWindow::InitUIWebViews(LPCWSTR browserExecutableFolder)
{
    // Get data directory for browser UI data
    std::wstring browserDataDirectory = GetAppDataDirectory();
    browserDataDirectory.append(L"\\Browser Data");

    // Create WebView environment for browser UI. A separate data directory is
    // used to isolate the browser UI from web content requested by the user.
    return CreateCoreWebView2EnvironmentWithOptions(browserExecutableFolder, browserDataDirectory.c_str(),
        nullptr, Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
    {
        // Environment is ready, create the WebView
        m_uiEnv = env;

        RETURN_IF_FAILED(CreateBrowserControlsWebView());
        RETURN_IF_FAILED(CreateBrowserOptionsWebView());

        return S_OK;
    }).Get());
}

HRESULT BrowserWindow::CreateBrowserControlsWebView()
{
    return m_uiEnv->CreateCoreWebView2Controller(m_hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [this](HRESULT result, ICoreWebView2Controller* host) -> HRESULT
    {
        if (!SUCCEEDED(result))
        {
            OutputDebugString(L"Controls WebView creation failed\n");
            return result;
        }
        // WebView created
        m_controlsController = host;
        CheckFailure(m_controlsController->get_CoreWebView2(&m_controlsWebView), L"");

        wil::com_ptr<ICoreWebView2Settings> settings;
        RETURN_IF_FAILED(m_controlsWebView->get_Settings(&settings));
        RETURN_IF_FAILED(settings->put_AreDevToolsEnabled(FALSE));

        RETURN_IF_FAILED(m_controlsController->add_ZoomFactorChanged(Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
            [](ICoreWebView2Controller* host, IUnknown* args) -> HRESULT
        {
            host->put_ZoomFactor(1.0);
            return S_OK;
        }
        ).Get(), &m_controlsZoomToken));

        RETURN_IF_FAILED(m_controlsWebView->add_WebMessageReceived(m_uiMessageBroker.Get(), &m_controlsUIMessageBrokerToken));
        RETURN_IF_FAILED(ResizeUIWebViews());

        std::wstring controlsPath = GetFullPathFor(L"wvbrowser_ui\\controls_ui\\default.html");
        RETURN_IF_FAILED(m_controlsWebView->Navigate(controlsPath.c_str()));

        return S_OK;
    }).Get());
}

HRESULT BrowserWindow::CreateBrowserOptionsWebView()
{
    return m_uiEnv->CreateCoreWebView2Controller(m_hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [this](HRESULT result, ICoreWebView2Controller* host) -> HRESULT
    {
        if (!SUCCEEDED(result))
        {
            OutputDebugString(L"Options WebView creation failed\n");
            return result;
        }
        // WebView created
        m_optionsController = host;
        CheckFailure(m_optionsController->get_CoreWebView2(&m_optionsWebView), L"");

        wil::com_ptr<ICoreWebView2Settings> settings;
        RETURN_IF_FAILED(m_optionsWebView->get_Settings(&settings));
        RETURN_IF_FAILED(settings->put_AreDevToolsEnabled(FALSE));

        RETURN_IF_FAILED(m_optionsController->add_ZoomFactorChanged(Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
            [](ICoreWebView2Controller* host, IUnknown* args) -> HRESULT
        {
            host->put_ZoomFactor(1.0);
            return S_OK;
        }
        ).Get(), &m_optionsZoomToken));

        // Hide by default
        RETURN_IF_FAILED(m_optionsController->put_IsVisible(FALSE));
        RETURN_IF_FAILED(m_optionsWebView->add_WebMessageReceived(m_uiMessageBroker.Get(), &m_optionsUIMessageBrokerToken));

        // Hide menu when focus is lost
        RETURN_IF_FAILED(m_optionsController->add_LostFocus(Callback<ICoreWebView2FocusChangedEventHandler>(
            [this](ICoreWebView2Controller* sender, IUnknown* args) -> HRESULT
        {
            nlohmann::json jsonObj;
            jsonObj["message"] = MG_OPTIONS_LOST_FOCUS;
            jsonObj["args"] = nullptr;

            PostJsonToWebView(jsonObj, m_controlsWebView.Get());

            return S_OK;
        }).Get(), &m_lostOptionsFocus));

        RETURN_IF_FAILED(ResizeUIWebViews());

        std::wstring optionsPath = GetFullPathFor(L"wvbrowser_ui\\controls_ui\\options.html");
        RETURN_IF_FAILED(m_optionsWebView->Navigate(optionsPath.c_str()));

        return S_OK;
    }).Get());
}

// Set the message broker for the UI webview. This will capture messages from ui web content.
// Lambda is used to capture the instance while satisfying Microsoft::WRL::Callback<T>()
void BrowserWindow::SetUIMessageBroker()
{
    m_uiMessageBroker = Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [this](ICoreWebView2* webview, ICoreWebView2WebMessageReceivedEventArgs* eventArgs) -> HRESULT
    {
        LPWSTR jsonString;
        CheckFailure(eventArgs->get_WebMessageAsJson(&jsonString), L"");  // Get the message from the UI WebView as JSON formatted string
        std::wstring stirngEvent(jsonString);

        nlohmann::json jsonObj = nlohmann::json::parse(stirngEvent);

        if (!jsonObj.contains("message"))
        {
            OutputDebugString(L"No message code provided\n");
            return S_OK;
        }

        if (!jsonObj.contains("args"))
        {
            OutputDebugString(L"The message has no args field\n");
            return S_OK;
        }

        int message = jsonObj["message"];

        switch (message)
        {
        case MG_CREATE_TAB:
        {
            size_t id = jsonObj["args"]["tabId"];
            bool shouldBeActive = jsonObj["args"]["active"];

            std::unique_ptr<Tab> newTab = Tab::CreateNewTab(m_hWnd, m_contentEnv.Get(), id, shouldBeActive);

            std::map<size_t, std::unique_ptr<Tab>>::iterator it = m_tabs.find(id);
            if (it == m_tabs.end())
            {
                m_tabs.insert(std::pair<size_t,std::unique_ptr<Tab>>(id, std::move(newTab)));
            }
            else
            {
                m_tabs.at(id)->m_contentController->Close();
                it->second = std::move(newTab);
            }
        }
        break;
        case MG_NAVIGATE:
        {
            std::string string = jsonObj["args"]["uri"].get<std::string>().c_str();
            std::wstring uri = to_wstring(string);
            std::wstring browserScheme(L"browser://");

            if (uri.substr(0, browserScheme.size()).compare(browserScheme) == 0)
            {
                // No encoded search URI
                std::wstring path = uri.substr(browserScheme.size());
                if (path.compare(L"favorites") == 0 ||
                    path.compare(L"settings") == 0 ||
                    path.compare(L"history") == 0)
                {
                    std::wstring filePath(L"wvbrowser_ui\\content_ui\\");
                    filePath.append(path);
                    filePath.append(L".html");
                    std::wstring fullPath = GetFullPathFor(filePath.c_str());
                    CheckFailure(m_tabs.at(m_activeTabId)->m_contentWebView->Navigate(fullPath.c_str()), L"Can't navigate to browser page.");
                }
                else
                {
                    OutputDebugString(L"Requested unknown browser page\n");
                }
            }
            else if (!SUCCEEDED(m_tabs.at(m_activeTabId)->m_contentWebView->Navigate(uri.c_str())))
            {
                std::string string = jsonObj["args"]["encodedSearchURI"].get<std::string>().c_str();
                std::wstring uri = to_wstring(string);
                CheckFailure(m_tabs.at(m_activeTabId)->m_contentWebView->Navigate(uri.c_str()), L"Can't navigate to requested page.");
            }
        }
        break;
        case MG_GO_FORWARD:
        {
            CheckFailure(m_tabs.at(m_activeTabId)->m_contentWebView->GoForward(), L"");
        }
        break;
        case MG_GO_BACK:
        {
            CheckFailure(m_tabs.at(m_activeTabId)->m_contentWebView->GoBack(), L"");
        }
        break;
        case MG_RELOAD:
        {
            CheckFailure(m_tabs.at(m_activeTabId)->m_contentWebView->Reload(), L"");
        }
        break;
        case MG_CANCEL:
        {
            CheckFailure(m_tabs.at(m_activeTabId)->m_contentWebView->CallDevToolsProtocolMethod(L"Page.stopLoading", L"{}", nullptr), L"");
        }
        break;
        case MG_SWITCH_TAB:
        {
            size_t tabId = jsonObj["args"]["tabId"];
            SwitchToTab(tabId);
        }
        break;
        case MG_CLOSE_TAB:
        {
            size_t id = jsonObj["args"]["tabId"];
            m_tabs.at(id)->m_contentController->Close();
            m_tabs.erase(id);
        }
        break;
        case MG_CLOSE_WINDOW:
        {
            DestroyWindow(m_hWnd);
        }
        break;
        case MG_SHOW_OPTIONS:
        {
            CheckFailure(m_optionsController->put_IsVisible(TRUE), L"");
            m_optionsController->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        break;
        case MG_HIDE_OPTIONS:
        {
            CheckFailure(m_optionsController->put_IsVisible(FALSE), L"Something went wrong when trying to close the options dropdown.");
        }
        break;
        case MG_OPTION_SELECTED:
        {
            m_tabs.at(m_activeTabId)->m_contentController->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        break;
        case MG_GET_FAVORITES:
        case MG_GET_SETTINGS:
        case MG_GET_HISTORY:
        {
            // Forward back to requesting tab
            size_t tabId = jsonObj["args"]["tabId"];
            jsonObj.erase("tabId");
            CheckFailure(PostJsonToWebView(jsonObj, m_tabs.at(tabId)->m_contentWebView.Get()), L"Requesting history failed.");
        }
        break;
        default:
        {
            OutputDebugString(L"Unexpected message\n");
        }
        break;
        }

        return S_OK;
    });
}

HRESULT BrowserWindow::SwitchToTab(size_t tabId)
{
    size_t previousActiveTab = m_activeTabId;

    RETURN_IF_FAILED(m_tabs.at(tabId)->ResizeWebView());
    RETURN_IF_FAILED(m_tabs.at(tabId)->m_contentController->put_IsVisible(TRUE));
    m_activeTabId = tabId;

    if (previousActiveTab != INVALID_TAB_ID && previousActiveTab != m_activeTabId) {
        auto previousTabIterator = m_tabs.find(previousActiveTab);
        if (previousTabIterator != m_tabs.end() && previousTabIterator->second &&
            previousTabIterator->second->m_contentController)
        {
            auto hr = m_tabs.at(previousActiveTab)->m_contentController->put_IsVisible(FALSE);
            if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_STATE)) {
                nlohmann::json jsonObj;
                jsonObj["message"] = MG_CLOSE_TAB;
                jsonObj["args"]["tabId"] = previousActiveTab;

                PostJsonToWebView(jsonObj, m_controlsWebView.Get());
            }
            RETURN_IF_FAILED(hr);
        }
    }

    return S_OK;
}

HRESULT BrowserWindow::HandleTabURIUpdate(size_t tabId, ICoreWebView2* webview)
{
    wil::unique_cotaskmem_string source;
    RETURN_IF_FAILED(webview->get_Source(&source));
    std::wstring uri(source.get());

    nlohmann::json jsonObj;
    jsonObj["message"] = MG_UPDATE_URI;
    jsonObj["args"]["tabId"] = tabId;
    jsonObj["args"]["uri"] = to_utf8(uri);

    std::wstring favoritesURI = GetFilePathAsURI(GetFullPathFor(L"wvbrowser_ui\\content_ui\\favorites.html"));
    std::wstring settingsURI = GetFilePathAsURI(GetFullPathFor(L"wvbrowser_ui\\content_ui\\settings.html"));
    std::wstring historyURI = GetFilePathAsURI(GetFullPathFor(L"wvbrowser_ui\\content_ui\\history.html"));

    if (uri.compare(favoritesURI) == 0)
    {
        jsonObj["args"]["uriToShow"] = "browser://favorites";
    }
    else if (uri.compare(settingsURI) == 0)
    {
        jsonObj["args"]["uriToShow"] = "browser://settings";
    }
    else if (uri.compare(historyURI) == 0)
    {
        jsonObj["args"]["uriToShow"] = "browser://history";
    }

    RETURN_IF_FAILED(PostJsonToWebView(jsonObj, m_controlsWebView.Get()));

    return S_OK;
}

HRESULT BrowserWindow::HandleTabHistoryUpdate(size_t tabId, ICoreWebView2* webview)
{
    wil::unique_cotaskmem_string source;
    RETURN_IF_FAILED(webview->get_Source(&source));
    std::wstring uri(source.get());

    nlohmann::json jsonObj;
    jsonObj["message"] = MG_UPDATE_URI;
    jsonObj["args"]["tabId"] = tabId;
    jsonObj["args"]["uri"] = to_utf8(uri);

    BOOL canGoForward = FALSE;
    RETURN_IF_FAILED(webview->get_CanGoForward(&canGoForward));
    jsonObj["args"]["canGoForward"] = canGoForward;

    BOOL canGoBack = FALSE;
    RETURN_IF_FAILED(webview->get_CanGoBack(&canGoBack));
    jsonObj["args"]["canGoBack"] = canGoBack;

    RETURN_IF_FAILED(PostJsonToWebView(jsonObj, m_controlsWebView.Get()));

    return S_OK;
}

HRESULT BrowserWindow::HandleTabNavStarting(size_t tabId, ICoreWebView2* webview)
{
    nlohmann::json jsonObj;
    jsonObj["message"] = MG_NAV_STARTING;
    jsonObj["args"]["tabId"] = tabId;

    return PostJsonToWebView(jsonObj, m_controlsWebView.Get());
}

HRESULT BrowserWindow::HandleTabNavCompleted(size_t tabId, ICoreWebView2* webview, ICoreWebView2NavigationCompletedEventArgs* args)
{
    std::wstring getTitleScript(
        // Look for a title tag
        L"(() => {"
        L"    const titleTag = document.getElementsByTagName('title')[0];"
        L"    if (titleTag) {"
        L"        return titleTag.innerHTML;"
        L"    }"
        // No title tag, look for the file name
        L"    pathname = window.location.pathname;"
        L"    var filename = pathname.split('/').pop();"
        L"    if (filename) {"
        L"        return filename;"
        L"    }"
        // No file name, look for the hostname
        L"    const hostname =  window.location.hostname;"
        L"    if (hostname) {"
        L"        return hostname;"
        L"    }"
        // Fallback: let the UI use a generic title
        L"    return '';"
        L"})();"
    );

    std::wstring getFaviconURI(
        L"(() => {"
        // Let the UI use a fallback favicon
        L"    let faviconURI = '';"
        L"    let links = document.getElementsByTagName('link');"
        // Test each link for a favicon
        L"    Array.from(links).map(element => {"
        L"        let rel = element.rel;"
        // Favicon is declared, try to get the href
        L"        if (rel && (rel == 'shortcut icon' || rel == 'icon')) {"
        L"            if (!element.href) {"
        L"                return;"
        L"            }"
        // href to icon found, check it's full URI
        L"            try {"
        L"                let urlParser = new URL(element.href);"
        L"                faviconURI = urlParser.href;"
        L"            } catch(e) {"
        // Try prepending origin
        L"                let origin = window.location.origin;"
        L"                let faviconLocation = `${origin}/${element.href}`;"
        L"                try {"
        L"                    urlParser = new URL(faviconLocation);"
        L"                    faviconURI = urlParser.href;"
        L"                } catch (e2) {"
        L"                    return;"
        L"                }"
        L"            }"
        L"        }"
        L"    });"
        L"    return faviconURI;"
        L"})();"
    );

    CheckFailure(webview->ExecuteScript(getTitleScript.c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [this, tabId](HRESULT error, PCWSTR result) -> HRESULT
    {
        RETURN_IF_FAILED(error);

        std::wstring title(result);

        nlohmann::json jsonObj;
        jsonObj["message"] = MG_UPDATE_TAB;
        jsonObj["args"]["title"] = to_utf8(title);
        jsonObj["args"]["tabId"] = tabId;

        CheckFailure(PostJsonToWebView(jsonObj, m_controlsWebView.Get()), L"Can't update title.");
        return S_OK;
    }).Get()), L"Can't update title.");

    CheckFailure(webview->ExecuteScript(getFaviconURI.c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [this, tabId](HRESULT error, PCWSTR result) -> HRESULT
    {
        RETURN_IF_FAILED(error);

        std::wstring uri(result);

        nlohmann::json jsonObj;
        jsonObj["message"] = MG_UPDATE_FAVICON;
        jsonObj["args"]["uri"] = to_utf8(uri);
        jsonObj["args"]["tabId"] = tabId;

        CheckFailure(PostJsonToWebView(jsonObj, m_controlsWebView.Get()), L"Can't update favicon.");
        return S_OK;
    }).Get()), L"Can't update favicon");

    nlohmann::json jsonObj;
    jsonObj["message"] = MG_NAV_COMPLETED;
    jsonObj["args"]["tabId"] = tabId;

    BOOL navigationSucceeded = FALSE;
    if (SUCCEEDED(args->get_IsSuccess(&navigationSucceeded)))
    {
        jsonObj["args"]["isError"] = !navigationSucceeded;
    }

    return PostJsonToWebView(jsonObj, m_controlsWebView.Get());
}

HRESULT BrowserWindow::HandleTabSecurityUpdate(size_t tabId, ICoreWebView2* webview, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args)
{
    LPWSTR jsonArgs;
    RETURN_IF_FAILED(args->get_ParameterObjectAsJson(&jsonArgs));
    std::wstring jsonString(jsonArgs);
    nlohmann::json jsonEvent = nlohmann::json::parse(jsonString);

    nlohmann::json jsonObj;
    jsonObj["message"] = MG_SECURITY_UPDATE;
    jsonObj["args"]["tabId"] = tabId;
    jsonObj["args"]["state"] = jsonEvent["visibleSecurityState"];

    return PostJsonToWebView(jsonObj, m_controlsWebView.Get());
}

void BrowserWindow::HandleTabCreated(size_t tabId, bool shouldBeActive)
{
    if (shouldBeActive)
    {
        if (m_lpCmdLine)
        {
            m_tabs.at(tabId)->m_contentWebView->Navigate(m_lpCmdLine);
            m_lpCmdLine = nullptr;
        }
        CheckFailure(SwitchToTab(tabId), L"");
    }
}

HRESULT BrowserWindow::HandleTabMessageReceived(size_t tabId, ICoreWebView2* webview, ICoreWebView2WebMessageReceivedEventArgs* eventArgs)
{
    LPWSTR jsonArgs;
    RETURN_IF_FAILED(eventArgs->get_WebMessageAsJson(&jsonArgs));
    std::wstring jsonString(jsonArgs);
    nlohmann::json jsonObj = nlohmann::json::parse(jsonString);

    wil::unique_cotaskmem_string uri;
    RETURN_IF_FAILED(webview->get_Source(&uri));

    int message = jsonObj["message"];

    wil::unique_cotaskmem_string source;
    RETURN_IF_FAILED(webview->get_Source(&source));

    switch (message)
    {
    case MG_GET_FAVORITES:
    case MG_REMOVE_FAVORITE:
    {
        std::wstring fileURI = GetFilePathAsURI(GetFullPathFor(L"wvbrowser_ui\\content_ui\\favorites.html"));
        // Only the favorites UI can request favorites
        if (fileURI.compare(source.get()) == 0)
        {
            jsonObj["args"]["tabId"] = tabId;
            CheckFailure(PostJsonToWebView(jsonObj, m_controlsWebView.Get()), L"Couldn't perform favorites operation.");
        }
    }
    break;
    case MG_GET_SETTINGS:
    {
        std::wstring fileURI = GetFilePathAsURI(GetFullPathFor(L"wvbrowser_ui\\content_ui\\settings.html"));
        // Only the settings UI can request settings
        if (fileURI.compare(source.get()) == 0)
        {
            jsonObj["args"]["tabId"] = tabId;
            CheckFailure(PostJsonToWebView(jsonObj, m_controlsWebView.Get()), L"Couldn't retrieve settings.");
        }
    }
    break;
    case MG_CLEAR_CACHE:
    {
        std::wstring fileURI = GetFilePathAsURI(GetFullPathFor(L"wvbrowser_ui\\content_ui\\settings.html"));
        // Only the settings UI can request cache clearing
        if (fileURI.compare(uri.get()) == 0)
        {
            jsonObj["args"]["content"] = false;
            jsonObj["args"]["controls"] = false;

            if (SUCCEEDED(ClearContentCache()))
            {
                jsonObj["args"]["content"] = true;
            }

            if (SUCCEEDED(ClearControlsCache()))
            {
                jsonObj["args"]["controls"] = true;
            }

            CheckFailure(PostJsonToWebView(jsonObj, m_tabs.at(tabId)->m_contentWebView.Get()), L"");
        }
    }
    break;
    case MG_CLEAR_COOKIES:
    {
        std::wstring fileURI = GetFilePathAsURI(GetFullPathFor(L"wvbrowser_ui\\content_ui\\settings.html"));
        // Only the settings UI can request cookies clearing
        if (fileURI.compare(uri.get()) == 0)
        {
            jsonObj["args"]["content"] = false;
            jsonObj["args"]["controls"] = false;

            if (SUCCEEDED(ClearContentCookies()))
            {
                jsonObj["args"]["content"] = true;
            }


            if (SUCCEEDED(ClearControlsCookies()))
            {
                jsonObj["args"]["controls"] = true;
            }

            CheckFailure(PostJsonToWebView(jsonObj, m_tabs.at(tabId)->m_contentWebView.Get()), L"");
        }
    }
    break;
    case MG_GET_HISTORY:
    case MG_REMOVE_HISTORY_ITEM:
    case MG_CLEAR_HISTORY:
    {
        std::wstring fileURI = GetFilePathAsURI(GetFullPathFor(L"wvbrowser_ui\\content_ui\\history.html"));
        // Only the history UI can request history
        if (fileURI.compare(uri.get()) == 0)
        {
            jsonObj["args"]["tabId"] = tabId;
            CheckFailure(PostJsonToWebView(jsonObj, m_controlsWebView.Get()), L"Couldn't perform history operation");
        }
    }
    break;
    default:
    {
        OutputDebugString(L"Unexpected message\n");
    }
    break;
    }

    return S_OK;
}

HRESULT BrowserWindow::ClearContentCache()
{
    return m_tabs.at(m_activeTabId)->m_contentWebView->CallDevToolsProtocolMethod(L"Network.clearBrowserCache", L"{}", nullptr);
}

HRESULT BrowserWindow::ClearControlsCache()
{
    return m_controlsWebView->CallDevToolsProtocolMethod(L"Network.clearBrowserCache", L"{}", nullptr);
}

HRESULT BrowserWindow::ClearContentCookies()
{
    return m_tabs.at(m_activeTabId)->m_contentWebView->CallDevToolsProtocolMethod(L"Network.clearBrowserCookies", L"{}", nullptr);
}

HRESULT BrowserWindow::ClearControlsCookies()
{
    return m_controlsWebView->CallDevToolsProtocolMethod(L"Network.clearBrowserCookies", L"{}", nullptr);
}

HRESULT BrowserWindow::ResizeUIWebViews()
{
    if (m_controlsWebView != nullptr)
    {
        RECT bounds;
        GetClientRect(m_hWnd, &bounds);
        bounds.bottom = bounds.top + GetDPIAwareBound(c_uiBarHeight);
        bounds.bottom += 1;

        RETURN_IF_FAILED(m_controlsController->put_Bounds(bounds));
    }

    if (m_optionsWebView != nullptr)
    {
        RECT bounds;
        GetClientRect(m_hWnd, &bounds);
        bounds.top = GetDPIAwareBound(c_uiBarHeight);
        bounds.bottom = bounds.top + GetDPIAwareBound(c_optionsDropdownHeight);
        bounds.left = bounds.right - GetDPIAwareBound(c_optionsDropdownWidth);

        RETURN_IF_FAILED(m_optionsController->put_Bounds(bounds));
    }

    // Workaround for black controls WebView issue in Windows 7
    HWND wvWindow = GetWindow(m_hWnd, GW_CHILD);
    while (wvWindow != nullptr)
    {
        UpdateWindow(wvWindow);
        wvWindow = GetWindow(wvWindow, GW_HWNDNEXT);
    }

    return S_OK;
}

void BrowserWindow::UpdateMinWindowSize()
{
    RECT clientRect;
    RECT windowRect;

    GetClientRect(m_hWnd, &clientRect);
    GetWindowRect(m_hWnd, &windowRect);

    int bordersWidth = (windowRect.right - windowRect.left) - clientRect.right;
    int bordersHeight = (windowRect.bottom - windowRect.top) - clientRect.bottom;

    m_minWindowWidth = GetDPIAwareBound(MIN_WINDOW_WIDTH) + bordersWidth;
    m_minWindowHeight = GetDPIAwareBound(MIN_WINDOW_HEIGHT) + bordersHeight;
}

void BrowserWindow::CheckFailure(HRESULT hr, LPCWSTR errorMessage)
{
    if (FAILED(hr))
    {
        std::wstring message;
        if (!errorMessage || !errorMessage[0])
        {
            message = std::wstring(L"Something went wrong.");
        }
        else
        {
            message = std::wstring(errorMessage);
        }

        MessageBoxW(nullptr, message.c_str(), nullptr, MB_OK);
    }
}

int BrowserWindow::GetDPIAwareBound(int bound)
{
    // On Windows prior to 10.0.1607, fall back to GetDeviceCaps()
    int dpi = DEFAULT_DPI;
    if (user32.GetDpiForWindow)
    {
        dpi = user32.GetDpiForWindow(m_hWnd);
    }
    else if (HDC const hdc = GetDC(m_hWnd))
    {
        dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(m_hWnd, hdc);
    }
    return dpi * bound / DEFAULT_DPI;
}

std::wstring BrowserWindow::GetAppDataDirectory()
{
    TCHAR path[MAX_PATH];
    std::wstring dataDirectory;
    HRESULT hr = SHGetFolderPath(nullptr, CSIDL_APPDATA, NULL, 0, path);
    if (SUCCEEDED(hr))
    {
        dataDirectory = std::wstring(path);
        dataDirectory.append(L"\\Microsoft\\");
    }
    else
    {
        dataDirectory = std::wstring(L".\\");
    }

    dataDirectory.append(s_title);
    return dataDirectory;
}

std::wstring BrowserWindow::GetFullPathFor(LPCWSTR relativePath)
{
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(m_hInst, path, MAX_PATH);
    std::wstring pathName(path);

    std::size_t index = pathName.find_last_of(L"\\") + 1;
    pathName.replace(index, pathName.length(), relativePath);

    return pathName;
}

std::wstring BrowserWindow::GetFilePathAsURI(std::wstring fullPath)
{
    std::wstring fileURI;
    ComPtr<IUri> uri;
    DWORD uriFlags = Uri_CREATE_ALLOW_IMPLICIT_FILE_SCHEME;
    HRESULT hr = CreateUri(fullPath.c_str(), uriFlags, 0, &uri);

    if (SUCCEEDED(hr))
    {
        wil::unique_bstr absoluteUri;
        uri->GetAbsoluteUri(&absoluteUri);
        fileURI = std::wstring(absoluteUri.get());
    }

    return fileURI;
}

HRESULT BrowserWindow::PostJsonToWebView(const nlohmann::json jsonObj, ICoreWebView2* webview)
{
    std::string dump = jsonObj.dump().c_str();
    std::wstring jsonString = to_wstring(dump);

    return webview->PostWebMessageAsJson(jsonString.c_str());
}
