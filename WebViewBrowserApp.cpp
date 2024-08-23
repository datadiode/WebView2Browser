// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WebViewBrowserApp.cpp : Defines the entry point for the application.
//

#include "BrowserWindow.h"
#include "WebViewBrowserApp.h"

using namespace Microsoft::WRL;

void tryLaunchWindow(HINSTANCE hInstance, LPCWSTR lpCmdLine, int nCmdShow);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR    lpCmdLine,
                      _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    SetProcessDPIAware();

    // Allow only a single application instance
    // (neglects occasional race conditions for simplicity)
    COPYDATASTRUCT cds;
    cds.dwData = 1;
    cds.cbData = static_cast<DWORD>(wcslen(lpCmdLine)) * sizeof *lpCmdLine;
    cds.lpData = lpCmdLine;
    if (ATOM const atom = BrowserWindow::RegisterClass(hInstance, cds))
    {
        if (HWND const hwnd = FindWindowW(reinterpret_cast<LPCWSTR>(atom), nullptr))
        {
            SetForegroundWindow(hwnd);
            if (cds.cbData)
            {
                cds.cbData += sizeof *lpCmdLine; // Include the terminating zero
                DWORD_PTR result = 0;
                while (SendMessageTimeout(
                    hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
                    SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG, 1000, &result) &&
                    result == IDRETRY)
                {
                    Sleep(100);
                }
            }
            return 0;
        }
    }

    tryLaunchWindow(hInstance, lpCmdLine, nCmdShow);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WEBVIEWBROWSERAPP));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

void tryLaunchWindow(HINSTANCE hInstance, LPCWSTR lpCmdLine, int nCmdShow)
{
    BOOL launched = BrowserWindow::LaunchWindow(hInstance, lpCmdLine, nCmdShow);
    if (!launched)
    {
        int msgboxID = MessageBox(NULL, L"Could not launch the browser", L"Error", MB_RETRYCANCEL);

        switch (msgboxID)
        {
        case IDRETRY:
            tryLaunchWindow(hInstance, lpCmdLine, nCmdShow);
            break;
        case IDCANCEL:
        default:
            PostQuitMessage(0);
        }
    }
}
