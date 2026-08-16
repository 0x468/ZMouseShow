# ADR 0002：局部放大镜捕获与 GPU 合成

- 状态：已接受
- 日期：2026-08-16
- 适用阶段：P4

## 背景

P3 的 Region + 小型 GDI 分层窗口适合低频暗化、孔洞、描边和波纹，但局部放大镜必须持续取得鼠标附近的真实桌面像素、缩放并裁剪成透明镜片。若继续使用 `BitBlt`、`StretchBlt` 和 CPU 位图，会增加 GPU 到 CPU 回读、逐帧内存复制、HDR/多屏差异和 120Hz 下的抖动风险。

局部放大镜还必须正确包含指针。部分单色指针使用 AND/XOR 蒙版，不能被固定 RGBA 位图精确表达；只有把指针形状与镜片内的实际背景共同合成，才能保持亮底黑色、暗底白色等语义。

## 决策

P4 采用以下主路径：

1. 使用 DXGI Desktop Duplication 按显示输出取得 `DXGI_FORMAT_B8G8R8A8_UNORM` 桌面纹理、帧变更信息和指针元数据。
2. 使用与目标输出适配器匹配的 D3D11 设备处理纹理裁剪、缩放和必要的指针合成；不把正常帧回读到 CPU。
3. 使用 Direct2D 1.1 在共享 DXGI 表面上绘制镜片裁剪、双重高对比描边、阴影和二维装饰。
4. 使用支持预乘 Alpha 的 DXGI composition swap chain 和 DirectComposition 呈现独立镜片窗口；窗口继续保持无激活、工具窗口和点击穿透。
5. 镜片窗口以及可能进入捕获范围的本程序顶层视觉窗口使用 `WDA_EXCLUDEFROMCAPTURE`，并把失败作为可诊断能力降级，避免递归镜厅效果。
6. P4 初期在主消息循环以零超时 `AcquireNextFrame(0)` 轮询活动输出，避免不可取消的阻塞等待；只有镜片可见时保持渲染节拍。真实性能数据证明需要后，再引入专用捕获线程。
7. `DXGI_ERROR_ACCESS_LOST`、显示模式变化、桌面切换或设备移除时释放当前会话并重建；安全桌面、受保护内容、远程会话或并发复制数量限制下安全关闭镜片，保留 P3 定位效果。

微软文档依据：

- Desktop Duplication API：<https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api>
- `IDXGIOutput1::DuplicateOutput`：<https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput>
- `IDXGIOutputDuplication::AcquireNextFrame`：<https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-acquirenextframe>
- Direct2D 与 Direct3D 互操作：<https://learn.microsoft.com/en-us/windows/win32/Direct2D/direct2d-and-direct3d-interoperation-overview>
- `WDA_EXCLUDEFROMCAPTURE`：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity>

## 指针与现有视觉效果

- 局部放大镜默认且必须包含指针。Desktop Duplication 可能已经把指针合入桌面纹理，也可能通过 `GetFramePointerShape` 单独返回；实现根据帧元数据只合成一次。
- 镜片居中跟随实际热点时，临时隐藏未放大的系统指针，只显示镜片内合成后的指针，避免重复。隐藏失败时不得再叠加第二份指针。
- 启用局部放大镜时，现有“只放大指针”副本不参与绘制；配置值可以保留，关闭镜片后恢复原选择。
- P4 验收后再决定是否移除“只放大指针”。在此之前它仍是低开销、不捕获屏幕、在 Desktop Duplication 不可用时可工作的兼容模式。

## 模式与可组合性

暗化遮罩和局部放大镜是两个可组合能力，而不是彼此依赖：

| 用户模式 | 暗化遮罩 | 局部放大镜 | 典型用途 |
| --- | --- | --- | --- |
| 聚焦 | 开 | 关 | 延续 P3，快速找到远处指针 |
| 放大 | 关 | 开 | 阅读小字、精确定位，画面干扰最少 |
| 聚焦 + 放大 | 开 | 开 | 多屏和复杂背景下最强提示 |

TOML 保存底层独立布尔能力，设置面板提供上述三个清晰预设，并允许进入高级区单独调整描边、波纹和十字线。任何组合都不得改变触发方式、输入穿透或前台焦点。

镜片默认使用圆形或大圆角矩形、双重明暗描边和轻微阴影。所谓“边缘曲率”在首版指曲线裁剪、边缘高光和羽化，不默认加入几何桶形畸变；后者可能引起眩晕，只有在独立像素着色器验证且可关闭时才考虑。

## 未选择的方案

### Windows Magnification API

优点是实现快、内置局部源矩形和放大指针；缺点是控制和合成边界较旧、递归排除与设备恢复不够可控。微软当前也建议新应用使用 Windows Graphics Capture 或 Desktop Duplication，因此只保留为技术回退研究，不作为正式主路径。

### Windows Graphics Capture

Win32 可以通过 `IGraphicsCaptureItemInterop::CreateForMonitor` 直接指定显示器，API 较现代。它更适合窗口/显示器录制和通用帧池，但 P4 需要按输出处理指针形状、跨显示器边界和精细失败恢复；Desktop Duplication 的输出与指针元数据更直接，因此 WGC 作为第二候选和未来 HDR 调研方向。

### GDI `BitBlt` / `StretchBlt`

实现和调试成本最低，可用于一次性诊断原型；但持续捕获、CPU 复制、缩放质量和递归窗口处理不符合目标，不进入正式后端。

### 全面替换 P3 Overlay

P4 不立即把暗化、孔洞、波纹全部迁移到 GPU。现有路径空闲开销低且已经验收；镜片作为独立后端接入，只有性能和维护数据证明统一更优时才考虑合并。

## 后果

- 仍然是单个原生 x64 EXE，只链接 Windows 自带的 DXGI、D3D11、Direct2D 和 DirectComposition 组件，不新增第三方运行时 DLL。
- 动画期间的显存、GPU 使用、设备丢失和输出切换成为新的测试面。
- P4 必须新增捕获能力诊断，且诊断只能记录适配器、输出、格式、错误码和状态，不能保存屏幕像素或截图。
- 真实三屏、混合 DPI、屏幕旋转、HDR、RDP、受保护内容和 120Hz 仍需硬件验收。
