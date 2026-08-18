#include "IMEDetector.h"
#include <imm.h>

#pragma comment(lib, "imm32.lib")

namespace IMEDetector {
    // Helper: query IME conversion mode directly via ImmGetContext/ImmGetConversionStatus.
    // Returns true if native (Korean) mode, false otherwise or on failure.
    static bool QueryImeConversionDirect(HWND hwnd) {
        if (!hwnd) return false;
        HIMC hImc = ImmGetContext(hwnd);
        if (!hImc) return false;
        DWORD dwConversion = 0, dwSentence = 0;
        bool result = false;
        if (ImmGetConversionStatus(hImc, &dwConversion, &dwSentence)) {
            result = (dwConversion & IME_CMODE_NATIVE) != 0;
        }
        ImmReleaseContext(hwnd, hImc);
        return result;
    }

    bool IsKoreanMode() {
        HWND hwndForeground = GetForegroundWindow();
        if (!hwndForeground) return false;

        // Get the process and thread ID of the foreground window
        DWORD dwProcessId = 0;
        DWORD dwThreadId = GetWindowThreadProcessId(hwndForeground, &dwProcessId);
        
        HWND hwndTarget = hwndForeground;
        if (dwThreadId != 0) {
            GUITHREADINFO gti = { 0 };
            gti.cbSize = sizeof(GUITHREADINFO);
            if (GetGUIThreadInfo(dwThreadId, &gti)) {
                if (gti.hwndFocus) {
                    hwndTarget = gti.hwndFocus;
                }
            }
        }

        // Primary method: ImmGetDefaultIMEWnd + WM_IME_CONTROL
        // Works for most classic Win32 applications.
        HWND hIMEWnd = ::ImmGetDefaultIMEWnd(hwndTarget);
        if (!hIMEWnd && hwndTarget != hwndForeground) {
            hIMEWnd = ::ImmGetDefaultIMEWnd(hwndForeground);
        }

        if (hIMEWnd) {
            // IMC_GETCONVERSIONMODE = 0x0001
            // We use SendMessageTimeout to avoid hanging if the target window is unresponsive.
            DWORD_PTR dwResult = 0;
            if (SendMessageTimeout(hIMEWnd, WM_IME_CONTROL, 0x0001, 0, SMTO_ABORTIFHUNG, 250, &dwResult)) {
                return (dwResult & IME_CMODE_NATIVE) != 0;
            }
        }

        // Fallback A: ImmGetContext / ImmGetConversionStatus directly on the focused window.
        // This covers apps like Windows Terminal (ConPTY) where ImmGetDefaultIMEWnd returns NULL
        // or an unresponsive pseudo-console window, but the IMC attached to the focused HWND
        // still carries the correct conversion mode.
        if (hwndTarget != hwndForeground) {
            bool result = QueryImeConversionDirect(hwndTarget);
            // ImmGetContext on a window from another process returns NULL in-process, but the
            // call succeeds when the window belongs to the same thread that loaded the IMM32 DLL.
            // If we got a valid context, trust the result.
            HIMC hImcCheck = ImmGetContext(hwndTarget);
            if (hImcCheck) {
                ImmReleaseContext(hwndTarget, hImcCheck);
                return result;
            }
        }
        {
            HIMC hImcCheck = ImmGetContext(hwndForeground);
            if (hImcCheck) {
                ImmReleaseContext(hwndForeground, hImcCheck);
                return QueryImeConversionDirect(hwndForeground);
            }
        }

        // Fallback B: keyboard layout language check (heuristic only; cannot distinguish
        // Korean-layout in English mode, so we return false conservatively).
        HKL hkl = GetKeyboardLayout(dwThreadId);
        WORD langId = LOWORD(hkl);
        (void)langId; // PRIMARYLANGID(langId) == LANG_KOREAN is unreliable here

        return false;
    }
}

