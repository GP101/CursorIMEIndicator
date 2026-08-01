# Cursor IME Indicator 개선 요약 문서 (Improvements Summary)

이 문서는 오래 실행해 두거나 컴퓨터 로그오프/로그인 시 발생하는 높은 CPU 점유율 문제와 작업 표시줄 아이콘이 사라지는 문제를 해결하기 위한 분석 내용 및 개선 사항을 정리한 문서입니다.

---

## 1. 발견된 문제점 및 원인 분석

### ① 장시간 실행 및 대기 시 높은 CPU 점유율
- **원인**: 마우스 커서 옆 한/영 지시창이 화면에 보이지 않는 대기 상태임에도 불구하고, 프로그램이 **16ms(60 FPS) 주기**로 활성 윈도우 감지, 마우스 커서 상태 조회 및 다른 프로세스로의 메시지 송신(`SendMessageTimeout`을 통한 IME 상태 확인)을 계속해서 반복하고 있었습니다. 크로스 프로세스 메시지 전송이 고주기로 반복되면서 상당한 CPU 연산 비용을 낭비하고 있었습니다.

### ② 작업 표시줄(Taskbar) 트레이 아이콘 분실
- **원인**: 윈도우 탐색기(Explorer.exe)가 비정상 종료 후 재시작되거나 세션이 다시 로드될 때, 윈도우가 시스템 전체에 뿌리는 탐색기 재시작 이벤트(`"TaskbarCreated"` 메시지)를 감지하여 트레이 아이콘을 복구(재등록)하는 루틴이 없었습니다.

### ③ 로그오프 후 재로그인 시 CPU 과점유 및 비정상 잔존
- **원인**: 백그라운드 메시지 처리를 담당하는 윈도우가 메시지 전용 윈도우(`HWND_MESSAGE`)로 생성되어 있었습니다. 메시지 전용 윈도우는 시스템 로그오프/종료 시 브로드캐스트되는 `WM_QUERYENDSESSION` 및 `WM_ENDSESSION` 메시지를 받지 못합니다.
- 이에 따라 사용자가 로그오프를 하더라도 프로세스가 죽지 않고 유령 세션에 남아 CPU를 과다하게 소모하였고, 다시 로그인했을 때 기존 프로세스 잔존으로 인해 중복 실행 Mutex에 걸려 트레이 아이콘이 생성되지 않는 등의 악순환이 발생했습니다.

---

## 2. 세부 개선 사항

### ① 타이머 동적 Throttling 및 Idle 모드 도입
- 지시창의 상태에 맞춰 동작 주기를 유연하게 조절하는 **동적 타이머 메커니즘**을 도입했습니다.
  - **Active 모드 (16ms / 60 FPS)**: 지시창이 화면에 나타나 페이드인/아웃 및 팝업 애니메이션을 수행하는 2.0초 동안만 고속으로 동작하여 시각적 부드러움을 유지합니다.
  - **Idle 모드 (150ms / ~6.7 FPS)**: 지시창이 화면에서 사라지면 즉시 저주기 상태로 들어가 시스템 자원 소비를 최소화합니다.
- 화면 잠금, 로그오프 등으로 활성 윈도우가 존재하지 않는 상황(`GetForegroundWindow() == NULL`)이 감지되면 연산을 즉시 중단하고 Idle 모드로 자동 강제 진입하도록 방어 코드를 적용했습니다.

### ② Explorer 재시작 시 트레이 아이콘 자동 복구
- 윈도우가 제공하는 `"TaskbarCreated"` 메시지를 등록(`RegisterWindowMessageW`)하여, 탐색기 재시작으로 트레이 영역이 새로 만들어지면 전역에 캐시된 트레이 아이콘(`g_hTrayIcon`) 정보를 이용해 아이콘을 즉각 자동으로 다시 추가(`NIM_ADD`)하도록 수정했습니다.

### ③ 백그라운드 윈도우 변경 및 로그오프 정상 종료 처리
- 백그라운드 윈도우를 최상위 투명 팝업 윈도우(`parent = NULL`, style `WS_POPUP`)로 변경하여 시스템 종료/로그오프 방송 메시지를 온전히 수신할 수 있도록 변경했습니다.
- `WM_QUERYENDSESSION` 수신 시 시스템의 세션 종료 요청을 승인(`TRUE` 반환)하며, `WM_ENDSESSION` 수신 시 `DestroyWindow`를 호출하여 메시지 루프를 안전하게 종료하고 프로세스가 즉시 완전히 소멸하도록 조치했습니다.

### ④ 기타 코드 정리 및 메모리 누수 방지
- 윈도우 인스턴스 생성/소멸 시마다 빈번하게 실행되던 GDI+ 라이브러리 초기화(`GdiplusStartup`) 및 해제(`GdiplusShutdown`) 코드를 프로세스 시작과 종료 시점에 1회만 구동되도록 전역(Global Lifetime)으로 재배치하여 오버헤드를 없앴습니다.
- 창 클래스 해제(`UnregisterClassW`) 누락 부분을 보완하여 프로그램 종료 시 잔여 리소스가 완벽히 정리되도록 하였습니다.

---

## 3. 수정된 소스 파일 목록

수정이 진행된 소스 파일 링크입니다:
- **[src/main.cpp](file:///D:/Work/TempWork/_ProgrammingProject/CursorInputInfo/src/main.cpp)**: 백그라운드 투명 팝업 창 생성 방식 변경, 시스템 세션 종료(`WM_ENDSESSION`) 및 탐색기 재시작(`"TaskbarCreated"`) 처리 루틴 구현, 전역 GDI+ 수명 관리 추가.
- **[src/IndicatorWindow.h](file:///D:/Work/TempWork/_ProgrammingProject/CursorInputInfo/src/IndicatorWindow.h)**: 동적 타이머용 주기 상수 및 상태 멤버 변수 선언, GDI+ 개별 인스턴스 토큰 제거.
- **[src/IndicatorWindow.cpp](file:///D:/Work/TempWork/_ProgrammingProject/CursorInputInfo/src/IndicatorWindow.cpp)**: 대기 상태 진입 시 타이머 주기를 150ms로 내리고, 트리거 발동 시 16ms로 올리는 Throttling 로직 구현, 비활성 세션 시 무부하 처리 추가.

---

## 4. 빌드 확인 결과

- `MSBuild.exe` 빌드 도구를 활용하여 솔루션 파일(`CursorIMEIndicator.sln`)을 Release x64 환경으로 빌드 검증을 수행했으며, **경고(Warning) 및 에러(Error) 없이 성공적으로 빌드**를 마쳤습니다.
- 결과 바이너리: `D:\Work\TempWork\_ProgrammingProject\CursorInputInfo\Release\CursorIMEIndicator.exe`
