#pragma once

#include <unordered_map>

#include <windows.h>

namespace winui {

// Build the PC keyboard (virtual-key) to MIDI mapping used by WinApp/Sandboxes
// based on a baseMidiNote (usually a C) and an octaveCount range limiter.
std::unordered_map<UINT, int> MakeKeyboardKeymap(int baseMidiNote,
                                                 int octaveCount);

// Same mapping as MakeKeyboardKeymap but writes into an existing container.
void PopulateKeyboardKeymap(std::unordered_map<UINT, int>& keymap,
                            int baseMidiNote,
                            int octaveCount);

}  // namespace winui
