
#include "kick_sample_chopper.hpp"
#include <iostream>
#include <cassert>

using namespace BehringerKickCustom::AudIO;

int main() {
    std::cout << "Running comprehensive Phase 2 Chopper & WSOLA unit tests..." << std::endl;

    // 1. Test SampleBuffer and Zero Crossing
    SampleBuffer buf(48000, 48000, 1);
    for (size_t i = 0; i < 48000; ++i) {
        buf.data()[i] = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    }
    size_t zc = buf.findNearestZeroCrossing(100, 50);
    assert(zc > 0);
    std::cout << "[PASS] Zero crossing found at frame: " << zc << std::endl;

    // 2. Test Grid Slicing
    SampleChopper chopper;
    chopper.loadBuffer(buf);
    chopper.sliceToGrid(16);
    assert(chopper.getSlice(0).startFrame == 0);
    assert(chopper.getSlice(15).endFrame == 48000);
    std::cout << "[PASS] Grid slicing: 16 slices configured successfully." << std::endl;

    // 3. Test Lazy Chopping
    chopper.registerLazyChopMarker(0, 0);
    chopper.registerLazyChopMarker(1, 12000);
    chopper.registerLazyChopMarker(2, 24000);
    assert(chopper.getSlice(0).endFrame == chopper.getSlice(1).startFrame);
    assert(chopper.getSlice(1).endFrame == chopper.getSlice(2).startFrame);
    std::cout << "[PASS] Lazy pad chopping boundary alignment verified." << std::endl;

    // 4. Test Transient Detector
    TransientDetector detector;
    auto onsets = detector.detectOnsets(buf);
    std::cout << "[PASS] Transient detection found " << onsets.size() << " transient onset(s)." << std::endl;
    assert(onsets.size() >= 1);

    // 5. Test Hardware Knobs
    chopper.applyHardwareKnobs(1, ChopperKnobBank::Bank1_TrimAndStretch, 2, 0.5f); // Stretch
    assert(chopper.getSlice(1).timeStretchRatio > 1.0f);
    chopper.applyHardwareKnobs(1, ChopperKnobBank::Bank1_TrimAndStretch, 3, 0.25f); // Pitch
    std::cout << "[PASS] Hardware knob control verified: timeStretch=" << chopper.getSlice(1).timeStretchRatio << " pitch=" << chopper.getSlice(1).pitchSemitones << std::endl;

    // 6. Test Zero-Copy Slice Export to Granular/FM
    auto sliceView = chopper.exportToSliceView(1);
    assert(sliceView.data == chopper.getBuffer().data());
    assert(sliceView.sliceStart == chopper.getSlice(1).startFrame);
    assert(sliceView.sliceEnd == chopper.getSlice(1).endFrame);
    std::cout << "[PASS] Zero-copy buffer export to AudIO::SampleSliceView verified." << std::endl;

    // 7. Test WSOLA Time Stretcher Block Processing
    WSOLATimeStretcher stretcher;
    stretcher.reset();
    std::vector<float> leftOut(512, 0.0f);
    std::vector<float> rightOut(512, 0.0f);
    stretcher.processSliceBlock(buf, chopper.getSlice(1).startFrame, chopper.getSlice(1).endFrame, 1.25f, leftOut.data(), rightOut.data(), 512);
    std::cout << "[PASS] WSOLA block processed 512 frames successfully." << std::endl;

    std::cout << "[SUCCESS] ALL PHASE 2 C++ CHOPPER, TRANSIENT, WSOLA & KNOB TESTS PASSED!" << std::endl;
    return 0;
}

