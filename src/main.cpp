#include <windows.h>
#include <shellapi.h>
#include "IndicatorWindow.h"

// Constants
#define WM_TRAY_CALLBACK (WM_USER + 100)
#define TRAY_ICON_ID 1

const wchar_t* const SETTINGS_REGISTRY_KEY = L"Software\\CursorIMEIndicator";
const wchar_t* const ALWAYS_SHOW_VALUE_NAME = L"AlwaysShow";

// Global Pointer to the Indicator Window
IndicatorWindow* g_pIndicatorWindow = nullptr;

// Globals for Tray Icon and Taskbar Re-creation
UINT g_wmTaskbarCreated = 0;
HICON g_hTrayIcon = NULL;

// Auto-start Registry Management
bool IsStartupRegistered() {
    HKEY hKey;
    bool isRegistered = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH];
        DWORD size = sizeof(path);
        if (RegQueryValueExW(hKey, L"CursorIMEIndicator", NULL, NULL, (LPBYTE)path, &size) == ERROR_SUCCESS) {
            isRegistered = true;
        }
        RegCloseKey(hKey);
    }
    return isRegistered;
}

void RegisterStartup(bool registerIt) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (registerIt) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            RegSetValueExW(hKey, L"CursorIMEIndicator", 0, REG_SZ, (const BYTE*)path, (lstrlenW(path) + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"CursorIMEIndicator");
        }
        RegCloseKey(hKey);
    }
}

bool IsAlwaysShowSaved() {
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);
    bool isEnabled = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_REGISTRY_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, ALWAYS_SHOW_VALUE_NAME, NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            isEnabled = value != 0;
        }
        RegCloseKey(hKey);
    }
    return isEnabled;
}

void SaveAlwaysShow(bool enabled) {
    HKEY hKey;
    DWORD disposition;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_REGISTRY_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, &disposition) == ERROR_SUCCESS) {
        DWORD value = enabled ? 1 : 0;
        RegSetValueExW(hKey, ALWAYS_SHOW_VALUE_NAME, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
        RegCloseKey(hKey);
    }
}

// Draw a beautiful custom icon dynamically using GDI+
HICON CreateTrayIcon() {
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);

    Gdiplus::Bitmap bitmap(cx, cy, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Transparent Background
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    // Draw beautiful Indigo pill/circle
    Gdiplus::SolidBrush brushBg(Gdiplus::Color(220, 79, 70, 229)); // Indigo
    graphics.FillEllipse(&brushBg, 1, 1, cx - 2, cy - 2);

    // Draw gold-white outline
    Gdiplus::Pen penBorder(Gdiplus::Color(200, 255, 255, 255), 1.0f);
    graphics.DrawEllipse(&penBorder, 1, 1, cx - 2, cy - 2);

    // Draw '한' character in the center
    Gdiplus::Font font(L"Malgun Gothic", (float)cx * 0.6f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    
    Gdiplus::SolidBrush brushText(Gdiplus::Color(255, 255, 255, 255));
    
    // Draw slightly offset for visual balance
    Gdiplus::RectF rect(0, -0.5f, (float)cx, (float)cy);
    graphics.DrawString(L"한", -1, &font, rect, &sf, &brushText);

    HICON hIcon = NULL;
    bitmap.GetHICON(&hIcon);
    return hIcon;
}

// Background window procedure for handling tray icon events
LRESULT CALLBACK BackgroundWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_wmTaskbarCreated != 0 && message == g_wmTaskbarCreated) {
        // Re-create the tray icon if Explorer restarts (e.g. after logoff/login or Explorer crash)
        NOTIFYICONDATAW nid = { 0 };
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = hWnd;
        nid.uID = TRAY_ICON_ID;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAY_CALLBACK;
        nid.hIcon = g_hTrayIcon;
        lstrcpyW(nid.szTip, L"Cursor IME Indicator");
        Shell_NotifyIconW(NIM_ADD, &nid);
        return 0;
    }

    switch (message) {
    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam == TRUE) {
            DestroyWindow(hWnd);
        }
        return 0;

    case WM_TRAY_CALLBACK:
        if (wParam == TRAY_ICON_ID) {
            if (lParam == WM_RBUTTONUP) {
                // Show context menu
                HMENU hMenu = CreatePopupMenu();
                
                bool isEnabled = g_pIndicatorWindow ? g_pIndicatorWindow->IsEnabled() : false;
                AppendMenuW(hMenu, MF_STRING | (isEnabled ? MF_CHECKED : MF_UNCHECKED), 101, L"마우스 커서 표시기 활성화");
                
                bool isStartup = IsStartupRegistered();
                AppendMenuW(hMenu, MF_STRING | (isStartup ? MF_CHECKED : MF_UNCHECKED), 102, L"윈도우 시작 시 자동 실행");

                bool isAlwaysShow = g_pIndicatorWindow ? g_pIndicatorWindow->IsAlwaysShow() : false;
                AppendMenuW(hMenu, MF_STRING | (isAlwaysShow ? MF_CHECKED : MF_UNCHECKED), 105, L"항상 한영 상태 표시");
                
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, 103, L"정보 (About)...");
                AppendMenuW(hMenu, MF_STRING, 104, L"종료 (Exit)");

                POINT pt;
                GetCursorPos(&pt);
                
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
                DestroyMenu(hMenu);
            }
            else if (lParam == WM_LBUTTONUP) {
                // Toggle state on left click
                if (g_pIndicatorWindow) {
                    g_pIndicatorWindow->SetEnabled(!g_pIndicatorWindow->IsEnabled());
                }
            }
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 101: // Toggle Enable/Disable
            if (g_pIndicatorWindow) {
                g_pIndicatorWindow->SetEnabled(!g_pIndicatorWindow->IsEnabled());
            }
            break;
        case 102: // Toggle Auto Start
            RegisterStartup(!IsStartupRegistered());
            break;
        case 105: // Toggle Always Show
            if (g_pIndicatorWindow) {
                bool alwaysShow = !g_pIndicatorWindow->IsAlwaysShow();
                g_pIndicatorWindow->SetAlwaysShow(alwaysShow);
                SaveAlwaysShow(alwaysShow);
            }
            break;
        case 103: // About Dialog
            MessageBoxW(hWnd, 
                L"Cursor IME Indicator v1.2\n"
                L"Copyright (c) 2026 jintaeks@gmail.com\n\n"
                L"마우스 커서의 모양을 그대로 유지하면서,\n"
                L"입력기 상태(한글 '한' / 영문 'E')를 실시간으로 보여주는 유틸리티입니다.\n\n"
                L"• 한/영 전환 또는 포커스 변경 시 2초간 표시\n"
                L"• 마우스 이동 시 자동 숨김\n"
                L"• 완벽한 클릭 관통(Click-Through) 지원",
                L"정보 (About)", MB_OK | MB_ICONINFORMATION);
            break;
        case 104: // Exit
            DestroyWindow(hWnd);
            break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // 1. Check for single instance
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\CursorIMEIndicatorMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"이미 실행 중인 인스턴스가 있습니다.", L"Cursor IME Indicator", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Initialize GDI+ globally for the program lifetime
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // Register Explorer taskbar creation message
    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // 2. Register Background Window Class
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = BackgroundWndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = L"CursorIMEBackgroundClass";
    RegisterClassExW(&wcex);

    // Create background window as an invisible top-level window (parent = NULL, style = WS_POPUP)
    // to properly receive system broadcast messages like WM_ENDSESSION and TaskbarCreated.
    HWND hBgWnd = CreateWindowExW(0, L"CursorIMEBackgroundClass", L"Cursor IME Background", 
                                  WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!hBgWnd) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 0;
    }

    // Create the tray icon using the global GDI+ session
    g_hTrayIcon = CreateTrayIcon();

    // 3. Add Tray Icon
    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hBgWnd;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_CALLBACK;
    nid.hIcon = g_hTrayIcon;
    lstrcpyW(nid.szTip, L"Cursor IME Indicator");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // 4. Create and Show the layered Indicator Window
    g_pIndicatorWindow = new IndicatorWindow();
    if (g_pIndicatorWindow->Create(hInstance)) {
        g_pIndicatorWindow->SetAlwaysShow(IsAlwaysShowSaved());
        g_pIndicatorWindow->Show(true);
    }

    // 5. Message Loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 6. Cleanup Tray Icon
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (g_hTrayIcon) {
        DestroyIcon(g_hTrayIcon);
        g_hTrayIcon = NULL;
    }

    // 7. Cleanup Indicator Window
    delete g_pIndicatorWindow;
    g_pIndicatorWindow = nullptr;

    // Unregister Background Window Class
    UnregisterClassW(L"CursorIMEBackgroundClass", hInstance);

    // Shutdown GDI+ globally
    Gdiplus::GdiplusShutdown(gdiplusToken);

    // 8. Release Mutex
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return (int)msg.wParam;
}
