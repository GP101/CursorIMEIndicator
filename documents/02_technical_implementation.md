# Cursor IME Indicator: 기술 구현 상세

> 선행 문서: [01_project_overview_and_architecture.md](01_project_overview_and_architecture.md)

이 문서는 현재 `src/`와 프로젝트 설정 파일을 기준으로, 각 처리 단계와 Win32/GDI+ 구현을 코드 수준에서 설명한다.

## 1. 빌드 대상과 의존성

`CursorIMEIndicator.vcxproj`는 `Debug|x64`, `Release|x64` 두 구성만 정의한다. 공통적으로 Unicode, C++17, Windows GUI subsystem을 사용하고 toolset은 `v143`, Windows SDK 대상 버전은 `10.0`이다.

링커 의존성은 다음과 같다.

| 라이브러리 | 용도 |
| --- | --- |
| `imm32.lib` | IME 기본 창 조회 (`ImmGetDefaultIMEWnd`) |
| `gdiplus.lib` | tray 아이콘과 배지의 anti-aliased 렌더링 |
| `gdi32.lib` | DIB section/DC 등 비트맵 자원 |
| `user32.lib` | 창, 메시지, 커서, 타이머, 입력 API |
| `shell32.lib` | 알림 영역 아이콘 |
| `advapi32.lib` | 레지스트리 설정 |
| `shlwapi.lib` | 프로젝트 링크 의존성으로 선언됨 |

Release 구성은 전역 최적화, 함수 수준 링크, COMDAT folding 및 참조 최적화를 사용한다. 현재 문서 작성 시점에는 소스 분석만 수행했으며 이 문서 작성 자체로 새 빌드를 실행하지는 않았다.

## 2. 프로세스 시작과 단일 인스턴스

진입점은 `src/main.cpp`의 `WinMain`이다.

1. `CreateMutexW(NULL, TRUE, L"Local\\CursorIMEIndicatorMutex")`로 로컬 세션 mutex를 만든다.
2. `ERROR_ALREADY_EXISTS`이면 이미 실행 중인 것으로 판단하고 메시지를 보인 뒤 종료한다.
3. `GdiplusStartup`을 한 번 호출해 프로세스 전체에서 tray 아이콘과 오버레이 렌더링이 공유할 GDI+ 세션을 연다.
4. `RegisterWindowMessageW(L"TaskbarCreated")`로 Explorer 재시작 감지용 동적 메시지 ID를 얻는다.
5. `CursorIMEBackgroundClass`를 등록하고 `WS_POPUP` 스타일의 보이지 않는 최상위 창을 만든다.

백그라운드 창을 message-only window로 만들지 않고 최상위 `WS_POPUP` 창으로 만드는 이유는 `WM_QUERYENDSESSION`, `WM_ENDSESSION`, `TaskbarCreated` 같은 시스템 브로드캐스트 메시지를 받기 위해서다.

## 3. tray와 명령 처리

### 3.1 아이콘 생성 및 등록

`CreateTrayIcon()`은 시스템 small-icon 크기를 구한 뒤 GDI+의 32-bit premultiplied-alpha bitmap에 원형 아이콘과 글자를 그린다. `Bitmap::GetHICON`으로 얻은 `HICON`은 전역 `g_hTrayIcon`에 보관한다.

`NOTIFYICONDATAW`는 다음 필드를 채워 `Shell_NotifyIconW(NIM_ADD, ...)`로 등록한다.

- `hWnd`: 백그라운드 창
- `uID`: `TRAY_ICON_ID` (= `1`)
- `uCallbackMessage`: `WM_USER + 100`
- `hIcon`: 동적으로 만든 tray 아이콘
- `szTip`: `Cursor IME Indicator`

Explorer가 재시작되면 `BackgroundWndProc`는 등록해 둔 `TaskbarCreated` 메시지를 받아 동일한 데이터로 `NIM_ADD`를 다시 호출한다. 종료 경로에서는 같은 `NOTIFYICONDATAW`에 대해 `NIM_DELETE`를 호출하고 `DestroyIcon`으로 아이콘 핸들을 해제한다.

### 3.2 메뉴와 명령 ID

tray 콜백은 오른쪽 버튼을 놓았을 때 popup menu를 만들고, 왼쪽 버튼을 놓았을 때 오버레이 enable 상태를 토글한다. 메뉴 명령은 다음과 같이 매핑된다.

| ID | 처리 | 상태 저장 |
| --- | --- | --- |
| `101` | `IndicatorWindow::SetEnabled`로 indicator 토글 | 저장하지 않음 |
| `102` | Run 키의 자동 시작 항목 토글 | Run 키에 저장 |
| `103` | About `MessageBoxW` 표시 | 없음 |
| `104` | 백그라운드 창 파괴 → 메시지 루프 종료 | 없음 |
| `105` | `SetAlwaysShow` 호출 | `AlwaysShow` DWORD 저장 |

`WM_ENDSESSION`에서 세션 종료가 확정되면 백그라운드 창을 파괴하고, `WM_DESTROY`는 `PostQuitMessage`로 공통 정리 경로로 이동시킨다.

## 4. IME 감지 알고리즘

구현은 `IMEDetector::IsKoreanMode()` 하나로 캡슐화되어 있다.

```text
GetForegroundWindow
  └─ GetWindowThreadProcessId
      └─ GetGUIThreadInfo → hwndFocus가 있으면 target으로 선택
          └─ ImmGetDefaultIMEWnd(target)
              └─ SendMessageTimeout(WM_IME_CONTROL, IMC_GETCONVERSIONMODE)
                  └─ result & IME_CMODE_NATIVE → Korean 여부
```

세부 동작은 다음과 같다.

1. 전경 창이 없으면 `false`를 반환한다.
2. 전경 창이 속한 GUI thread의 `GUITHREADINFO.hwndFocus`를 얻을 수 있으면 그것을 우선 대상 창으로 쓴다. 포커스 창이 없거나 조회에 실패하면 전경 창을 쓴다.
3. `ImmGetDefaultIMEWnd`로 대상의 기본 IME 창을 얻는다. 포커스 창의 조회가 실패하고 전경 창과 다를 때는 전경 창으로 한 번 더 시도한다.
4. IME 창에 `WM_IME_CONTROL`과 `IMC_GETCONVERSIONMODE` (`0x0001`)를 보낸다. `SendMessageTimeout`에 `SMTO_ABORTIFHUNG`, 250 ms를 사용하므로 멈춘 대상 프로세스가 이 유틸리티를 오래 막지 않는다.
5. 반환 변환 모드에서 `IME_CMODE_NATIVE` 비트를 검사한다. 설정되어 있으면 한글 모드(`true`)다.
6. 메시지 질의가 실패하면 현재 구현은 keyboard layout의 언어를 보조로 확인하지만, 이를 근거로 `true`를 반환하지는 않는다. 최종 결과는 `false`다.

따라서 `false`는 확실한 영문 모드뿐 아니라 전경 창 부재, IME 창 부재, 타임아웃, 호환되지 않는 대상 애플리케이션을 포함한다. UI와 문서에서 이를 “영문/알 수 없음”으로 이해해야 한다.

## 5. 오버레이 창의 생성 특성

`IndicatorWindow::Create()`는 `CursorIMEIndicatorClass`를 등록하고 64 x 64 `WS_POPUP` 창을 만든다.

| 스타일 | 효과 |
| --- | --- |
| `WS_EX_LAYERED` | `UpdateLayeredWindow`를 통한 per-pixel alpha 사용 |
| `WS_EX_TRANSPARENT` | 마우스 hit test를 통과시켜 입력을 가로채지 않음 |
| `WS_EX_NOACTIVATE` | 보일 때 전경 창의 포커스를 빼앗지 않음 |
| `WS_EX_TOOLWINDOW` | taskbar와 Alt+Tab 목록에 표시하지 않음 |
| `WS_EX_TOPMOST` | 다른 일반 창 위에 표시 |

창 생성 직후 IME 상태를 한 번 읽고, 마지막 이동 시간을 현재 tick으로 초기화한 다음 ID `1`의 timer를 16 ms로 시작한다. `GWLP_USERDATA`에 객체 포인터를 저장하고 정적 `WndProc`가 `WM_TIMER`를 인스턴스의 `OnTimerTick()`으로 전달한다.

## 6. 타이머, 입력 유휴, 표시 정책

### 6.1 타이머 모드

| 상수 | 값 | 사용 시점 |
| --- | ---: | --- |
| `TIMER_INTERVAL_ACTIVE` | 16 ms | 시작 직후, 일반 모드에서 표시 트리거 발생 시 |
| `TIMER_INTERVAL_IDLE` | 150 ms | 비활성 상태, 전경 창이 없을 때, 일반 모드가 숨겨져 안정된 때 |
| `TIMER_INTERVAL_ALWAYS_SHOW` | 1000 ms | Always Show가 켜진 상태에서 유휴 표시를 갱신할 때 |

현재 코드에는 `m_colorLerp`, `m_currentAlpha`, `m_targetAlpha`라는 상태 변수가 남아 있지만, `OnTimerTick()`은 목표 알파를 현재 알파에 바로 대입하고 IME 변경 시 색상 lerp도 목표값에 바로 맞춘다. 즉 이름과 일부 주석과 달리 시간 기반 fade, scale, color interpolation은 수행하지 않는다. 16 ms 주기는 일반 모드의 트리거 표시 카운트(120 tick, 약 2초)의 해상도를 제공한다.

### 6.2 매 tick의 입력 및 컨텍스트 수집

`OnTimerTick()`은 다음 순서로 현재 상태를 모은다.

1. indicator가 비활성이면 창을 숨기고 idle timer로 전환한다.
2. 전경 창이 없으면 창을 숨기고 idle timer로 전환한다.
3. `GetCursorInfo`와 `GetIconInfo`로 현재 커서 핸들, 표시 여부, 크기, hotspot을 얻는다. `GetIconInfo`가 만든 bitmap handle은 즉시 `DeleteObject`한다.
4. `GetCursorPos`로 이전 위치와 비교한다.
5. `GetAsyncKeyState`로 왼쪽/오른쪽/가운데/X1/X2 버튼이 눌렸는지 확인한다.
6. 위치 변화, 버튼이 눌린 상태, 또는 버튼 up/down 변화가 있으면 `m_lastMoveTime`을 현재 `GetTickCount64`로 갱신한다.
7. 현재 시간이 마지막 입력 시각보다 1,000 ms 이상 뒤인지로 유휴 여부를 계산한다.
8. 전경 창의 GUI thread에서 포커스 HWND를 구하고, `IMEDetector::IsKoreanMode()`로 입력 모드를 읽는다.

## 7. 트리거와 상태 전이

일반 모드의 표시 트리거는 다음 네 가지다.

- `IME` 변환 상태가 달라짐
- 포커스 HWND가 달라짐
- 전경 HWND가 달라짐
- 커서가 I-beam으로 바뀜

트리거가 발생해도 마우스가 움직였거나 커서가 보이지 않으면 표시하지 않는다. 조건을 만족하면 현재 커서 위치/geometry를 trigger anchor로 보관하고 `m_showTimeRemaining = 120`, `m_targetAlpha = 1.0f`로 설정한다. active timer에서 매 tick 카운트를 하나 줄이고 0이 되면 숨김을 요청한다.

Always Show는 일반 트리거와 별도 정책이다.

```text
Always Show ON
  ├─ cursor visible AND input idle >= 1 second → target alpha = 1, anchor 갱신
  └─ otherwise                               → target alpha = 0

Always Show OFF
  └─ trigger AND mouse stationary → 약 120 active ticks 표시
```

주의할 점은 일반 모드의 `mouseMoved` 조건이 Always Show에서는 숨김을 직접 실행하지 않지만, 바로 앞의 유휴 판정이 false가 되어 Always Show 분기에서 숨김을 요청한다는 것이다. 버튼 입력도 마지막 입력 시각을 갱신하므로 동일한 결과가 난다.

## 8. 위치 계산과 렌더링 파이프라인

### 8.1 커서 기준 위치

표시할 때 저장한 trigger 정보를 사용해 커서 이미지의 오른쪽 아래에 배지 중심을 배치한다.

```text
wndX = triggerX + (cursorWidth  - xHotspot) + 2
wndY = triggerY + (cursorHeight - yHotspot) + 2
SetWindowPos(..., wndX - 32, wndY - 32, 64, 64, SWP_NOACTIVATE | SWP_NOSIZE)
```

이 계산은 단순 고정 오프셋보다 커서 크기와 hotspot이 다른 시스템 커서에서도 배지가 커서 밖에 놓이도록 한다. 앵커는 트리거 시점에 고정되므로 일반 모드의 2초 표시 중 마우스를 움직이면 숨겨지고, 이동하는 배지를 계속 따라다니지 않는다.

### 8.2 64 x 64 alpha surface

`Render()`는 매 표시 갱신마다 다음 작업을 수행한다.

1. 화면 DC와 호환 memory DC를 만든다.
2. top-down 32-bit `BI_RGB` DIB section(64 x 64)을 만들고 memory DC에 선택한다.
3. DIB의 픽셀 버퍼로 `Gdiplus::Bitmap(PixelFormat32bppPARGB)`를 감싼다.
4. 투명하게 초기화한 뒤, 34 x 22 rounded rectangle에 그림자, 세로 gradient, 테두리, 중앙 텍스트를 그린다.
5. 한글 상태면 `L"\\uD55C"`, 그렇지 않으면 `L"E"`를 그린다.
6. `UpdateLayeredWindow(..., ULW_ALPHA)`로 완성된 DIB를 레이어드 창에 게시한다.
7. 원래 bitmap을 다시 선택하고 DIB, DC, 화면 DC를 역순으로 해제한다.

배경 gradient는 `m_colorLerp`에 따라 English 계열 slate 색 또는 Korean 계열 indigo 색을 선택한다. 현재 상태 변경 시 값이 즉시 0 또는 1이 되므로 중간 색은 정상 흐름에서 오래 유지되지 않는다. `m_currentAlpha`도 즉시 목표값이 되며, `UpdateLayeredWindow`의 `AC_SRC_ALPHA`가 픽셀별 투명도를 적용한다.

## 9. 레지스트리 구현

`main.cpp`의 헬퍼 함수는 Win32 registry API만 사용한다.

| 함수 | 동작 |
| --- | --- |
| `IsStartupRegistered` | Run 키에서 `CursorIMEIndicator` 값의 존재 여부를 확인 |
| `RegisterStartup(true)` | `GetModuleFileNameW`로 현재 exe 경로를 읽어 Run 키 `REG_SZ` 값으로 저장 |
| `RegisterStartup(false)` | Run 키 값을 삭제 |
| `IsAlwaysShowSaved` | `HKCU\Software\CursorIMEIndicator\AlwaysShow` DWORD를 읽어 bool 반환 |
| `SaveAlwaysShow` | 키가 없으면 만들고 DWORD `0`/`1` 저장 |

시작 프로그램 등록은 값 존재만 확인하며 저장된 경로가 현재 실행 파일과 동일한지 비교하지 않는다. 이 점은 설치 위치 변경이나 이식성 관련 기능을 추가할 때 고려할 구현 제약이다.

## 10. 변경 작업 시 점검 지점

| 변경하려는 영역 | 주로 수정할 파일 | 회귀 확인 |
| --- | --- | --- |
| tray 메뉴/설정/시작/종료 | `src/main.cpp` | 메뉴 check 상태, Run 키, Explorer 재시작 후 아이콘, 종료 |
| 한글 모드 판정 | `src/IMEDetector.cpp` | 한/영 전환, 앱별 포커스 전환, 응답 없는 대상에서 UI 정지 여부 |
| 표시 시점과 유휴 정책 | `src/IndicatorWindow.cpp/.h` | 마우스 이동/버튼 입력, 일반 2초 표시, Always Show 1초 유휴 |
| 시각 디자인/리소스 | `IndicatorWindow.cpp`, `main.cpp` | DPI/다양한 커서, 클릭 통과, Alt+Tab/taskbar 비노출 |
| 빌드/플랫폼 | `.vcxproj` | Debug/Release x64 빌드, 라이브러리 링크 |

빌드 후에는 최소한 한글/영문 전환, 일반 모드와 Always Show, tray enable/startup/exit, Explorer 재시작 뒤 tray 복구를 수동 점검하는 것이 좋다. 단순 컴파일 성공은 다른 프로세스의 IME 상태와 실제 Windows shell 통합까지 보장하지 않는다.
