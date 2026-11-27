#pragma once

#include "alchemy/led/panel.h"

namespace paradox_palette {

// Page & Latch Indicator Colors (B1 Page Cycle & B3 VaR/RnD Latch)
constexpr alchemy::LedPanel::Rgb kPageVarIndicator = {
    0x98, 0xFF, 0x10}; // Phosphor Lime   ("who is" VaR)
constexpr alchemy::LedPanel::Rgb kPageRndIndicator = {
    0xE8, 0x24, 0x08}; // Crimson Orange  ("who is" RnD)
constexpr alchemy::LedPanel::Rgb kPageReverbIndicator = {
    0x88, 0x20, 0xFF}; // Electric Violet ("here?" Reverb)

// Button 2 Preset Slot Cycle & Save Overlay Accents
constexpr alchemy::LedPanel::Rgb kSnapshotIdle  = {0x30, 0x20,
                                                   0x50}; // Dim Violet
constexpr alchemy::LedPanel::Rgb kSnapshotFlash = {
    0xFF, 0xD0, 0x40}; // Warm Gold (slot load / segment readout)
constexpr alchemy::LedPanel::Rgb kPresetSaveHold = {
    0xFF, 0x50, 0x10}; // Ember Orange (B2 hold save charge)
constexpr alchemy::LedPanel::Rgb kPresetSaveDone = {
    0xFF, 0xFF, 0xE8}; // Bright White-Gold (save commit flash)

// Page 0 ("who is" VaR) Knob Ring Colors
constexpr alchemy::LedPanel::Rgb kKnobVar = {
    0x98, 0xFF, 0x10}; // Phosphor Lime / Acid Chartreuse
constexpr alchemy::LedPanel::Rgb kCometCore = {
    0xE8, 0xFF, 0xA8}; // White-hot Phosphor comet core

// Page 1 ("who is" RnD) Knob Ring Colors
constexpr alchemy::LedPanel::Rgb kKnobRnd      = {0xDC, 0x22,
                                                  0x06}; // Deep Crimson Orange
constexpr alchemy::LedPanel::Rgb kVoiceScatter = {
    0xFF, 0x58, 0x10}; // Glowing ember-orange 8-voice cluster highlight
constexpr alchemy::LedPanel::Rgb kReseedSpark = {
    0xFF, 0xB8, 0x60}; // Incandescent amber-white re-seed burst
constexpr alchemy::LedPanel::Rgb kGhostPip = {
    0xB8, 0xB8, 0xCC}; // Dim cool-white uncaught turning ghost pip

// Page 2 ("here?" Reverb) Knob Ring Colors
constexpr alchemy::LedPanel::Rgb kKnobReverb = {
    0x88, 0x20, 0xFF}; // Electric Violet / Ultraviolet
constexpr alchemy::LedPanel::Rgb kAsymPos    = kKnobReverb;
constexpr alchemy::LedPanel::Rgb kAsymNeg    = kKnobReverb;
constexpr alchemy::LedPanel::Rgb kAsymCenter = {0x50, 0x38,
                                                0x70}; // Neutral Violet Center

} // namespace paradox_palette
