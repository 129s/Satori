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
4. AudioEngine（待实现）
   - 基于 WASAPI Event-Driven（或 XAudio2）拉取核心样本
   - 提供线程安全的参数更新与事件触发
5. FX::ConvolutionReverb（占位）
   - 定义卷积核加载与处理接口
   - 最小原型不启用，但保留钩子
6. Audio::WaveWriter
   - 负责封装 WAV Header（16-bit PCM）与样本写入
   - 接受 std::vector<float> 并进行幅度归一化与量化
7. UI::Direct2DLayer（待实现）
   - 管理 ID2D1Factory/RenderTarget 生命周期
   - 自绘参数控件、波形/电平视图与虚拟键盘

> `core`（synthesis + dsp + audio + future fx）将构建为静态/动态库，CLI 与 Win32 前端均链接此库，避免重复实现。

## 数据流
CLI：参数或预设 -> KarplusStrongString -> (可选 FilterChain) -> (可选 Convolution) -> WaveWriter -> WAV 文件

Win32：UI 交互/输入事件 -> 参数模型 -> AudioEngine 拉取合成器 -> (FilterChain/Convolution) -> WASAPI 输出，同时推送监视数据回 UI

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
