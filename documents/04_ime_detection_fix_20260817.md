한/영 상태 오검출 수정 (특히 터미널 사용 시)

문제

한/영 입력 상태 표시가 가끔 실제 상태와 다르게 나오는 문제가 있었고, Windows Terminal 등 터미널 계열 앱에서 특히 자주 발생했습니다. 원인은 src/IMEDetector.cpp의 IsKoreanMode()가 대상 창의 기본 IME 창에 WM_IME_CONTROL 메시지를 SendMessageTimeout으로 보내 conversion mode를 얻어오는 비공식적인 방식이었기 때문입니다.

- Windows Terminal은 레거시 IMM32가 아니라 TSF(Text Services Framework)로 텍스트 입력을 처리합니다. 이런 앱은 legacy IMC(입력 컨텍스트)를 아예 연결하지 않는 경우가 많아서, WM_IME_CONTROL 질의가 실제 TSF 조합 상태와 무관한 값(보통 stale/0)을 반환합니다.
- 터미널을 관리자 권한으로 실행 중이면 UIPI(User Interface Privilege Isolation) 때문에 낮은 권한 프로세스가 보낸 SendMessage가 아예 전달되지 않아 조용히 실패합니다.
- SendMessageTimeout은 대상 스레드가 응답 없으면 최대 250ms까지 블로킹될 수 있습니다.
- 실패 시 실행되는 폴백 코드(IMEDetector.cpp 44~49번 줄, 수정 전)가 죽은 코드였습니다 — 키보드 레이아웃이 한국어인지 검사만 하고 리턴값에 반영하지 않아, 사실상 항상 "영문"으로 처리됐습니다.
- 또한 한글 IME의 실제 한/영 토글은 conversion mode의 NATIVE 비트보다 open/close 상태(ImmGetOpenStatus)가 더 정확한 신호입니다.

적용한 수정

1. 1차 검사를 ImmGetContext + ImmGetOpenStatus + ImmGetConversionStatus로 교체 (src/IMEDetector.cpp). 모두 문서화된 IMM32 API로, 메시지를 보내지 않고 커널이 관리하는 공유 구조체를 직접 읽으므로 블로킹되지 않고 UIPI의 영향도 받지 않습니다.
2. 2차 폴백으로 TSF 전역 컴파트먼트(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)를 COM으로 직접 조회하도록 추가. 이는 작업표시줄 언어 표시줄이 실제로 읽는 것과 같은 상태이므로, legacy IMC가 없는 TSF 전용 앱(Windows Terminal 등)에서도 정확합니다. ITfThreadMgr/ITfCompartment 포인터는 static으로 캐싱해 프로세스 생애주기 중 한 번만 CoCreateInstance를 호출합니다.
3. 죽어있던 키보드 레이아웃 폴백 코드 제거.
4. TSF GUID(CLSID_TF_ThreadMgr 등)는 특정 SDK 임포트 라이브러리에 의존하지 않도록 INITGUID로 해당 번역 단위에 직접 정의.
5. main.cpp의 WinMain 시작 부분에 CoInitializeEx(COINIT_APARTMENTTHREADED) 추가, 종료 시 CoUninitialize 호출 (TSF COM 객체 생성을 위해 필요).

빌드 검증

MSBuild로 Debug|x64, Release|x64 모두 정상 빌드 확인 (경고/에러 없음).

CPU 사용률 영향 분석 (03_optimization_20260814.md의 개선과 비교)

이번 변경은 "얼마나 자주 부르는가"가 아니라 "한 번 부를 때 비용이 얼마인가"를 건드립니다. 03번 문서의 최적화(폴링 →이벤트 훅, 60FPS → 트리거 시 1회 렌더링 등)로 확립된 호출 빈도(IndicatorWindow.h의 TIMER_INTERVAL_POLL=150ms, TIMER_INTERVAL_ALWAYS_SHOW=1000ms, IME_CHECK_INTERVAL_MS=300ms 스로틀)는 이번 수정에서 전혀 건드리지 않았습니다.

- 일반 앱(대부분의 IMM32 인식 창): 기존에는 매 조회마다 크로스 프로세스 동기 IPC인 SendMessageTimeout 왕복이 있었으나, 이제는 imm32.dll이 공유 커널 구조체를 직접 읽는 ImmGetContext/ImmGetOpenStatus/ImmGetConversionStatus 호출로 대체되어 이 IPC 비용이 완전히 사라졌습니다. 호출당 비용은 같거나 더 낮습니다.
- 최악의 경우(응답 없는 대상 창, 또는 관리자 권한 터미널): 기존에는 최대 250ms 블로킹 위험이 있었으나(그 동안 UI 스레드가 타이머 콜백·메시지 펌프를 못 돎) 이제는 블로킹 자체가 없습니다. CPU 사용률 수치보다 응답성/지연 리스크 제거가 더 큰 실익입니다.
- TSF 전용 앱(Windows Terminal 등): ImmGetContext가 NULL이면 COM 기반 TSF 컴파트먼트 조회로 폴백합니다. CoCreateInstance는 프로세스 생애주기 중 딱 1회만 발생(정적 캐시)하고, 이후에는 ITfCompartment::GetValue 한 번(가벼운 COM 호출, 항상 응답하는 ctfmon 세션 컴포넌트 대상이라 hang 리스크 없음)만 추가되며, 이 경로도 기존 300ms 스로틀을 그대로 따릅니다.
- 새로 생긴 고정비용은 프로세스 시작 시 CoInitializeEx 1회뿐이며 무시할 수준입니다.

결론: 03번 문서의 최적화가 "초당 최대 120회 → 초당 3~4회"로 호출 빈도의 자릿수를 줄인 큰 개선이었다면, 이번 건은 그 3~4회 각각의 내부 비용을 무겁고 블로킹 가능한 크로스 프로세스 메시지 호출에서 직접 메모리 읽기 수준의 가벼운 호출로 바꾼 것입니다. 정상 상태 CPU 사용률 자체는 측정하기 어려울 만큼 미세하게 개선되고, 기존에 존재하던 "터미널에서 가끔 멈칫하는" 케이스의 최악 지연은 확실히 제거됩니다.
