/**
 * paradox.cpp — Paradox DSP Firmware for Hermetic Modular Alchemy Lab
 *
 * Control Surface & UI:
 *   Page 1 ("who is" tone generator):
 *     - Default layer (kPageVar): Controls VaR (0..127) of the 6 parameters
 *       (A, P, >RM, >FM, M>, DEL).
 *     - Latched sub-page (kPageRnd): Pressing B3 toggles (.Latch) between
 *       VaR and RnD (0..127) of the 6 parameters.
 *   Page 2 ("here?" reverb):
 *     - Pressing B1 toggles (.Cycle) between "who is" and "here?"
 *       (SizE, AsyM, Decay, Mix, HP, LP).
 *   Button 2 (B2):
 *     - Tap: Cycles through the 16 QSPI preset slots (pre-seeded at first boot
 *       with 16 default factory presets) and displays a 1-second segmented
 *       slot overlay across K1-K6.
 *     - Hold (1.5 s): Overwrites the currently active preset slot in QSPI
 *       flash.
 */

#include <algorithm>
#include <cmath>

#include "alchemy/host_link/host.h"
#include "alchemy/hw/alchemy_lab.h"
#include "alchemy/led/ring_frame.h"
#include "alchemy/led/signal.h"
#include "alchemy/surface/button_bank.h"
#include "alchemy/surface/control_loop.h"
#include "alchemy/surface/cv_matrix.h"
#include "alchemy/surface/jack.h"
#include "alchemy/surface/page.h"
#include "alchemy/surface/pager.h"
#include "alchemy/surface/param_lock.h"
#include "alchemy/surface/presets.h"
#include "alchemy/surface/settings.h"
#include "alchemy/surface/virtual_button.h"
#include "alchemy/surface/virtual_knob.h"
#include "daisy_seed.h"

#include "paradox_dsp.h"
#include "paradox_palette.h"
#include "paradox_snapshots.h"

using namespace alchemy;

enum : uint8_t {
    kPageVar     = 0,
    kPageRnd     = 1,
    kPageReverb  = 2,
    kNumAppPages = 3,
};

/* ── Custom Page 1 Ring Renderer Forward Declarations ───────────────────────
 */
struct WhoRingCtx {
    uint8_t ch;
};

static WhoRingCtx who_ring_ctx[paradox_dsp::kNumChannels] = {
    {paradox_dsp::kChA},  {paradox_dsp::kChP}, {paradox_dsp::kChRM},
    {paradox_dsp::kChFM}, {paradox_dsp::kChM}, {paradox_dsp::kChDEL},
};

static void RenderWhoRing(LedPanel &panel, uint8_t pot, const ArcGeometry &geo,
                          float norm, uint32_t t_ms, void *ctx);
static void RenderReverbCatchPip(LedPanel &panel, uint8_t pot,
                                 const ArcGeometry &geo, float norm,
                                 uint32_t t_ms, void *ctx);

/* ── Page 0: "who is" Tone Generator — VaR (0..127) ──────────────────────── */
static VirtualKnob p1_a_var =
    VirtualKnob(kPotTopLeft, "A")
        .Linear(0.f, 127.f)
        .Ident("who.a.var")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChA]));

static VirtualKnob p1_p_var =
    VirtualKnob(kPotTopRight, "P")
        .Linear(0.f, 127.f)
        .Ident("who.p.var")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChP]));

static VirtualKnob p1_rm_var =
    VirtualKnob(kPotMiddleLeft, ">RM")
        .Linear(0.f, 127.f)
        .Ident("who.rm.var")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChRM]));

static VirtualKnob p1_fm_var =
    VirtualKnob(kPotMiddleRight, ">FM")
        .Linear(0.f, 127.f)
        .Ident("who.fm.var")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChFM]));

static VirtualKnob p1_m_var =
    VirtualKnob(kPotBottomLeft, "M>")
        .Linear(0.f, 127.f)
        .Ident("who.m.var")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChM]));

static VirtualKnob p1_del_var =
    VirtualKnob(kPotBottomRight, "DEL")
        .Linear(0.f, 127.f)
        .Ident("who.del.var")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChDEL]));

/* ── Page 1: "who is" Tone Generator — RnD (0..127, Latched on B3) ───────── */
static VirtualKnob p1_a_rnd =
    VirtualKnob(kPotTopLeft, "A RND")
        .Linear(0.f, 127.f)
        .Ident("who.a.rnd")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChA]));

static VirtualKnob p1_p_rnd =
    VirtualKnob(kPotTopRight, "P RND")
        .Linear(0.f, 127.f)
        .Ident("who.p.rnd")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChP]));

static VirtualKnob p1_rm_rnd =
    VirtualKnob(kPotMiddleLeft, ">RM RND")
        .Linear(0.f, 127.f)
        .Ident("who.rm.rnd")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChRM]));

static VirtualKnob p1_fm_rnd =
    VirtualKnob(kPotMiddleRight, ">FM RND")
        .Linear(0.f, 127.f)
        .Ident("who.fm.rnd")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChFM]));

static VirtualKnob p1_m_rnd =
    VirtualKnob(kPotBottomLeft, "M> RND")
        .Linear(0.f, 127.f)
        .Ident("who.m.rnd")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChM]));

static VirtualKnob p1_del_rnd =
    VirtualKnob(kPotBottomRight, "DEL RND")
        .Linear(0.f, 127.f)
        .Ident("who.del.rnd")
        .Ring(Custom(RenderWhoRing, &who_ring_ctx[paradox_dsp::kChDEL]));

/* ── Page 2: "here?" Reverb Controls ─────────────────────────────────────── */
static VirtualKnob p2_size = VirtualKnob(kPotTopLeft, "SizE")
                                 .Linear(0.f, 4.f)
                                 .Ident("here.size")
                                 .Ring(Level(paradox_palette::kKnobReverb))
                                 .Overdraw(RenderReverbCatchPip);

static VirtualKnob p2_asym =
    VirtualKnob(kPotTopRight, "AsyM")
        .Linear(-4.f, 4.f)
        .Ident("here.asym")
        .Ring(Bipolar(paradox_palette::kAsymPos, paradox_palette::kAsymNeg,
                      paradox_palette::kAsymCenter))
        .Overdraw(RenderReverbCatchPip);

static VirtualKnob p2_decay = VirtualKnob(kPotMiddleLeft, "Decay")
                                  .Linear(0.f, 100.f)
                                  .Ident("here.decay")
                                  .Ring(Level(paradox_palette::kKnobReverb))
                                  .Overdraw(RenderReverbCatchPip);

static VirtualKnob p2_mix = VirtualKnob(kPotMiddleRight, "Mix")
                                .Linear(0.f, 1.f)
                                .Ident("here.mix")
                                .Ring(Level(paradox_palette::kKnobReverb))
                                .Overdraw(RenderReverbCatchPip);

static VirtualKnob p2_hp = VirtualKnob(kPotBottomLeft, "HP")
                               .Linear(0.f, 127.f)
                               .Ident("here.hp")
                               .Ring(Level(paradox_palette::kKnobReverb))
                               .Overdraw(RenderReverbCatchPip);

static VirtualKnob p2_lp = VirtualKnob(kPotBottomRight, "LP")
                               .Linear(0.f, 130.f)
                               .Ident("here.lp")
                               .Ring(Level(paradox_palette::kKnobReverb))
                               .Overdraw(RenderReverbCatchPip);

/* ── Pages ───────────────────────────────────────────────────────────────── */
static Page page_var =
    Page(kPageVar)
        .Name("who is (VaR)")
        .Color(paradox_palette::kPageVarIndicator)
        .Knobs(p1_a_var, p1_p_var, p1_rm_var, p1_fm_var, p1_m_var, p1_del_var);

static Page page_rnd =
    Page(kPageRnd)
        .Name("who is (RnD)")
        .Color(paradox_palette::kPageRndIndicator)
        .Knobs(p1_a_rnd, p1_p_rnd, p1_rm_rnd, p1_fm_rnd, p1_m_rnd, p1_del_rnd);

static Page page_reverb =
    Page(kPageReverb)
        .Name("here?")
        .Color(paradox_palette::kPageReverbIndicator)
        .Knobs(p2_size, p2_asym, p2_decay, p2_mix, p2_hp, p2_lp);

/* ── SDK Surfaces ────────────────────────────────────────────────────────── */
static AlchemyLab  hw;
static ControlLoop loop(hw);

static Pager pager = Pager(kNumAppPages, kNumPots)
                         .Cycle(hw.buttons[kButtonB1], kPageVar, kPageReverb)
                         .Latch(hw.buttons[kButtonB3], kPageVar, kPageRnd);

using Locks = ParamLock<kNumAppPages * kNumPots>;
static uint16_t DSY_SDRAM_BSS lock_arena[Locks::kArenaSamples];
static Locks                  locks(hw.buttons[kButtonB1], pager,
                                    LockArena{lock_arena, sizeof(lock_arena)});
static ButtonBank             buttons;
static Presets                presets(hw.seed.qspi);
static Settings               settings(hw, &pager);
static CvMatrix               cv_matrix(kNumCvInputs);
static hostlink::Host host(presets, "paradox", "Paradox", "0.2.0", "v0.13.0");

/* ── HostLink Panel Metadata (Jacks & Buttons) ───────────────────────────── */
static Jack j_in_l =
    Jack("J1", "Audio In L", JackSig::AudioIn)
        .Short("IN L")
        .Help("Left external audio input, mono-summed with In R and blended "
              "into the `here?` reverb via Mix.")
        .SeeAlso(p2_mix);

static Jack j_in_r =
    Jack("J2", "Audio In R", JackSig::AudioIn)
        .Short("IN R")
        .Help("Right external audio input, mono-summed with In L and blended "
              "into the `here?` reverb via Mix.")
        .SeeAlso(p2_mix);

static Jack j_cv_a =
    Jack("J3", "A CV", JackSig::CvBi)
        .Short("A")
        .Help("Bipolar ±5 V CV modulating the `who is` Amplitude (`A`) VaR "
              "parameter.")
        .SeeAlso(p1_a_var);

static Jack j_cv_p =
    Jack("J4", "P CV", JackSig::CvBi)
        .Short("P")
        .Help("Bipolar ±5 V CV modulating the `who is` Pitch (`P`) VaR "
              "parameter.")
        .SeeAlso(p1_p_var);

static Jack j_cv_rm =
    Jack("J5", ">RM CV", JackSig::CvBi)
        .Short(">RM")
        .Help("Bipolar ±5 V CV modulating the `who is` Ring Mod (`>RM`) VaR "
              "parameter.")
        .SeeAlso(p1_rm_var);

static Jack j_cv_fm =
    Jack("J6", ">FM CV", JackSig::CvBi)
        .Short(">FM")
        .Help("Bipolar ±5 V CV modulating the `who is` FM Index (`>FM`) VaR "
              "parameter.")
        .SeeAlso(p1_fm_var);

static Jack j_cv_m =
    Jack("J7", "M> CV", JackSig::CvBi)
        .Short("M>")
        .Help("Bipolar ±5 V CV modulating the `who is` Mod Bus Send (`M>`) "
              "VaR parameter.")
        .SeeAlso(p1_m_var);

static Jack j_cv_del =
    Jack("J8", "DEL CV", JackSig::CvBi)
        .Short("DEL")
        .Help("Bipolar ±5 V CV modulating the `who is` Feedback Delay (`DEL`) "
              "VaR parameter.")
        .SeeAlso(p1_del_var);

static Jack j_out_l =
    Jack("J9", "Audio Out L", JackSig::AudioOut)
        .Short("OUT L")
        .Help("Left stereo output from the `here?` cross-coupled comb reverb "
              "and per-voice AGC leveler.");

static Jack j_out_r =
    Jack("J10", "Audio Out R", JackSig::AudioOut)
        .Short("OUT R")
        .Help("Right stereo output from the `here?` cross-coupled comb reverb "
              "and per-voice AGC leveler.");

static VirtualButton host_btn_b1 =
    VirtualButton("b1", "Page / Lock")
        .Role(VirtualButton::Role::Modal)
        .Action("tap", "Cycle who is / here?")
        .Action("hold+knob", "Record / Clear Param Lock")
        .Help("Cycles between Page 1 (`who is`) and Page 2 (`here?`). Hold "
              "while moving a knob to record or clear a looping parameter "
              "lock.");

static VirtualButton host_btn_b3 =
    VirtualButton("b3", "VaR / RnD")
        .Role(VirtualButton::Role::Modal)
        .Action("tap", "Toggle VaR / RnD layer")
        .Action("hold+b2", "Open Settings")
        .Help("Latches Page 1 (`who is`) between the `VaR` comet layer and "
              "the `RnD` bipolar spread & seed-reroll layer. Inactive on "
              "Page 2 (`here?`).");

/* ── Custom Page 1 ("who is") VaR / RnD Compositional Ring Renderer ──────── */
namespace {

inline bool TimeBefore(uint32_t now, uint32_t target) {
    return static_cast<int32_t>(target - now) > 0;
}

inline bool TimeReached(uint32_t now, uint32_t target) {
    return static_cast<int32_t>(now - target) >= 0;
}

struct Deadline {
    uint32_t until_ms = 0u;
    bool     active   = false;

    void Arm(uint32_t now, uint32_t dur_ms) {
        until_ms = now + dur_ms;
        active   = true;
    }

    void Clear() { active = false; }

    bool Active(uint32_t now) {
        if (active && TimeReached(now, until_ms))
            active = false;
        return active;
    }

    uint32_t Remaining(uint32_t now) const {
        return (active && TimeBefore(now, until_ms)) ? (until_ms - now) : 0u;
    }
};

struct GhostTracker {
    float    last_phys     = -1.0f;
    uint8_t  last_page     = 0xFFu;
    Deadline show_deadline = {};
    bool     was_caught    = true;
};

constexpr uint32_t kReseedBurstMs = 160u;
constexpr uint32_t kGhostHoldMs   = 450u;
constexpr uint32_t kGhostFadeMs   = 180u;
constexpr float    kGhostMoveEps  = 0.012f;

Deadline     reseed_deadline[paradox_dsp::kNumChannels] = {};
uint32_t     reseed_epoch[paradox_dsp::kNumChannels]    = {};
GhostTracker ghost_state[kNumPots]                      = {};

const VirtualKnob *const kWhoVarKnobs[paradox_dsp::kNumChannels] = {
    &p1_a_var, &p1_p_var, &p1_rm_var, &p1_fm_var, &p1_m_var, &p1_del_var,
};

const VirtualKnob *const kWhoRndKnobs[paradox_dsp::kNumChannels] = {
    &p1_a_rnd, &p1_p_rnd, &p1_rm_rnd, &p1_fm_rnd, &p1_m_rnd, &p1_del_rnd,
};

} // namespace

static void RenderWhoRing(LedPanel &panel, uint8_t pot, const ArcGeometry &geo,
                          float /*norm*/, uint32_t t_ms, void *ctx) {
    if (!ctx || geo.arc_leds == 0u)
        return;

    const uint8_t ch = static_cast<const WhoRingCtx *>(ctx)->ch;
    if (ch >= paradox_dsp::kNumChannels)
        return;

    const uint8_t active_page = pager.ActivePage();
    const bool    is_rnd_page = (active_page == kPageRnd);

    const float var_norm = kWhoVarKnobs[ch]->Norm();
    const float rnd_norm = kWhoRndKnobs[ch]->Norm();
    const float rnd_val  = kWhoRndKnobs[ch]->Value();

    /* Subtle active-layer emphasis while keeping both layers legible. */
    const float var_gain      = is_rnd_page ? 0.78f : 1.00f;
    const float rnd_gain      = is_rnd_page ? 0.78f : 0.42f;
    const float scatter_gain  = is_rnd_page ? 0.45f : 0.30f;
    const float shimmer_depth = (is_rnd_page ? 0.42f : 0.30f) * rnd_norm;

    RingFrame f;
    f.Begin(geo);

    const uint8_t n         = f.NumLeds();
    const float   period    = static_cast<float>(n);
    const float   center_f  = var_norm * static_cast<float>(n - 1u);
    const float   half_span = 0.5f * rnd_norm * period;

    /* 1. Wrapped bipolar RnD spread + 8-voice hash scatter + FieldShimmer. */
    if (rnd_norm > 0.004f) {
        float voice_dens[RingFrame::kMaxLeds] = {};
        if (const float *hashes = paradox_dsp::GetChannelVoiceHashes(ch)) {
            for (size_t v = 0; v < paradox_dsp::kNumVoices; ++v) {
                const float offset_leds =
                    ((hashes[v] - 0.5f * rnd_val) / 127.0f) * period;
                float pos_v = center_f + offset_leds;
                pos_v -= period * std::floor(pos_v / period);
                const uint8_t k0   = static_cast<uint8_t>(pos_v) % n;
                const uint8_t k1   = static_cast<uint8_t>((k0 + 1u) % n);
                const float   frac = pos_v - std::floor(pos_v);
                voice_dens[k0] += (1.0f - frac);
                voice_dens[k1] += frac;
            }
        }

        const FieldDesc shimmer_desc = FieldShimmer(75u);
        const float     reach        = half_span + 0.5f;

        for (uint8_t k = 0u; k < n; ++k) {
            float diff = static_cast<float>(k) - center_f;
            diff -= period * std::round(diff / period);
            const float dist = std::fabs(diff);

            if (dist < reach) {
                const float edge_aa = std::min(1.0f, reach - dist);
                const float taper   = 1.0f - 0.55f * (dist / reach);
                f.Add(k, paradox_palette::kKnobRnd, edge_aa * taper * rnd_gain);

                const float cluster = std::min(1.0f, voice_dens[k] * 0.45f);
                if (cluster > 0.0f) {
                    f.Add(k, paradox_palette::kVoiceScatter,
                          edge_aa * cluster * scatter_gain);
                }

                const float m =
                    FieldEval(shimmer_desc, static_cast<uint32_t>(k) + ch * 16u,
                              t_ms, -1.0f);
                f.Dim(k, 1.0f - shimmer_depth * m);
            }
        }
    }

    /* 2. Re-seed sparkle burst (~160 ms) when turning RnD rolls new seeds. */
    if (paradox_dsp::ConsumeChannelReseed(ch)) {
        reseed_deadline[ch].Arm(t_ms, kReseedBurstMs);
        ++reseed_epoch[ch];
    }
    if (reseed_deadline[ch].Active(t_ms)) {
        const float env =
            static_cast<float>(reseed_deadline[ch].Remaining(t_ms)) /
            static_cast<float>(kReseedBurstMs);
        const uint32_t bucket = reseed_epoch[ch] * 8u + (t_ms / 32u);
        for (uint8_t k = 0u; k < n; ++k) {
            const float h = Hash01(static_cast<uint32_t>(k) + ch * 19u, bucket);
            if (h > 0.52f) {
                const float intensity = ((h - 0.52f) / 0.48f) * env;
                f.Add(k, paradox_palette::kReseedSpark, intensity);
            }
        }
    }

    /* 3. Foreground VaR comet: smooth sub-LED white-hot Phosphor Lime core +
     *    symmetric 1-step Phosphor Lime tails. */
    for (uint8_t k = 0u; k < n; ++k) {
        const float d = std::fabs(static_cast<float>(k) - center_f);
        if (d < 1.0f) {
            const float core = 1.0f - d;
            const float halo = 0.22f * d;
            f.Dim(k, 1.0f - 0.85f * core);

            const LedPanel::Rgb c = LedPanel::Mix(
                paradox_palette::kKnobVar, paradox_palette::kCometCore, core);
            f.Add(k, c, core * var_gain);
            if (halo > 0.0f)
                f.Add(k, paradox_palette::kKnobVar, halo * var_gain);
        } else if (d < 2.0f) {
            const float halo = 0.22f * (2.0f - d);
            f.Add(k, paradox_palette::kKnobVar, halo * var_gain);
        }
    }

    /* 4. Custom pot-catch UX:
     *    - Caught: illuminate 6 o'clock BottomPip in active page color.
     *    - Uncaught: BottomPip off; show a dim turning ghost pip at physical
     *      pot position only while the user turns the pot until caught. */
    if (pot < kNumPots) {
        const PotState ps   = pager.State(active_page, pot);
        const float    phys = loop.Phys()[pot];
        GhostTracker  &gt   = ghost_state[pot];

        if (gt.last_page != active_page || gt.last_phys < 0.0f ||
            (gt.was_caught && !ps.caught)) {
            gt.last_phys = phys;
            gt.show_deadline.Clear();
        } else if (!ps.caught) {
            if (std::fabs(phys - gt.last_phys) > kGhostMoveEps) {
                gt.last_phys = phys;
                gt.show_deadline.Arm(t_ms, kGhostHoldMs);
            }
        } else {
            gt.last_phys = phys;
            gt.show_deadline.Clear();
        }
        gt.last_page  = active_page;
        gt.was_caught = ps.caught;

        if (ps.caught) {
            PipDesc bottom_desc;
            bottom_desc.color = is_rnd_page
                                    ? paradox_palette::kPageRndIndicator
                                    : paradox_palette::kPageVarIndicator;
            f.Pip(Region::BottomPip, bottom_desc, 0.0f, 1.0f);
        } else if (gt.show_deadline.Active(t_ms)) {
            const float fade = std::min(
                1.0f, static_cast<float>(gt.show_deadline.Remaining(t_ms)) /
                          static_cast<float>(kGhostFadeMs));
            PipDesc ghost_desc;
            ghost_desc.color   = paradox_palette::kGhostPip;
            ghost_desc.smooth  = true;
            ghost_desc.compose = PipCompose::Add;
            f.Pip(Region::Full, ghost_desc, phys, 0.45f * fade);
        }
    }

    /* 2-arg Emit skips the SDK's default white arc catch pip. */
    f.Emit(panel, pot);
}

/* Page 2 ("here?") overdraw: keep the stock SDK arc and white catch pip while
 * illuminating the 6 o'clock BottomPip in the page color whenever the pot is
 * caught (active), matching Page 1's bottom-pip catch indicator. */
static void RenderReverbCatchPip(LedPanel &panel, uint8_t pot,
                                 const ArcGeometry &geo, float /*norm*/,
                                 uint32_t /*t_ms*/, void * /*ctx*/) {
    if (pot >= kNumPots || geo.arc_leds == 0u)
        return;

    const PotState ps = pager.State(kPageReverb, pot);
    if (!ps.caught)
        return;

    RingFrame f;
    f.BeginOverlay(geo);
    PipDesc bottom_desc;
    bottom_desc.color = paradox_palette::kPageReverbIndicator;
    f.Pip(Region::BottomPip, bottom_desc, 0.0f, 1.0f);
    f.Emit(panel, pot);
}

/* ── 16-Slot Preset Management & B2 Tap/Hold Overlay ─────────────────────── */
class WhoSeeds : public Serializable {
  public:
    size_t SerializedSize() const override {
        return paradox_dsp::kNumChannels * 2u;
    }

    void Serialize(uint8_t *out) const override { paradox_dsp::GetSeeds(out); }

    bool Deserialize(const uint8_t *in) override {
        paradox_dsp::SetSeeds(in);
        return true;
    }

    uint32_t SchemaHash() const override { return 0x57534431u; /* 'WSD1' */ }
};

static WhoSeeds who_seeds;

static constexpr uint8_t  kNumPresetSlots      = Presets::kNumSlots; /* 16 */
static constexpr uint32_t kB2SaveHoldMs        = 1500u;
static constexpr uint32_t kB2HoldVisualStartMs = 250u;
static constexpr uint32_t kSlotOverlayMs       = 1000u;
static constexpr uint32_t kSaveConfirmFlashMs  = 260u;
static constexpr uint32_t kPersistSlotDelayMs  = 1500u;

/* Dedicated 2-sector ping-pong QSPI store (8 KiB) right below factory
 * calibration (0x9075F000) to remember the active preset slot across power
 * cycles without touching calibration or the 16 user preset slots. */
static constexpr uintptr_t kLastSlotFlashBase =
    kV2CalQspiAddr - 2u * kPresetSectorSize; /* 0x9075D000..0x9075EFFF */

static constexpr uint32_t kLastSlotTag = 0x50445853u; /* 'PDXS' */

struct LastSlotRecord {
    uint32_t tag;
    uint8_t  slot;
    uint8_t  pad[3];
};

static PresetStore<LastSlotRecord, 1u> last_slot_store;
static PresetGestureUi                *settings_preset_ui   = nullptr;
static bool                            prev_settings_commit = false;

static_assert(paradox_dsp::kNumFactorySnapshots == kNumPresetSlots,
              "Factory snapshot count must match QSPI preset slot count");

static uint8_t  current_preset_slot   = 0u;
static uint8_t  persisted_preset_slot = 0u;
static uint32_t persist_slot_after_ms = 0u;
static int8_t   pending_load_slot     = -1;
static bool     pending_save_slot     = false;

static bool     b2_prev_pressed   = false;
static bool     b2_chord_poisoned = false;
static uint32_t b2_press_start_ms = 0u;

static Deadline snapshot_flash_deadline  = {};
static Deadline slot_overlay_deadline    = {};
static Deadline slot_save_flash_deadline = {};
static bool     slot_overlay_was_save    = false;

static void PersistCurrentSlotNow() {
    LastSlotRecord rec = {kLastSlotTag, current_preset_slot, {0u, 0u, 0u}};
    if (last_slot_store.Save(0u, rec))
        persisted_preset_slot = current_preset_slot;
}

static inline float NormRange(float v, float lo, float hi) {
    return std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
}

static void ApplyFactorySnapshot(uint8_t snap_idx, const float *phys) {
    if (snap_idx >= paradox_dsp::kNumFactorySnapshots)
        snap_idx = 0;

    const auto &s = paradox_dsp::kFactorySnapshots[snap_idx];
    paradox_dsp::ApplySnapshotSeeds(s);

    // Pages 0 & 1: "who is" VaR & RnD (0..127 -> normalized 0..1)
    const paradox_dsp::ChannelSnap *chs[paradox_dsp::kNumChannels] = {
        &s.A, &s.P, &s.RM, &s.FM, &s.M, &s.DEL};
    for (uint8_t ch = 0u; ch < paradox_dsp::kNumChannels; ++ch) {
        pager.SetStored(kPageVar, kWhoVarKnobs[ch]->Pot(),
                        NormRange(chs[ch]->var, 0.f, 127.f), phys);
        pager.SetStored(kPageRnd, kWhoRndKnobs[ch]->Pot(),
                        NormRange(chs[ch]->rnd, 0.f, 127.f), phys);
    }

    // Page 2: "here?" Reverb (keep current Mix setting so cycling presets
    // doesn't override external audio blend)
    pager.SetStored(kPageReverb, kPotTopLeft, NormRange(s.size, 0.f, 4.f),
                    phys);
    pager.SetStored(kPageReverb, kPotTopRight, NormRange(s.asym, -4.f, 4.f),
                    phys);
    pager.SetStored(kPageReverb, kPotMiddleLeft, NormRange(s.decay, 0.f, 100.f),
                    phys);
    pager.SetStored(kPageReverb, kPotBottomLeft, NormRange(s.hp, 0.f, 127.f),
                    phys);
    pager.SetStored(kPageReverb, kPotBottomRight, NormRange(s.lp, 0.f, 130.f),
                    phys);
}

static void DrawSlotSegment(uint8_t slot, LedPanel::Rgb color) {
    for (uint8_t p = 0u; p < kNumPots; ++p)
        hw.leds.ClearRing(p);

    const uint8_t target_pot = static_cast<uint8_t>(slot / 3u);
    const uint8_t target_seg = static_cast<uint8_t>(slot % 3u);
    if (target_pot >= kNumPots)
        return;

    RingFrame f;
    f.Begin(hw.Arc());

    SelectorDesc sel;
    sel.zone_geo       = ZoneGeometry::Region;
    sel.num_zones      = (target_pot < 5u) ? 3u : 1u;
    sel.active_color   = color;
    sel.inactive_color = {0u, 0u, 0u};
    f.BaseZone(sel, (target_pot < 5u) ? target_seg : 0u);
    f.Emit(hw.leds, target_pot);
}

static void PollB2Chord(uint32_t t_ms) {
    const bool b2 = hw.buttons[kButtonB2].Pressed();
    const bool b3 = hw.buttons[kButtonB3].Pressed();

    if (b2 && !b2_prev_pressed) {
        b2_press_start_ms = t_ms;
        b2_chord_poisoned = b3 || settings.IsActive();
    }
    if (b2 && (b3 || settings.IsActive())) {
        b2_chord_poisoned = true;
    }
    b2_prev_pressed = b2;
}

static void OnNextPreset(void * /*ctx*/) {
    /* Guard against B2+B3 chord attempts entering Settings. */
    if (b2_chord_poisoned || hw.buttons[kButtonB3].Pressed() ||
        settings.IsActive())
        return;

    pending_load_slot =
        static_cast<int8_t>((current_preset_slot + 1u) % kNumPresetSlots);
}

static void OnSavePreset(void * /*ctx*/) {
    /* Guard against B2+B3 chord attempts entering Settings. */
    if (b2_chord_poisoned || hw.buttons[kButtonB3].Pressed() ||
        settings.IsActive())
        return;

    pending_save_slot = true;
}

static VirtualButton preset_button =
    VirtualButton(kButtonB2, "Preset")
        .Ident("b2")
        .Role(VirtualButton::Role::Modal)
        .Tap(OnNextPreset, "Next Preset (1-16)")
        .Hold(kB2SaveHoldMs, OnSavePreset, "Overwrite Current Preset Slot")
        .Action("hold+b3", "Open Settings")
        .Help("Tap to cycle through the 16 preset slots (with a 1 s K1-K6 "
              "segment overlay). Hold for 1.5 s to overwrite the current slot "
              "in flash. Hold together with B3 to open Settings.");

static void UpdateParams() {
    const uint32_t now = daisy::System::GetNow();

    /* Execute deferred B2 preset load/save on the main control frame. */
    if (pending_load_slot >= 0) {
        const uint8_t slot = static_cast<uint8_t>(pending_load_slot);
        pending_load_slot  = -1;

        if (!presets.Load(slot))
            ApplyFactorySnapshot(slot, loop.Phys());
        pager.LockPage(pager.ActivePage(), loop.Phys());

        current_preset_slot   = slot;
        persist_slot_after_ms = now + kPersistSlotDelayMs;
        slot_overlay_was_save = false;
        slot_overlay_deadline.Arm(now, kSlotOverlayMs);
        snapshot_flash_deadline.Arm(now, 180u);
    } else if (pending_save_slot) {
        pending_save_slot = false;

        presets.Save(current_preset_slot);
        if (current_preset_slot != persisted_preset_slot)
            PersistCurrentSlotNow();

        slot_overlay_was_save = true;
        slot_save_flash_deadline.Arm(now, kSaveConfirmFlashMs);
        slot_overlay_deadline.Arm(now, kSlotOverlayMs);
        snapshot_flash_deadline.Arm(now, 300u);
    }

    /* Debounced persistence of the active slot after browsing settles. */
    if (current_preset_slot != persisted_preset_slot &&
        TimeReached(now, persist_slot_after_ms)) {
        PersistCurrentSlotNow();
    }

    /* Only allow a channel's RnD seeds to reroll when we are actively on the
     * RnD page and that pot is both caught and awake from genuine user motion
     * (InitCatch sets awake = false on page switch / preset load even when a
     * pot snaps within ±kCatchTolerance). */
    uint8_t allow_reseed_mask = 0u;
    if (pager.ActivePage() == kPageRnd) {
        for (uint8_t ch = 0u; ch < paradox_dsp::kNumChannels; ++ch) {
            const PotState ps = pager.State(kPageRnd, kWhoRndKnobs[ch]->Pot());
            if (ps.caught && ps.awake)
                allow_reseed_mask |= static_cast<uint8_t>(1u << ch);
        }
    }

    paradox_dsp::SetToneParams(
        {
            p1_a_var.Value(),
            p1_p_var.Value(),
            p1_rm_var.Value(),
            p1_fm_var.Value(),
            p1_m_var.Value(),
            p1_del_var.Value(),
            p1_a_rnd.Value(),
            p1_p_rnd.Value(),
            p1_rm_rnd.Value(),
            p1_fm_rnd.Value(),
            p1_m_rnd.Value(),
            p1_del_rnd.Value(),
        },
        allow_reseed_mask);

    paradox_dsp::SetReverbParams({
        p2_size.Value(),
        p2_asym.Value(),
        p2_decay.Value(),
        p2_hp.Value(),
        p2_lp.Value(),
        p2_mix.Value(),
    });

    paradox_dsp::UpdateControlRate();
}

static void RenderPresetUi(uint32_t t_ms) {
    /* Keep current_preset_slot synced if the user loads/saves a slot from
     * the Settings PresetGestureUi screen. */
    if (settings_preset_ui) {
        const auto &gs = settings_preset_ui->GestureState();
        const bool  committed =
            (gs.phase == PresetGesturePhase::FlashConfirm) && gs.last_ok;
        if (committed && !prev_settings_commit && gs.slot < kNumPresetSlots) {
            current_preset_slot   = gs.slot;
            persist_slot_after_ms = t_ms + 250u;
        }
        prev_settings_commit = committed;
    }

    if (settings.IsActive())
        return;

    /* 1. B2 hold charge-up preview (before 1.5 s save commit). */
    const bool b2_held = hw.buttons[kButtonB2].Pressed() &&
                         !b2_chord_poisoned && !hw.buttons[kButtonB3].Pressed();
    if (b2_held && !slot_overlay_was_save &&
        TimeReached(t_ms, b2_press_start_ms)) {
        const uint32_t held = t_ms - b2_press_start_ms;
        if (held >= kB2HoldVisualStartMs && held < kB2SaveHoldMs) {
            const float prog = std::clamp(
                static_cast<float>(held - kB2HoldVisualStartMs) /
                    static_cast<float>(kB2SaveHoldMs - kB2HoldVisualStartMs),
                0.0f, 1.0f);
            const LedPanel::Rgb seg_c = LedPanel::Scale(
                LedPanel::Mix(paradox_palette::kSnapshotFlash,
                              paradox_palette::kPresetSaveHold, prog),
                0.40f + 0.60f * prog);
            DrawSlotSegment(current_preset_slot, seg_c);
            hw.leds.SetButtonPair(
                kButtonB2,
                LedPanel::Mix(paradox_palette::kSnapshotIdle,
                              paradox_palette::kPresetSaveHold, prog));
            return;
        }
    }

    /* 2. 1-second segmented slot overlay on K1-K6 after B2 load or save. */
    if (slot_overlay_deadline.Active(t_ms)) {
        const LedPanel::Rgb seg_c =
            (slot_overlay_was_save && slot_save_flash_deadline.Active(t_ms))
                ? paradox_palette::kPresetSaveDone
                : paradox_palette::kSnapshotFlash;
        DrawSlotSegment(current_preset_slot, seg_c);
    } else {
        slot_overlay_was_save = false;
    }

    /* 3. B2 button indicator LED. */
    const LedPanel::Rgb b2_c =
        snapshot_flash_deadline.Active(t_ms)
            ? (slot_overlay_was_save ? paradox_palette::kPresetSaveDone
                                     : paradox_palette::kSnapshotFlash)
            : paradox_palette::kSnapshotIdle;
    hw.leds.SetButtonPair(kButtonB2, b2_c);
}

int main() {
    hw.Init();
    locks.Init();
    paradox_dsp::Init(hw.SampleRate());

    /* Page & Latch indicator colors (painted automatically on B1 & B3):
     *   kPageVar ("who is" VaR)    -> Phosphor Lime
     *   kPageRnd ("who is" RnD)    -> Crimson Orange
     *   kPageReverb ("here?")      -> Electric Violet
     */
    pager.SetPageColor(kPageVar, paradox_palette::kPageVarIndicator);
    pager.SetPageColor(kPageRnd, paradox_palette::kPageRndIndicator);
    pager.SetPageColor(kPageReverb, paradox_palette::kPageReverbIndicator);

    /* Register B2 globally across all pages for 16-slot preset cycle / save. */
    buttons.Global(preset_button);

    /* HostLink panel metadata & fast mirror/edit path for
     * hermeticmodular.com/program. */
    host.Jacks(j_in_l, j_in_r, j_cv_a, j_cv_p, j_cv_rm, j_cv_fm, j_cv_m,
               j_cv_del, j_out_l, j_out_r)
        .Buttons(host_btn_b1, preset_button, host_btn_b3)
        .Fast(pager, settings, who_seeds);

    /* Attach all surfaces and pages to ControlLoop before BootLoad(). */
    loop.Use(pager)
        .Use(locks)
        .Use(buttons)
        .Use(settings)
        .Use(cv_matrix)
        .Use(host)
        .Use(page_var)
        .Use(page_rnd)
        .Use(page_reverb)
        .OnPoll(PollB2Chord)
        .OnFrame(UpdateParams)
        .OnRender(RenderPresetUi);

    /* Read physical pot positions for initial catch arming. */
    float init_phys[kNumPots] = {};
    for (uint8_t p = 0; p < kNumPots; ++p)
        init_phys[p] = hw.pots[p].Value();

    /* CV Routing: 6 CV jacks modulate the 6 "who is" VaR parameters. */
    cv_matrix.Jack(0).To(p1_a_var);
    cv_matrix.Jack(1).To(p1_p_var);
    cv_matrix.Jack(2).To(p1_rm_var);
    cv_matrix.Jack(3).To(p1_fm_var);
    cv_matrix.Jack(4).To(p1_m_var);
    cv_matrix.Jack(5).To(p1_del_var);

    /* Settings UI opt-in (keeps standard Settings preset gesture alongside B2).
     */
    settings.UseBrightness();
    settings_preset_ui = &settings.UsePresets(presets);

    /* Presets management: knobs + locks + settings + (bs1, bs2) seeds + names.
     */
    presets.Manage(pager);
    presets.Manage(locks);
    presets.Manage(settings);
    presets.Manage(who_seeds);
    PresetName &preset_name = presets.UseNames();
    presets.Init();

    /* Initialize dedicated 2-sector ping-pong store for last active slot. */
    last_slot_store.Init(PresetFlashOps(&hw.seed.qspi), kLastSlotFlashBase,
                         kPresetSectorSize, 1u);

    /* Option A: Seed any empty or schema-mismatched QSPI slots with the 16
     * default factory presets so all 16 appear named in HostLink immediately.
     */
    pager.SetStored(kPageReverb, kPotMiddleRight, 0.0f, init_phys);
    for (uint8_t slot = 0u; slot < kNumPresetSlots; ++slot) {
        if (!presets.HasValid(slot)) {
            ApplyFactorySnapshot(slot, init_phys);
            preset_name.Set(paradox_dsp::kFactorySnapshots[slot].name);

            hw.leds.Clear();
            DrawSlotSegment(slot, paradox_palette::kSnapshotFlash);
            hw.leds.Show();

            presets.Save(slot);
        }
    }

    /* Seed Slot 0 factory defaults before BootLoad() so HostLink captures
     * Slot 0 as the factory default state, then restore the last active slot
     * from before power-off (defaulting to Slot 0 on first boot). */
    ApplyFactorySnapshot(0u, init_phys);
    preset_name.Set(paradox_dsp::kFactorySnapshots[0].name);
    presets.BootLoad();

    uint8_t        boot_slot = 0u;
    LastSlotRecord rec       = {};
    if (last_slot_store.Load(0u, rec) && rec.tag == kLastSlotTag &&
        rec.slot < kNumPresetSlots) {
        boot_slot = rec.slot;
    }
    if (boot_slot != 0u && presets.HasValid(boot_slot)) {
        presets.Load(boot_slot);
    }
    host.BootSlot(boot_slot);
    pager.LockPage(pager.ActivePage(), init_phys);
    current_preset_slot   = boot_slot;
    persisted_preset_slot = boot_slot;

    /* Run one control frame before starting audio so ControlLoop wires every
     * VirtualKnob to pager storage and pushes the restored boot_slot into the
     * DSP, then snap delay/mix smoothers immediately to avoid a boot transient.
     */
    loop.Tick();
    paradox_dsp::UpdateControlRate(true);
    hw.StartAudio(paradox_dsp::Process);

    for (;;)
        loop.Tick();
}
