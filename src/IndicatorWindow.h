#pragma once
#include <windows.h>
#include <gdiplus.h>

class IndicatorWindow {
public:
    IndicatorWindow();
    ~IndicatorWindow();

    // Create the layered window
    bool Create(HINSTANCE hInstance);

    // Show or hide the window
    void Show(bool show);

    // Toggle enabled state
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_isEnabled; }

    // Keep the current IME state visible while the cursor is available.
    void SetAlwaysShow(bool alwaysShow);
    bool IsAlwaysShow() const { return m_alwaysShow; }

    // Window procedure callback
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hWnd;
    HINSTANCE m_hInstance;
    bool m_isEnabled;
    bool m_alwaysShow;

    // Foreground/focus tracking is event-driven (see WinEventProc) instead of polled every tick.
    HWINEVENTHOOK m_hForegroundHook;
    HWINEVENTHOOK m_hFocusHook;
    HWND m_currentForegroundWnd;
    HWND m_currentFocusWnd;
    static IndicatorWindow* s_pInstance;
    static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                                       LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime);
    
    // Visible state. There is no per-frame animation: alpha is applied immediately and the badge
    // color/text is fully determined by m_isKorean (see PaintBadge).
    bool m_isKorean;
    float m_currentAlpha;    // 0.0f = Transparent, 1.0f = Opaque
    float m_targetAlpha;
    
    POINT m_lastCursorPos;
    ULONGLONG m_lastMoveTime;
    bool m_lastMouseButtonDown;
    bool m_windowVisible;

    // Trigger & Anchor Variables
    HWND m_lastFocusWnd;
    HWND m_lastForegroundWnd;
    HCURSOR m_lastCursorHandle;
    ULONGLONG m_hideAtTime; // 0 = no hide scheduled; otherwise GetTickCount64() deadline
    POINT m_triggerPos;
    int m_triggerCursorWidth;
    int m_triggerCursorHeight;
    int m_triggerXHotspot;
    int m_triggerYHotspot;

    // Cached cursor icon metrics, keyed off m_lastCursorHandle. GetIconInfo/GetObject allocate and
    // free GDI handles, so we only re-query them when the cursor handle actually changes.
    int m_cachedCursorWidth;
    int m_cachedCursorHeight;
    int m_cachedCursorXHotspot;
    int m_cachedCursorYHotspot;

    // IME polling is throttled: checked immediately on focus/foreground change or while the badge
    // is showing, otherwise only every IME_CHECK_INTERVAL_MS (SendMessageTimeout is a cross-process
    // synchronous call and can block for up to 250ms if the target is unresponsive).
    ULONGLONG m_lastImeCheckTime;
    // Deferred re-check: after a focus/foreground change the target app (e.g. Windows Terminal)
    // may not have updated its IME state yet. We schedule a second query ~200 ms later to catch
    // the correct state once the window has fully activated.
    ULONGLONG m_pendingImeRecheckTime; // 0 = none pending
    static const UINT IME_CHECK_INTERVAL_MS = 300;

    // Timer Interval Settings
    // The badge has no per-frame animation (color/alpha are applied immediately), so a single
    // low-frequency poll interval is enough for state detection; we only render when the visible
    // state actually changes instead of redrawing every tick.
    UINT m_currentTimerInterval;
    static const UINT TIMER_INTERVAL_POLL = 150;         // state polling / trigger detection
    static const UINT TIMER_INTERVAL_ALWAYS_SHOW = 1000; // 1 Hz while always-show is enabled

    // Pre-rendered badge bitmaps. Only two visible states exist (Korean / English) and neither
    // animates, so both are drawn once and Render() just picks one instead of re-running the full
    // GDI+ pipeline (DIB section, Graphics, Font, Brushes, Paths) on every redraw.
    HDC m_hdcBadgeKorean;
    HBITMAP m_hBmpBadgeKorean;
    HDC m_hdcBadgeEnglish;
    HBITMAP m_hBmpBadgeEnglish;
    void CreatePrerenderedBadges();
    void DestroyPrerenderedBadges();
    static void PaintBadge(void* pBits, int width, int height, bool korean);

    // Draw the indicator onto a layered window
    void Render();

    // Update positions and animations
    void OnTimerTick();
};
