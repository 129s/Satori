#pragma once

#include <memory>
#include <string>
#include <vector>

#include "win/ui/UIModel.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class DropdownSelectorNode;

class HeaderBarNode : public UILayoutNode {
public:
    HeaderBarNode();

    void setModel(const HeaderBarModel& model);

    std::shared_ptr<DropdownSelectorNode> deviceSelector() const { return deviceSelector_; }
    std::shared_ptr<DropdownSelectorNode> midiInputSelector() const { return midiInputSelector_; }
    std::shared_ptr<DropdownSelectorNode> sampleRateSelector() const { return sampleRateSelector_; }
    std::shared_ptr<DropdownSelectorNode> bufferFramesSelector() const { return bufferFramesSelector_; }

    std::vector<std::shared_ptr<DropdownSelectorNode>> selectors() const;

    float preferredHeight(float width) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    enum class MidiButtonId { Load, Play, Pause, Stop };
    struct MidiButtonState {
        MidiButtonId id = MidiButtonId::Load;
        D2D1_RECT_F bounds{};
        bool pressed = false;
        bool hovered = false;
        bool enabled = false;
        std::function<void()> onClick;
    };

     std::wstring logoText_ = L"Satori";

    std::wstring deviceLabel_ = L"Device";
    std::wstring midiInputLabel_ = L"MIDI In";
    std::wstring sampleRateLabel_ = L"SampleRate";
    std::wstring bufferFramesLabel_ = L"BufferFrames";

    std::shared_ptr<DropdownSelectorNode> deviceSelector_;
    std::shared_ptr<DropdownSelectorNode> midiInputSelector_;
    std::shared_ptr<DropdownSelectorNode> sampleRateSelector_;
     std::shared_ptr<DropdownSelectorNode> bufferFramesSelector_;

     D2D1_RECT_F logoRect_{};
     D2D1_RECT_F midiRect_{};
     D2D1_RECT_F deviceLabelRect_{};
     D2D1_RECT_F midiInputLabelRect_{};
     D2D1_RECT_F sampleRateLabelRect_{};
     D2D1_RECT_F bufferFramesLabelRect_{};

    std::vector<MidiButtonState> midiButtons_;
    int activeMidiButton_ = -1;
};

}  // namespace winui
