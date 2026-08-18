#include "IndicatorWindow.h"
#include "IMEDetector.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

IndicatorWindow* IndicatorWindow::s_pInstance = nullptr;

// Helper function to create a rounded rectangle path in GDI+
static void GetRoundedRectPath(Gdiplus::GraphicsPath& path, Gdiplus::RectF rect, float radius) {
    float diameter = radius * 2.0f;
    path.Reset();
    
    // Check if the radius is too large for the rectangle
    if (diameter > rect.Width) diameter = rect.Width;
    if (diameter > rect.Height) diameter = rect.Height;
    
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

IndicatorWindow::IndicatorWindow()
    : m_hWnd(NULL)
    , m_hInstance(NULL)
    , m_isEnabled(true)
    , m_alwaysShow(false)
    , m_hForegroundHook(NULL)
    , m_hFocusHook(NULL)
    , m_currentForegroundWnd(NULL)
    , m_currentFocusWnd(NULL)
    , m_isKorean(false)
    , m_currentAlpha(0.0f)
    , m_targetAlpha(0.0f)
    , m_lastMoveTime(0)
    , m_lastMouseButtonDown(false)
    , m_windowVisible(false)
    , m_lastFocusWnd(NULL)
    , m_lastForegroundWnd(NULL)
    , m_lastCursorHandle(NULL)
    , m_hideAtTime(0)
    , m_triggerCursorWidth(32)
    , m_triggerCursorHeight(32)
    , m_triggerXHotspot(0)
    , m_triggerYHotspot(0)
    , m_cachedCursorWidth(32)
    , m_cachedCursorHeight(32)
    , m_cachedCursorXHotspot(0)
    , m_cachedCursorYHotspot(0)
    , m_lastImeCheckTime(0)
    , m_pendingImeRecheckTime(0)
    , m_currentTimerInterval(TIMER_INTERVAL_POLL)
    , m_hdcBadgeKorean(NULL)
    , m_hBmpBadgeKorean(NULL)
    , m_hdcBadgeEnglish(NULL)
    , m_hBmpBadgeEnglish(NULL)
{
    m_lastCursorPos.x = 0;
    m_lastCursorPos.y = 0;
    m_triggerPos.x = 0;
    m_triggerPos.y = 0;
}

IndicatorWindow::~IndicatorWindow() {
    if (m_hForegroundHook) {
        UnhookWinEvent(m_hForegroundHook);
        m_hForegroundHook = NULL;
    }
    if (m_hFocusHook) {
        UnhookWinEvent(m_hFocusHook);
        m_hFocusHook = NULL;
    }
    if (s_pInstance == this) {
        s_pInstance = nullptr;
    }

    DestroyPrerenderedBadges();

    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        UnregisterClassW(L"CursorIMEIndicatorClass", m_hInstance);
    }
}

void CALLBACK IndicatorWindow::WinEventProc(HWINEVENTHOOK /*hWinEventHook*/, DWORD event, HWND hwnd,
                                             LONG idObject, LONG /*idChild*/, DWORD /*idEventThread*/,
                                             DWORD /*dwmsEventTime*/) {
    if (!s_pInstance || !hwnd) {
        return;
    }

    if (event == EVENT_SYSTEM_FOREGROUND) {
        if (idObject == OBJID_WINDOW) {
            s_pInstance->m_currentForegroundWnd = hwnd;
        }
    } else if (event == EVENT_OBJECT_FOCUS) {
        s_pInstance->m_currentFocusWnd = hwnd;
    }
}

bool IndicatorWindow::Create(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    // Register Window Class
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = IndicatorWindow::WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.lpszClassName = L"CursorIMEIndicatorClass";

    RegisterClassExW(&wcex);

    // Create window
    // We use WS_EX_LAYERED, WS_EX_TRANSPARENT (click-through), WS_EX_NOACTIVATE (prevents focus),
    // and WS_EX_TOOLWINDOW (prevents taskbar and Alt-Tab display).
    m_hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"CursorIMEIndicatorClass",
        L"Cursor IME Indicator",
        WS_POPUP,
        0, 0, 64, 64,
        NULL, NULL, hInstance, this
    );

    if (!m_hWnd) {
        return false;
    }

    // Store this pointer in window user data
    SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, (LONG_PTR)this);

    // Initial state: detect IME immediately
    m_isKorean = IMEDetector::IsKoreanMode();
    m_lastMoveTime = GetTickCount64();
    m_lastImeCheckTime = m_lastMoveTime;

    // Pre-render both badge states once; Render() just selects between them afterwards.
    CreatePrerenderedBadges();

    // Seed the current foreground/focus window once; from here on they are kept up to date by
    // WinEventProc instead of being polled with GetForegroundWindow()/GetGUIThreadInfo() every tick.
    s_pInstance = this;
    m_currentForegroundWnd = GetForegroundWindow();
    m_currentFocusWnd = m_currentForegroundWnd;
    if (m_currentForegroundWnd) {
        DWORD dwThreadId = GetWindowThreadProcessId(m_currentForegroundWnd, NULL);
        if (dwThreadId != 0) {
            GUITHREADINFO gti = { 0 };
            gti.cbSize = sizeof(GUITHREADINFO);
            if (GetGUIThreadInfo(dwThreadId, &gti) && gti.hwndFocus) {
                m_currentFocusWnd = gti.hwndFocus;
            }
        }
    }

    m_hForegroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL,
                                         WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    m_hFocusHook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, NULL,
                                    WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // Start the low-frequency polling timer used for trigger detection.
    SetTimer(m_hWnd, 1, TIMER_INTERVAL_POLL, NULL);
    m_currentTimerInterval = TIMER_INTERVAL_POLL;

    return true;
}

void IndicatorWindow::Show(bool show) {
    if (show && m_isEnabled) {
        ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
        m_windowVisible = true;
    } else {
        ShowWindow(m_hWnd, SW_HIDE);
        m_windowVisible = false;
    }
}

void IndicatorWindow::SetEnabled(bool enabled) {
    m_isEnabled = enabled;
    if (!enabled) {
        m_targetAlpha = 0.0f;
    }
}

void IndicatorWindow::SetAlwaysShow(bool alwaysShow) {
    m_alwaysShow = alwaysShow;
    if (m_hWnd) {
        UINT interval = alwaysShow ? TIMER_INTERVAL_ALWAYS_SHOW : TIMER_INTERVAL_POLL;
        if (m_currentTimerInterval != interval) {
            SetTimer(m_hWnd, 1, interval, NULL);
            m_currentTimerInterval = interval;
        }
    }
    if (!alwaysShow) {
        m_hideAtTime = 0;
        m_targetAlpha = 0.0f;
    }
}

LRESULT CALLBACK IndicatorWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    IndicatorWindow* pThis = (IndicatorWindow*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (message) {
    case WM_TIMER:
        if (pThis && wParam == 1) {
            pThis->OnTimerTick();
        }
        break;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        break;
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

void IndicatorWindow::OnTimerTick() {
    if (!m_isEnabled) {
        if (m_currentAlpha > 0.0f || m_windowVisible) {
            m_targetAlpha = 0.0f;
            m_currentAlpha = 0.0f;
            m_hideAtTime = 0;
            Show(false);
        }
        return;
    }

    // Foreground window is tracked via WinEventProc (EVENT_SYSTEM_FOREGROUND) instead of being
    // polled here. If there is no active foreground window (e.g. system is logged off, locked, or
    // screen transitioning), we should immediately hide the indicator.
    HWND hwndForeground = m_currentForegroundWnd;
    if (!hwndForeground) {
        m_targetAlpha = 0.0f;
        m_currentAlpha = 0.0f;
        m_hideAtTime = 0;
        if (m_windowVisible) {
            Show(false);
        }
        return;
    }

    // 1. Detect current cursor info. GetIconInfo/GetObject/DeleteObject allocate and free GDI
    // handles, so only re-query icon metrics when the cursor handle actually changed since last
    // tick; otherwise reuse the cached values.
    CURSORINFO ci = { sizeof(CURSORINFO) };
    bool cursorShowing = false;
    HCURSOR currentCursor = NULL;

    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
        cursorShowing = true;
        currentCursor = ci.hCursor;

        if (currentCursor != m_lastCursorHandle) {
            ICONINFO ii;
            if (GetIconInfo(currentCursor, &ii)) {
                m_cachedCursorXHotspot = ii.xHotspot;
                m_cachedCursorYHotspot = ii.yHotspot;
                BITMAP bmpColor = { 0 };
                BITMAP bmpMask = { 0 };
                if (ii.hbmColor) {
                    GetObject(ii.hbmColor, sizeof(BITMAP), &bmpColor);
                    m_cachedCursorWidth = bmpColor.bmWidth;
                    m_cachedCursorHeight = bmpColor.bmHeight;
                } else if (ii.hbmMask) {
                    GetObject(ii.hbmMask, sizeof(BITMAP), &bmpMask);
                    m_cachedCursorWidth = bmpMask.bmWidth;
                    m_cachedCursorHeight = bmpMask.bmHeight / 2;
                }
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
                if (ii.hbmMask) DeleteObject(ii.hbmMask);
            }
        }
    }

    int cursorWidth = m_cachedCursorWidth;
    int cursorHeight = m_cachedCursorHeight;
    int xHotspot = m_cachedCursorXHotspot;
    int yHotspot = m_cachedCursorYHotspot;

    // 2. Track mouse position & movement
    ULONGLONG now = GetTickCount64();
    POINT pt;
    GetCursorPos(&pt);
    bool mouseMoved = false;
    if (pt.x != m_lastCursorPos.x || pt.y != m_lastCursorPos.y) {
        m_lastCursorPos = pt;
        mouseMoved = true;
    }

    bool mouseButtonDown =
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
    bool mouseButtonChanged = mouseButtonDown != m_lastMouseButtonDown;
    if (mouseMoved || mouseButtonDown || mouseButtonChanged) {
        m_lastMoveTime = now;
    }
    m_lastMouseButtonDown = mouseButtonDown;
    bool mouseIdleForOneSecond = now - m_lastMoveTime >= 1000;

    // 3. Current active focused control, tracked via WinEventProc (EVENT_OBJECT_FOCUS).
    HWND hwndFocus = m_currentFocusWnd ? m_currentFocusWnd : hwndForeground;

    // 4. Query current IME status. IMEDetector uses SendMessageTimeout, a cross-process synchronous
    // call that can block up to 250ms if the target is unresponsive, so we don't want to pay for it
    // every poll tick. Check immediately when focus/foreground just changed (the new target may be
    // in a different IME state) or while the badge is showing; otherwise throttle to
    // IME_CHECK_INTERVAL_MS so a standalone IME toggle (e.g. Hangul key) while idle is still caught,
    // just at a lower rate.
    bool focusOrForegroundChanged = (hwndFocus != m_lastFocusWnd && hwndFocus != NULL) ||
                                     (hwndForeground != m_lastForegroundWnd && hwndForeground != NULL);

    // When focus/foreground just changed, schedule a deferred re-check ~200 ms from now.
    // Windows Terminal (ConPTY) and some other apps update their IME state asynchronously after
    // the focus event, so the first query can return a stale value.
    if (focusOrForegroundChanged) {
        m_pendingImeRecheckTime = now + 200;
    }

    bool shouldCheckIme = focusOrForegroundChanged ||
                          m_targetAlpha > 0.0f ||
                          (m_pendingImeRecheckTime != 0 && now >= m_pendingImeRecheckTime) ||
                          (now - m_lastImeCheckTime >= IME_CHECK_INTERVAL_MS);

    bool imeChanged = false;
    if (shouldCheckIme) {
        // Clear the pending re-check once we service it.
        if (m_pendingImeRecheckTime != 0 && now >= m_pendingImeRecheckTime) {
            m_pendingImeRecheckTime = 0;
        }
        m_lastImeCheckTime = now;
        bool currentKorean = IMEDetector::IsKoreanMode();
        imeChanged = (currentKorean != m_isKorean);
        if (imeChanged) {
            m_isKorean = currentKorean;
        }
    }

    // The badge has no per-frame animation: alpha/color are applied immediately (see step 8),
    // so we only need to redraw when the visible state actually changes, not on every poll tick.
    bool needsRender = false;

    // 5. If mouse is moving, immediately hide the indicator unless it is pinned on screen.
    if (mouseMoved && !m_alwaysShow) {
        m_targetAlpha = 0.0f;
        m_hideAtTime = 0;
    }

    if (m_alwaysShow) {
        if (cursorShowing && mouseIdleForOneSecond) {
            bool wasHidden = (m_targetAlpha == 0.0f);
            m_hideAtTime = 0;
            m_targetAlpha = 1.0f;
            if (wasHidden || imeChanged || pt.x != m_triggerPos.x || pt.y != m_triggerPos.y) {
                needsRender = true;
            }
            m_triggerPos = pt;
            m_triggerCursorWidth = cursorWidth;
            m_triggerCursorHeight = cursorHeight;
            m_triggerXHotspot = xHotspot;
            m_triggerYHotspot = yHotspot;
        } else {
            m_hideAtTime = 0;
            m_targetAlpha = 0.0f;
        }
    }

    // 6. Detect triggers to show the indicator (Only trigger when NOT moving)
    bool triggerFired = false;

    // Trigger A: IME conversion status changed
    if (imeChanged) {
        triggerFired = true;
    }

    // Trigger B: Focus control changed (within same window or globally)
    if (hwndFocus != m_lastFocusWnd && hwndFocus != NULL) {
        triggerFired = true;
    }

    // Trigger C: Foreground active window changed
    if (hwndForeground != m_lastForegroundWnd && hwndForeground != NULL) {
        triggerFired = true;
    }

    // Trigger D: Cursor changed to I-beam (e.g. hovering/focusing text area)
    HCURSOR hIBeam = LoadCursor(NULL, IDC_IBEAM);
    if (currentCursor == hIBeam && m_lastCursorHandle != hIBeam) {
        triggerFired = true;
    }

    // Update tracked states
    m_lastFocusWnd = hwndFocus;
    m_lastForegroundWnd = hwndForeground;
    m_lastCursorHandle = currentCursor;

    // Trigger show if focus changes or IME toggles while stationary
    if (!m_alwaysShow && triggerFired && !mouseMoved && cursorShowing) {
        m_hideAtTime = now + 2000; // Show for ~2.0 seconds
        m_targetAlpha = 1.0f;
        m_triggerPos = pt;
        m_triggerCursorWidth = cursorWidth;
        m_triggerCursorHeight = cursorHeight;
        m_triggerXHotspot = xHotspot;
        m_triggerYHotspot = yHotspot;
        needsRender = true;
    } else if (imeChanged && m_targetAlpha > 0.0f) {
        // Badge is already visible and stationary but its color/text needs to reflect the new state.
        needsRender = true;
    }

    // 7. Manage display countdown (time-based, independent of poll interval)
    if (!m_alwaysShow && m_hideAtTime != 0 && now >= m_hideAtTime) {
        m_hideAtTime = 0;
        m_targetAlpha = 0.0f;
    }

    // 8. Apply visibility immediately with a fixed badge size and color.
    m_currentAlpha = m_targetAlpha;

    // 9. Position layered window and render only when something actually changed
    if (m_currentAlpha > 0.01f) {
        if (!m_windowVisible) {
            Show(true);
            needsRender = true;
        }

        if (needsRender) {
            // Anchor position to the point where the trigger was registered
            int wndX = m_triggerPos.x + (m_triggerCursorWidth - m_triggerXHotspot) + 2;
            int wndY = m_triggerPos.y + (m_triggerCursorHeight - m_triggerYHotspot) + 2;

            // Position layered window (centered around the hotpoint coordinates)
            SetWindowPos(m_hWnd, HWND_TOPMOST, wndX - 32, wndY - 32, 64, 64,
                         SWP_NOACTIVATE | SWP_NOSIZE);

            Render();
        }
    } else if (m_windowVisible) {
        m_currentAlpha = 0.0f;
        Show(false);
    }
}

// Draws one badge state (Korean or English) onto a 64x64 top-down 32bpp PARGB pixel buffer.
// m_currentAlpha is always exactly 0.0f or 1.0f in practice (see OnTimerTick step 8 - there is no
// per-frame animation), so both pre-rendered bitmaps are simply drawn once at full opacity.
void IndicatorWindow::PaintBadge(void* pBits, int width, int height, bool korean) {
    Gdiplus::Bitmap gdiBmp(width, height, width * 4, PixelFormat32bppPARGB, (BYTE*)pBits);
    Gdiplus::Graphics graphics(&gdiBmp);

    // High quality rendering options
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Clear background (completely transparent)
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    // Badge Dimensions
    float badgeW = 34.0f;
    float badgeH = 22.0f;
    float badgeX = ((float)width - badgeW) / 2.0f;
    float badgeY = ((float)height - badgeH) / 2.0f;

    float colorLerp = korean ? 1.0f : 0.0f;

    // 1. Calculate color states based on colorLerp
    // English Start: Slate-600 (45, 55, 72, Alpha 180)
    // Korean Start: Indigo-500 (99, 102, 241, Alpha 220)
    BYTE startA = (BYTE)(170.0f + colorLerp * (210.0f - 170.0f));
    BYTE startR = (BYTE)(45.0f + colorLerp * (99.0f - 45.0f));
    BYTE startG = (BYTE)(55.0f + colorLerp * (102.0f - 55.0f));
    BYTE startB = (BYTE)(72.0f + colorLerp * (241.0f - 72.0f));

    // English End: Slate-800 (26, 32, 44, Alpha 180)
    // Korean End: Indigo-600 (79, 70, 229, Alpha 220)
    BYTE endA = (BYTE)(170.0f + colorLerp * (210.0f - 170.0f));
    BYTE endR = (BYTE)(26.0f + colorLerp * (79.0f - 26.0f));
    BYTE endG = (BYTE)(32.0f + colorLerp * (70.0f - 32.0f));
    BYTE endB = (BYTE)(44.0f + colorLerp * (229.0f - 44.0f));

    // Border: White (Alpha 60 -> 140)
    BYTE borderA = (BYTE)(60.0f + colorLerp * (140.0f - 60.0f));

    Gdiplus::Color colorStart(startA, startR, startG, startB);
    Gdiplus::Color colorEnd(endA, endR, endG, endB);
    Gdiplus::Color colorBorder(borderA, 255, 255, 255);
    Gdiplus::Color colorShadow(45, 0, 0, 0);
    Gdiplus::Color colorSoftShadow(25, 0, 0, 0);

    // 2. Draw Soft Drop Shadows
    Gdiplus::GraphicsPath pathShadow;
    Gdiplus::RectF rectShadow1(badgeX, badgeY + 1.5f, badgeW, badgeH + 0.5f);
    GetRoundedRectPath(pathShadow, rectShadow1, 6.0f);
    Gdiplus::SolidBrush brushShadow(colorShadow);
    graphics.FillPath(&brushShadow, &pathShadow);

    Gdiplus::GraphicsPath pathSoftShadow;
    Gdiplus::RectF rectShadow2(badgeX, badgeY + 0.5f, badgeW, badgeH + 1.5f);
    GetRoundedRectPath(pathSoftShadow, rectShadow2, 6.0f);
    Gdiplus::SolidBrush brushSoftShadow(colorSoftShadow);
    graphics.FillPath(&brushSoftShadow, &pathSoftShadow);

    // 3. Draw Badge Background (Linear Gradient)
    Gdiplus::GraphicsPath pathBadge;
    Gdiplus::RectF rectBadge(badgeX, badgeY, badgeW, badgeH);
    GetRoundedRectPath(pathBadge, rectBadge, 6.0f);

    Gdiplus::LinearGradientBrush brushBackground(
        Gdiplus::PointF(badgeX, badgeY),
        Gdiplus::PointF(badgeX, badgeY + badgeH),
        colorStart,
        colorEnd
    );
    graphics.FillPath(&brushBackground, &pathBadge);

    // 4. Draw Border
    Gdiplus::Pen penBorder(colorBorder, 1.0f);
    graphics.DrawPath(&penBorder, &pathBadge);

    // 5. Draw Text (Crisp Typography)
    const wchar_t* pText = korean ? L"\uD55C" : L"E";  // 한 (U+D55C)
    Gdiplus::Font font(L"Segoe UI", 9.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);

    // Center alignment
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::SolidBrush brushText(Gdiplus::Color(255, 255, 255, 255));

    // Draw text with a very slight vertical adjustment to make it look visually perfectly centered
    Gdiplus::RectF rectText(badgeX, badgeY - 0.5f, badgeW, badgeH);
    graphics.DrawString(pText, -1, &font, rectText, &sf, &brushText);
}

void IndicatorWindow::CreatePrerenderedBadges() {
    DestroyPrerenderedBadges();

    const int width = 64;
    const int height = 64;

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcScreen = GetDC(NULL);

    HDC hdcEnglish = CreateCompatibleDC(hdcScreen);
    void* pBitsEnglish = nullptr;
    HBITMAP hBmpEnglish = CreateDIBSection(hdcEnglish, &bmi, DIB_RGB_COLORS, &pBitsEnglish, NULL, 0);
    SelectObject(hdcEnglish, hBmpEnglish);
    PaintBadge(pBitsEnglish, width, height, false);

    HDC hdcKorean = CreateCompatibleDC(hdcScreen);
    void* pBitsKorean = nullptr;
    HBITMAP hBmpKorean = CreateDIBSection(hdcKorean, &bmi, DIB_RGB_COLORS, &pBitsKorean, NULL, 0);
    SelectObject(hdcKorean, hBmpKorean);
    PaintBadge(pBitsKorean, width, height, true);

    ReleaseDC(NULL, hdcScreen);

    m_hdcBadgeEnglish = hdcEnglish;
    m_hBmpBadgeEnglish = hBmpEnglish;
    m_hdcBadgeKorean = hdcKorean;
    m_hBmpBadgeKorean = hBmpKorean;
}

void IndicatorWindow::DestroyPrerenderedBadges() {
    // Deleting the DC before the bitmap is the documented-safe alternative to explicitly
    // deselecting the bitmap first (see CreateDIBSection / DeleteObject remarks on MSDN).
    if (m_hdcBadgeKorean) {
        DeleteDC(m_hdcBadgeKorean);
        m_hdcBadgeKorean = NULL;
    }
    if (m_hBmpBadgeKorean) {
        DeleteObject(m_hBmpBadgeKorean);
        m_hBmpBadgeKorean = NULL;
    }
    if (m_hdcBadgeEnglish) {
        DeleteDC(m_hdcBadgeEnglish);
        m_hdcBadgeEnglish = NULL;
    }
    if (m_hBmpBadgeEnglish) {
        DeleteObject(m_hBmpBadgeEnglish);
        m_hBmpBadgeEnglish = NULL;
    }
}

void IndicatorWindow::Render() {
    HDC hdcSrc = m_isKorean ? m_hdcBadgeKorean : m_hdcBadgeEnglish;
    if (!hdcSrc) {
        return;
    }

    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA; // Per-pixel alpha

    POINT ptZero = { 0, 0 };
    SIZE sizeWnd = { 64, 64 };

    HDC hdcScreen = GetDC(NULL);
    UpdateLayeredWindow(m_hWnd, hdcScreen, NULL, &sizeWnd, hdcSrc, &ptZero, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, hdcScreen);
}
