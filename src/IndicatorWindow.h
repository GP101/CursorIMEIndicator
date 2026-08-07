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
    
    // Animation & State Variables
    bool m_isKorean;
    float m_colorLerp;       // 0.0f = English, 1.0f = Korean
    float m_targetColorLerp;
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
    int m_showTimeRemaining;
    POINT m_triggerPos;
    int m_triggerCursorWidth;
    int m_triggerCursorHeight;
    int m_triggerXHotspot;
    int m_triggerYHotspot;

    // Timer Interval Settings
    UINT m_currentTimerInterval;
    static const UINT TIMER_INTERVAL_ACTIVE = 16;  // ~60 FPS for smooth animation
    static const UINT TIMER_INTERVAL_IDLE = 150;   // ~6 FPS for low-CPU polling
    static const UINT TIMER_INTERVAL_ALWAYS_SHOW = 1000; // 1 FPS while always-show is enabled

    // Draw the indicator onto a layered window
    void Render();

    // Update positions and animations
    void OnTimerTick();
};
