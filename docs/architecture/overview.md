# 系统架构概览

## 目标
- 保持 Karplus-Strong 算法核心，形成可跨前端复用的 `core` 库
- CLI 版本继续用于离线回归，生成 WAV 样本校验声学表现
- 新增 Windows 独立 EXE，内含实时音频输出和 Direct2D 自绘 UI
- 为后续滤波器重设计、卷积混响、多复音、MIDI/键盘输入预留接口

## 模块划分
1. App Entrypoint (main.cpp / WinMain)
   - CLI：解析命令行，调用合成器生成样本并交给 WAV 写入器
   - Win32：注册窗口类、初始化 COM，驱动消息循环与音频引擎
2. Synthesis::KarplusStrongString
   - 负责初始化噪声缓冲、循环延时线与衰减/阻尼
   - 暴露 pluck(frequency, duration) 生成单音样本
   - 后续可在内部插入可替换滤波器链（低通或自定义）
3. DSP::FilterChain（占位）
   - 定义统一接口，当前最小实现可返回直通
   - 未来方便注入自定义滤波器重设计
4. AudioEngine
   - 当前实现：`win/audio/WASAPIAudioEngine` 以事件驱动方式驱动 `SatoriRealtimeEngine`
   - 对外暴露启动/停止与 RenderCallback，后续可替换为 XAudio2
5. FX::ConvolutionReverb（占位）
   - 定义卷积核加载与处理接口
   - 最小原型不启用，但保留钩子
6. Audio::WaveWriter
   - 负责封装 WAV Header（16-bit PCM）与样本写入
   - 接受 std::vector<float> 并进行幅度归一化与量化
7. UI::Direct2DLayer（迭代中）
   - 管理 ID2D1Factory/RenderTarget 生命周期
   - 自绘参数控件、波形/电平视图与虚拟键盘（当前已完成滑块骨架）
   - 监听鼠标/触摸事件并回写参数，最终通过 `SatoriRealtimeEngine::setSynthConfig` 生效

> `core`（synthesis + dsp + audio + future fx）将构建为静态/动态库，CLI 与 Win32 前端均链接此库，避免重复实现。

## 数据流
CLI：参数或预设 -> KarplusStrongString -> (可选 FilterChain) -> (可选 Convolution) -> WaveWriter -> WAV 文件

Win32：UI 交互/输入事件 -> 参数模型 -> AudioEngine 拉取合成器 -> (FilterChain/Convolution) -> WASAPI 输出，同时推送监视数据回 UI

## 实时音频链路
- **线程模型**：UI 线程创建 `SatoriRealtimeEngine`，内部持有
  - `WASAPIAudioEngine`：事件驱动渲染线程，负责设备/缓冲管理
  - `RealtimeSynthRenderer`：维护活跃音符缓冲，带互斥锁保证与 UI 调度同步
- **回调路径**：WASAPI 渲染线程 -> `renderCallback_(float*, frames)` -> `SatoriRealtimeEngine::handleRender` -> `RealtimeSynthRenderer::render`
- **延迟控制**：`AudioEngineConfig` 提供 `bufferFrames`，默认 512，可在初始化成功后读取驱动实际大小
- **故障点**：
  - 设备/权限问题：`IMMDeviceEnumerator::GetDefaultAudioEndpoint`、`IAudioClient::Initialize`
  - 事件超时：`WaitForSingleObject(audioEvent_)`
  - 线程初始化：`CoInitializeEx` 必须在渲染线程中使用 `COINIT_MULTITHREADED`
- **诊断**：所有失败路径记录 `[WASAPI] ... failed`，并在 UI 层 MessageBox 提示。详见 [win_audio.md](../troubleshooting/win_audio.md)。

## Win32 UI 层
- `Direct2DContext`：负责 D2D/DWrite 工厂、渲染目标与画刷创建；在 `WM_SIZE` 下复用 RenderTarget 并重新布局控件
- `ParameterSlider`：封装命名 + 轨道 + 拖拽交互，使用回调直接绑定 `StringConfig` 字段；未来将扩展为可配置控件列表
- 输入通道：
  - 键盘 `A~K` -> `KeyToFrequency` -> `SatoriRealtimeEngine::triggerNote`
  - 鼠标事件 -> `Direct2DContext::onPointer*` -> `ParameterSlider::onPointer*` -> 回调更新参数
- 规划中的组件：波形/电平监视器、虚拟键盘、预设列表、日志浮窗；相关 Direct2D 元素需在本层集中管理，防止多处重复初始化 COM/D2D

## 关键配置
- 采样率：44100 Hz（常见 CD 标准，兼容性强）
- 位深：16-bit PCM（兼顾大小与兼容性）
- 默认时长：2 秒（可通过参数调整）
- 弦长样本数：sampleRate / frequency 四舍五入并至少为 2
- 实时模式需锁定 buffer size（建议 256/512 samples）并使用双缓冲或环形缓冲

## 扩展点
- 滤波器设计：在弦回路中插入 IIR/FIR 以控制音色
- 卷积混响：在写文件前或实时链路中对样本做卷积处理
- 多复音：构建 KarplusStrongSynth 聚合多个弦实例并调度
- 输入控制：增加键盘或 MIDI 映射层，实时触发 pluck
- UI/UX：Direct2D 参数自动布局、预设导入导出、波形/频谱可视化
- 自动化验证：Catch2 用例覆盖核心合成与 WASAPI 管线，CI 需在具备音频设备的 runner 上执行

## 测试与诊断
- 单元测试：`Catch2` 驱动 `tests/core_tests.cpp`、`tests/win_audio_tests.cpp`
- CLI/核心：校验样本长度、归一化范围、混合后的归一化逻辑
- Win32/WASAPI：覆盖引擎初始化、触发音符等路径。若运行环境缺少声卡，可使用 Catch2 过滤器排除 `[wasapi]`、`[realtime-engine]`（示例见根 README）
- 日志与排障：所有 WASAPI 调用失败时输出 HRESULT 并建议参考 [docs/troubleshooting/win_audio.md](../troubleshooting/win_audio.md)
- 自动化计划：加入“设备探测 + 条件跳过”逻辑，使 CI 在无硬件时返回 `skipped` 而非 `failed`；同时扩充 `Direct2D` 层的截图/像素测试
