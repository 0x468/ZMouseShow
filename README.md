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

No configuration file is required. When present, `ZMouseShow.ini` is loaded from the executable directory; an alternate path can be selected with `--config <path>`. Empty, missing, unknown, malformed, and out-of-range fields safely fall back to embedded defaults.

The tray menu can reload the active configuration or export a documented default INI. Export never overwrites an existing file.

## Current state

- Accepted requirements and architecture decision record
- VS 2026 / CMake 4.4 / C++23 project baseline
- Strict double-left-Ctrl trigger through Raw Input
- Optional experimental mouse-shake trigger, disabled by default and available from the tray menu
- Per-monitor Region overlays with a DPI-aware transparent spotlight and cursor ring
- Spotlight tracking, input dismissal, optional auto-timeout, and display-change rebuild
- Embedded defaults plus optional portable INI loading, reloading, and safe default export

Run `ZMouseShow.exe`, then press and release the left Ctrl key twice in quick succession without pressing any other key or mouse button. The spotlight remains visible by default until another key, click, wheel input, pause, or exit. Right-click the tray icon to pause, enable the experimental shake trigger, enable the optional auto-timeout, or exit.

See [the Chinese requirements](docs/需求规格说明书.md) for the full scope.
