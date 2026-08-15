# ZMouseShow

ZMouseShow is a portable Windows mouse locator for multi-monitor and high-DPI desktops. The project is currently in its P0 implementation phase.

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

The tray menu can reload the active configuration or export a documented default TOML file. Export never overwrites an existing file. Changes to the shake trigger and automatic timeout are written back atomically; existing comments and unknown fields are retained. Pause remains a session-only state.

## Current state

- Accepted requirements and architecture decision record
- VS 2026 / CMake 4.4 / C++23 project baseline
- Strict double-Ctrl trigger through Raw Input, configurable for left, right, or either side
- Optional exact custom hotkey (`Ctrl+Alt+F12` by default, disabled until enabled in TOML)
- Optional experimental mouse-shake trigger, disabled by default and available from the tray menu
- Per-monitor Region overlays with a DPI-aware transparent spotlight and cursor ring
- 220 ms fade transitions and a contracting pulse ring driven by a tested animation state machine
- Spotlight tracking, input dismissal, optional auto-timeout, and display-change rebuild
- Embedded defaults plus optional portable TOML loading, reloading, atomic preference persistence, and safe default export

Run `ZMouseShow.exe`, then press and release the configured Ctrl key twice in quick succession without pressing any other key or mouse button. The default remains left Ctrl. The spotlight remains visible by default until another key, click, wheel input, pause, or exit. Right-click the tray icon to pause, enable the experimental shake trigger, enable the optional auto-timeout, or exit. Configure `[double_ctrl].side` and the optional `[hotkey]` table in `ZMouseShow.toml`, then reload it from the tray.

See [the Chinese requirements](docs/需求规格说明书.md) for the full scope.

The TOML parser is the vendored single-header distribution of [toml++](https://github.com/marzer/tomlplusplus), so builds remain offline and the executable has no parser DLL dependency.
