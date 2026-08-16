# ZMouseShow

ZMouseShow is a portable Windows mouse locator for multi-monitor and high-DPI desktops. P1 is complete and the project is now in its P2 experience-enhancement phase.

## Requirements

- Visual Studio 2026 with the Desktop development with C++ workload
- MSVC v145
- CMake 4.4 or newer
- Windows 10 Build 19041 or newer

Visual Studio 2022 and 2019 generators are intentionally not supported.

## Configure, build, and test

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug
ctest --preset debug
```

Release builds use the static MSVC runtime and produce a native Windows executable:

```powershell
cmake --build --preset release
```

Generated Visual Studio solutions and projects live under `out/` and are not committed. CMake is the only project source of truth.

## Configuration

No configuration file is required. When present, `ZMouseShow.toml` is loaded from the executable directory; an alternate path can be selected with `--config <path>`. Empty and missing files use embedded defaults. Unknown, mistyped, and out-of-range fields are ignored, while a syntactically invalid TOML document is rejected as a whole.

Open the native settings dialog from the tray menu or with `ZMouseShow.exe --settings`. The initial panel covers shake enablement, Ctrl side, custom-hotkey enablement, automatic timeout, spotlight radius, and dim opacity. Applying changes updates the running process and writes them back atomically; existing comments and unknown fields are retained. Advanced timing, hotkey contents, and shake thresholds remain available in TOML. The tray menu can also reload the active configuration or export a documented default TOML file; export never overwrites an existing file. Pause remains a session-only state.

On a single-monitor development machine, launch the built executable with `--simulate-displays` to open an interactive three-monitor preview. It models negative coordinates, vertical offsets, 100%/125%/150% DPI, and a cross-monitor spotlight. This is a visual development aid, not a substitute for real DWM multi-monitor validation.

## Current state

- Accepted requirements and architecture decision record
- VS 2026 / CMake 4.4 / C++23 project baseline
- Strict double-Ctrl trigger through Raw Input, configurable for left, right, or either side
- Optional exact custom hotkey (`Ctrl+Alt+F11` by default, disabled until enabled in TOML)
- Optional experimental mouse-shake trigger, disabled by default and available from the tray menu
- Per-monitor Region overlays with a DPI-aware transparent spotlight and cursor ring
- 220 ms fade transitions and a contracting pulse ring driven by a tested animation state machine
- Spotlight tracking, input dismissal, optional auto-timeout, and display-change rebuild
- Embedded defaults plus optional portable TOML loading, reloading, atomic preference persistence, and safe default export
- Privacy-bounded diagnostics export with effective settings and multi-monitor/DPI topology
- Interactive single-screen preview for a mixed-DPI three-monitor topology
- Native Win32 settings dialog with immediate apply, validation, and comment-preserving TOML persistence

Run `ZMouseShow.exe`, then press and release the configured Ctrl key twice in quick succession without pressing any other key or mouse button. The default remains left Ctrl. The spotlight remains visible by default until another key, click, wheel input, pause, or exit. Right-click the tray icon to open settings, pause, toggle common preferences, or exit. Configure advanced fields in `ZMouseShow.toml`, then reload them from the tray.

See [the Chinese requirements](docs/需求规格说明书.md) for the full scope and [the P1 acceptance checklist](docs/P1验收清单.md) for hardware and interaction validation.

The TOML parser is the vendored single-header distribution of [toml++](https://github.com/marzer/tomlplusplus), so builds remain offline and the executable has no parser DLL dependency.
