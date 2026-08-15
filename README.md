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

## Current state

- Accepted requirements and architecture decision record
- VS 2026 / CMake 4.4 / C++23 project baseline
- Pure C++ double-Ctrl and mouse-shake detector models
- Minimal Win32 tray process and Raw Input adapter

The monitor overlay and Region rendering spike are the next implementation milestone. See [the Chinese requirements](docs/需求规格说明书.md) for the full scope.
