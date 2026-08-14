핵심 문제: "60FPS 애니메이션"인데 실제로는 애니메이션이 없음

IndicatorWindow.cpp:338에서 알파값을 보간(lerp) 없이 즉시 대입합니다.
m_currentAlpha = m_targetAlpha;   // 보간 없음, 즉시 반영
m_colorLerp도 IME 상태가 바뀌는 순간 즉시 목표값으로 설정됩니다(IndicatorWindow.cpp:259-261). 즉 About 다이얼로그에 적힌 "60 FPS 부드러운 전환 효과"는 실제 코드에는 존재하지 않고, 값은 매번 즉시 확정되는데도 배지가 보이는 2초 동안(120프레임) 16ms 간격으로 계속 폴링·렌더링을 하고 있습니다. 이게 CPU 사용량의 가장 큰 원인입니다.

배지가 뜰 때마다 16ms마다 반복되는 작업:
- GetForegroundWindow, GetGUIThreadInfo
- IMEDetector::IsKoreanMode() → SendMessageTimeout(최대 250ms 대기, 프로세스 간 동기 메시지)
- GetCursorInfo + GetIconInfo + GetObject + DeleteObject (커서 아이콘마다 GDI 핸들 생성/삭제)
- Render() 전체: CreateCompatibleDC, CreateDIBSection, GDI+ Bitmap/Graphics/Font/LinearGradientBrush/Pen/GraphicsPath 매 프레임 새로 생성 후 파괴, UpdateLayeredWindow 호출

값이 바뀌지 않는데도 이 전체 파이프라인을 초당 60번, 2초간(120회) 반복 실행합니다.

상시 유휴 폴링도 존재

앱이 켜져 있는 한 유휴 상태에서도 TIMER_INTERVAL_IDLE = 150ms(IndicatorWindow.h:59)마다 위와 거의 같은 작업(포그라운드 조회, IME 조회 via SendMessageTimeout, 커서 아이콘 조회 등)이 영구히 반복됩니다. "항상 표시" 옵션을 켜면 1초 간격으로 줄어들지만, 기본 유휴 상태는 초당 ~6.7회입니다.

개선 방안 (우선순위 순)

1. 표시 중 타이머를 60FPS로 유지할 이유가 없음 — 실제 보간이 없으므로, 트리거 발생 시 딱 1번만 렌더링하고, 2초 후 숨김 처리를 위한 타이머만 저빈도(예: 100~200ms)로 돌리면 됩니다. 이것만으로 배지 표시 시 발생하는 CPU 부하 대부분(최대 120회 → 1회 렌더링)이 사라집니다.
2. 폴링 대신 이벤트 훅 사용 — GetForegroundWindow/GetGUIThreadInfo로 포그라운드·포커스 변경을 매 틱마다 감지하는 대신, SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_FOCUS, ...)로 실제 변경 시에만 콜백을 받도록 하면 상시 폴링 자체를 없앨 수 있습니다.
3. IME 상태 조회 빈도 최소화 — SendMessageTimeout은 대상 윈도우가 응답 없으면 최대 250ms까지 블로킹될 수 있는 프로세스 간 동기 호출입니다. 매 틱(16ms/150ms)마다 부를 게 아니라, 포커스/포그라운드가 바뀌었을 때, 그리고 표시 중일 때만 저빈도(예: 200~300ms)로 확인하면 충분합니다.
4. 커서 아이콘 조회 캐싱 — GetIconInfo+GetObject+DeleteObject는 GDI 핸들을 매번 만들고 지우는 상대적으로 비싼 호출입니다. currentCursor == m_lastCursorHandle이면 스킵하도록(현재는 커서가 바뀌지 않아도 매 틱 호출) 캐싱하세요.
5. Render()의 GDI/GDI+ 리소스 매 프레임 재생성 제거 — HDC, DIB 섹션, Font, Brush, GraphicsPath 등을 멤버로 캐싱하고 크기가 바뀔 때만 재생성하세요. 상태는 한/영 두 가지뿐이므로 아예 두 상태의 비트맵을 한 번씩만 미리 그려두고 UpdateLayeredWindow에서 골라 쓰는 방식도 가능합니다.

1. There is no reason to keep the display timer running at 60 FPS — since no actual interpolation is taking place, render only once when the trigger occurs, and use a low-frequency timer (e.g., every 100–200 ms) only to hide it after 2 seconds. This alone will eliminate most of the CPU load caused by displaying the badge, reducing rendering from up to 120 times to just once.

2. Use event hooks instead of polling — rather than detecting foreground and focus changes on every tick using `GetForegroundWindow` and `GetGUIThreadInfo`, use `SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_FOCUS, ...)` so that callbacks are received only when actual changes occur. This eliminates continuous polling altogether.

3. Minimize the frequency of IME status queries — `SendMessageTimeout` is a synchronous cross-process call that can block for up to 250 ms if the target window is unresponsive. Instead of calling it on every tick (every 16 ms or 150 ms), it should be sufficient to query it when the focus or foreground window changes, and at a low frequency (e.g., every 200–300 ms) only while the indicator is visible.

4. Cache cursor icon information — `GetIconInfo + GetObject + DeleteObject` are relatively expensive calls because they repeatedly create and destroy GDI handles. Cache the cursor handle and skip these calls when `currentCursor == m_lastCursorHandle`. Currently, these functions are called on every tick even when the cursor has not changed.

5. Avoid recreating GDI/GDI+ resources in `Render()` on every frame — cache resources such as the HDC, DIB section, `Font`, `Brush`, and `GraphicsPath` as member objects, and recreate them only when the size changes. Since there are only two states, Korean and English, another option is to pre-render a bitmap for each state once and simply select the appropriate one when calling `UpdateLayeredWindow`. 
