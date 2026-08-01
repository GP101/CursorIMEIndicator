# Cursor IME Indicator

Cursor IME Indicator is a small native Windows utility that briefly shows the
current input mode beside the mouse cursor. It displays `E` for English/
alphanumeric input and `한` for Korean (Hangul) input, helping you confirm the
active IME mode without looking away from the insertion point.

The application lives in the notification area and runs as a single instance.

## Features

- Displays a compact, animated input-mode badge next to the cursor.
- Detects Hangul conversion mode for the focused control in the foreground
  application.
- Shows the badge when the IME mode, focused control, foreground window, or
  cursor context changes; hides it while the mouse is moving.
- Uses a click-through, non-activating, topmost overlay, so the badge does not
  block input or appear in Alt+Tab or the taskbar.
- Provides a notification-area menu to enable or disable the indicator,
  configure automatic startup, view About information, or exit.
- Restores its notification-area icon when Windows Explorer recreates the
  taskbar.
- Uses a faster timer during animation and a lower-frequency timer while idle
  to reduce background work.

## Manual

### Run the included build

1. Run [`Release-public/CursorIMEIndicator.exe`](Release-public/CursorIMEIndicator.exe).
2. Look for the circular `한` icon in the Windows notification area. Open the
   hidden-icons overflow menu if it is not immediately visible.
3. Focus a text field, change between Korean and English input, or switch to a
   different text control while the mouse is stationary. The badge appears near
   the cursor for about two seconds.

### Notification-area controls

| Action | Result |
| --- | --- |
| Left-click the tray icon | Enable or disable the cursor indicator. |
| Right-click the tray icon | Open the menu. |
| **Enable cursor indicator** | Toggle whether the overlay can appear. |
| **Run at startup** | Add or remove the application from the current user's Windows startup entries. |
| **About** | Show application information. |
| **Exit** | Close the application. |

Automatic startup is stored for the current user in
`HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run` under the
value name `CursorIMEIndicator`.

### If the expected badge does not appear

- Confirm that the indicator is enabled from the tray icon menu.
- Keep the mouse still after changing input mode or focus; mouse movement
  immediately hides the badge.
- The detector relies on the standard Windows IME window and conversion-mode
  interface exposed by the foreground application. An application that does
  not respond to that interface may be reported as English mode.

## Technical Overview

This is a Unicode, native Win32 C++17 application with a Windows-subsystem
entry point. It is built only for x64 and uses the Visual Studio `v143` toolset
with Windows SDK 10.0.

### IME detection

`IMEDetector` obtains the foreground window and its focused control, retrieves
the control's default IME window with `ImmGetDefaultIMEWnd`, and queries its
conversion mode through `WM_IME_CONTROL` (`IMC_GETCONVERSIONMODE`). The message
uses `SendMessageTimeout` with `SMTO_ABORTIFHUNG` and a 250 ms timeout so an
unresponsive target does not block the utility. The `IME_CMODE_NATIVE` flag is
used to identify Korean mode.

### Overlay and animation

`IndicatorWindow` creates a `WS_POPUP` layered window with `WS_EX_LAYERED`,
`WS_EX_TRANSPARENT`, `WS_EX_NOACTIVATE`, `WS_EX_TOOLWINDOW`, and
`WS_EX_TOPMOST`. It renders a 64 x 64 per-pixel-alpha surface with GDI+ and
publishes it through `UpdateLayeredWindow`.

The rendered badge has a rounded gradient, shadow, border, and text. Its color
interpolates between English and Korean states, and an IME change triggers a
short scale-pop animation. Positioning uses the current cursor image size and
hotspot so the badge is placed just beyond the cursor rather than at a fixed
screen offset.

### Runtime behavior

The overlay is triggered by an IME-state change, focused-control change,
foreground-window change, or a transition to the I-beam cursor. While visible
or animating, it updates every 16 ms (approximately 60 FPS). Once hidden and
settled, it switches to a 150 ms idle interval. The background window also
handles `TaskbarCreated` to add the tray icon again after Explorer restarts and
uses a named mutex to prevent multiple instances.

## Build from Source

### Requirements

- Visual Studio 2022 or the Visual Studio Build Tools with the C++ desktop
  development workload.
- MSVC toolset `v143` and a Windows 10 SDK.

### Build

Open `CursorIMEIndicator.sln` in Visual Studio, select **Release** and **x64**,
then build the solution. From a Developer PowerShell for Visual Studio, the
equivalent command is:

```powershell
MSBuild.exe .\CursorIMEIndicator.sln /p:Configuration=Release /p:Platform=x64 /m
```

The output executable is written to `Release\CursorIMEIndicator.exe`.

## Repository Layout

```text
src/main.cpp              Application lifecycle, tray icon, and startup setting
src/IMEDetector.*         Focused-window IME conversion-mode detection
src/IndicatorWindow.*     Layered cursor overlay, timing, and GDI+ rendering
Release-public/           Included executable build
```

## License

This project is licensed under the GNU General Public License, version 3
(GPL-3.0-only). See [LICENSE](LICENSE) for the complete terms.

In short, you may run, study, modify, and redistribute the software under the
GPLv3 terms. Distributing modified versions or binaries requires providing the
corresponding source code under the same license. The software is provided
without warranty, as described in the license.
