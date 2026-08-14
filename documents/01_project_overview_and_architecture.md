# Cursor IME Indicator: 프로젝트 개요와 아키텍처

> 이 문서는 프로젝트를 처음 접하는 개발자를 위한 첫 번째 안내서다. 먼저 이 문서로 목적, 실행 흐름, 구성 요소의 책임을 파악한 뒤, [02_technical_implementation.md](02_technical_implementation.md)에서 Win32 API와 구현 세부 사항을 확인한다.

## 1. 프로젝트가 해결하는 문제

**Cursor IME Indicator**는 현재 포커스를 가진 Windows 애플리케이션의 IME 입력 모드를 감지해, 마우스 커서 옆에 작은 배지를 표시하는 네이티브 Windows 유틸리티다.

- 한글(Hangul) 입력 모드이면 `한`
- 영문/알파벳 입력 모드이거나 감지에 실패하면 `E`

텍스트 입력 중에 시선을 멀리 옮기지 않고 현재 입력 모드를 확인하는 것이 목적이다. 프로그램은 일반적인 주 창을 제공하지 않고 알림 영역(tray) 아이콘과 입력을 방해하지 않는 투명 오버레이로 동작한다.

## 2. 사용자 관점의 동작

프로그램은 시작되면 다음 두 개의 창을 만든다.

1. 보이지 않는 백그라운드 창: tray 아이콘, 메뉴, 종료와 Explorer 재시작 복구를 담당한다.
2. 커서 근처의 오버레이 창: IME 상태를 `한` 또는 `E` 배지로 렌더링한다.

일반 모드에서는 IME 상태, 포커스 컨트롤, 전경 창, 또는 I-beam 커서의 변화가 감지되고 마우스가 움직이지 않을 때 약 2초간 배지를 보인다. 마우스를 움직이면 즉시 숨긴다.

`Always Show` 모드를 켜면 마우스 이동 또는 버튼 입력이 1초 이상 없을 때 배지를 계속 보인다. 이동 또는 버튼 상태 변화가 발생하면 다시 숨긴다. 이 선택은 다음 실행에도 유지된다.

tray 아이콘의 동작은 다음과 같다.

| 조작 | 결과 |
| --- | --- |
| 왼쪽 클릭 | 오버레이 사용 여부 토글 |
| 오른쪽 클릭 | 설정/정보/종료 메뉴 표시 |
| 메뉴: indicator | 오버레이 사용 여부 토글 |
| 메뉴: startup | 현재 사용자 시작 프로그램 등록 여부 토글 |
| 메뉴: Always Show | 유휴 상태의 지속 표시 토글 및 저장 |
| 메뉴: About / Exit | 정보 표시 / 프로그램 종료 |

## 3. 큰 그림

```text
Windows foreground application
        │
        │  foreground window + focused control
        ▼
 IMEDetector ── WM_IME_CONTROL / conversion mode ──► Korean? (bool)
        │
        ▼
 IndicatorWindow ── input/trigger/timer state ──► layered overlay near cursor
        ▲                                                │
        │                                                ▼
 main.cpp ◄── tray commands, registry settings, lifecycle ── Windows desktop
```

핵심 설계 원칙은 다음과 같다.

- **프로세스 하나, 책임 세 부분**: `main.cpp`는 애플리케이션 수명과 tray, `IMEDetector`는 상태 조회, `IndicatorWindow`는 표시와 폴링을 맡는다.
- **입력 비간섭**: 오버레이는 클릭 통과(click-through), 비활성화(non-activating), 도구 창(tool window)으로 생성된다.
- **응답성 보호**: 다른 프로세스의 IME 창에 보내는 메시지는 타임아웃을 둔다.
- **유휴 시 비용 절감**: 일반 유휴 상태에서는 느린 타이머를 사용하고, Always Show에서는 1초 주기로 폴링한다.
- **사용자별 설정**: 시작 프로그램과 Always Show 값은 HKCU 아래에 저장하며 관리자 권한을 요구하지 않는다.

## 4. 구성 요소와 경계

| 구성 요소 | 파일 | 책임 | 다른 구성 요소와의 관계 |
| --- | --- | --- |
| 애플리케이션 셸 | `src/main.cpp` | 단일 인스턴스, GDI+ 수명, 숨은 창, tray, 메뉴 명령, 레지스트리, 메시지 루프 | `IndicatorWindow`를 만들고 제어한다. |
| IME 감지기 | `src/IMEDetector.h/.cpp` | 전경 창과 포커스 컨트롤의 한글 변환 모드 조회 | `IndicatorWindow`가 주기적으로 호출한다. |
| 오버레이 | `src/IndicatorWindow.h/.cpp` | 레이어드 창, 입력/전경 상태 추적, 표시 조건, 타이머, GDI+ 렌더링 | `IMEDetector` 결과를 표시하고 `main.cpp`의 설정 명령을 받는다. |
| 빌드 정의 | `CursorIMEIndicator.vcxproj`, `.sln` | x64 Debug/Release 구성과 링크 라이브러리 | 세 C++ 소스를 Windows GUI 응용 프로그램으로 빌드한다. |

이 구조에서 `IMEDetector`는 화면을 그리거나 설정을 저장하지 않고, `IndicatorWindow`도 tray 메뉴나 시작 프로그램 항목을 직접 다루지 않는다. 따라서 감지 방식, 표시 정책, shell 통합을 비교적 독립적으로 변경할 수 있다.

## 5. 실행 수명주기

```text
WinMain
  ├─ named mutex 생성 → 기존 인스턴스가 있으면 종료
  ├─ GDI+ 시작
  ├─ TaskbarCreated 메시지 등록
  ├─ 숨은 백그라운드 창 + tray 아이콘 생성
  ├─ IndicatorWindow 생성
  │    └─ 저장된 Always Show 설정 복원
  ├─ GetMessage / DispatchMessage 루프
  └─ tray 아이콘, 창, GDI+, mutex 순서로 정리
```

Explorer가 taskbar를 다시 만들면 Windows가 `TaskbarCreated` 메시지를 브로드캐스트한다. 백그라운드 창은 이를 받고 기존 아이콘 핸들을 이용해 tray 아이콘을 다시 등록한다. 종료 시에는 메시지 루프가 끝난 뒤 tray 아이콘을 제거하고 할당한 오버레이와 GDI+ 리소스를 정리한다.

## 6. 데이터와 영속 설정

프로그램은 별도 파일이나 서버를 사용하지 않는다. 현재 소스에서 쓰는 영속 상태는 다음 두 레지스트리 값뿐이다.

| 목적 | 위치 | 값 |
| --- | --- | --- |
| 자동 시작 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` | `CursorIMEIndicator` 문자열 값에 실행 파일 경로 저장 |
| Always Show | `HKCU\Software\CursorIMEIndicator` | `AlwaysShow` `REG_DWORD` (`0` 또는 `1`) |

오버레이의 현재 IME 상태, 마지막 커서 위치, 포커스 창, 표시 시간 등의 상태는 모두 메모리에만 존재한다.

## 7. 빌드와 실행의 출발점

프로젝트는 Unicode Windows-subsystem C++17 애플리케이션이며 x64 구성만 정의한다. Visual Studio 2022 또는 Build Tools의 MSVC `v143`과 Windows 10 SDK가 필요하다.

```powershell
MSBuild.exe .\CursorIMEIndicator.sln /p:Configuration=Release /p:Platform=x64 /m
```

기본 출력 경로는 `Release\CursorIMEIndicator.exe`다. 저장소에는 배포용 실행 파일도 `Release-public\CursorIMEIndicator.exe`에 포함되어 있다. 빌드 설정, 링크 라이브러리, 런타임 처리 흐름은 다음 문서에서 상세히 설명한다.

## 8. 읽을 때 알아둘 제한 사항

- 감지는 표준 Windows IME 창과 변환 모드 인터페이스에 의존한다. 대상 애플리케이션이 응답하지 않거나 해당 인터페이스를 제공하지 않으면 `E`로 처리한다.
- `Always Show`는 이름과 달리 마우스 입력이 유휴 상태일 때만 표시한다. 마우스가 움직이거나 버튼 상태가 바뀌면 숨긴다.
- 소스의 현재 렌더링은 상태별 색상값을 즉시 선택하고 투명도도 즉시 적용한다. 시간에 따라 확대/축소하거나 색을 보간하는 프레임 애니메이션은 수행하지 않는다.

다음 문서: [02_technical_implementation.md](02_technical_implementation.md)
