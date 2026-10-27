#include "../AudIO/CustomEngines/kick_custom_firmware_core.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace BehringerKickCustom;

int main() {
    // 1. Synthetic drum/transient sample buffer in AudIO RAM (48 kHz, 0.5 sec)
    std::vector<float> audioBuffer(24000, 0.0f);
    for (size_t i = 0; i < audioBuffer.size(); ++i) {
        float t = static_cast<float>(i) / 48000.0f;
        audioBuffer[i] = std::exp(-t * 12.0f) * std::sin(kTwoPi * 220.0f * t);
    }
    AudIO::SampleSliceView slice{audioBuffer.data(), audioBuffer.size(), 48000, 0, audioBuffer.size(), true, 0.05f};

    // 2. Test 4-Operator FM Engine across all 8 algorithms + Sample-FM injection
    AudIO::FM4Patch fmPatch;
    fmPatch.sampleFMDepth = 0.35f;
    AudIO::FM4Voice fmVoice;
    fmVoice.setSampleRate(48000.0f);

    AudIO::FM4Voice::applyHardwareKnob(fmPatch, AudIO::FMKnobBank::Bank1_RatiosAndTuning, 1, 0.25f, false);
    AudIO::FM4Voice::applyHardwareKnob(fmPatch, AudIO::FMKnobBank::Bank2_LevelsAndFeedback, 1, 0.1f, false);

    float fmRmsSum = 0.0f;
    for (int alg = 0; alg < 8; ++alg) {
        fmPatch.algorithm = static_cast<AudIO::FMAlgorithm>(alg);
        fmVoice.noteOn(60, 110, fmPatch);
        for (int s = 0; s < 512; ++s) {
            float y = fmVoice.renderSample(fmPatch, &slice);
            fmRmsSum += y * y;
        }
        fmVoice.noteOff();
    }
    assert(fmRmsSum > 0.1f && "FM4Voice must output non-zero audio across all 8 algorithms");

    // 3. Test Sample-Coupled Granular Voice reading zero-copy from SampleSliceView
    AudIO::GranularParams gParams;
    gParams.position    = 0.3f;
    gParams.grainSizeMs = 45.0f;
    gParams.densityHz   = 35.0f;
    gParams.pitchSpray  = 0.25f;

    AudIO::GranularVoice grainVoice;
    grainVoice.setSampleRate(48000.0f);
    grainVoice.triggerCloud(60, 115);

    float grainRmsSum = 0.0f;
    for (int s = 0; s < 4800; ++s) {
        float y = grainVoice.renderSample(gParams, slice);
        grainRmsSum += y * y;
    }
    assert(grainRmsSum > 0.01f && "GranularVoice must output non-zero audio from shared slice buffer");

    // 4. Test 16-Pad Scale & Chord Progression Engine + Modifiers + Arpeggiator
    Kick::ChordScaleEngine chordEngine;
    chordEngine.setScale(60, Kick::ScaleType::NaturalMinor); // C Minor
    Kick::ChordModifiers mods{true, false, 1, false};        // Add 7th/9th + 1st Inversion
    auto cm9Notes = chordEngine.triggerPad(0, mods);         // Pad 1 (i chord)
    assert(cm9Notes.size() == 5);

    Kick::ArpParams arpParams{Kick::ArpMode::UpDown, kPPQN / 4, 2, 0.7f};
    Kick::Arpeggiator arp;
    arp.setHeldNotes(cm9Notes, arpParams);
    for (int step = 0; step < 8; ++step) {
        auto arpNotes = arp.nextStepNotes(arpParams);
        assert(!arpNotes.empty());
    }

    // 5. Test 64-Step Sequencer 4-Knob Edit & Live 960 PPQN Quantized Pad Recording
    Kick::StepSequencerTrack seq;
    seq.setPatternLength(16);
    size_t recStep = seq.recordLivePadEvent(258, cm9Notes, 108, 2.0f, Kick::QuantizeMode::SmartHumanize_75Pct);
    assert(recStep == 1);
    assert(seq.getStep(1).enabled);

    seq.editStepWithFourKnobs(1, 12, 10, 1.5f, 1.0f, true);
    assert(seq.getStep(1).velocity == 118);
    assert(seq.getStep(1).ratchetCount == 2);

    std::cout << "[PASS] Phase 1 Custom Firmware Core verified! FM RMS="
              << std::sqrt(fmRmsSum / 4096.0f)
              << " | Granular RMS=" << std::sqrt(grainRmsSum / 4800.0f) << std::endl;
    return 0;
}

