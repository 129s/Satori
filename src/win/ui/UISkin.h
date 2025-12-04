#pragma once

#include <string>

namespace winui {

// 非持久化的皮肤标识，用于在代码中区分不同主题。
enum class UISkinId {
    kDefault = 0,
    kSerumPrototype = 1,
};

// 皮肤的静态配置（不依赖具体图形 API），集中描述名称、字体与资源目录。
struct UISkinConfig {
    UISkinId id = UISkinId::kDefault;
    std::wstring name;

    // 项目自有 UI 资源所在的基础目录，例如：
    // - L"assets/ui/default"
    // - L"assets/ui/serum_theme"
    std::wstring assetsBaseDir;

    // 文本渲染首选字体及字号（若字体不可用，调用方应自行回退）。
    std::wstring primaryFontFamily;
    float baseFontSize = 18.0f;
};

// 皮肤在渲染阶段的运行时资源占位（与具体 UI 控件解耦）。
// 当前仅承载 UISkinConfig，后续可以在此集中挂接 D2D 位图等句柄，
// 避免在各个控件中分散管理文件路径和纹理坐标。
struct UISkinResources {
    UISkinConfig config;
    // TODO: 后续可在此添加 ID2D1Bitmap* 等图形资源句柄。
};

}  // namespace winui

