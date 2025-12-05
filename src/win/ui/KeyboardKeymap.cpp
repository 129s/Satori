#include "win/ui/KeyboardKeymap.h"

#include <array>

namespace winui {
namespace {

constexpr int kSemitonesPerOctave = 12;

struct KeyBinding {
    UINT vk = 0;
    int semitoneOffset = 0;
};

constexpr std::array<KeyBinding, 7> kWhiteBindings{{
    {'A', 0}, {'S', 2}, {'D', 4}, {'F', 5}, {'G', 7}, {'H', 9}, {'J', 11},
}};

constexpr std::array<KeyBinding, 5> kBlackBindings{{
    {'W', 1}, {'E', 3}, {'T', 6}, {'Y', 8}, {'U', 10},
}};

}  // namespace

std::unordered_map<UINT, int> MakeKeyboardKeymap(int baseMidiNote,
                                                 int octaveCount) {
    std::unordered_map<UINT, int> keymap;
    PopulateKeyboardKeymap(keymap, baseMidiNote, octaveCount);
    return keymap;
}

void PopulateKeyboardKeymap(std::unordered_map<UINT, int>& keymap,
                            int baseMidiNote,
                            int octaveCount) {
    keymap.clear();
    const int semitoneLimit =
        octaveCount > 0 ? octaveCount * kSemitonesPerOctave : 0;
    if (semitoneLimit <= 0) {
        return;
    }

    auto addBinding = [&](const KeyBinding& binding) {
        if (binding.semitoneOffset < semitoneLimit) {
            keymap[binding.vk] = baseMidiNote + binding.semitoneOffset;
        }
    };
    for (const auto& binding : kWhiteBindings) {
        addBinding(binding);
    }
    for (const auto& binding : kBlackBindings) {
        addBinding(binding);
    }
}

}  // namespace winui
