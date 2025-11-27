# 系统架构概览

## 目标
- 实现基于 Karplus-Strong 算法的最小可行弦乐合成原型
- 输出无界面命令行程序，生成单音 WAV 文件
- 预留滤波器重设计与卷积混响模块，便于后续扩展多复音与 MIDI 输入

## 模块划分
1. App Entrypoint (main.cpp)
   - 解析命令行（或使用默认参数）
   - 调用合成器生成样本缓冲，再交给 WAV 写入器
2. Synthesis::KarplusStrongString
   - 负责初始化噪声缓冲、循环延时线与衰减/阻尼
   - 暴露 pluck(frequency, duration) 生成单音样本
   - 后续可在内部插入可替换滤波器链（低通或自定义）
3. DSP::FilterChain（占位）
   - 定义统一接口，当前最小实现可返回直通
   - 未来方便注入自定义滤波器重设计
4. FX::ConvolutionReverb（占位）
   - 定义卷积核加载与处理接口
   - 最小原型不启用，但保留钩子
5. Audio::WaveWriter
   - 负责封装 WAV Header（16-bit PCM）与样本写入
   - 接受 std::vector<float> 并进行幅度归一化与量化

## 数据流
参数或预设 -> KarplusStrongString -> (可选 FilterChain) -> (可选 Convolution) -> WaveWriter -> wav 文件

## 关键配置
- 采样率：44100 Hz（常见 CD 标准，兼容性强）
- 位深：16-bit PCM（兼顾大小与兼容性）
- 默认时长：2 秒（可通过参数调整）
- 弦长样本数：sampleRate / frequency 四舍五入并至少为 2

## 扩展点
- 滤波器设计：在弦回路中插入 IIR/FIR 以控制音色
- 卷积混响：在写文件前对样本做卷积处理
- 多复音：构建 KarplusStrongSynth 聚合多个弦实例并调度
- 输入控制：增加键盘或 MIDI 映射层，实时触发 pluck
