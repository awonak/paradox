#pragma once

#include "daisy_seed.h"
#include "paradox_snapshots.h"
#include <cstddef>
#include <cstdint>

namespace paradox_dsp {

constexpr size_t kNumVoices   = 8;
constexpr size_t kNumChannels = 6;

// Power-of-2 ring buffer capacities in external 64 MB SDRAM:
//   - `it?he is` Single Delay: MaxDly = 3000 ms @ 48 kHz = 144,000 samples -> 2^18 = 262,144 floats (8 MB total)
//   - `here?` Reverb Single Delay: MaxDly = 10000 ms @ 48 kHz = 480,000 samples -> 2^19 = 524,288 floats (32 MB total)
constexpr size_t kItDelayCap   = 262144;
constexpr size_t kItDelayMask  = kItDelayCap - 1;
constexpr size_t kRevDelayCap  = 524288;
constexpr size_t kRevDelayMask = kRevDelayCap - 1;

enum ChannelIndex : uint8_t {
    kChA   = 0,
    kChP   = 1,
    kChRM  = 2,
    kChFM  = 3,
    kChM   = 4,
    kChDEL = 5,
};

struct ToneParams {
    // VaR values in [0.0 .. 127.0]
    float a_var   = 48.0f;
    float p_var   = 30.0f;
    float rm_var  = 0.0f;
    float fm_var  = 0.0f;
    float m_var   = 30.0f;
    float del_var = 19.0f;

    // RnD values in [0.0 .. 127.0]
    float a_rnd   = 97.0f;
    float p_rnd   = 76.0f;
    float rm_rnd  = 0.0f;
    float fm_rnd  = 60.0f;
    float m_rnd   = 127.0f;
    float del_rnd = 108.0f;
};

struct ReverbParams {
    float size  = 1.54f;  // [0.0 .. 4.0]
    float asym  = 1.8f;   // [-4.0 .. 4.0]
    float decay = 52.0f;  // [0.0 .. 100.0]
    float hp    = 12.0f;  // [0.0 .. 127.0]
    float lp    = 130.0f; // [0.0 .. 130.0]
    float mix   = 0.0f;   // [0.0 = 100% "who is", 0.5 = 50/50, 1.0 = 100% ext audio]
};

/** Initialize SDRAM delay lines, sine LUT, and Snapshot 0 seeds. Call AFTER hw.seed.sdram_handle.Init(). */
void Init(float sample_rate);

/** Apply the (bs1, bs2) random seed registers from a factory snapshot. */
void ApplySnapshotSeeds(const FactorySnapshot& snap);

/** Copy the 12 current (bs1, bs2) seed bytes [bs1[0..5], bs2[0..5]] for preset serialization. */
void GetSeeds(uint8_t out[kNumChannels * 2]);

/** Restore 12 (bs1, bs2) seed bytes from a preset blob and sync the RnD integer baseline. */
void SetSeeds(const uint8_t in[kNumChannels * 2]);

/** Set `who is` VaR and RnD parameters. Only channels whose bit is set in
 *  `allow_reseed_mask` (bit c = 1 << c) may reroll (bs1, bs2) when their
 *  integer RnD value changes — preventing InitCatch proximity snaps on page
 *  switches or preset loads from triggering spurious rerolls. */
void SetToneParams(const ToneParams& params, uint8_t allow_reseed_mask = 0x3Fu);

/** Set `here?` reverb and dry/external Mix parameters. */
void SetReverbParams(const ReverbParams& params);

/** Recompute 8-voice `who is` hash tables and filter/delay targets at control rate (~60 Hz - 1 kHz).
 *  Pass `immediate = true` to snap smoothed delay/mix state directly to targets (e.g. at boot). */
void UpdateControlRate(bool immediate = false);

/** Read the 8 raw per-voice hash values r[0..7] in [0, RnD) for UI visualization (main-thread only). */
const float* GetChannelVoiceHashes(uint8_t ch);

/** Returns true once after channel `ch` re-rolled its (bs1, bs2) seeds, then clears the flag (main-thread only). */
bool ConsumeChannelReseed(uint8_t ch);

/** Sample-accurate 8-voice audio callback at 48 kHz. */
void Process(daisy::AudioHandle::InputBuffer  in,
             daisy::AudioHandle::OutputBuffer out,
             size_t                           size);

} // namespace paradox_dsp
