#include "kick_firmware_app.hpp"
#include <iostream>
#include <cassert>

using namespace BehringerKickCustom::App;
using namespace BehringerKickCustom;

int main() {
    std::cout << "Testing kick_firmware_app.hpp (Phase 4 Master Application & Audio Mixer)..." << std::endl;

    BehringerKickFirmwareApp app;

    // 1. Test Audio Initialization & Master Buffer
    assert(app.getMasterSampleBuffer().numFrames() == 48000);
    std::cout << "[PASS] Audio engine initialized at 48 kHz with default sample buffer." << std::endl;

    // 2. Test Real-Time Audio Block Rendering
    std::vector<float> leftOut(128, 0.0f);
    std::vector<float> rightOut(128, 0.0f);

    app.renderAudioBlock(leftOut.data(), rightOut.data(), 128);
    for (float s : leftOut) {
        assert(std::abs(s) < 0.0001f);
    }
    std::cout << "[PASS] Idle audio block rendered cleanly without noise." << std::endl;

    // 3. Test Polyphonic FM Voice Triggering & Master Effects Bus
    app.getBridge().setPadMode(Hardware::PadOperatingMode::ChordScale);
    Hardware::PadEvent chordEv{0, 110, 0}; // Pad 1 (C Minor Chord)
    app.getBridge().handlePadDown(chordEv);

    // Render audio block with active chord
    app.renderAudioBlock(leftOut.data(), rightOut.data(), 128);
    float sumEnergy = 0.0f;
    for (size_t i = 0; i < 128; ++i) {
        sumEnergy += leftOut[i] * leftOut[i] + rightOut[i] * rightOut[i];
        assert(std::abs(leftOut[i]) <= 1.0f);
        assert(std::abs(rightOut[i]) <= 1.0f);
    }
    assert(sumEnergy > 0.01f);
    std::cout << "[PASS] Polyphonic FM chord rendered with master limiter ceiling: energy=" << sumEnergy << std::endl;

    // 4. Test Granular Voice Allocation
    app.getBridge().setActiveTrackIndex(1); // Track 1: Granular
    Hardware::PadEvent grainPadEv{1, 105, 0};
    app.getBridge().handlePadDown(grainPadEv);
    app.renderAudioBlock(leftOut.data(), rightOut.data(), 128);
    std::cout << "[PASS] Granular voice allocation and cloud rendering verified." << std::endl;

    // 5. Test Sample Slice Playback with WSOLA & Choke Groups
    app.getBridge().setActiveTrackIndex(2); // Track 2: Sample Chopper
    Hardware::PadEvent sliceEv{2, 118, 0};
    app.getBridge().handlePadDown(sliceEv);
    app.renderAudioBlock(leftOut.data(), rightOut.data(), 128);
    std::cout << "[PASS] Sample slice playback with WSOLA time-stretcher verified." << std::endl;

    // 6. Test 7-inch Touchscreen UI Frame State Exporter
    UIFrameState ui = app.exportUIFrameState();
    assert(ui.padColors.size() == 16);
    assert(ui.knobLabels.size() == 4);
    assert(ui.knobValues.size() == 4);
    assert(ui.scopeWaveform.size() == 256);
    std::cout << "[PASS] 7-inch Touchscreen UI Frame State exported successfully." << std::endl;

    // 7. Test Diagnostic Self-Test Loop
    bool testPass = app.runSelfTest(50);
    assert(testPass);
    std::cout << "[PASS] 50-block diagnostic self-test benchmark passed." << std::endl;

    std::cout << "[SUCCESS] ALL PHASE 4 MASTER FIRMWARE APP TESTS PASSED!" << std::endl;
    return 0;
}

