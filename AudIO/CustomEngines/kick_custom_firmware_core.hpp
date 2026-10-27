/**
 * @file kick_custom_firmware_core.hpp
 * @brief Phase 1 Custom Firmware Core for Behringer KICK (BE 0700-AAM)
 * @license AGPL-3.0-or-later (compatible with MusicTribeOpenSource/0700-AAM)
 *
 * Extends the stock `AudIO/` (DSP) and `Kick/` (Sequencer & Pad Controller) layers:
 *   1. AudIO::FM4Voice         - 4-Operator, 8-Algorithm FM synth with Sample-FM injection
 *                                and 4-Knob hardware bank mapping.
 *   2. AudIO::GranularVoice    - Zero-copy Sample-Coupled Granular synth sharing sampler/chop
 *                                buffers with window morphing, spray, and Touch Strip scrub.
 *   3. Kick::ChordScaleEngine  - 16-Pad Scale & Chord Progression engine (Pads 1-12 diatonic &
 *                                borrowed chords; Pads 13-16 live 7th/9th, Sus, Inversion, Strum).
 *   4. Kick::Arpeggiator       - 4-Knob multi-mode arpeggiator feeding live output or sequencer.
 *   5. Kick::StepSequencerTrack- 64-step polyphonic sequencer with 4-Knob step editing (Pitch,
 *                                Velocity, Length, Microtiming/Ratchet), P-Locks, and live
 *                                960 PPQN pad recording with Input Quantize.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace BehringerKickCustom {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPi    = 3.14159265358979323846f;
constexpr int   kPPQN  = 960; // High-resolution ticks per quarter note for live pad capture

// Helper to convert MIDI note number (0-127) + fine cents to frequency in Hz
inline float midiToFreqHz(float midiNote) {
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
}

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

// ============================================================================
// SECTION 1: SHARED SAMPLE & SLICE BUFFER VIEW (Zero-Copy AudIO Integration)
// ============================================================================
namespace AudIO {

struct SampleSliceView {
    const float* data         = nullptr; // Pointer to mono or interleaved stereo PCM in AudIO RAM
    size_t       numFrames    = 0;
    uint32_t     sampleRate   = 48000;
    size_t       sliceStart   = 0;       // Start frame of chop/slice
    size_t       sliceEnd     = 0;       // End frame of chop/slice
    bool         loopEnabled  = false;
    float        loopCrossfade = 0.05f;

    size_t length() const {
        if (!data || sliceEnd <= sliceStart || sliceEnd > numFrames) return 0;
        return sliceEnd - sliceStart;
    }

    // Linear-interpolated read within the active slice bounds
    float readInterpolated(float normalizedPos) const {
        size_t len = length();
        if (len < 2) return 0.0f;
        normalizedPos = clampf(normalizedPos, 0.0f, 0.999999f);
        float exactFrame = static_cast<float>(sliceStart) + normalizedPos * static_cast<float>(len - 1);
        size_t idx0 = static_cast<size_t>(exactFrame);
        size_t idx1 = std::min(idx0 + 1, sliceEnd - 1);
        float frac = exactFrame - static_cast<float>(idx0);
        return data[idx0] * (1.0f - frac) + data[idx1] * frac;
    }
};

// ============================================================================
// SECTION 2: 4-OPERATOR FM SYNTHESIS ENGINE (AudIO::FM4Voice)
// ============================================================================

enum class FMAlgorithm : uint8_t {
    Alg1_SerialStack4321 = 0, // 4 -> 3 -> 2 -> 1 (Aggressive bass / metallic)
    Alg2_Branched34To2To1,    // (3 + 4) -> 2 -> 1 (Woody marimba / slap bass)
    Alg3_Stack431_Plus_2To1,  // (4 -> 3 -> 1) + (2 -> 1) (E-Piano tine + body)
    Alg4_ThreeModsTo1,        // (2 + 3 + 4) -> 1 (Bells / chimes)
    Alg5_DualTwoOpStacks,     // (4 -> 3) + (2 -> 1) (Classic dual-layer FM)
    Alg6_OneModThreeCarriers, // 4 -> (1 + 2 + 3) (Lush FM strings / organ)
    Alg7_TwoOpPlusTwoAdd,     // (4 -> 3) + 2 + 1 (Hybrid pad + sub foundation)
    Alg8_FourParallelCarriers // 1 + 2 + 3 + 4 (Additive / 4-osc VA + Sample-FM)
};

enum class FMKnobBank : uint8_t {
    Bank1_RatiosAndTuning = 0,
    Bank2_LevelsAndFeedback,
    Bank3_WaveformsAndEnvelopes,
    Bank4_FilterAndDrive
};

struct FMOperatorParams {
    float ratio      = 1.0f;  // 0.25x to 16.0x
    float fineDetune = 0.0f;  // -50 to +50 cents
    float level      = 0.8f;  // 0.0 to 1.0 (Modulation index for modulators, gain for carriers)
    float attackSec  = 0.005f;
    float decaySec   = 0.45f;
    float sustainLvl = 0.5f;
    float releaseSec = 0.25f;
};

struct FM4Patch {
    std::string name                      = "Init Digitone Pluck";
    FMAlgorithm algorithm                 = FMAlgorithm::Alg5_DualTwoOpStacks;
    std::array<FMOperatorParams, 4> ops   = {{
        {1.0f,  0.0f, 0.90f, 0.003f, 0.60f, 0.70f, 0.30f}, // Op 1 (Primary Carrier)
        {2.0f,  2.0f, 0.45f, 0.002f, 0.25f, 0.10f, 0.15f}, // Op 2 (Modulator)
        {1.0f, -2.0f, 0.70f, 0.005f, 0.80f, 0.60f, 0.35f}, // Op 3 (Secondary Carrier/Mod)
        {3.0f,  0.0f, 0.35f, 0.001f, 0.18f, 0.00f, 0.10f}  // Op 4 (Top Modulator w/ Feedback)
    }};
    float op4Feedback     = 0.20f; // 0.0 to 1.0 self-phase feedback on Operator 4
    float sampleFMDepth   = 0.00f; // 0.0 to 1.0 phase modulation from loaded sample slice
    float waveMorph       = 0.00f; // 0.0 = Sine, 0.33 = Triangle, 0.66 = Saw-Harmonics, 1.0 = Wavefolded
    float filterCutoffHz  = 8500.0f;
    float filterResonance = 0.20f;
    float drive           = 0.10f;
};

class FM4Voice {
public:
    void setSampleRate(float sr) { sampleRate_ = sr; }

    void noteOn(uint8_t midiNote, uint8_t velocity, const FM4Patch& patch) {
        active_       = true;
        gateOpen_     = true;
        midiNote_     = midiNote;
        velocityGain_ = static_cast<float>(velocity) / 127.0f;
        baseFreqHz_   = midiToFreqHz(static_cast<float>(midiNote));
        sampleCursor_ = 0.0f;
        op4Prev_      = 0.0f;
        for (int i = 0; i < 4; ++i) {
            phases_[i]   = 0.0f;
            envStage_[i] = EnvStage::Attack;
            envLevel_[i] = (patch.ops[i].attackSec <= 0.001f) ? 1.0f : 0.0f;
        }
    }

    void noteOff() {
        gateOpen_ = false;
        for (int i = 0; i < 4; ++i) {
            if (envStage_[i] != EnvStage::Idle) {
                envStage_[i] = EnvStage::Release;
            }
        }
    }

    // 4-Knob & Jog Wheel direct hardware handler (Zero menu diving)
    static void applyHardwareKnob(FM4Patch& patch, FMKnobBank bank, int knobIndex, float normalizedDelta, bool pushHeld = false) {
        knobIndex = std::max(0, std::min(3, knobIndex));
        switch (bank) {
            case FMKnobBank::Bank1_RatiosAndTuning: {
                auto& op = patch.ops[knobIndex];
                if (pushHeld) {
                    op.fineDetune = clampf(op.fineDetune + normalizedDelta * 50.0f, -50.0f, 50.0f);
                } else {
                    // Snap or step in musical 0.5x increments
                    float stepped = std::round((op.ratio + normalizedDelta * 8.0f) * 2.0f) * 0.5f;
                    op.ratio = clampf(stepped, 0.25f, 16.0f);
                }
                break;
            }
            case FMKnobBank::Bank2_LevelsAndFeedback: {
                auto& op = patch.ops[knobIndex];
                op.level = clampf(op.level + normalizedDelta, 0.0f, 1.0f);
                break;
            }
            case FMKnobBank::Bank3_WaveformsAndEnvelopes: {
                // Knob 1: Mod Attack, Knob 2: Mod Decay, Knob 3: Carrier Attack, Knob 4: Carrier Release
                if (knobIndex == 0) {
                    for (int i : {1, 3}) patch.ops[i].attackSec = clampf(patch.ops[i].attackSec + normalizedDelta * 1.5f, 0.001f, 3.0f);
                } else if (knobIndex == 1) {
                    for (int i : {1, 3}) patch.ops[i].decaySec = clampf(patch.ops[i].decaySec + normalizedDelta * 2.0f, 0.01f, 4.0f);
                } else if (knobIndex == 2) {
                    for (int i : {0, 2}) patch.ops[i].attackSec = clampf(patch.ops[i].attackSec + normalizedDelta * 1.5f, 0.001f, 3.0f);
                } else {
                    for (int i : {0, 2}) patch.ops[i].releaseSec = clampf(patch.ops[i].releaseSec + normalizedDelta * 2.5f, 0.01f, 5.0f);
                }
                break;
            }
            case FMKnobBank::Bank4_FilterAndDrive: {
                if (knobIndex == 0) patch.filterCutoffHz = clampf(patch.filterCutoffHz * std::pow(2.0f, normalizedDelta * 4.0f), 60.0f, 18000.0f);
                if (knobIndex == 1) patch.filterResonance = clampf(patch.filterResonance + normalizedDelta, 0.0f, 0.95f);
                if (knobIndex == 2) patch.drive = clampf(patch.drive + normalizedDelta, 0.0f, 1.0f);
                if (knobIndex == 3) patch.waveMorph = clampf(patch.waveMorph + normalizedDelta, 0.0f, 1.0f);
                break;
            }
        }
    }

    // Render one mono sample with optional Sample-FM modulator slice
    float renderSample(const FM4Patch& patch, const SampleSliceView* sampleMod = nullptr) {
        if (!active_) return 0.0f;

        // 1. Advance operator envelopes and phases
        std::array<float, 4> envOut{};
        bool anyAlive = false;
        for (int i = 0; i < 4; ++i) {
            envOut[i] = stepEnvelope(i, patch.ops[i]);
            if (envStage_[i] != EnvStage::Idle) anyAlive = true;

            float detuneFactor = std::pow(2.0f, patch.ops[i].fineDetune / 1200.0f);
            float freq = baseFreqHz_ * patch.ops[i].ratio * detuneFactor;
            phases_[i] += (kTwoPi * freq) / sampleRate_;
            if (phases_[i] >= kTwoPi) phases_[i] -= kTwoPi;
        }
        if (!anyAlive) {
            active_ = false;
            return 0.0f;
        }

        // 2. Compute Operator 4 (with self-feedback + optional Sample-FM transient injection)
        float samplePhaseMod = 0.0f;
        if (sampleMod && sampleMod->length() > 0 && patch.sampleFMDepth > 0.001f) {
            samplePhaseMod = sampleMod->readInterpolated(sampleCursor_) * patch.sampleFMDepth * 4.0f;
            sampleCursor_ += 1.0f / static_cast<float>(sampleMod->length());
            if (sampleCursor_ >= 1.0f) sampleCursor_ = sampleMod->loopEnabled ? (sampleCursor_ - 1.0f) : 1.0f;
        }

        float op4Phase = phases_[3] + op4Prev_ * (patch.op4Feedback * 2.5f) + samplePhaseMod;
        float op4Raw   = evalMorphedWave(op4Phase, patch.waveMorph) * envOut[3] * patch.ops[3].level;
        op4Prev_       = op4Raw;

        // Mod index scaling (radians)
        constexpr float kModScale = 6.0f;
        float out = 0.0f;

        // 3. Evaluate selected 4-Operator Algorithm topology
        switch (patch.algorithm) {
            case FMAlgorithm::Alg1_SerialStack4321: {
                float op3 = evalMorphedWave(phases_[2] + op4Raw * kModScale, patch.waveMorph) * envOut[2] * patch.ops[2].level;
                float op2 = evalMorphedWave(phases_[1] + op3 * kModScale, patch.waveMorph) * envOut[1] * patch.ops[1].level;
                out       = evalMorphedWave(phases_[0] + op2 * kModScale, patch.waveMorph) * envOut[0] * patch.ops[0].level;
                break;
            }
            case FMAlgorithm::Alg2_Branched34To2To1: {
                float op3 = evalMorphedWave(phases_[2], patch.waveMorph) * envOut[2] * patch.ops[2].level;
                float op2 = evalMorphedWave(phases_[1] + (op3 + op4Raw) * kModScale, patch.waveMorph) * envOut[1] * patch.ops[1].level;
                out       = evalMorphedWave(phases_[0] + op2 * kModScale, patch.waveMorph) * envOut[0] * patch.ops[0].level;
                break;
            }
            case FMAlgorithm::Alg3_Stack431_Plus_2To1: {
                float op3 = evalMorphedWave(phases_[2] + op4Raw * kModScale, patch.waveMorph) * envOut[2] * patch.ops[2].level;
                float op2 = evalMorphedWave(phases_[1], patch.waveMorph) * envOut[1] * patch.ops[1].level;
                out       = evalMorphedWave(phases_[0] + (op3 + op2) * kModScale, patch.waveMorph) * envOut[0] * patch.ops[0].level;
                break;
            }
            case FMAlgorithm::Alg4_ThreeModsTo1: {
                float op3 = evalMorphedWave(phases_[2], patch.waveMorph) * envOut[2] * patch.ops[2].level;
                float op2 = evalMorphedWave(phases_[1], patch.waveMorph) * envOut[1] * patch.ops[1].level;
                out       = evalMorphedWave(phases_[0] + (op2 + op3 + op4Raw) * kModScale, patch.waveMorph) * envOut[0] * patch.ops[0].level;
                break;
            }
            case FMAlgorithm::Alg5_DualTwoOpStacks: {
                float op3 = evalMorphedWave(phases_[2] + op4Raw * kModScale, patch.waveMorph) * envOut[2] * patch.ops[2].level;
                float op2 = evalMorphedWave(phases_[1], patch.waveMorph) * envOut[1] * patch.ops[1].level;
                float op1 = evalMorphedWave(phases_[0] + op2 * kModScale, patch.waveMorph) * envOut[0] * patch.ops[0].level;
                out       = 0.5f * (op1 + op3);
                break;
            }
            case FMAlgorithm::Alg6_OneModThreeCarriers: {
                float mod = op4Raw * kModScale;
                float op3 = evalMorphedWave(phases_[2] + mod, patch.waveMorph) * envOut[2] * patch.ops[2].level;
                float op2 = evalMorphedWave(phases_[1] + mod, patch.waveMorph) * envOut[1] * patch.ops[1].level;
                float op1 = evalMorphedWave(phases_[0] + mod, patch.waveMorph) * envOut[0] * patch.ops[0].level;
                out       = (op1 + op2 + op3) / 3.0f;
                break;
            }
            case FMAlgorithm::Alg7_TwoOpPlusTwoAdd: {
                float op3 = evalMorphedWave(phases_[2] + op4Raw * kModScale, patch.waveMorph) * envOut[2] * patch.ops[2].level;
                float op2 = evalMorphedWave(phases_[1], patch.waveMorph) * envOut[1] * patch.ops[1].level;
                float op1 = evalMorphedWave(phases_[0], patch.waveMorph) * envOut[0] * patch.ops[0].level;
                out       = (op1 + op2 + op3) / 3.0f;
                break;
            }
            case FMAlgorithm::Alg8_FourParallelCarriers: {
                float op3 = evalMorphedWave(phases_[2], patch.waveMorph) * envOut[2] * patch.ops[2].level;
                float op2 = evalMorphedWave(phases_[1], patch.waveMorph) * envOut[1] * patch.ops[1].level;
                float op1 = evalMorphedWave(phases_[0], patch.waveMorph) * envOut[0] * patch.ops[0].level;
                out       = 0.25f * (op1 + op2 + op3 + op4Raw);
                break;
            }
        }

        // 4. Apply soft harmonic drive & 1-pole SVF low-pass filter
        if (patch.drive > 0.01f) {
            float gain = 1.0f + patch.drive * 4.0f;
            out = std::tanh(out * gain) / std::tanh(gain);
        }
        float f = clampf(2.0f * std::sin(kPi * std::min(patch.filterCutoffHz, sampleRate_ * 0.45f) / sampleRate_), 0.005f, 0.99f);
        lpState_ += f * (out - lpState_);
        return lpState_ * velocityGain_;
    }

    bool isActive() const { return active_; }

private:
    enum class EnvStage : uint8_t { Idle = 0, Attack, Decay, Sustain, Release };

    float stepEnvelope(int idx, const FMOperatorParams& p) {
        switch (envStage_[idx]) {
            case EnvStage::Idle:
                return 0.0f;
            case EnvStage::Attack: {
                float step = 1.0f / std::max(1.0f, p.attackSec * sampleRate_);
                envLevel_[idx] += step;
                if (envLevel_[idx] >= 1.0f) {
                    envLevel_[idx] = 1.0f;
                    envStage_[idx] = EnvStage::Decay;
                }
                return envLevel_[idx];
            }
            case EnvStage::Decay: {
                float step = (1.0f - p.sustainLvl) / std::max(1.0f, p.decaySec * sampleRate_);
                envLevel_[idx] -= step;
                if (envLevel_[idx] <= p.sustainLvl) {
                    envLevel_[idx] = p.sustainLvl;
                    envStage_[idx] = gateOpen_ ? EnvStage::Sustain : EnvStage::Release;
                }
                return envLevel_[idx];
            }
            case EnvStage::Sustain:
                return p.sustainLvl;
            case EnvStage::Release: {
                float step = std::max(0.05f, envLevel_[idx]) / std::max(1.0f, p.releaseSec * sampleRate_);
                envLevel_[idx] -= step;
                if (envLevel_[idx] <= 0.0001f) {
                    envLevel_[idx] = 0.0f;
                    envStage_[idx] = EnvStage::Idle;
                }
                return envLevel_[idx];
            }
        }
        return 0.0f;
    }

    // Continuous waveform morph: Sine -> Triangle-like -> Digitone 2nd/3rd Harmonics -> Wavefolded
    static float evalMorphedWave(float phase, float morph) {
        float s1 = std::sin(phase);
        if (morph <= 0.001f) return s1;
        float s2 = std::sin(2.0f * phase) * 0.5f;
        float s3 = std::sin(3.0f * phase) * 0.33f;
        float rich = s1 + morph * (s2 + s3);
        if (morph > 0.6f) {
            float foldAmt = (morph - 0.6f) * 2.5f;
            rich = std::sin(rich * (1.0f + foldAmt * 2.0f));
        }
        return clampf(rich, -1.0f, 1.0f);
    }

    float sampleRate_               = 48000.0f;
    bool  active_                   = false;
    bool  gateOpen_                 = false;
    uint8_t midiNote_               = 60;
    float velocityGain_             = 1.0f;
    float baseFreqHz_               = 261.63f;
    float sampleCursor_             = 0.0f;
    float op4Prev_                  = 0.0f;
    float lpState_                  = 0.0f;
    std::array<float, 4> phases_    = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> envLevel_  = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<EnvStage, 4> envStage_ = {EnvStage::Idle, EnvStage::Idle, EnvStage::Idle, EnvStage::Idle};
};

// ============================================================================
// SECTION 3: SAMPLE-COUPLED GRANULAR SYNTHESIS ENGINE (AudIO::GranularVoice)
// ============================================================================

struct GranularParams {
    float position        = 0.25f; // Knob 1 (Bank 1): 0.0 to 1.0 scrub position in slice
    float grainSizeMs     = 85.0f; // Knob 2 (Bank 1): 5.0 ms to 1000.0 ms
    float densityHz       = 18.0f; // Knob 3 (Bank 1): Grains spawned per second (1.0 to 80.0 Hz)
    float pitchSemitones  = 0.0f;  // Knob 4 (Bank 1): -24.0 to +24.0 semitones
    float positionSpray   = 0.08f; // Knob 1 (Bank 2): Random offset around playhead (0.0 to 0.5)
    float pitchSpray      = 0.0f;  // Knob 2 (Bank 2): Random fifth/octave shimmer probability
    float windowMorph     = 0.5f;  // Knob 3 (Bank 2): 0.0 = Percussive Ramp, 0.5 = Hann, 1.0 = Tukey
    float reverseProb     = 0.15f; // Knob 4 (Bank 2): 0.0 to 1.0 chance a grain plays backward
};

class GranularVoice {
public:
    static constexpr size_t kMaxGrains = 16;

    void setSampleRate(float sr) { sampleRate_ = sr; }

    void triggerCloud(uint8_t midiNote, uint8_t velocity) {
        active_        = true;
        noteTranspose_ = static_cast<float>(midiNote) - 60.0f;
        velocityGain_  = static_cast<float>(velocity) / 127.0f;
        spawnCounter_  = 0.0f;
    }

    void releaseCloud() { active_ = false; }

    float renderSample(const GranularParams& params, const SampleSliceView& slice) {
        if (!active_ || slice.length() < 4) return 0.0f;

        // Spawn new grains at densityHz
        spawnCounter_ += params.densityHz / sampleRate_;
        if (spawnCounter_ >= 1.0f) {
            spawnCounter_ -= 1.0f;
            spawnGrain(params, slice);
        }

        float sum = 0.0f;
        for (auto& g : grains_) {
            if (!g.active) continue;

            float env = evaluateWindow(g.phase, params.windowMorph);
            float sampleVal = slice.readInterpolated(g.samplePos);
            sum += sampleVal * env;

            g.phase += g.phaseInc;
            g.samplePos += g.posDelta;
            if (g.phase >= 1.0f || g.samplePos < 0.0f || g.samplePos > 1.0f) {
                g.active = false;
            }
        }
        return std::tanh(sum * 0.55f) * velocityGain_;
    }

private:
    struct Grain {
        bool  active    = false;
        float samplePos = 0.0f;
        float posDelta  = 0.0f;
        float phase     = 0.0f;
        float phaseInc  = 0.01f;
    };

    float nextPseudoRand() {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        return static_cast<float>(rngState_ & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }

    void spawnGrain(const GranularParams& p, const SampleSliceView& slice) {
        for (auto& g : grains_) {
            if (!g.active) {
                g.active = true;
                g.phase  = 0.0f;
                float durSamples = std::max(64.0f, (p.grainSizeMs * 0.001f) * sampleRate_);
                g.phaseInc = 1.0f / durSamples;

                float sprayOffset = (nextPseudoRand() * 2.0f - 1.0f) * p.positionSpray;
                float startPos    = clampf(p.position + sprayOffset, 0.02f, 0.98f);

                float shimmerSemi = 0.0f;
                if (nextPseudoRand() < p.pitchSpray) {
                    shimmerSemi = (nextPseudoRand() < 0.5f) ? 7.0f : 12.0f; // +5th or +1 Octave shimmer
                }
                float playbackRate = std::pow(2.0f, (p.pitchSemitones + noteTranspose_ + shimmerSemi) / 12.0f);
                bool  reverse      = (nextPseudoRand() < p.reverseProb);
                float baseDelta    = playbackRate / static_cast<float>(slice.length());

                g.samplePos = startPos;
                g.posDelta  = reverse ? -baseDelta : baseDelta;
                return;
            }
        }
    }

    static float evaluateWindow(float phase, float morph) {
        // 0.0 = Percussive decaying ramp, 0.5 = Hann bell, 1.0 = Flat-top Tukey cloud
        float hann = 0.5f * (1.0f - std::cos(kTwoPi * phase));
        if (morph < 0.5f) {
            float t = morph * 2.0f;
            float ramp = (phase < 0.05f) ? (phase / 0.05f) : (1.0f - (phase - 0.05f) / 0.95f);
            return ramp * (1.0f - t) + hann * t;
        } else {
            float t = (morph - 0.5f) * 2.0f;
            float tukey = (phase < 0.2f) ? (0.5f * (1.0f - std::cos(kPi * phase / 0.2f)))
                        : (phase > 0.8f) ? (0.5f * (1.0f - std::cos(kPi * (1.0f - phase) / 0.2f)))
                                         : 1.0f;
            return hann * (1.0f - t) + tukey * t;
        }
    }

    float sampleRate_                  = 48000.0f;
    bool  active_                      = false;
    float noteTranspose_               = 0.0f;
    float velocityGain_                = 1.0f;
    float spawnCounter_                = 0.0f;
    uint32_t rngState_                 = 0x13579BDFu;
    std::array<Grain, kMaxGrains> grains_{};
};

} // namespace AudIO

// ============================================================================
// SECTION 4: 16-PAD SCALE & CHORD PROGRESSION ENGINE (Kick::ChordScaleEngine)
// ============================================================================
namespace Kick {

enum class ScaleType : uint8_t {
    Major = 0,
    NaturalMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    HarmonicMinor
};

struct ChordModifiers {
    bool add7th9th      = false; // Pad 13: Upgrades Triad -> 7th/9th extension
    bool sus4Toggle     = false; // Pad 14: Replaces 3rd with Sus4 (or Sus2)
    uint8_t inversion   = 0;     // Pad 15: 0 = Root, 1 = 1st Inv, 2 = 2nd Inv / Drop-2 spread
    bool strumAndLatch  = false; // Pad 16: Strums chord notes or latches for Arpeggiator
};

class ChordScaleEngine {
public:
    void setScale(uint8_t rootMidiNote, ScaleType scale) {
        rootMidi_ = rootMidiNote;
        scale_    = scale;
    }

    // Pads 0..11 (Physical Pads 1-12) return up to 5 MIDI notes for the selected chord
    // Pads 12..15 (Physical Pads 13-16) toggle/hold live chord modifiers
    std::vector<uint8_t> triggerPad(uint8_t padIndex, const ChordModifiers& mods) const {
        if (padIndex >= 12) return {}; // Pads 13-16 are modifier pads

        const auto& intervals = getScaleIntervals(scale_);
        std::vector<int> chordNotes;

        if (padIndex < 7) {
            // Pads 1-7: Diatonic Scale Degrees I through vii
            int deg = static_cast<int>(padIndex);
            chordNotes.push_back(degreeToPitch(deg, intervals));
            chordNotes.push_back(degreeToPitch(deg + (mods.sus4Toggle ? 3 : 2), intervals));
            chordNotes.push_back(degreeToPitch(deg + 4, intervals));
            if (mods.add7th9th) {
                chordNotes.push_back(degreeToPitch(deg + 6, intervals));
                chordNotes.push_back(degreeToPitch(deg + 8, intervals));
            }
        } else {
            // Pads 8-12: Curated Progression / Borrowed / Turnaround Chords
            switch (padIndex) {
                case 7: // Pad 8: High Tonic Add9 (i9 / I9 resolution)
                    chordNotes = {rootMidi_ + 12, rootMidi_ + 12 + intervals[2], rootMidi_ + 12 + intervals[4], rootMidi_ + 12 + 14};
                    break;
                case 8: // Pad 9: Borrowed IV Major / Dorian Lift
                    chordNotes = {rootMidi_ + 5, rootMidi_ + 9, rootMidi_ + 12, rootMidi_ + 16};
                    break;
                case 9: // Pad 10: Harmonic Dominant V7 (strong cadence back to root)
                    chordNotes = {rootMidi_ + 7, rootMidi_ + 11, rootMidi_ + 14, rootMidi_ + 17};
                    break;
                case 10: // Pad 11: Neapolitan / Phrygian bII Maj7
                    chordNotes = {rootMidi_ + 1, rootMidi_ + 5, rootMidi_ + 8, rootMidi_ + 12};
                    break;
                case 11: // Pad 12: Secondary Dominant V7/iv (Root Dom7 leading to iv/IV)
                    chordNotes = {rootMidi_, rootMidi_ + 4, rootMidi_ + 7, rootMidi_ + 10};
                    break;
            }
        }

        // Apply smooth voice-leading inversions (Pad 15)
        for (uint8_t inv = 0; inv < (mods.inversion % 3); ++inv) {
            if (!chordNotes.empty()) {
                chordNotes[0] += 12;
                std::sort(chordNotes.begin(), chordNotes.end());
            }
        }

        std::vector<uint8_t> result;
        result.reserve(chordNotes.size());
        for (int n : chordNotes) {
            result.push_back(static_cast<uint8_t>( std::max(0, std::min(127, n)) ));
        }
        return result;
    }

private:
    static const std::array<int, 7>& getScaleIntervals(ScaleType s) {
        static const std::array<int, 7> kMajor      = {0, 2, 4, 5, 7, 9, 11};
        static const std::array<int, 7> kNatMinor   = {0, 2, 3, 5, 7, 8, 10};
        static const std::array<int, 7> kDorian     = {0, 2, 3, 5, 7, 9, 10};
        static const std::array<int, 7> kPhrygian   = {0, 1, 3, 5, 7, 8, 10};
        static const std::array<int, 7> kLydian     = {0, 2, 4, 6, 7, 9, 11};
        static const std::array<int, 7> kMixolydian = {0, 2, 4, 5, 7, 9, 10};
        static const std::array<int, 7> kHarmMinor  = {0, 2, 3, 5, 7, 8, 11};
        switch (s) {
            case ScaleType::Major:         return kMajor;
            case ScaleType::NaturalMinor:  return kNatMinor;
            case ScaleType::Dorian:        return kDorian;
            case ScaleType::Phrygian:      return kPhrygian;
            case ScaleType::Lydian:        return kLydian;
            case ScaleType::Mixolydian:    return kMixolydian;
            case ScaleType::HarmonicMinor: return kHarmMinor;
        }
        return kNatMinor;
    }

    int degreeToPitch(int degreeIndex, const std::array<int, 7>& intervals) const {
        int octave = degreeIndex / 7;
        int deg    = degreeIndex % 7;
        return static_cast<int>(rootMidi_) + octave * 12 + intervals[deg];
    }

    uint8_t   rootMidi_ = 60; // C4 default
    ScaleType scale_    = ScaleType::NaturalMinor;
};

// ============================================================================
// SECTION 5: 4-KNOB MULTI-MODE ARPEGGIATOR (Kick::Arpeggiator)
// ============================================================================

enum class ArpMode : uint8_t {
    Up = 0,
    Down,
    UpDown,
    Converge,
    PinkyThumbPedal,
    ChordRepeat
};

struct ArpParams {
    ArpMode mode        = ArpMode::Up; // Knob 1
    int     stepDivTicks = kPPQN / 4;  // Knob 2: Default 1/16th note (240 ticks at 960 PPQN)
    uint8_t octaveRange = 2;           // Knob 3: 1 to 4 octaves
    float   gateLength  = 0.65f;       // Knob 4: 0.05 to 1.20 (legato)
};

class Arpeggiator {
public:
    void setHeldNotes(const std::vector<uint8_t>& notes, const ArpParams& params) {
        expandedPool_.clear();
        if (notes.empty()) return;

        std::vector<uint8_t> sorted = notes;
        std::sort(sorted.begin(), sorted.end());

        uint8_t octs = std::max<uint8_t>(1, std::min<uint8_t>(4, params.octaveRange));
        for (uint8_t o = 0; o < octs; ++o) {
            for (uint8_t n : sorted) {
                int pitch = static_cast<int>(n) + o * 12;
                if (pitch <= 127) expandedPool_.push_back(static_cast<uint8_t>(pitch));
            }
        }
        stepCursor_ = 0;
    }

    // Returns the next arpeggiated note(s) on each clock step
    std::vector<uint8_t> nextStepNotes(const ArpParams& params) {
        if (expandedPool_.empty()) return {};
        if (params.mode == ArpMode::ChordRepeat) {
            return expandedPool_;
        }

        size_t N = expandedPool_.size();
        uint8_t chosen = expandedPool_[0];

        switch (params.mode) {
            case ArpMode::Up:
                chosen = expandedPool_[stepCursor_ % N];
                break;
            case ArpMode::Down:
                chosen = expandedPool_[(N - 1) - (stepCursor_ % N)];
                break;
            case ArpMode::UpDown: {
                size_t cycle = (N > 2) ? (2 * N - 2) : N;
                size_t pos   = stepCursor_ % cycle;
                chosen       = (pos < N) ? expandedPool_[pos] : expandedPool_[cycle - pos];
                break;
            }
            case ArpMode::Converge: {
                size_t pairIdx = (stepCursor_ / 2) % ((N + 1) / 2);
                chosen = (stepCursor_ % 2 == 0) ? expandedPool_[pairIdx] : expandedPool_[N - 1 - pairIdx];
                break;
            }
            case ArpMode::PinkyThumbPedal: {
                if (stepCursor_ % 2 == 0 || N == 1) {
                    chosen = expandedPool_[0]; // Root pedal
                } else {
                    chosen = expandedPool_[1 + ((stepCursor_ / 2) % (N - 1))];
                }
                break;
            }
            case ArpMode::ChordRepeat:
                break;
        }
        ++stepCursor_;
        return {chosen};
    }

private:
    std::vector<uint8_t> expandedPool_;
    size_t               stepCursor_ = 0;
};

// ============================================================================
// SECTION 6: 64-STEP POLYPHONIC SEQUENCER WITH 4-KNOB EDIT & LIVE QUANTIZE
// ============================================================================

enum class QuantizeMode : uint8_t {
    Off_Unquantized = 0,
    SmartHumanize_75Pct,
    HardSnap_100Pct
};

struct StepParameterLock {
    bool  hasFilterCutoffLock = false;
    float filterCutoffHz      = 8000.0f;
    bool  hasGrainPosLock     = false;
    float grainPosition       = 0.25f;
    bool  hasFMModIndexLock   = false;
    float fmOp2Level          = 0.50f;
};

struct SequencerStep {
    bool                  enabled         = false;
    std::vector<uint8_t>  notes           = {60};  // Knob 1: Single note or polyphonic 1-Pad Chord
    uint8_t               velocity        = 100;   // Knob 2: 1 to 127
    float                 lengthSteps     = 1.0f;  // Knob 3: 0.1 (staccato) to 64.0 (drone tie)
    float                 microtimingPct  = 0.0f;  // Knob 4 (Turn): -49% to +49% step offset
    uint8_t               ratchetCount    = 1;     // Knob 4 (Push+Turn): 1x, 2x, 3x, 4x, 8x
    uint8_t               probabilityPct  = 100;   // Conditional trigger probability (10% - 100%)
    StepParameterLock     plocks{};                // Elektron-style per-step parameter locks
};

class StepSequencerTrack {
public:
    static constexpr size_t kMaxSteps    = 64;
    static constexpr int    kTicksPerStep = kPPQN / 4; // 240 ticks per 1/16th step

    StepSequencerTrack() {
        steps_.resize(kMaxSteps);
    }

    void setPatternLength(size_t numSteps) {
        patternLength_ = std::max<size_t>(1, std::min(kMaxSteps, numSteps));
    }

    // Direct 4-Knob hardware step editing when holding a step pad or selecting a touchscreen cell
    void editStepWithFourKnobs(size_t stepIdx, int pitchDelta, int velDelta, float lengthDelta, float microDelta, bool pushHeld = false) {
        if (stepIdx >= patternLength_) return;
        auto& s = steps_[stepIdx];
        s.enabled = true;

        // Knob 1: Pitch transpose for all notes/chord tones on this step
        if (pitchDelta != 0) {
            for (auto& n : s.notes) {
                int updated = static_cast<int>(n) + pitchDelta;
                n = static_cast<uint8_t>(std::max(0, std::min(127, updated)));
            }
        }
        // Knob 2: Velocity (1 - 127)
        if (velDelta != 0) {
            int v = static_cast<int>(s.velocity) + velDelta;
            s.velocity = static_cast<uint8_t>(std::max(1, std::min(127, v)));
        }
        // Knob 3: Note Length in steps (0.1 to 64.0)
        if (std::abs(lengthDelta) > 0.0001f) {
            s.lengthSteps = clampf(s.lengthSteps + lengthDelta, 0.1f, 64.0f);
        }
        // Knob 4: Microtiming (-49% to +49%) OR Ratchet sub-steps when push-turned
        if (std::abs(microDelta) > 0.0001f) {
            if (pushHeld) {
                int r = static_cast<int>(s.ratchetCount) + (microDelta > 0.0f ? 1 : -1);
                s.ratchetCount = static_cast<uint8_t>(std::max(1, std::min(8, r)));
            } else {
                s.microtimingPct = clampf(s.microtimingPct + microDelta * 100.0f, -49.0f, 49.0f);
            }
        }
    }

    // Live Drum/Chord Pad Recording at 960 PPQN with selectable Input Quantize
    size_t recordLivePadEvent(int32_t currentTick, const std::vector<uint8_t>& chordOrNote, uint8_t velocity, float lengthSteps, QuantizeMode qMode) {
        int32_t totalPatternTicks = static_cast<int32_t>(patternLength_) * kTicksPerStep;
        int32_t wrappedTick       = ((currentTick % totalPatternTicks) + totalPatternTicks) % totalPatternTicks;

        int32_t nearestStepIdx = static_cast<int32_t>(std::round(static_cast<float>(wrappedTick) / static_cast<float>(kTicksPerStep)));
        int32_t quantizedTick  = nearestStepIdx * kTicksPerStep;
        int32_t tickError      = wrappedTick - quantizedTick;

        size_t finalStepIdx = static_cast<size_t>((nearestStepIdx % static_cast<int32_t>(patternLength_) + static_cast<int32_t>(patternLength_)) % static_cast<int32_t>(patternLength_));
        float  finalMicroPct = 0.0f;

        switch (qMode) {
            case QuantizeMode::HardSnap_100Pct:
                finalMicroPct = 0.0f; // 100% locked to the 1/16th step grid
                break;
            case QuantizeMode::SmartHumanize_75Pct:
                // Pulls 75% toward the grid while keeping 25% of the player's natural pocket feel
                finalMicroPct = clampf((static_cast<float>(tickError) * 0.25f / static_cast<float>(kTicksPerStep)) * 100.0f, -49.0f, 49.0f);
                break;
            case QuantizeMode::Off_Unquantized:
                finalMicroPct = clampf((static_cast<float>(tickError) / static_cast<float>(kTicksPerStep)) * 100.0f, -49.0f, 49.0f);
                break;
        }

        auto& s          = steps_[finalStepIdx];
        s.enabled        = true;
        s.notes          = chordOrNote;
        s.velocity       = velocity;
        s.lengthSteps    = lengthSteps;
        s.microtimingPct = finalMicroPct;
        return finalStepIdx;
    }

    const SequencerStep& getStep(size_t idx) const { return steps_[idx % patternLength_]; }
    SequencerStep&       getStepMutable(size_t idx) { return steps_[idx % patternLength_]; }
    size_t               getPatternLength() const { return patternLength_; }

private:
    size_t                     patternLength_ = 16;
    std::vector<SequencerStep> steps_;
};

} // namespace Kick
} // namespace BehringerKickCustom
