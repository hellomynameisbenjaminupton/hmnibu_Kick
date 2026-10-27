/**
 * @file kick_hardware_bridge.hpp
 * @brief Phase 3 Hardware Event Router & ComProtocol Bridge for Behringer KICK (BE 0700-AAM)
 * @license AGPL-3.0-or-later (compatible with MusicTribeOpenSource/0700-AAM)
 *
 * Connects Behringer ComProtocol/ hardware peripheral events to CustomEngines:
 *   1. 16 Velocity/Pressure RGB Pads -> ChordScaleEngine, SampleChopper, StepSequencerTrack
 *   2. 4 Endless Optical Rotary Encoders -> Contextual 4-Knob Page Router (Zero Menu Diving)
 *   3. Optical Jog Wheel + Push -> Rapid Navigation, Alg Switch & Parameter Fine-Tuning
 *   4. Capacitive Touch Strip -> Real-time Macro, Grain Scrubber & Scene A/B Morphing
 *   5. AI Gesture Camera (VL53L5CX / ToF) -> Hands-Free 3D Spatial Modulation
 *   6. Elektron-Style Parameter Locks (P-Locks) -> Step-locked parameter automation
 *   7. Dynamic RGB Pad LED Matrix -> Real-time state visualization (Chords, Slices, Steps)
 */

#pragma once

#include "kick_custom_firmware_core.hpp"
#include "kick_sample_chopper.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace BehringerKickCustom {
namespace Hardware {

enum class HardwareEventType : uint8_t {
    PadDown = 0,
    PadUp,
    PadPressure,       // Polyphonic aftertouch (0-127)
    KnobTurn,          // Delta increment/decrement from optical encoder
    KnobPush,          // Encoder switch toggle
    JogTurn,           // Jog wheel detent step (+1 / -1)
    JogPush,           // Jog wheel center button
    TouchStripChange,  // Capacitive ribbon touch/position
    AIGestureFrame,    // Optical / ToF camera hand coordinates
    TransportButton,   // Play, Stop, Rec, Tap, Shift
    ScreenViewSelect   // Touchscreen tab switch
};

enum class ScreenView : uint8_t {
    View_FM4Synth = 0,
    View_GranularCloud,
    View_SampleChopper,
    View_StepSequencer
};

enum class PadOperatingMode : uint8_t {
    ChordScale = 0,    // Pads 1-12 Chords, Pads 13-16 Live Modifiers
    SampleSlices,      // 16 Sample Slices / Drum Chops
    StepSequencer      // 16 Step Triggers / Parameter Lock Entry
};

enum class TransportControl : uint8_t {
    Play = 0,
    Stop,
    Record,
    TapTempo,
    ShiftToggle
};

struct PadEvent {
    uint8_t padIndex = 0;   // 0 to 15 (Pads 1 to 16)
    uint8_t velocity = 100; // 0 to 127
    uint8_t pressure = 0;   // 0 to 127 (continuous aftertouch)
};

struct KnobEvent {
    uint8_t knobIndex = 0;  // 0 to 3 (Physical Knobs 1 to 4)
    float   delta     = 0.0f; // Normalized change (-1.0 to +1.0)
    bool    pushHeld  = false;// Push-turn secondary modifier active
};

struct JogEvent {
    int8_t  stepDelta = 0;  // -1 (Counter-Clockwise) or +1 (Clockwise)
    bool    pushHeld  = false;
};

struct TouchStripEvent {
    bool    isTouched = false;
    float   position  = 0.5f; // 0.0 (Left/Bottom) to 1.0 (Right/Top)
    float   pressure  = 0.0f; // 0.0 to 1.0
};

struct AIGestureEvent {
    bool    handDetected = false;
    float   posX         = 0.5f; // 0.0 to 1.0 (Left-Right)
    float   posY         = 0.5f; // 0.0 to 1.0 (Up-Down)
    float   distanceZ    = 0.5f; // 0.0 (Close) to 1.0 (Far)
};

// RGB LED Color representation for 16 pads
struct RGBColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    static constexpr RGBColor Black()       { return {0,   0,   0};   }
    static constexpr RGBColor DimWhite()    { return {40,  40,  40};  }
    static constexpr RGBColor FullWhite()   { return {255, 255, 255}; }
    static constexpr RGBColor Green()       { return {16,  185, 129}; } // Diatonic chord
    static constexpr RGBColor BrightGreen() { return {52,  211, 153}; }
    static constexpr RGBColor Amber()       { return {245, 158, 11};  } // Borrowed / Turnaround
    static constexpr RGBColor BrightAmber() { return {251, 191, 36};  }
    static constexpr RGBColor Cyan()        { return {6,   182, 212}; } // 7th/9th Modifier
    static constexpr RGBColor Purple()      { return {168, 85,  247}; } // Inversion / Step
    static constexpr RGBColor Gold()        { return {234, 179, 8};   } // Strum/Latch / Playhead
    static constexpr RGBColor Rose()        { return {244, 63,  94};  } // Recording
};

// Scene Snapshot for Touch Strip Morphing (Macro A/B)
struct SceneSnapshot {
    float filterCutoffHz   = 8500.0f;
    float filterResonance  = 0.20f;
    float drive            = 0.10f;
    float waveMorph        = 0.00f;
    float fmOp4Feedback    = 0.20f;
    float grainSizeMs      = 85.0f;
    float grainDensityHz   = 18.0f;
    float grainSpray       = 0.08f;
    float wsolaStretch     = 1.0f;
};

class KickHardwareBridge {
public:
    static constexpr size_t kNumTracks = 4;
    static constexpr size_t kNumPads   = 16;
    static constexpr size_t kNumKnobs  = 4;

    KickHardwareBridge() {
        tracks_.resize(kNumTracks);
        padLeds_.fill(RGBColor::Black());
        activeNotesPerTrack_.resize(kNumTracks);

        for (auto& trk : tracks_) {
            trk.setPatternLength(16);
        }

        sceneA_.filterCutoffHz  = 4200.0f;
        sceneA_.fmOp4Feedback   = 0.15f;
        sceneA_.grainSizeMs     = 60.0f;
        sceneA_.grainDensityHz  = 16.0f;
        sceneA_.wsolaStretch    = 1.0f;

        sceneB_.filterCutoffHz  = 14500.0f;
        sceneB_.fmOp4Feedback   = 0.75f;
        sceneB_.grainSizeMs     = 280.0f;
        sceneB_.grainDensityHz  = 45.0f;
        sceneB_.wsolaStretch    = 1.75f;

        updatePadLeds();
    }

    ScreenView getScreenView() const { return currentView_; }
    void setScreenView(ScreenView view) {
        currentView_ = view;
        updatePadLeds();
    }

    PadOperatingMode getPadMode() const { return padMode_; }
    void setPadMode(PadOperatingMode mode) {
        padMode_ = mode;
        updatePadLeds();
    }

    uint8_t getActiveTrackIndex() const { return activeTrackIdx_; }
    void setActiveTrackIndex(uint8_t trk) {
        activeTrackIdx_ = trk % kNumTracks;
        updatePadLeds();
    }

    bool isPlaying() const   { return isPlaying_; }
    bool isRecording() const { return isRecording_; }
    void setPlaying(bool play) {
        isPlaying_ = play;
        if (!isPlaying_) {
            currentTick_ = 0;
            currentStep_ = 0;
        }
        updatePadLeds();
    }
    void setRecording(bool rec) {
        isRecording_ = rec;
        updatePadLeds();
    }

    float getBpm() const { return bpm_; }
    void setBpm(float bpm) { bpm_ = clampf(bpm, 40.0f, 300.0f); }

    Kick::QuantizeMode getQuantizeMode() const { return quantizeMode_; }
    void setQuantizeMode(Kick::QuantizeMode q) { quantizeMode_ = q; }

    AudIO::FM4Patch&             getFMPatch()       { return fmPatch_; }
    const AudIO::FM4Patch&       getFMPatch() const { return fmPatch_; }
    AudIO::GranularParams&       getGrainParams()   { return grainParams_; }
    const AudIO::GranularParams& getGrainParams() const { return grainParams_; }
    AudIO::SampleChopper&        getChopper()       { return chopper_; }
    const AudIO::SampleChopper&  getChopper() const { return chopper_; }
    Kick::ChordScaleEngine&      getChordEngine()   { return chordEngine_; }
    Kick::ChordModifiers&        getChordMods()     { return chordMods_; }
    Kick::Arpeggiator&           getArpeggiator()   { return arpeggiator_; }
    Kick::ArpParams&             getArpParams()     { return arpParams_; }

    Kick::StepSequencerTrack&       getTrack(size_t idx)       { return tracks_[idx % kNumTracks]; }
    const Kick::StepSequencerTrack& getTrack(size_t idx) const { return tracks_[idx % kNumTracks]; }

    const std::array<RGBColor, kNumPads>& getPadLeds() const { return padLeds_; }
    uint8_t getCurrentStep() const { return currentStep_; }
    uint8_t getSelectedSliceIndex() const { return selectedSliceIdx_; }
    uint8_t getSelectedStepIndex() const { return selectedStepIdx_; }
    std::vector<uint8_t>& getActiveNotesForTrack(size_t t) { return activeNotesPerTrack_[t % kNumTracks]; }
    uint8_t getLastTriggeredVelocity() const { return lastTriggeredVelocity_; }

    void handlePadDown(const PadEvent& ev) {
        if (ev.padIndex >= kNumPads) return;
        padPressed_[ev.padIndex] = true;

        if (padMode_ == PadOperatingMode::ChordScale) {
            handleChordPadDown(ev.padIndex, ev.velocity);
        } else if (padMode_ == PadOperatingMode::SampleSlices) {
            handleSlicePadDown(ev.padIndex, ev.velocity);
        } else if (padMode_ == PadOperatingMode::StepSequencer) {
            handleStepPadDown(ev.padIndex, ev.velocity);
        }
        updatePadLeds();
    }

    void handlePadUp(const PadEvent& ev) {
        if (ev.padIndex >= kNumPads) return;
        padPressed_[ev.padIndex] = false;

        if (padMode_ == PadOperatingMode::ChordScale) {
            if (ev.padIndex == 15 && !chordMods_.strumAndLatch) {
                heldChordNotes_.clear();
            }
        } else if (padMode_ == PadOperatingMode::StepSequencer) {
            if (heldStepPad_ == static_cast<int>(ev.padIndex)) {
                heldStepPad_ = -1;
            }
        }
        updatePadLeds();
    }

    void handlePadPressure(const PadEvent& ev) {
        if (ev.padIndex >= kNumPads) return;
        float normPressure = static_cast<float>(ev.pressure) / 127.0f;
        if (activeTrackIdx_ == 0) {
            fmPatch_.filterCutoffHz = clampf(4000.0f + normPressure * 12000.0f, 60.0f, 18000.0f);
        } else if (activeTrackIdx_ == 1) {
            grainParams_.densityHz  = clampf(14.0f + normPressure * 50.0f, 1.0f, 80.0f);
            grainParams_.pitchSpray = normPressure * 0.4f;
        }
    }

    void handleKnobTurn(const KnobEvent& ev) {
        if (ev.knobIndex >= kNumKnobs) return;

        if (padMode_ == PadOperatingMode::StepSequencer && heldStepPad_ >= 0) {
            recordParameterLock(static_cast<size_t>(heldStepPad_), ev);
            return;
        }

        switch (currentView_) {
            case ScreenView::View_FM4Synth:
                AudIO::FM4Voice::applyHardwareKnob(fmPatch_, fmKnobBank_, ev.knobIndex, ev.delta, ev.pushHeld);
                break;

            case ScreenView::View_GranularCloud:
                applyGranularKnob(ev.knobIndex, ev.delta, ev.pushHeld);
                break;

            case ScreenView::View_SampleChopper:
                chopper_.applyHardwareKnobs(selectedSliceIdx_, chopperKnobBank_, ev.knobIndex, ev.delta, ev.pushHeld);
                break;

            case ScreenView::View_StepSequencer:
                applySequencerKnob(selectedStepIdx_, ev.knobIndex, ev.delta, ev.pushHeld);
                break;
        }
    }

    void handleJogWheel(const JogEvent& ev) {
        if (currentView_ == ScreenView::View_FM4Synth) {
            int alg = static_cast<int>(fmPatch_.algorithm) + ev.stepDelta;
            alg = (alg % 8 + 8) % 8;
            fmPatch_.algorithm = static_cast<AudIO::FMAlgorithm>(alg);
        } else if (currentView_ == ScreenView::View_SampleChopper) {
            int sl = static_cast<int>(selectedSliceIdx_) + ev.stepDelta;
            selectedSliceIdx_ = static_cast<uint8_t>((sl % 16 + 16) % 16);
        } else if (currentView_ == ScreenView::View_StepSequencer) {
            int st = static_cast<int>(selectedStepIdx_) + ev.stepDelta;
            selectedStepIdx_ = static_cast<uint8_t>((st % 16 + 16) % 16);
        } else if (currentView_ == ScreenView::View_GranularCloud) {
            grainParams_.pitchSemitones = clampf(grainParams_.pitchSemitones + static_cast<float>(ev.stepDelta), -24.0f, 24.0f);
        }
        updatePadLeds();
    }

    void handleTouchStrip(const TouchStripEvent& ev) {
        lastTouchStrip_ = ev;
        if (!ev.isTouched) return;

        if (touchStripMode_ == TouchStripMode::SceneMorphAB) {
            morphScenes(ev.position);
        } else if (touchStripMode_ == TouchStripMode::GrainScrub) {
            grainParams_.position = clampf(ev.position, 0.01f, 0.99f);
        } else if (touchStripMode_ == TouchStripMode::StrumBar) {
            if (!heldChordNotes_.empty()) {
                triggerVoiceChord(heldChordNotes_, static_cast<uint8_t>(clampf(ev.pressure * 127.0f, 60.0f, 127.0f)));
            }
        }
    }

    void handleAIGesture(const AIGestureEvent& ev) {
        lastGesture_ = ev;
        if (!ev.handDetected) return;

        float cutoffMod = (ev.posX - 0.5f) * 6000.0f;
        fmPatch_.filterCutoffHz = clampf(fmPatch_.filterCutoffHz + cutoffMod * 0.05f, 60.0f, 18000.0f);
        fmPatch_.drive          = clampf(ev.distanceZ, 0.0f, 1.0f);
        grainParams_.grainSizeMs= clampf(20.0f + ev.posY * 300.0f, 10.0f, 800.0f);
    }

    void handleTransportButton(TransportControl btn) {
        switch (btn) {
            case TransportControl::Play:
                setPlaying(!isPlaying_);
                break;
            case TransportControl::Stop:
                setPlaying(false);
                break;
            case TransportControl::Record:
                setRecording(!isRecording_);
                break;
            case TransportControl::TapTempo:
                registerTapTempo();
                break;
            case TransportControl::ShiftToggle:
                shiftHeld_ = !shiftHeld_;
                break;
        }
    }

    void advanceClockTick() {
        if (!isPlaying_) return;

        constexpr int32_t kTicksPerStep = kPPQN / 4;

        if (currentTick_ % kTicksPerStep == 0) {
            uint8_t stepIdx = static_cast<uint8_t>((currentTick_ / kTicksPerStep) % tracks_[activeTrackIdx_].getPatternLength());
            currentStep_ = stepIdx;

            if (arpParams_.mode != Kick::ArpMode::ChordRepeat && !heldChordNotes_.empty()) {
                auto arpNotes = arpeggiator_.nextStepNotes(arpParams_);
                if (!arpNotes.empty()) {
                    triggerVoiceChord(arpNotes, 108);
                }
            }

            for (size_t t = 0; t < kNumTracks; ++t) {
                const auto& step = tracks_[t].getStep(stepIdx);
                if (step.enabled) {
                    if (step.plocks.hasFilterCutoffLock) {
                        fmPatch_.filterCutoffHz = step.plocks.filterCutoffHz;
                    }
                    if (step.plocks.hasGrainPosLock) {
                        grainParams_.position = step.plocks.grainPosition;
                    }
                    if (step.plocks.hasFMModIndexLock) {
                        fmPatch_.ops[1].level = step.plocks.fmOp2Level;
                    }
                    triggerTrackStepNotes(t, step.notes, step.velocity);
                }
            }
            updatePadLeds();
        }

        ++currentTick_;
        int32_t maxTicks = static_cast<int32_t>(tracks_[activeTrackIdx_].getPatternLength()) * kTicksPerStep;
        if (currentTick_ >= maxTicks) {
            currentTick_ = 0;
        }
    }

private:
    enum class TouchStripMode : uint8_t {
        SceneMorphAB = 0,
        GrainScrub,
        StrumBar
    };

    void handleChordPadDown(uint8_t padIdx, uint8_t velocity) {
        if (padIdx >= 12) {
            if (padIdx == 12) chordMods_.add7th9th = !chordMods_.add7th9th;
            if (padIdx == 13) chordMods_.sus4Toggle = !chordMods_.sus4Toggle;
            if (padIdx == 14) chordMods_.inversion = (chordMods_.inversion + 1) % 3;
            if (padIdx == 15) chordMods_.strumAndLatch = !chordMods_.strumAndLatch;
            return;
        }

        auto notes = chordEngine_.triggerPad(padIdx, chordMods_);
        heldChordNotes_ = notes;
        arpeggiator_.setHeldNotes(notes, arpParams_);
        triggerVoiceChord(notes, velocity);

        if (isRecording_) {
            tracks_[activeTrackIdx_].recordLivePadEvent(currentTick_, notes, velocity, 1.0f, quantizeMode_);
        }
    }

    void handleSlicePadDown(uint8_t padIdx, uint8_t velocity) {
        selectedSliceIdx_ = padIdx;
        chopper_.registerLazyChopMarker(padIdx, padIdx * 3000);
        triggerVoiceSlice(padIdx, velocity);

        if (isRecording_) {
            std::vector<uint8_t> sliceNote = {static_cast<uint8_t>(36 + padIdx)};
            tracks_[activeTrackIdx_].recordLivePadEvent(currentTick_, sliceNote, velocity, 1.0f, quantizeMode_);
        }
    }

    void handleStepPadDown(uint8_t padIdx, uint8_t velocity) {
        selectedStepIdx_ = padIdx;
        heldStepPad_     = static_cast<int>(padIdx);

        auto& step = tracks_[activeTrackIdx_].getStepMutable(padIdx);
        step.enabled = !step.enabled;
        if (step.enabled && step.notes.empty()) {
            step.notes = {60};
            step.velocity = velocity;
        }
    }

    void triggerVoiceChord(const std::vector<uint8_t>& notes, uint8_t velocity) {
        activeNotesPerTrack_[activeTrackIdx_] = notes;
        lastTriggeredVelocity_ = velocity;
    }

    void triggerVoiceSlice(uint8_t sliceIdx, uint8_t velocity) {
        activeNotesPerTrack_[activeTrackIdx_] = {static_cast<uint8_t>(36 + sliceIdx)};
        lastTriggeredVelocity_ = velocity;
    }

    void triggerTrackStepNotes(size_t trackIdx, const std::vector<uint8_t>& notes, uint8_t velocity) {
        activeNotesPerTrack_[trackIdx] = notes;
        lastTriggeredVelocity_ = velocity;
    }

    void applyGranularKnob(uint8_t knobIdx, float delta, bool pushHeld) {
        if (grainKnobBank_ == 0) {
            if (knobIdx == 0) grainParams_.position       = clampf(grainParams_.position + delta * 0.5f, 0.0f, 1.0f);
            if (knobIdx == 1) grainParams_.grainSizeMs   = clampf(grainParams_.grainSizeMs + delta * 200.0f, 5.0f, 1000.0f);
            if (knobIdx == 2) grainParams_.densityHz     = clampf(grainParams_.densityHz + delta * 30.0f, 1.0f, 80.0f);
            if (knobIdx == 3) {
                if (pushHeld) {
                    grainParams_.pitchSpray = clampf(grainParams_.pitchSpray + delta, 0.0f, 1.0f);
                } else {
                    grainParams_.pitchSemitones = clampf(grainParams_.pitchSemitones + std::round(delta * 12.0f), -24.0f, 24.0f);
                }
            }
        } else {
            if (knobIdx == 0) grainParams_.positionSpray = clampf(grainParams_.positionSpray + delta * 0.25f, 0.0f, 0.5f);
            if (knobIdx == 1) grainParams_.pitchSpray    = clampf(grainParams_.pitchSpray + delta, 0.0f, 1.0f);
            if (knobIdx == 2) grainParams_.windowMorph   = clampf(grainParams_.windowMorph + delta, 0.0f, 1.0f);
            if (knobIdx == 3) grainParams_.reverseProb   = clampf(grainParams_.reverseProb + delta, 0.0f, 1.0f);
        }
    }

    void applySequencerKnob(size_t stepIdx, uint8_t knobIdx, float delta, bool pushHeld) {
        int pitchD = (knobIdx == 0) ? static_cast<int>(std::round(delta * 12.0f)) : 0;
        int velD   = (knobIdx == 1) ? static_cast<int>(std::round(delta * 64.0f)) : 0;
        float lenD = (knobIdx == 2) ? delta * 2.0f : 0.0f;
        float micD = (knobIdx == 3) ? delta : 0.0f;
        tracks_[activeTrackIdx_].editStepWithFourKnobs(stepIdx, pitchD, velD, lenD, micD, pushHeld);
    }

    void recordParameterLock(size_t stepIdx, const KnobEvent& ev) {
        auto& step = tracks_[activeTrackIdx_].getStepMutable(stepIdx);
        step.enabled = true;

        if (currentView_ == ScreenView::View_FM4Synth) {
            if (ev.knobIndex == 0) {
                step.plocks.hasFilterCutoffLock = true;
                step.plocks.filterCutoffHz = clampf(fmPatch_.filterCutoffHz * std::pow(2.0f, ev.delta * 2.0f), 60.0f, 18000.0f);
            } else if (ev.knobIndex == 1) {
                step.plocks.hasFMModIndexLock = true;
                step.plocks.fmOp2Level = clampf(fmPatch_.ops[1].level + ev.delta, 0.0f, 1.0f);
            }
        } else if (currentView_ == ScreenView::View_GranularCloud) {
            if (ev.knobIndex == 0) {
                step.plocks.hasGrainPosLock = true;
                step.plocks.grainPosition = clampf(grainParams_.position + ev.delta * 0.5f, 0.0f, 1.0f);
            }
        }
    }

    void morphScenes(float t) {
        t = clampf(t, 0.0f, 1.0f);
        fmPatch_.filterCutoffHz = sceneA_.filterCutoffHz + t * (sceneB_.filterCutoffHz - sceneA_.filterCutoffHz);
        fmPatch_.op4Feedback    = sceneA_.fmOp4Feedback  + t * (sceneB_.fmOp4Feedback  - sceneA_.fmOp4Feedback);
        grainParams_.grainSizeMs= sceneA_.grainSizeMs    + t * (sceneB_.grainSizeMs    - sceneA_.grainSizeMs);
        grainParams_.densityHz  = sceneA_.grainDensityHz + t * (sceneB_.grainDensityHz - sceneA_.grainDensityHz);
    }

    void updatePadLeds() {
        if (padMode_ == PadOperatingMode::ChordScale) {
            for (size_t i = 0; i < kNumPads; ++i) {
                if (padPressed_[i]) {
                    padLeds_[i] = RGBColor::FullWhite();
                } else if (i < 7) {
                    padLeds_[i] = RGBColor::Green();
                } else if (i < 12) {
                    padLeds_[i] = RGBColor::Amber();
                } else {
                    if (i == 12) padLeds_[i] = chordMods_.add7th9th ? RGBColor::FullWhite() : RGBColor::Cyan();
                    if (i == 13) padLeds_[i] = chordMods_.sus4Toggle ? RGBColor::FullWhite() : RGBColor::Cyan();
                    if (i == 14) padLeds_[i] = (chordMods_.inversion > 0) ? RGBColor::FullWhite() : RGBColor::Purple();
                    if (i == 15) padLeds_[i] = chordMods_.strumAndLatch ? RGBColor::FullWhite() : RGBColor::Gold();
                }
            }
        } else if (padMode_ == PadOperatingMode::SampleSlices) {
            for (size_t i = 0; i < kNumPads; ++i) {
                if (padPressed_[i]) {
                    padLeds_[i] = RGBColor::FullWhite();
                } else if (i == selectedSliceIdx_) {
                    padLeds_[i] = RGBColor::BrightAmber();
                } else {
                    padLeds_[i] = RGBColor::Green();
                }
            }
        } else if (padMode_ == PadOperatingMode::StepSequencer) {
            const auto& trk = tracks_[activeTrackIdx_];
            for (size_t i = 0; i < kNumPads; ++i) {
                if (isPlaying_ && currentStep_ == i) {
                    padLeds_[i] = RGBColor::Gold();
                } else if (padPressed_[i] || heldStepPad_ == static_cast<int>(i)) {
                    padLeds_[i] = RGBColor::FullWhite();
                } else if (trk.getStep(i).enabled) {
                    padLeds_[i] = RGBColor::Purple();
                } else {
                    padLeds_[i] = RGBColor::DimWhite();
                }
            }
        }
    }

    void registerTapTempo() {
        bpm_ = std::min(240.0f, bpm_ + 2.0f);
    }

    ScreenView           currentView_       = ScreenView::View_FM4Synth;
    PadOperatingMode     padMode_           = PadOperatingMode::ChordScale;
    uint8_t              activeTrackIdx_    = 0;
    bool                 isPlaying_         = false;
    bool                 isRecording_       = false;
    bool                 shiftHeld_         = false;
    float                bpm_               = 118.0f;
    int32_t              currentTick_       = 0;
    uint8_t              currentStep_       = 0;
    Kick::QuantizeMode   quantizeMode_      = Kick::QuantizeMode::SmartHumanize_75Pct;

    uint8_t              selectedSliceIdx_  = 0;
    uint8_t              selectedStepIdx_   = 0;
    int                  heldStepPad_       = -1;

    AudIO::FM4Patch      fmPatch_{};
    AudIO::FMKnobBank    fmKnobBank_        = AudIO::FMKnobBank::Bank1_RatiosAndTuning;

    AudIO::GranularParams grainParams_{};
    uint8_t              grainKnobBank_     = 0;

    AudIO::SampleChopper chopper_{};
    AudIO::ChopperKnobBank chopperKnobBank_ = AudIO::ChopperKnobBank::Bank1_TrimAndStretch;

    Kick::ChordScaleEngine chordEngine_{};
    Kick::ChordModifiers   chordMods_{};
    Kick::Arpeggiator      arpeggiator_{};
    Kick::ArpParams        arpParams_{};
    std::vector<uint8_t>   heldChordNotes_{};

    std::vector<Kick::StepSequencerTrack> tracks_;
    std::vector<std::vector<uint8_t>>     activeNotesPerTrack_;
    uint8_t                               lastTriggeredVelocity_ = 100;

    std::array<bool, kNumPads>     padPressed_{};
    std::array<RGBColor, kNumPads> padLeds_{};
    TouchStripEvent                lastTouchStrip_{};
    TouchStripMode                 touchStripMode_    = TouchStripMode::SceneMorphAB;
    SceneSnapshot                  sceneA_{};
    SceneSnapshot                  sceneB_{};
    AIGestureEvent                 lastGesture_{};
};

} // namespace Hardware
} // namespace BehringerKickCustom

