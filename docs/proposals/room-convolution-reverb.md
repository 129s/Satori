# Proposal：将 Room 模块升级为卷积混响（Mix / IR）

## 背景与问题

当前 Room（旧实现）是极短延迟 + 简单阻尼的立体扩展处理（约 1.2ms / 1.9ms 两个 tap），在多数音色与参数组合下听感非常克制，因此用户常反馈 “Room Amount 调了无感”。

用户期望的方向是「卷积混响」：可调 `mix`（干湿）与 `ir`（采样/脉冲响应），并提供 IR 波形可视化与快速选择交互。

## 目标

- DSP：Room 变为卷积混响，参数名明确为：
  - `Mix`：0..1 干湿混合
  - `IR`：内置 IR 列表选择
- IR：内置 IR 采样“构建时编译进程序资源”，运行时不进行文件 IO
- UI：Room 面板布局类似 Serum（从上到下：下拉菜单、波形窗、旋钮）
  - 左右箭头循环切换
  - 点击名称弹出下拉
  - 下拉列表分页（Prev/Next），不使用滚动条
  - IR 波形可视化随选择变化
- 预设：支持保存/加载 `roomMix` / `roomIR`（稳定 ID），并兼容旧字段 `roomAmount`

## 设计要点

- 卷积采用分区卷积（partitioned convolution），避免长 IR 带来的 CPU 峰值
- IR 切换要避免爆音：按 block 做交叉淡化（crossfade）
- 输出电平需要可控：IR 做归一化，必要时对 wet 做固定缩放以避免能量过高
- UI 风格保持单色系与现有字体/旋钮方案，本工单不做旋钮视觉重构

## 实施范围（已落地的代码点）

- Core/DSP：
  - `src/dsp/Fft.{h,cpp}`：最小 radix-2 FFT
  - `src/dsp/PartitionedConvolver.{h,cpp}`：分区卷积 + overlap-add
  - `src/dsp/ConvolutionReverb.{h,cpp}`：IR 切换交叉淡化 + Mix
  - `scripts/generate_room_ir_data.py` + `assets/ir_src/*.wav`：构建时生成 `RoomIrData.cpp/.h` 并编译进程序
  - `src/dsp/RoomIrLibrary.{h,cpp}`：对外提供 IR 列表/采样/预览波形
- Engine：
  - 新增参数 `RoomIR`，并在运行时根据 IR 数量自动设置 enum 最大值
  - `RoomAmount` 在 Room 中语义作为 `Mix`
- UI（WinApp）：
  - `src/win/ui/nodes/DropdownSelectorNode.{h,cpp}`：左右箭头循环 + 分页下拉
  - `src/win/ui/nodes/RoomReverbPreviewNode.{h,cpp}`：下拉 + IR 波形窗（Serum 风格纵向结构）
  - Room 旋钮保留现有方案，仅将文案改为 `Mix`，并新增隐藏参数 `IR` 供下拉绑定
- Preset：
  - `roomMix` / `roomIR` 写入；读取时兼容旧 `roomAmount`

## 风险与验证

- CPU：长 IR / 高采样率时卷积开销上升；需要限制 IR 最大长度或分区大小
- 电平：不同 IR 能量差异导致响度变化；需要统一归一化策略或固定 wetLevel
- UI：下拉 overlay 需确保事件优先级正确，避免误触底层控件

验证方式：

- `ctest --test-dir build --output-on-failure`
- 运行 `SatoriWinApp`：
  - `Mix` 从 0 到 1 听感差异明显
  - `IR` 切换无明显 click/pop
  - 下拉分页与左右箭头循环符合预期

