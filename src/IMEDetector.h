#pragma once
#include <windows.h>

namespace IMEDetector {
    // Returns true if the current active foreground window is in Hangul (Korean) input mode.
    // Returns false if it is in English/Alphanumeric mode or if detection fails.
    bool IsKoreanMode();
}
