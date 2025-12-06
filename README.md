# Satori

Satori 是一个基于 Karplus-Strong 算法的数字弦乐合成器实验项目，包含一套可复用的 C++20 合成核心、命令行离线渲染工具，以及基于 Win32 + Direct2D 的实时 UI。

## 功能

- **跨形态合成**：`SatoriCoreLib` 静态库封装音频写入、基本 DSP、Karplus-Strong String/Synth，可同时驱动命令行和实时前端。
- **命令行离线渲染**：`SatoriCLI` 可通过参数快速输出单音或和弦 WAV（支持噪声类型、滤波、随机种子等调参），适合批量生成示例素材。
- **Windows 实时体验**：`SatoriWinApp` 基于 WASAPI + Direct2D/DirectWrite，包含参数旋钮/推子、波形视图和预设管理，并集成了来自 Keyboard Sandbox 的钢琴式虚拟键盘（PC 键盘映射同沙盒）；视觉语言借鉴 Serum 但目前提供统一主题。
- **可测试性**：使用 Catch2（预置在 `third_party/catch2`）实现核心 DSP 与 Win 音频回调测试（`SatoriUnitTests`），支持 `ctest` 集成。

## 架构

```
assets/Fonts/           Nunito 字体资源，生成 Win 资源文件时必需
presets/                默认参数预设（JSON）
scripts/                一键构建脚本（Debug/Release/All）
src/audio|dsp|synthesis Karplus-Strong 核心与辅助模块
src/win/app             Win32 入口、预设管理
src/win/audio           WASAPI 引擎与实时渲染桥
src/win/ui              Direct2D 控件、布局、皮肤
tests/                  Catch2 单元 / 集成测试
third_party/catch2      Catch2 amalgamated 源码
```

## 构建要求

- Windows 10/11，支持 WASAPI 与 Direct2D。
- CMake ≥ 3.16。
- C++20 编译器（建议 Visual Studio 2022 或 clang-cl）。
- 已安装 Windows 10 SDK（提供 `d2d1.lib`、`dwrite.lib`、`mmdevapi.lib` 等）。
- `assets/Fonts/Nunito-Regular.ttf` 必须存在；若缺失需从设计仓补齐。

## 配置与构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Debug/Release 任选，其它配置同理
cmake --build build --config Release
```

- `SatoriCoreLib`、`SatoriCLI`、`SatoriUnitTests` 跨平台；`SatoriWinApp` 与 `SatoriKnobSandbox` 仅在 Windows 生成。
- Serum 的设计语言仅作为视觉参考，目前不提供独立皮肤切换。

### 构建脚本

- `pwsh scripts/build_release.ps1 [-RunTests]`：配置并编译 Release，可选顺带执行 `ctest`。
- `pwsh scripts/build_debug.ps1 [-RunTests]`：同上但目标为 Debug，适合调试循环。
- `pwsh scripts/build_all.ps1 [-RunTests]`：依次构建 Debug 与 Release，两种配置都会在需要时执行测试。
- 若要自定义生成器、体系结构、指定目标（例如只编译 `SatoriWinApp`），可直接调用 `scripts/build.ps1` 并传入 `-Generator/-Arch/-Targets` 等参数。

## 运行

- **命令行渲染器**：`SatoriCLI` 支持多参数，常见示例：

  ```powershell
  .\build\Release\SatoriCLI.exe `
    --notes 261.63,329.63,392 `
    --duration 1.2 `
    --samplerate 48000 `
    --brightness 0.65 `
    --noise binary `
    --filter lowpass `
    --output chord.wav
  ```

  若未指定 `--notes`，则使用 `--freq` 渲染单音；输出路径默认为 `satori_demo.wav`。

- **Windows UI**：构建后运行 `build\Release\SatoriWinApp.exe`。首次启动会加载内置预设，当前阶段仅提供默认参数体验，暂未开放用户自定义保存；内置虚拟键盘与 `SatoriKeyboardSandbox.exe` 使用相同的钢琴布局与 PC 键盘映射。`SatoriKnobSandbox.exe` 只渲染测试面板，便于独立调校控件。  
  - `F12`：启用 UI 调试模式，只在 Debug 构建可用，并会以纯黄色高亮当前鼠标命中的布局或控件，便于排查排版/命中区域问题。
  - `F11`：导出布局尺寸到调试输出。
- **Keyboard Sandbox**：`build\Release\SatoriKeyboardSandbox.exe` 渲染标准钢琴键盘（默认 3 个八度，可在 `src/win/app/KeyboardSandboxMain.cpp` 中调整常量），颜色为黑白基调，带 hover/按压高亮。支持鼠标点击/拖扫和常见 PC 键盘映射：`A S D F G H J` 对应白键，`W E T Y U` 对应黑键，回调会把音名与频率写入 `OutputDebugString`；`F12` 可切换盒模型调试视图。

## 预设与资产

- 所有预设以 JSON 存放在 `presets/`，`PresetManager` 负责序列化/反序列化 Karplus-Strong `StringConfig`。
- Win 版本依赖 `assets/Fonts/Nunito-Regular.ttf`，CMake 会在配置阶段将其打包到资源脚本；更新字体后需要重新配置生成。

## 测试

```powershell
cmake --build build --config Debug --target SatoriUnitTests
ctest --test-dir build --output-on-failure
```

- `tests/core_tests.cpp` 针对 Karplus-Strong DSP。
- `tests/win_audio_tests.cpp` 仅在 `_WIN32` 下编译，需实际 WASAPI 设备；若在无声卡或 CI 环境执行，可直接运行 `SatoriUnitTests.exe "~[wasapi]" "~[realtime-engine]"` 来跳过依赖硬件的用例。
