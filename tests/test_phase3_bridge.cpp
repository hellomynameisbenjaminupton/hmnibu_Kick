
#include "kick_hardware_bridge.hpp"
#include <iostream>
#include <cassert>

using namespace BehringerKickCustom::Hardware;
using namespace BehringerKickCustom;

int main() {
    std::cout << "Testing kick_hardware_bridge.hpp (Phase 3 ComProtocol Bridge)..." << std::endl;

    KickHardwareBridge bridge;

    // 1. Test 16-Pad Chord Mode & LED Matrix
    bridge.setPadMode(PadOperatingMode::ChordScale);
    assert(bridge.getPadLeds()[0].r == RGBColor::Green().r); // Pad 1: Diatonic degree
    assert(bridge.getPadLeds()[8].r == RGBColor::Amber().r); // Pad 9: Borrowed chord
    assert(bridge.getPadLeds()[12].r == RGBColor::Cyan().r); // Pad 13: 7th/9th modifier

    // Tap Pad 1 (Diatonic Root Chord)
    PadEvent padEv{0, 115, 0};
    bridge.handlePadDown(padEv);
    assert(bridge.getPadLeds()[0].r == RGBColor::FullWhite().r); // Pressed pad glow

    // Toggle Pad 13 (7th/9th modifier)
    PadEvent modEv{12, 120, 0};
    bridge.handlePadDown(modEv);
    assert(bridge.getChordMods().add7th9th == true);
    std::cout << "[PASS] 16-Pad Chord Scale & Modifiers tested successfully." << std::endl;

    // 2. Test Polyphonic Aftertouch Modulation
    PadEvent pressureEv{0, 110, 127}; // Max aftertouch pressure
    bridge.handlePadPressure(pressureEv);
    assert(bridge.getFMPatch().filterCutoffHz > 14000.0f);
    std::cout << "[PASS] Polyphonic aftertouch pressure modulation verified." << std::endl;

    // 3. Test 4 Optical Encoders Contextual Routing in FM View
    bridge.setScreenView(ScreenView::View_FM4Synth);
    KnobEvent knob1Ev{0, 0.5f, false};
    bridge.handleKnobTurn(knob1Ev);
    assert(bridge.getFMPatch().ops[0].ratio > 1.0f);

    // Push-Turn Secondary function (Fine detune)
    KnobEvent knobFineEv{0, 0.2f, true};
    bridge.handleKnobTurn(knobFineEv);
    assert(bridge.getFMPatch().ops[0].fineDetune != 0.0f);
    std::cout << "[PASS] Contextual 4-Knob encoder turn & push-turn verified." << std::endl;

    // 4. Test Jog Wheel Algorithm Cycling
    int initialAlg = static_cast<int>(bridge.getFMPatch().algorithm);
    JogEvent jogEv{1, false};
    bridge.handleJogWheel(jogEv);
    int newAlg = static_cast<int>(bridge.getFMPatch().algorithm);
    assert(newAlg == (initialAlg + 1) % 8);
    std::cout << "[PASS] Optical Jog Wheel algorithm switching verified." << std::endl;

    // 5. Test Touch Strip Scene A/B Morphing
    TouchStripEvent stripEv{true, 0.5f, 0.8f}; // Center position (50% morph)
    bridge.handleTouchStrip(stripEv);
    assert(bridge.getFMPatch().filterCutoffHz > 8000.0f);
    std::cout << "[PASS] Touch strip Scene A/B macro morphing verified." << std::endl;

    // 6. Test AI Gesture Camera Modulation
    AIGestureEvent aiEv{true, 0.9f, 0.8f, 0.75f};
    bridge.handleAIGesture(aiEv);
    assert(bridge.getFMPatch().drive == 0.75f);
    assert(bridge.getGrainParams().grainSizeMs > 200.0f);
    std::cout << "[PASS] AI Gesture camera 3D spatial modulation verified." << std::endl;

    // 7. Test Elektron-Style Parameter Lock (P-Lock) on Held Step
    bridge.setPadMode(PadOperatingMode::StepSequencer);
    PadEvent holdStepEv{3, 100, 0}; // Hold Step Pad 4 (index 3)
    bridge.handlePadDown(holdStepEv);
    KnobEvent plockKnobEv{0, 0.5f, false}; // Turn Knob 1 while holding Step Pad 4
    bridge.handleKnobTurn(plockKnobEv);

    const auto& lockedStep = bridge.getTrack(0).getStep(3);
    assert(lockedStep.enabled);
    assert(lockedStep.plocks.hasFilterCutoffLock);
    bridge.handlePadUp(holdStepEv); // Release held step
    std::cout << "[PASS] Elektron-style step parameter locking verified." << std::endl;

    // 8. Test Master Clock & Step Sequencer Advance (960 PPQN)
    bridge.setPlaying(true);
    // Advance clock by 240 ticks (1/16th note step)
    for (int t = 0; t <= 240; ++t) {
        bridge.advanceClockTick();
    }
    // Pad LED on step 1 (index 1) should now show the running playhead (Gold)
    assert(bridge.getPadLeds()[1].r == RGBColor::Gold().r);
    std::cout << "[PASS] Master 960 PPQN clock tick & step progression verified." << std::endl;

    std::cout << "[SUCCESS] ALL PHASE 3 HARDWARE BRIDGE TESTS PASSED!" << std::endl;
    return 0;
}

