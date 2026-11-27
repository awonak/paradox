#include "paradox_dsp.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace paradox_dsp {

namespace {

constexpr float  kPi      = 3.14159265358979323846f;
constexpr float  kTwoPi   = 6.28318530717958647692f;
constexpr size_t kLutSize = 4096;
constexpr size_t kLutMask = kLutSize - 1;

// Raw float arrays in 64 MB external SDRAM — zero C++ static constructors!
float DSY_SDRAM_BSS it_delay_buf[kNumVoices][kItDelayCap];
float DSY_SDRAM_BSS rev_delay_l_buf[kNumVoices][kRevDelayCap];
float DSY_SDRAM_BSS rev_delay_r_buf[kNumVoices][kRevDelayCap];

// On-chip SRAM/DTCMRAM fast sine lookup table (16 KB)
float sin_lut_[kLutSize + 1];

// Engine state in fast on-chip DTCMRAM
float        sample_rate_ = 48000.0f;
ToneParams   tone_params_{};
ReverbParams rev_params_{};

// Per-channel seed registers (bs1, bs2 in [0..127]) and last integer RnD value
uint8_t  ch_bs1_[kNumChannels]            = {34, 16, 124, 67, 35, 58};
uint8_t  ch_bs2_[kNumChannels]            = {81, 72, 62, 97, 82, 29};
int16_t  prev_rnd_int_[kNumChannels]      = {97, 76, 0, 60, 127, 108};
bool     ch_reseed_pending_[kNumChannels] = {};
bool     sync_rnd_int_on_next_set_        = true;
uint32_t rng_state_                       = 0x6D2B79F5u;

// Computed 8-voice `who is` FM index [0.0 .. 128.0] and raw hash offsets r in
// [0, RnD)
float who_FM_[kNumVoices]                   = {};
float who_FM_target_[kNumVoices]            = {};
float who_FM_curr_[kNumVoices]              = {};
float who_hash_r_[kNumChannels][kNumVoices] = {};

// Precomputed `it?he is` per-voice targets & audio-rate states
float base_freq_[kNumVoices]             = {};
float vca_gain_[kNumVoices]              = {};
float rm_mix_[kNumVoices]                = {};
float m_fm_weight_[kNumVoices]           = {};
float m_rm_weight_[kNumVoices]           = {};
float base_freq_target_[kNumVoices]      = {};
float base_freq_curr_[kNumVoices]        = {};
float vca_gain_target_[kNumVoices]       = {};
float vca_gain_curr_[kNumVoices]         = {};
float rm_mix_target_[kNumVoices]         = {};
float rm_mix_curr_[kNumVoices]           = {};
float m_fm_weight_target_[kNumVoices]    = {};
float m_fm_weight_curr_[kNumVoices]      = {};
float m_rm_weight_target_[kNumVoices]    = {};
float m_rm_weight_curr_[kNumVoices]      = {};
float it_dly_samples_target_[kNumVoices] = {};
float it_dly_samples_curr_[kNumVoices]   = {};

float  osc_phase_[kNumVoices]    = {};
float  y_sat_prev_[kNumVoices]   = {};
size_t it_write_idx_[kNumVoices] = {};

// Precomputed `here?` per-voice targets & audio-rate states
float  rev_dly_samples_target_l_[kNumVoices] = {};
float  rev_dly_samples_target_r_[kNumVoices] = {};
float  rev_dly_samples_curr_l_[kNumVoices]   = {};
float  rev_dly_samples_curr_r_[kNumVoices]   = {};
float  rev_fb_gain_l_[kNumVoices]            = {};
float  rev_fb_gain_r_[kNumVoices]            = {};
float  rev_fb_gain_target_l_[kNumVoices]     = {};
float  rev_fb_gain_target_r_[kNumVoices]     = {};
float  rev_fb_gain_curr_l_[kNumVoices]       = {};
float  rev_fb_gain_curr_r_[kNumVoices]       = {};
size_t rev_write_idx_l_[kNumVoices]          = {};
size_t rev_write_idx_r_[kNumVoices]          = {};

float rev_hp_state_l_[kNumVoices] = {};
float rev_lp_state_l_[kNumVoices] = {};
float rev_hp_state_r_[kNumVoices] = {};
float rev_lp_state_r_[kNumVoices] = {};
float rev_hp_g_                   = 0.0f;
float rev_lp_g_                   = 0.0f;
float rev_hp_g_target_            = 0.0f;
float rev_hp_g_curr_              = 0.0f;
float rev_lp_g_target_            = 0.0f;
float rev_lp_g_curr_              = 0.0f;

// Per-sample 1-pole smoothing coefficient (~10 ms time constant at 48 kHz)
constexpr float kParamSmoothTauSec = 0.010f;
float           param_smooth_coeff_ = 0.002f;

// Polyphonic per-voice dynamic leveler (AGC)
float peak_rel_coeff_             = 0.0f;
float peak_env_l_[kNumVoices]     = {};
float peak_env_r_[kNumVoices]     = {};
float lvl_lp_state_l_[kNumVoices] = {};
float lvl_hp_state_l_[kNumVoices] = {};
float lvl_lp_state_r_[kNumVoices] = {};
float lvl_hp_state_r_[kNumVoices] = {};
float lvl_lp_g_                   = 0.0f;
float lvl_lp_g_div_               = 1.0f;
float lvl_hp_g_                   = 0.0f;
float lvl_hp_g_div_               = 1.0f;

// Fixed Master output gain (-21.5 dB = 10^(-21.5/20))
constexpr float kFixedMasterGain = 0.084139514f;

float ext_mix_target_ = 0.0f;
float ext_mix_curr_   = 0.0f;
float dry_prev_       = 0.0f;

inline uint8_t RollSeed128() {
    uint32_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x;
    return static_cast<uint8_t>(x & 0x7Fu);
}

// Exponential pitch-to-frequency / time conversion: MIDI pitch P -> Hz or ms
inline double ExpF64(double pitch) {
    return 440.0 * std::pow(2.0, (pitch - 69.0) / 12.0);
}

inline float ExpF32(float pitch) {
    return 440.0f * std::pow(2.0f, (pitch - 69.0f) / 12.0f);
}

// Decibel level to linear amplitude conversion
inline float ExpA32(float db) { return std::pow(10.0f, db / 20.0f); }

// Smooth cubic soft-clipper saturating at +-1.0
inline float Saturator(float x) {
    if (x >= 1.0f)
        return 1.0f;
    if (x <= -1.0f)
        return -1.0f;
    return x * (1.5f - 0.5f * x * x);
}

// Fast linear-interpolated sine from wrapped phase in [0.0f, 1.0f)
inline float FastSinPhase(float phase) {
    float  pos  = phase * static_cast<float>(kLutSize);
    size_t idx0 = static_cast<size_t>(pos) & kLutMask;
    float  frac = pos - std::floor(pos);
    float  s0   = sin_lut_[idx0];
    float  s1   = sin_lut_[idx0 + 1];
    return s0 + frac * (s1 - s0);
}

// 1-pole TPT (trapezoidal) low-pass and high-pass filters
inline float StepTptLp(float x, float &s, float g, float g_div) {
    const float v = (x - s) * g_div;
    const float y = g * v + s;
    s             = y + g * v;
    return y;
}

inline float StepTptHp(float x, float &s, float g, float g_div) {
    const float v  = (x - s) * g_div;
    const float lp = g * v + s;
    s              = lp + g * v;
    return x - lp;
}

// Fractional-sample ring-buffer read: split integer and fractional delay before
// subtracting from write_idx so frac retains full 24-bit float mantissa
// precision.
inline float ReadDelayLine(const float *buf, size_t write_idx, size_t mask,
                           float delay_samples) {
    float d = delay_samples;
    if (d < 0.0f)
        d = 0.0f;
    float max_s = static_cast<float>(mask - 2);
    if (d > max_s)
        d = max_s;

    int32_t d_int = static_cast<int32_t>(d);
    float   frac  = d - static_cast<float>(d_int);
    size_t  idx1  = (write_idx - 1u - static_cast<size_t>(d_int)) & mask;
    size_t  idx0  = (idx1 - 1u) & mask;
    float   b     = buf[idx1];
    float   a     = buf[idx0];
    return b + frac * (a - b);
}

// Evaluates one `who is > ch > rnd` channel value for voice clk in {0..7} at
// control rate. Quantizes rng_val to an integer in [0..127] (matching
// SetToneParams' re-seed threshold) so sub-integer knob movement does not
// cause std::fmod(p1 * p2, rng) to wrap chaotically every control frame.
inline float ComputeWhoIsVoice(int clk, float var_val, float rng_val,
                               uint8_t bs1, uint8_t bs2,
                               float *out_r = nullptr) {
    double       r       = 0.0;
    const double rng_int =
        static_cast<double>(std::clamp(std::lround(rng_val), 0L, 127L));
    if (rng_int > 0.0) {
        double p1   = ExpF64(static_cast<double>(clk + bs1));
        double p2   = ExpF64(static_cast<double>(clk + bs2));
        double prod = p1 * p2;
        r           = std::fmod(prod, rng_int);
        if (r < 0.0)
            r += rng_int;
    }
    if (out_r)
        *out_r = static_cast<float>(r);
    double delta = r - 2.0 * static_cast<double>(var_val);
    double err   = delta - 256.0 * std::round(delta / 256.0);
    return static_cast<float>(std::fabs(err));
}

void RecomputeAllTargets(bool immediate) {
    for (int v = 0; v < static_cast<int>(kNumVoices); ++v) {
        const float who_a = ComputeWhoIsVoice(
            v, tone_params_.a_var, tone_params_.a_rnd, ch_bs1_[kChA],
            ch_bs2_[kChA], &who_hash_r_[kChA][v]);
        const float who_p = ComputeWhoIsVoice(
            v, tone_params_.p_var, tone_params_.p_rnd, ch_bs1_[kChP],
            ch_bs2_[kChP], &who_hash_r_[kChP][v]);
        const float who_rm = ComputeWhoIsVoice(
            v, tone_params_.rm_var, tone_params_.rm_rnd, ch_bs1_[kChRM],
            ch_bs2_[kChRM], &who_hash_r_[kChRM][v]);
        who_FM_[v]        = ComputeWhoIsVoice(v, tone_params_.fm_var,
                                              tone_params_.fm_rnd, ch_bs1_[kChFM],
                                              ch_bs2_[kChFM], &who_hash_r_[kChFM][v]);
        who_FM_target_[v] = ComputeWhoIsVoice(
            v, tone_params_.fm_var, tone_params_.fm_rnd, ch_bs1_[kChFM],
            ch_bs2_[kChFM], &who_hash_r_[kChFM][v]);
        const float who_m = ComputeWhoIsVoice(
            v, tone_params_.m_var, tone_params_.m_rnd, ch_bs1_[kChM],
            ch_bs2_[kChM], &who_hash_r_[kChM][v]);
        const float who_del = ComputeWhoIsVoice(
            v, tone_params_.del_var, tone_params_.del_rnd, ch_bs1_[kChDEL],
            ch_bs2_[kChDEL], &who_hash_r_[kChDEL][v]);

        float p_osc     = 2.0f * who_p - 128.0f;
        base_freq_[v]   = ExpF32(p_osc);
        vca_gain_[v]    = who_a / 16.0f;
        rm_mix_[v]      = std::clamp(who_rm / 128.0f, 0.0f, 1.0f);
        m_fm_weight_[v] = who_m / 2.0f;
        m_rm_weight_[v] = who_m / 128.0f;
        float p_osc            = 2.0f * who_p - 128.0f;
        base_freq_target_[v]   = ExpF32(p_osc);
        vca_gain_target_[v]    = who_a / 16.0f;
        rm_mix_target_[v]      = std::clamp(who_rm / 128.0f, 0.0f, 1.0f);
        m_fm_weight_target_[v] = who_m / 2.0f;
        m_rm_weight_target_[v] = who_m / 128.0f;

        float p_del               = 1.8f * who_del - 128.0f;
        float dly_ms              = std::min(3000.0f, ExpF32(p_del));
        it_dly_samples_target_[v] = dly_ms * 0.001f * sample_rate_;
        if (immediate) {
            base_freq_curr_[v]      = base_freq_target_[v];
            who_FM_curr_[v]         = who_FM_target_[v];
            vca_gain_curr_[v]       = vca_gain_target_[v];
            rm_mix_curr_[v]         = rm_mix_target_[v];
            m_fm_weight_curr_[v]    = m_fm_weight_target_[v];
            m_rm_weight_curr_[v]    = m_rm_weight_target_[v];
            it_dly_samples_curr_[v] = it_dly_samples_target_[v];
        }
    }

    // Reverb (`Room`, `+tovoice`, `asymmetry`, `Reflections`)
    float sz_sq   = rev_params_.size * rev_params_.size;
    float asym_l  = std::max(0.0f, rev_params_.asym);
    float asym_r  = std::max(0.0f, -rev_params_.asym);
    float dec_lin = ExpA32(rev_params_.decay);

    float fc_hp =
        std::clamp(ExpF32(rev_params_.hp), 0.01f, 0.49f * sample_rate_);
    rev_hp_g_ = std::tan(kPi * fc_hp / sample_rate_);
    rev_hp_g_target_ = std::tan(kPi * fc_hp / sample_rate_);

    float fc_lp =
        std::clamp(ExpF32(rev_params_.lp), 0.01f, 0.49f * sample_rate_);
    rev_lp_g_ = std::tan(kPi * fc_lp / sample_rate_);
    rev_lp_g_target_ = std::tan(kPi * fc_lp / sample_rate_);

    if (immediate) {
        rev_hp_g_curr_ = rev_hp_g_target_;
        rev_lp_g_curr_ = rev_lp_g_target_;
    }

    for (size_t v = 0; v < kNumVoices; ++v) {
        float pitch_base = static_cast<float>(v + 2) * sz_sq;
        float tm_l_ms    = std::min(10000.0f, ExpF32(pitch_base + asym_l));
        float tm_r_ms    = std::min(10000.0f, ExpF32(pitch_base + asym_r));

        rev_dly_samples_target_l_[v] = tm_l_ms * 0.001f * sample_rate_;
        rev_dly_samples_target_r_[v] = tm_r_ms * 0.001f * sample_rate_;
        rev_fb_gain_target_l_[v]     = dec_lin / (dec_lin + tm_l_ms);
        rev_fb_gain_target_r_[v]     = dec_lin / (dec_lin + tm_r_ms);
        if (immediate) {
            rev_dly_samples_curr_l_[v] = rev_dly_samples_target_l_[v];
            rev_dly_samples_curr_r_[v] = rev_dly_samples_target_r_[v];
            rev_fb_gain_curr_l_[v]     = rev_fb_gain_target_l_[v];
            rev_fb_gain_curr_r_[v]     = rev_fb_gain_target_r_[v];
        }

        rev_fb_gain_l_[v] = dec_lin / (dec_lin + tm_l_ms);
        rev_fb_gain_r_[v] = dec_lin / (dec_lin + tm_r_ms);
    }

    ext_mix_target_ = std::clamp(rev_params_.mix, 0.0f, 1.0f);
    if (immediate) {
        ext_mix_curr_ = ext_mix_target_;
    }
}

} // namespace

void Init(float sample_rate) {
    sample_rate_ = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    param_smooth_coeff_ =
        1.0f - std::exp(-1.0f / (kParamSmoothTauSec * sample_rate_));

    // 1. Initialize 4096-entry sine lookup table
    for (size_t i = 0; i <= kLutSize; ++i) {
        sin_lut_[i] = std::sin(kTwoPi * static_cast<float>(i) /
                               static_cast<float>(kLutSize));
    }

    // 2. Zero external SDRAM delay buffers AFTER SDRAM controller is
    // initialized
    std::memset(it_delay_buf, 0, sizeof(it_delay_buf));
    std::memset(rev_delay_l_buf, 0, sizeof(rev_delay_l_buf));
    std::memset(rev_delay_r_buf, 0, sizeof(rev_delay_r_buf));

    // 3. Initialize fixed `level` filter constants (Peak Detector Rel=65 ->
    // tau=1.77422s, LP P=40, HP P=20)
    constexpr float kTauRel = 1.77422f;
    peak_rel_coeff_         = 1.0f / (kTauRel * sample_rate_);

    float fc40    = std::min(0.49f * sample_rate_, ExpF32(40.0f));
    lvl_lp_g_     = std::tan(kPi * fc40 / sample_rate_);
    lvl_lp_g_div_ = 1.0f / (1.0f + lvl_lp_g_);

    float fc20    = std::min(0.49f * sample_rate_, ExpF32(20.0f));
    lvl_hp_g_     = std::tan(kPi * fc20 / sample_rate_);
    lvl_hp_g_div_ = 1.0f / (1.0f + lvl_hp_g_);

    for (size_t v = 0; v < kNumVoices; ++v) {
        osc_phase_[v]  = 0.0f;
        y_sat_prev_[v] = 0.0f;

        // Stagger initial write indices across the 128 Cortex-M7 L1 D-Cache
        // sets (32 floats = 128 bytes = 4 cache lines per buffer) so the 24
        // power-of-2 SDRAM ring buffers do not alias onto the same 4-way set.
        it_write_idx_[v]    = (v * 32u) & kItDelayMask;
        rev_write_idx_l_[v] = ((v + 8u) * 32u) & kRevDelayMask;
        rev_write_idx_r_[v] = ((v + 16u) * 32u) & kRevDelayMask;

        rev_hp_state_l_[v] = 0.0f;
        rev_lp_state_l_[v] = 0.0f;
        rev_hp_state_r_[v] = 0.0f;
        rev_lp_state_r_[v] = 0.0f;

        peak_env_l_[v]     = 1e-4f;
        peak_env_r_[v]     = 1e-4f;
        lvl_lp_state_l_[v] = 0.0f;
        lvl_hp_state_l_[v] = 0.0f;
        lvl_lp_state_r_[v] = 0.0f;
        lvl_hp_state_r_[v] = 0.0f;
    }

    dry_prev_       = 0.0f;
    ext_mix_target_ = 0.0f;
    ext_mix_curr_   = 0.0f;
    ApplySnapshotSeeds(kFactorySnapshots[0]);
    RecomputeAllTargets(true);
}

void ApplySnapshotSeeds(const FactorySnapshot &snap) {
    const ChannelSnap *chs[kNumChannels] = {&snap.A,  &snap.P, &snap.RM,
                                            &snap.FM, &snap.M, &snap.DEL};
    for (size_t c = 0; c < kNumChannels; ++c) {
        ch_bs1_[c]            = chs[c]->bs1;
        ch_bs2_[c]            = chs[c]->bs2;
        prev_rnd_int_[c]      = static_cast<int16_t>(chs[c]->rnd);
        ch_reseed_pending_[c] = false;
    }
    sync_rnd_int_on_next_set_ = true;
}

void GetSeeds(uint8_t out[kNumChannels * 2]) {
    for (size_t c = 0; c < kNumChannels; ++c) {
        out[c]                = ch_bs1_[c];
        out[kNumChannels + c] = ch_bs2_[c];
    }
}

void SetSeeds(const uint8_t in[kNumChannels * 2]) {
    for (size_t c = 0; c < kNumChannels; ++c) {
        ch_bs1_[c]            = in[c] & 0x7Fu;
        ch_bs2_[c]            = in[kNumChannels + c] & 0x7Fu;
        ch_reseed_pending_[c] = false;
    }
    sync_rnd_int_on_next_set_ = true;
}

void SetToneParams(const ToneParams &params, uint8_t allow_reseed_mask) {
    tone_params_ = params;

    const float rnd_vals[kNumChannels] = {params.a_rnd,  params.p_rnd,
                                          params.rm_rnd, params.fm_rnd,
                                          params.m_rnd,  params.del_rnd};

    if (sync_rnd_int_on_next_set_) {
        for (size_t c = 0; c < kNumChannels; ++c) {
            prev_rnd_int_[c] = static_cast<int16_t>(
                std::clamp(std::lround(rnd_vals[c]), 0L, 127L));
        }
        sync_rnd_int_on_next_set_ = false;
        return;
    }

    // Turning an RnD knob to a new non-zero integer value re-rolls the
    // channel's (bs1, bs2) seeds in [0..127]. Gated by allow_reseed_mask so
    // page-entry InitCatch snaps or preset loads update prev_rnd_int_ silently
    // without rerolling seeds.
    for (size_t c = 0; c < kNumChannels; ++c) {
        int16_t cur_int = static_cast<int16_t>(
            std::clamp(std::lround(rnd_vals[c]), 0L, 127L));
        if (cur_int != prev_rnd_int_[c]) {
            prev_rnd_int_[c] = cur_int;
            const bool can_reseed =
                (allow_reseed_mask & static_cast<uint8_t>(1u << c)) != 0u;
            if (can_reseed && cur_int > 0) {
                ch_bs1_[c]            = RollSeed128();
                ch_bs2_[c]            = RollSeed128();
                ch_reseed_pending_[c] = true;
            }
        }
    }
}

void SetReverbParams(const ReverbParams &params) { rev_params_ = params; }

void UpdateControlRate(bool immediate) { RecomputeAllTargets(immediate); }

const float *GetChannelVoiceHashes(uint8_t ch) {
    if (ch >= kNumChannels)
        return nullptr;
    return who_hash_r_[ch];
}

bool ConsumeChannelReseed(uint8_t ch) {
    if (ch >= kNumChannels)
        return false;
    const bool pending     = ch_reseed_pending_[ch];
    ch_reseed_pending_[ch] = false;
    return pending;
}

void Process(daisy::AudioHandle::InputBuffer  in,
             daisy::AudioHandle::OutputBuffer out, size_t size) {
    const float sr             = sample_rate_;
    const float smooth         = param_smooth_coeff_;
    const float peak_rel_coeff = peak_rel_coeff_;

    const float rev_hp_g     = rev_hp_g_;
    const float rev_hp_g_div = 1.0f / (1.0f + rev_hp_g);
    const float rev_lp_g     = rev_lp_g_;
    const float rev_lp_g_div = 1.0f / (1.0f + rev_lp_g);

    const float lvl_lp_g     = lvl_lp_g_;
    const float lvl_lp_g_div = lvl_lp_g_div_;
    const float lvl_hp_g     = lvl_hp_g_;
    const float lvl_hp_g_div = lvl_hp_g_div_;

    for (size_t i = 0; i < size; ++i) {
        ext_mix_curr_ += 0.002f * (ext_mix_target_ - ext_mix_curr_);
        ext_mix_curr_  += smooth * (ext_mix_target_ - ext_mix_curr_);
        rev_hp_g_curr_ += smooth * (rev_hp_g_target_ - rev_hp_g_curr_);
        rev_lp_g_curr_ += smooth * (rev_lp_g_target_ - rev_lp_g_curr_);

        const float rev_hp_g     = rev_hp_g_curr_;
        const float rev_hp_g_div = 1.0f / (1.0f + rev_hp_g);
        const float rev_lp_g     = rev_lp_g_curr_;
        const float rev_lp_g_div = 1.0f / (1.0f + rev_lp_g);

        // --- Stage 1: `it?he is` 8-voice coupled synthesis ---
        float bus_fm = 0.0f;
        float bus_rm = 0.0f;
        for (size_t v = 0; v < kNumVoices; ++v) {
            bus_fm += m_fm_weight_[v] * y_sat_prev_[v];
            bus_rm += m_rm_weight_[v] * y_sat_prev_[v];
            m_fm_weight_curr_[v] +=
                smooth * (m_fm_weight_target_[v] - m_fm_weight_curr_[v]);
            m_rm_weight_curr_[v] +=
                smooth * (m_rm_weight_target_[v] - m_rm_weight_curr_[v]);
            bus_fm += m_fm_weight_curr_[v] * y_sat_prev_[v];
            bus_rm += m_rm_weight_curr_[v] * y_sat_prev_[v];
        }

        float dry_sum = 0.0f;
        for (size_t v = 0; v < kNumVoices; ++v) {
            base_freq_curr_[v] +=
                smooth * (base_freq_target_[v] - base_freq_curr_[v]);
            who_FM_curr_[v] += smooth * (who_FM_target_[v] - who_FM_curr_[v]);
            rm_mix_curr_[v] += smooth * (rm_mix_target_[v] - rm_mix_curr_[v]);
            vca_gain_curr_[v] +=
                smooth * (vca_gain_target_[v] - vca_gain_curr_[v]);
            it_dly_samples_curr_[v] +=
                0.002f * (it_dly_samples_target_[v] - it_dly_samples_curr_[v]);
                smooth * (it_dly_samples_target_[v] - it_dly_samples_curr_[v]);

            float f_inst = base_freq_[v] + who_FM_[v] * bus_fm;
            float rm     = rm_mix_[v];
            float f_inst = base_freq_curr_[v] + who_FM_curr_[v] * bus_fm;
            float rm     = rm_mix_curr_[v];
            float a_inst = (1.0f - rm) + rm * bus_rm;

            float ph      = osc_phase_[v];
            float y_osc   = a_inst * FastSinPhase(ph);
            float next_ph = ph + f_inst / sr;
            next_ph -= std::floor(next_ph);
            osc_phase_[v] = next_ph;

            float y_vca = vca_gain_[v] * y_osc;
            float y_vca = vca_gain_curr_[v] * y_osc;
            dry_sum += y_vca;

            float *it_buf    = it_delay_buf[v];
            size_t w_idx     = it_write_idx_[v];
            it_buf[w_idx]    = y_vca;
            w_idx            = (w_idx + 1) & kItDelayMask;
            it_write_idx_[v] = w_idx;

            float y_dly    = ReadDelayLine(it_buf, w_idx, kItDelayMask,
                                           it_dly_samples_curr_[v]);
            y_sat_prev_[v] = Saturator(y_dly);
        }

        // Crossfade between internal "who is" synth (CCW = 0.0) and external
        // audio input (CW = 1.0)
        float ext_in = (in[0][i] + in[1][i]) * 0.5f;
        float dry_in =
            (1.0f - ext_mix_curr_) * dry_prev_ + ext_mix_curr_ * ext_in;

        // --- Stage 2: `here?` 8-voice cross-coupled reverb & per-voice leveler
        // ---
        float out_sum_l = 0.0f;
        float out_sum_r = 0.0f;

        for (size_t v = 0; v < kNumVoices; ++v) {
            rev_dly_samples_curr_l_[v] +=
                0.002f *
                smooth *
                (rev_dly_samples_target_l_[v] - rev_dly_samples_curr_l_[v]);
            rev_dly_samples_curr_r_[v] +=
                0.002f *
                smooth *
                (rev_dly_samples_target_r_[v] - rev_dly_samples_curr_r_[v]);
            rev_fb_gain_curr_l_[v] +=
                smooth * (rev_fb_gain_target_l_[v] - rev_fb_gain_curr_l_[v]);
            rev_fb_gain_curr_r_[v] +=
                smooth * (rev_fb_gain_target_r_[v] - rev_fb_gain_curr_r_[v]);

            float *buf_l   = rev_delay_l_buf[v];
            float *buf_r   = rev_delay_r_buf[v];
            size_t w_idx_l = rev_write_idx_l_[v];
            size_t w_idx_r = rev_write_idx_r_[v];

            float fb_l = ReadDelayLine(buf_l, w_idx_l, kRevDelayMask,
                                       rev_dly_samples_curr_l_[v]);
            float fb_r = ReadDelayLine(buf_r, w_idx_r, kRevDelayMask,
                                       rev_dly_samples_curr_r_[v]);

            // Cross-coupled feedback: Left receives fb_r, Right receives fb_l
            float in_l = dry_in + rev_fb_gain_l_[v] * fb_r;
            float in_r = dry_in + rev_fb_gain_r_[v] * fb_l;
            float in_l = dry_in + rev_fb_gain_curr_l_[v] * fb_r;
            float in_r = dry_in + rev_fb_gain_curr_r_[v] * fb_l;

            // Left / Right 1-Pole HP -> 1-Pole LP
            const float hp_out_l =
                StepTptHp(in_l, rev_hp_state_l_[v], rev_hp_g, rev_hp_g_div);
            const float w_l =
                StepTptLp(hp_out_l, rev_lp_state_l_[v], rev_lp_g, rev_lp_g_div);

            const float hp_out_r =
                StepTptHp(in_r, rev_hp_state_r_[v], rev_hp_g, rev_hp_g_div);
            const float w_r =
                StepTptLp(hp_out_r, rev_lp_state_r_[v], rev_lp_g, rev_lp_g_div);

            // Write filtered tank outputs into Single Delay lines
            buf_l[w_idx_l]      = w_l;
            rev_write_idx_l_[v] = (w_idx_l + 1) & kRevDelayMask;
            buf_r[w_idx_r]      = w_r;
            rev_write_idx_r_[v] = (w_idx_r + 1) & kRevDelayMask;

            // Per-voice dynamic leveler (AGC):
            // 1) Peak Detector (Rel = 65)
            float abs_l = std::fabs(w_l);
            if (abs_l > peak_env_l_[v])
                peak_env_l_[v] = abs_l;
            else
                peak_env_l_[v] += peak_rel_coeff * (abs_l - peak_env_l_[v]);

            float abs_r = std::fabs(w_r);
            if (abs_r > peak_env_r_[v])
                peak_env_r_[v] = abs_r;
            else
                peak_env_r_[v] += peak_rel_coeff * (abs_r - peak_env_r_[v]);

            // 2) 1-Pole LP at P = 40 (82.407 Hz)
            const float env_lp_l = StepTptLp(peak_env_l_[v], lvl_lp_state_l_[v],
                                             lvl_lp_g, lvl_lp_g_div);
            const float env_lp_r = StepTptLp(peak_env_r_[v], lvl_lp_state_r_[v],
                                             lvl_lp_g, lvl_lp_g_div);

            // 3) Divide (w / env_lp)
            const float u_l = (env_lp_l > 1e-7f) ? (w_l / env_lp_l) : 0.0f;
            const float u_r = (env_lp_r > 1e-7f) ? (w_r / env_lp_r) : 0.0f;

            // 4) 1-Pole HP at P = 20 (25.957 Hz)
            const float z_l =
                StepTptHp(u_l, lvl_hp_state_l_[v], lvl_hp_g, lvl_hp_g_div);
            const float z_r =
                StepTptHp(u_r, lvl_hp_state_r_[v], lvl_hp_g, lvl_hp_g_div);

            out_sum_l += z_l;
            out_sum_r += z_r;
        }

        dry_prev_ = dry_sum;

        // Apply fixed Master gain (-21.5 dB) and cubic soft-clipping Saturator
        out[0][i] = Saturator(out_sum_l * kFixedMasterGain);
        out[1][i] = Saturator(out_sum_r * kFixedMasterGain);
    }
}

} // namespace paradox_dsp
