#pragma once

#include <string>

#include "win/ui/RenderResources.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

// 顶部状态栏：显示标题、采样率与音频在线指示灯。
class TopBarNode : public UILayoutNode {
public:
    TopBarNode();

    void setTitle(std::wstring title);
    void setSampleRate(float sampleRate);
    void setAudioOnline(bool online);
    void setStatusText(std::wstring text);
    void setSecondaryStatusText(std::wstring text);

    float preferredHeight(float) const override;
    void draw(const RenderResources& resources) override;

private:
    std::wstring title_;
    std::wstring statusText_;
    std::wstring secondaryStatusText_;
    float sampleRate_ = 0.0f;
    bool audioOnline_ = false;
};

}  // namespace winui
