# Satori

Satori 是一个基于 Karplus-Strong 算法的实验性弦乐合成器，目标是在同一套核心 DSP 代码上同时提供 CLI 批处理与 Windows 实时预览体验。当前仓库已具备：
- `SatoriCore` 静态库：包含弦模型、滤波器链与 WAV 写入
- CLI 应用：可生成单音/多音 WAV、调节衰减/亮度/拾音点等参数
- Win32 预览：集成 WASAPI 实时输出与 Direct2D 滑块 UI，可通过键盘触发音符
- Catch2 测试：覆盖核心 DSP 与 Win 音频封装

## 目录速览
```
.
├── CMakeLists.txt          # 构建脚本
├── src/                    # 核心、DSP、Win32 前端源码
├── tests/                  # Catch2 测试入口
├── docs/                   # 架构、路线图与诊断文档
├── third_party/catch2/     # Catch2 合并源码
└── build/                  # 建议的构建输出（未提交）
```

## 快速开始
### 1. 生成与构建
```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
- CLI 可执行文件：`build/Satori`（或 Windows 下 `Satori.exe`）
- Win32 预览：`build/SatoriWin.exe`
- Tests：`build/SatoriTests`

### 2. 运行 CLI
```powershell
# 生成 440Hz、时长 2 秒的 WAV
build/Satori --freq 440 --duration 2 --output out.wav

# 同时渲染多个音符（csv），并关闭低通滤波
build/Satori --notes 261.63,329.63,392 --duration 1.5 --filter none
```
常用参数：`--samplerate`、`--decay`、`--brightness`、`--pickpos`、`--noise`（white/binary）、`--seed`。详情可执行 `build/Satori --help`。

### 3. 运行 Win32 预览
1. Windows 10+、具备可用的输出声卡
2. 在 Release 构建中运行 `SatoriWin.exe`
3. 键盘 `A~K` 或窗口底部的虚拟键盘触发 C4~C5，滑块实时调节 `Decay/Brightness/Pick Position`
4. 顶部按钮可加载/保存 `presets/` 目录下的 JSON 预设，波形视图实时展示最近一次触发的弦形

若系统无可用音频设备，应用会进入“离线”模式：UI 仍可调参、浏览预设与观察波形，但不会触发实际声音输出。常见异常的解决方案见 [docs/troubleshooting/win_audio.md](docs/troubleshooting/win_audio.md)。

### 4. 预设与打包
- 预设文件位于 `presets/`，默认为 `default.json`，保存的用户预设输出到 `presets/user.json`
- 可执行 `scripts/package_win.ps1 -BuildDir build -StagingDir build/package`，快速打包 `SatoriWin.exe`、预设与诊断文档，便于分发离线 Demo

## 测试
```powershell
ctest --output-on-failure --test-dir build
# 或直接运行 Catch2：
build/SatoriTests "[!wasapi][!realtime-engine]"   # 无音频设备环境可跳过 Win 测试
```
- Core 测试：验证样本长度、归一化范围
- Win 音频测试：默认依赖真实 WASAPI 设备，可用 Catch2 过滤器排除

## 文档导航
- [docs/architecture/overview.md](docs/architecture/overview.md)：系统结构与数据流
- [docs/roadmap/milestones.md](docs/roadmap/milestones.md)：迭代计划与验收标准
- [docs/troubleshooting/win_audio.md](docs/troubleshooting/win_audio.md)：实时音频诊断
- `scripts/package_win.ps1`：Win32 预览的最小打包脚本

更新文档时请保持中文描述，并在相关章节添加交叉链接，便于新贡献者快速定位上下文。

## 贡献约定
1. **代码风格**：C++20，头文件位于 `src/`，命名空间按模块划分（audio/dsp/synthesis/win）
2. **构建验证**：提交前本地运行 `cmake --build`, `ctest`，Win 平台可额外打开 `SatoriWin` 验证 UI
3. **文档同步**：新增模块需更新 `docs/architecture`；路线图或计划变更需同步 `docs/roadmap`
4. **测试策略**：不可控硬件依赖需在文档中说明规避方案（跳过策略、模拟器等）

欢迎通过 Issues/PR 提交改进——请附带运行环境（OS、音频后端、编译器）与任何音频日志，便于复现。
