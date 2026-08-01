#include "IndicatorWindow.h"
#include "IMEDetector.h"
#include <cmath>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

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
    , m_isKorean(false)
    , m_colorLerp(0.0f)
    , m_targetColorLerp(0.0f)
    , m_currentAlpha(0.0f)
    , m_targetAlpha(0.0f)
    , m_currentScale(1.0f)
    , m_lastMoveTime(0)
    , m_windowVisible(false)
    , m_lastFocusWnd(NULL)
    , m_lastForegroundWnd(NULL)
    , m_lastCursorHandle(NULL)
    , m_showTimeRemaining(0)
    , m_triggerCursorWidth(32)
    , m_triggerCursorHeight(32)
    , m_triggerXHotspot(0)
    , m_triggerYHotspot(0)
    , m_currentTimerInterval(TIMER_INTERVAL_ACTIVE)
{
    m_lastCursorPos.x = 0;
    m_lastCursorPos.y = 0;
    m_triggerPos.x = 0;
    m_triggerPos.y = 0;
}

IndicatorWindow::~IndicatorWindow() {
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        UnregisterClassW(L"CursorIMEIndicatorClass", m_hInstance);
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
    m_colorLerp = m_isKorean ? 1.0f : 0.0f;
    m_targetColorLerp = m_colorLerp;
    m_lastMoveTime = GetTickCount64();

    // Start timer at ~60 FPS (16 ms)
    SetTimer(m_hWnd, 1, TIMER_INTERVAL_ACTIVE, NULL);
    m_currentTimerInterval = TIMER_INTERVAL_ACTIVE;

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
            Show(false);
        }
        if (m_currentTimerInterval != TIMER_INTERVAL_IDLE) {
            SetTimer(m_hWnd, 1, TIMER_INTERVAL_IDLE, NULL);
            m_currentTimerInterval = TIMER_INTERVAL_IDLE;
        }
        return;
    }

    // Query current active foreground window first.
    // If there is no active foreground window (e.g. system is logged off, locked, or screen transitioning),
    // we should immediately hide the indicator and switch to idle mode to save CPU.
    HWND hwndForeground = GetForegroundWindow();
    if (!hwndForeground) {
        m_targetAlpha = 0.0f;
        m_currentAlpha = 0.0f;
        m_showTimeRemaining = 0;
        if (m_windowVisible) {
            Show(false);
        }
        if (m_currentTimerInterval != TIMER_INTERVAL_IDLE) {
            SetTimer(m_hWnd, 1, TIMER_INTERVAL_IDLE, NULL);
            m_currentTimerInterval = TIMER_INTERVAL_IDLE;
        }
        return;
    }

    // 1. Detect current cursor info
    CURSORINFO ci = { sizeof(CURSORINFO) };
    bool cursorShowing = false;
    int cursorWidth = 32;
    int cursorHeight = 32;
    int xHotspot = 0;
    int yHotspot = 0;
    HCURSOR currentCursor = NULL;

    if (GetCursorInfo(&ci)) {
        if (ci.flags & CURSOR_SHOWING) {
            cursorShowing = true;
            currentCursor = ci.hCursor;
            ICONINFO ii;
            if (GetIconInfo(ci.hCursor, &ii)) {
                xHotspot = ii.xHotspot;
                yHotspot = ii.yHotspot;
                BITMAP bmpColor = { 0 };
                BITMAP bmpMask = { 0 };
                if (ii.hbmColor) {
                    GetObject(ii.hbmColor, sizeof(BITMAP), &bmpColor);
                    cursorWidth = bmpColor.bmWidth;
                    cursorHeight = bmpColor.bmHeight;
                } else if (ii.hbmMask) {
                    GetObject(ii.hbmMask, sizeof(BITMAP), &bmpMask);
                    cursorWidth = bmpMask.bmWidth;
                    cursorHeight = bmpMask.bmHeight / 2;
                }
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
                if (ii.hbmMask) DeleteObject(ii.hbmMask);
            }
        }
    }

    // 2. Track mouse position & movement
    POINT pt;
    GetCursorPos(&pt);
    bool mouseMoved = false;
    if (pt.x != m_lastCursorPos.x || pt.y != m_lastCursorPos.y) {
        m_lastCursorPos = pt;
        m_lastMoveTime = GetTickCount64();
        mouseMoved = true;
    }

    // 3. Query current active focused control
    HWND hwndFocus = NULL;
    DWORD dwThreadId = GetWindowThreadProcessId(hwndForeground, NULL);
    if (dwThreadId != 0) {
        GUITHREADINFO gti = { 0 };
        gti.cbSize = sizeof(GUITHREADINFO);
        if (GetGUIThreadInfo(dwThreadId, &gti)) {
            hwndFocus = gti.hwndFocus;
        }
    }
    if (!hwndFocus) {
        hwndFocus = hwndForeground;
    }

    // 4. Query current IME status
    bool currentKorean = IMEDetector::IsKoreanMode();
    bool imeChanged = (currentKorean != m_isKorean);
    if (imeChanged) {
        m_isKorean = currentKorean;
        m_targetColorLerp = currentKorean ? 1.0f : 0.0f;
        m_currentScale = 1.35f; // Pop scale animation
    }

    // 5. If mouse is moving, immediately hide the indicator
    if (mouseMoved) {
        m_targetAlpha = 0.0f;
        m_showTimeRemaining = 0;
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
    if (triggerFired && !mouseMoved && cursorShowing) {
        m_showTimeRemaining = 120; // Show for 120 frames (~2.0 seconds)
        m_targetAlpha = 1.0f;
        m_triggerPos = pt;
        m_triggerCursorWidth = cursorWidth;
        m_triggerCursorHeight = cursorHeight;
        m_triggerXHotspot = xHotspot;
        m_triggerYHotspot = yHotspot;

        // Switch to active high-frequency timer for smooth animation
        if (m_currentTimerInterval != TIMER_INTERVAL_ACTIVE) {
            SetTimer(m_hWnd, 1, TIMER_INTERVAL_ACTIVE, NULL);
            m_currentTimerInterval = TIMER_INTERVAL_ACTIVE;
        }
    }

    // 7. Manage display countdown
    if (m_showTimeRemaining > 0) {
        m_showTimeRemaining--;
        if (m_showTimeRemaining == 0) {
            m_targetAlpha = 0.0f;
        }
    }

    // 8. Interpolate animations
    float scaleDiff = 1.0f - m_currentScale;
    float colorDiff = m_targetColorLerp - m_colorLerp;

    if (m_targetAlpha <= 0.0f) {
        m_currentAlpha = 0.0f;
    } else {
        m_currentAlpha += (m_targetAlpha - m_currentAlpha) * 0.2f;
    }
    m_currentScale += scaleDiff * 0.15f;
    m_colorLerp += colorDiff * 0.15f;

    // Boundary corrections
    if (std::abs(m_currentAlpha - m_targetAlpha) < 0.005f) m_currentAlpha = m_targetAlpha;
    if (std::abs(m_currentScale - 1.0f) < 0.005f) m_currentScale = 1.0f;
    if (std::abs(m_colorLerp - m_targetColorLerp) < 0.005f) m_colorLerp = m_targetColorLerp;

    // 9. Position layered window and render
    if (m_currentAlpha > 0.01f) {
        if (!m_windowVisible) {
            Show(true);
        }

        // Anchor position to the point where the trigger was registered
        int wndX = m_triggerPos.x + (m_triggerCursorWidth - m_triggerXHotspot) + 2;
        int wndY = m_triggerPos.y + (m_triggerCursorHeight - m_triggerYHotspot) + 2;

        // Position layered window (centered around the hotpoint coordinates)
        SetWindowPos(m_hWnd, HWND_TOPMOST, wndX - 32, wndY - 32, 64, 64, 
                     SWP_NOACTIVATE | SWP_NOSIZE);

        Render();
    } else {
        if (m_windowVisible) {
            m_currentAlpha = 0.0f;
            Show(false);
        }

        // When animations are fully settled and the window is hidden, switch to low-CPU idle timer
        if (m_showTimeRemaining == 0 &&
            m_currentAlpha == 0.0f &&
            m_targetAlpha == 0.0f &&
            m_currentScale == 1.0f &&
            m_colorLerp == m_targetColorLerp &&
            m_currentTimerInterval != TIMER_INTERVAL_IDLE)
        {
            SetTimer(m_hWnd, 1, TIMER_INTERVAL_IDLE, NULL);
            m_currentTimerInterval = TIMER_INTERVAL_IDLE;
        }
    }
}

void IndicatorWindow::Render() {
    int width = 64;
    int height = 64;

    // Create DIB section for 32-bit PARGB drawing
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    // GDI+ Graphics from DIB
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
    float badgeX = (64.0f - badgeW) / 2.0f;
    float badgeY = (64.0f - badgeH) / 2.0f;

    // 1. Calculate color states based on m_colorLerp
    // English Start: Slate-600 (45, 55, 72, Alpha 180)
    // Korean Start: Indigo-500 (99, 102, 241, Alpha 220)
    BYTE startA = (BYTE)(170.0f + m_colorLerp * (210.0f - 170.0f));
    BYTE startR = (BYTE)(45.0f + m_colorLerp * (99.0f - 45.0f));
    BYTE startG = (BYTE)(55.0f + m_colorLerp * (102.0f - 55.0f));
    BYTE startB = (BYTE)(72.0f + m_colorLerp * (241.0f - 72.0f));

    // English End: Slate-800 (26, 32, 44, Alpha 180)
    // Korean End: Indigo-600 (79, 70, 229, Alpha 220)
    BYTE endA = (BYTE)(170.0f + m_colorLerp * (210.0f - 170.0f));
    BYTE endR = (BYTE)(26.0f + m_colorLerp * (79.0f - 26.0f));
    BYTE endG = (BYTE)(32.0f + m_colorLerp * (70.0f - 32.0f));
    BYTE endB = (BYTE)(44.0f + m_colorLerp * (229.0f - 44.0f));

    // Border: White (Alpha 60 -> 140)
    BYTE borderA = (BYTE)(60.0f + m_colorLerp * (140.0f - 60.0f));

    // Multiply overall window alpha (m_currentAlpha) into drawing opacities
    float finalAlphaMultiplier = m_currentAlpha;
    Gdiplus::Color colorStart((BYTE)(startA * finalAlphaMultiplier), startR, startG, startB);
    Gdiplus::Color colorEnd((BYTE)(endA * finalAlphaMultiplier), endR, endG, endB);
    Gdiplus::Color colorBorder((BYTE)(borderA * finalAlphaMultiplier), 255, 255, 255);
    Gdiplus::Color colorShadow((BYTE)(45 * finalAlphaMultiplier), 0, 0, 0);
    Gdiplus::Color colorSoftShadow((BYTE)(25 * finalAlphaMultiplier), 0, 0, 0);

    // Apply scale transform centered on the badge
    float centerX = 32.0f;
    float centerY = 32.0f;
    graphics.TranslateTransform(centerX, centerY);
    graphics.ScaleTransform(m_currentScale, m_currentScale);
    graphics.TranslateTransform(-centerX, -centerY);

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
    const wchar_t* pText = (m_colorLerp < 0.5f) ? L"E" : L"\uD55C";  // 한 (U+D55C)
    Gdiplus::Font font(L"Segoe UI", 9.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    
    // Center alignment
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::SolidBrush brushText(Gdiplus::Color((BYTE)(255 * finalAlphaMultiplier), 255, 255, 255));
    
    // Draw text with a very slight vertical adjustment to make it look visually perfectly centered
    Gdiplus::RectF rectText(badgeX, badgeY - 0.5f, badgeW, badgeH);
    graphics.DrawString(pText, -1, &font, rectText, &sf, &brushText);

    // 6. Update Layered Window via API
    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA; // Per-pixel alpha

    POINT ptZero = { 0, 0 };
    SIZE sizeWnd = { width, height };

    UpdateLayeredWindow(m_hWnd, hdcScreen, NULL, &sizeWnd, hdcMem, &ptZero, 0, &blend, ULW_ALPHA);

    // Cleanup GDI objects
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}
