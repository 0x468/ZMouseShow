# ZMouseShow

ZMouseShow 是一个面向 Windows 多显示器和高 DPI 桌面的绿色鼠标定位工具。程序无需安装、不修改系统鼠标方案，默认通过快速双击左 Ctrl 显示定位效果，也可启用自定义全局组合键或鼠标晃动触发。

P4“局部放大与 GPU 渲染”候选实现已经完成：在保留 P3 轻量定位路径的基础上，新增可独立启用的 Desktop Duplication + Direct2D/DirectComposition 局部内容放大镜、三种定位模式、受约束配置、捕获诊断和安全降级。

## 环境要求

- Windows 10 Build 19041 或更高版本；
- Visual Studio 2026，安装“使用 C++ 的桌面开发”工作负载；
- MSVC v145；
- CMake 4.4 或更高版本。

项目有意不支持 Visual Studio 2022/2019 生成器。发布版使用静态 MSVC 运行库，目标电脑不需要另装 VC 运行库或第三方 DLL。

## 构建与测试

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug
ctest --preset debug --output-on-failure
cmake --build --preset release
ctest --preset release --output-on-failure
```

生成内容位于 `out/build/vs2026-x64/`，不提交到 Git；CMake 是唯一工程源。

## 快速使用

直接运行 `ZMouseShow.exe`。默认设置下，按下并释放左 Ctrl 两次即可显示定位效果；两次之间不能夹杂其它按键或鼠标按键。再次输入、点击、滚轮、暂停或退出会关闭效果；自动超时默认关闭。

右键托盘图标可以打开设置、暂停、重新加载 TOML、导出默认配置、导出诊断或退出。设置窗口支持“聚焦”“放大”“聚焦 + 放大”模式，以及镜片倍率、尺寸、形状、触发方式、视觉效果、全屏抑制、程序排除、恢复默认配置和当前用户登录自启动。

程序不要求配置文件。存在 `ZMouseShow.toml` 时默认从 EXE 同目录读取，也可用 `--config <路径>` 指定。设置保存采用同目录临时文件和原子替换，保留原有注释、未知字段和未知表；旧版配置缺少的新字段使用默认值，并在首次保存时自动补全。

开发机只有一块显示器时，可运行：

```powershell
ZMouseShow.exe --simulate-displays
```

该模式模拟三块具有负坐标、垂直偏移和不同 DPI 的显示器，可按 `1`、`2`、`3` 切换圆形、圆角方形和菱形。它不会进入正常常驻流程，也不替代真实多屏与 DWM 验收。

## 文档

- [使用说明](docs/使用说明.md)
- [完整功能说明](docs/功能说明.md)
- [需求规格说明书](docs/需求规格说明书.md)
- [P2 实施计划](docs/P2实施计划.md)
- [P2 验收清单](docs/P2验收清单.md)
- [P3 实施计划](docs/P3实施计划.md)
- [P3 验收清单](docs/P3验收清单.md)
- [P3 候选验证记录](docs/P3候选验证记录.md)
- [P4 实施计划](docs/P4实施计划.md)
- [P4 验收清单](docs/P4验收清单.md)
- [P4 候选验证记录](docs/P4候选验证记录.md)
- [P4 性能报告](docs/P4性能报告.md)
- [ADR 0002：局部放大镜捕获与 GPU 合成](docs/adr/0002-local-magnifier-rendering.md)
- [渲染性能对比报告](docs/性能对比报告.md)
- [P1 验收清单](docs/P1验收清单.md)

TOML 解析器使用仓库内置的 [toml++](https://github.com/marzer/tomlplusplus) 单头文件版本，因此离线构建且没有解析器 DLL 依赖。
