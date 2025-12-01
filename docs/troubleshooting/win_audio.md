# Win 实时音频诊断指南

本指南用于辅助定位 Windows 平台上 `SatoriWin` / `SatoriRealtimeEngine` / `WASAPIAudioEngine` 相关问题。阅读前建议先了解 [系统架构概览](../architecture/overview.md) 中的“实时音频链路”章节。

## 运行前检查清单
1. **系统要求**：Windows 10 及以上；拥有启用状态的输出设备（耳机、扬声器等）
2. **权限**：当前进程需具备 `WASAPI` 共享模式的访问权限，禁止被音频驱动独占
3. **依赖**：已安装 `Microsoft Visual C++` 可再发行组件（若使用 MSVC 构建）
4. **设备状态**：在“声音设置 → 输出”中确认目标设备为“就绪”状态
5. **离线模式**：若设备不可用，`SatoriWin` 会显示“音频：离线”，但仍可加载预设与观察波形；恢复设备后重启应用即可重新启用声音输出

## 常见错误与解决方案

### 1. `无法启动实时音频引擎`
- **触发位置**：`SatoriWin` 在 `WM_CREATE` 中初始化 `SatoriRealtimeEngine` 时弹框
- **含义**：`WASAPIAudioEngine::initialize` 或 `start` 返回 false，多为设备/权限问题
- **排查步骤**：
  1. 打开 Visual Studio 调试控制台或 `OutputDebugString` 监听器，查找 `[WASAPI] ... failed` 日志
  2. 运行 `build/SatoriTests [wasapi]` 复现；若同样失败，问题位于底层音频
  3. 确认 `系统设置 -> 声音 -> 高级 -> 允许应用独占控制` 未被其他 DAW 占用

### 2. `WaitForSingleObject` 超时 / 线程卡死
- **触发位置**：`WASAPIAudioEngine::renderLoop` 中等待事件句柄
- **含义**：驱动未按期投递缓冲，常见于声卡被重新插拔或睡眠唤醒
- **解决**：调用 `SatoriWin` 的关闭按钮触发 `WM_DESTROY`，确保 `stop()` 后重新运行；后续会在路线图的“诊断与日志”条目中补充自动重建逻辑

### 3. `CoInitializeEx` 失败 (0x80010106)
- **场景**：测试进程或宿主应用已在同线程初始化 COM，且模式不同
- **处理**：在启动 `SatoriTests` 或外部宿主时，确保自定义线程采用 `COINIT_MULTITHREADED`，并避免重复调用 `CoInitialize`；必要时手动 `CoUninitialize`

### 4. 无法获取默认设备 (`IMMDeviceEnumerator::GetDefaultAudioEndpoint`)
- **原因**：Windows 声音服务未运行/被禁用，或机器无输出设备
- **临时方案**：切换到虚拟声卡（如 VB-Audio）或插入任一音频输出；同时在 `SatoriTests` 中使用 Catch2 过滤器 `"[!wasapi][!realtime-engine]"` 跳过相关用例

## 无音频设备 / CI 环境策略
1. **仅运行核心测试**：`build/SatoriTests "[!wasapi][!realtime-engine]"`（或在 `ctest` 中通过 `CTEST_CUSTOM_TESTS_IGNORE` 排除）
2. **构建通过但跳过实测**：CI 日志中需写明“WASAPI 测试跳过，原因：无物理设备”
3. **计划中的改进**：路线图 M4/M5 将加入“WASAPI mock/skip”选项，使 `ctest` 能自动检测设备并输出 `skipped`

## 提交 Issue/PR 前请提供
- 操作系统版本、音频驱动/声卡型号
- 构建工具链（MSVC/Clang-MinGW 等）
- 运行的命令与完整输出（尤其是 `[WASAPI] ... failed` 日志）
- 是否触发了 `SatoriTests` 中的 `[wasapi]` 或 `[realtime-engine]` 测试

这些信息将帮助我们更快地定位真实硬件环境中的兼容性问题。
