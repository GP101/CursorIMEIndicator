#include "IMEDetector.h"
#include <imm.h>

#pragma comment(lib, "imm32.lib")

namespace IMEDetector {
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

        // Retrieve the default IME window for the target window
        HWND hIMEWnd = ::ImmGetDefaultIMEWnd(hwndTarget);
        if (!hIMEWnd && hwndTarget != hwndForeground) {
            hIMEWnd = ::ImmGetDefaultIMEWnd(hwndForeground);
        }

        if (hIMEWnd) {
            // IMC_GETCONVERSIONMODE = 0x0001
            // We use SendMessageTimeout to avoid hanging if the target window is unresponsive.
            DWORD_PTR dwResult = 0;
            if (SendMessageTimeout(hIMEWnd, WM_IME_CONTROL, 0x0001, 0, SMTO_ABORTIFHUNG, 250, &dwResult)) {
                // IME_CMODE_NATIVE / IME_CMODE_HANGEUL is 0x0001
                return (dwResult & IME_CMODE_NATIVE) != 0;
            }
        }

        // Fallback: Check active keyboard layout language ID as a secondary heuristic
        // (Korean primary language ID is 0x12)
        HKL hkl = GetKeyboardLayout(dwThreadId);
        WORD langId = LOWORD(hkl);
        if (PRIMARYLANGID(langId) == LANG_KOREAN) {
            // In Korean keyboard layout, if IME default window query failed, we might check keys or fallback to false.
            // But let's keep it safe. Most standard apps respond to ImmGetDefaultIMEWnd.
        }

        return false;
    }
}
