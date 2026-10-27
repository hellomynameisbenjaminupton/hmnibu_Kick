/**
 * @file kick_sample_chopper.hpp
 * @brief Phase 2 Sample Chopper, Trimmer, Transient Detector & Time-Stretcher for Behringer KICK (BE 0700-AAM)
 * @license AGPL-3.0-or-later (compatible with MusicTribeOpenSource/0700-AAM)
 *
 * Extends `AudIO/` with:
 *   1. AudIO::SampleBuffer       - PCM audio buffer container with normalization, reverse, and zero-crossing search.
 *   2. AudIO::SampleSlice        - Non-destructive per-slice parameters (start/end, loop, pitch, reverse, choke group).
 *   3. AudIO::TransientDetector  - Energy-flux and peak-picking onset detector for automatic transient slicing.
 *   4. AudIO::SampleChopper      - Auto-chop, beat-grid slice, lazy pad chopping, and slice-to-Granular bridge.
 *   5. AudIO::WSOLATimeStretcher - Real-time Waveform Similarity Overlap-Add time-stretching (tempo change without pitch shift).
 */

#pragma once

#include "kick_custom_firmware_core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace BehringerKickCustom {
namespace AudIO {

// ============================================================================
// SECTION 1: SAMPLE BUFFER CONTAINER (With Normalization & Zero-Crossing Snap)
// ============================================================================

class SampleBuffer {
public:
    SampleBuffer() = default;

    SampleBuffer(size_t numFrames, uint32_t sampleRate = 48000, uint8_t numChannels = 1)
        : sampleRate_(sampleRate), channels_(std::max<uint8_t>(1, std::min<uint8_t>(2, numChannels))) {
        data_.resize(numFrames * channels_, 0.0f);
    }

    void resize(size_t numFrames, uint32_t sampleRate = 48000, uint8_t numChannels = 1) {
        sampleRate_ = sampleRate;
        channels_   = std::max<uint8_t>(1, std::min<uint8_t>(2, numChannels));
        data_.assign(numFrames * channels_, 0.0f);
    }

    size_t   numFrames()  const { return channels_ > 0 ? (data_.size() / channels_) : 0; }
    uint32_t sampleRate() const { return sampleRate_; }
    uint8_t  channels()   const { return channels_; }
    float*   data()             { return data_.data(); }
    const float* data()   const { return data_.data(); }

    float getSample(size_t frame, uint8_t channel = 0) const {
        if (frame >= numFrames()) return 0.0f;
        uint8_t ch = std::min(channel, static_cast<uint8_t>(channels_ - 1));
        return data_[frame * channels_ + ch];
    }

    void setSample(size_t frame, uint8_t channel, float value) {
        if (frame >= numFrames()) return;
        uint8_t ch = std::min(channel, static_cast<uint8_t>(channels_ - 1));
        data_[frame * channels_ + ch] = value;
    }

    // Normalizes audio buffer to peak 0.0 dBFS (or specified target linear gain)
    void normalize(float targetPeakLinear = 0.98f) {
        if (data_.empty()) return;
        float maxPeak = 0.0f;
        for (float s : data_) {
            float absVal = std::abs(s);
            if (absVal > maxPeak) maxPeak = absVal;
        }
        if (maxPeak > 0.0001f) {
            float gainFactor = targetPeakLinear / maxPeak;
            for (float& s : data_) {
                s *= gainFactor;
            }
        }
    }

    // Finds the nearest zero-crossing point around a given target frame within a window
    size_t findNearestZeroCrossing(size_t targetFrame, size_t searchRadiusFrames = 256) const {
        size_t total = numFrames();
        if (total < 2 || targetFrame >= total) return targetFrame;

        size_t start = (targetFrame > searchRadiusFrames) ? (targetFrame - searchRadiusFrames) : 0;
        size_t end   = std::min(total - 1, targetFrame + searchRadiusFrames);

        size_t bestFrame = targetFrame;
        float  bestDiff  = 1000.0f;

        for (size_t f = start; f < end; ++f) {
            float s0 = getSample(f, 0);
            float s1 = getSample(f + 1, 0);
            // Crossing occurs if sign changes (product <= 0)
            if (s0 * s1 <= 0.0f) {
                float distFromTarget = static_cast<float>(std::abs(static_cast<int>(f) - static_cast<int>(targetFrame)));
                if (distFromTarget < bestDiff) {
                    bestDiff  = distFromTarget;
                    bestFrame = (std::abs(s0) < std::abs(s1)) ? f : (f + 1);
                }
            }
        }
        return bestFrame;
    }

private:
    std::vector<float> data_;
    uint32_t sampleRate_ = 48000;
    uint8_t  channels_   = 1;
};

// ============================================================================
// SECTION 2: SLICE PARAMETERS & METADATA (MPC & Digitakt-Inspired)
// ============================================================================

enum class PlaybackDirection : uint8_t {
    Forward = 0,
    Reverse
};

struct SampleSlice {
    size_t   startFrame      = 0;
    size_t   endFrame        = 0;
    float    pitchSemitones  = 0.0f;  // -24.0 to +24.0 semitones
    float    fineCents       = 0.0f;  // -50.0 to +50.0 cents
    float    gainLinear      = 1.0f;  // 0.0 to 2.0 (volume trim)
    float    pan             = 0.0f;  // -1.0 (Left) to +1.0 (Right)
    bool     loopEnabled     = false;
    float    loopCrossfade   = 0.05f; // Crossfade percentage to eliminate clicks
    PlaybackDirection direction = PlaybackDirection::Forward;
    uint8_t  chokeGroup      = 0;     // 0 = no choke; 1..8 = voice mute groups
    float    timeStretchRatio = 1.0f; // 0.5x (double speed) to 2.0x (half speed)
};

// ============================================================================
// SECTION 3: AUTOMATIC TRANSIENT DETECTOR (Energy Flux & Peak Picking)
// ============================================================================

struct TransientSettings {
    size_t windowSize       = 256;   // Short-time RMS window (~5.3 ms at 48 kHz)
    size_t hopSize          = 128;   // Analysis hop size (~2.7 ms)
    float  threshold        = 1.65f; // Relative threshold ratio above local moving average
    size_t minSliceInterval = 2400;  // Minimum distance between transients (50 ms at 48 kHz)
};

class TransientDetector {
public:
    static std::vector<size_t> detectOnsets(const SampleBuffer& buffer, const TransientSettings& cfg) {
        std::vector<size_t> onsets;
        size_t totalFrames = buffer.numFrames();
        if (totalFrames < cfg.windowSize * 2) return {0};

        // 1. Calculate Short-Time Energy curve
        size_t numHops = (totalFrames - cfg.windowSize) / cfg.hopSize;
        std::vector<float> energy(numHops, 0.0f);
        for (size_t h = 0; h < numHops; ++h) {
            size_t start = h * cfg.hopSize;
            float sumSq = 0.0f;
            for (size_t i = 0; i < cfg.windowSize; ++i) {
                float val = buffer.getSample(start + i, 0);
                sumSq += val * val;
            }
            energy[h] = std::sqrt(sumSq / static_cast<float>(cfg.windowSize));
        }

        // 2. High-frequency Spectral / Energy Flux (difference of positive energy jumps)
        std::vector<float> flux(numHops, 0.0f);
        for (size_t h = 1; h < numHops; ++h) {
            float diff = energy[h] - energy[h - 1];
            flux[h] = (diff > 0.0f) ? diff : 0.0f;
        }

        // 3. Peak-picking with adaptive moving threshold
        constexpr size_t kAvgRadius = 8;
        size_t lastOnsetFrame = 0;
        onsets.push_back(0); // Always include start of sample as first slice

        for (size_t h = kAvgRadius; h < numHops - kAvgRadius; ++h) {
            float localSum = 0.0f;
            for (size_t r = h - kAvgRadius; r <= h + kAvgRadius; ++r) {
                localSum += flux[r];
            }
            float localMean = localSum / static_cast<float>(2 * kAvgRadius + 1);
            float threshVal = localMean * cfg.threshold + 0.005f;

            if (flux[h] > threshVal && flux[h] > flux[h - 1] && flux[h] >= flux[h + 1]) {
                size_t estimatedFrame = h * cfg.hopSize;
                // Snap to zero-crossing
                size_t snapped = buffer.findNearestZeroCrossing(estimatedFrame, 256);
                if (snapped > lastOnsetFrame + cfg.minSliceInterval && snapped + 1024 < totalFrames) {
                    onsets.push_back(snapped);
                    lastOnsetFrame = snapped;
                }
            }
        }
        return onsets;
    }

    static std::vector<size_t> detectOnsets(const SampleBuffer& buffer) {
        TransientSettings defaultCfg;
        return detectOnsets(buffer, defaultCfg);
    }
};

// ============================================================================
// SECTION 4: REAL-TIME WSOLA TIME-STRETCHER (Waveform Similarity Overlap-Add)
// ============================================================================
// Allows independent time-stretching (0.5x to 2.0x playback speed) without
// altering pitch, preserving transient phase alignment.

class WSOLATimeStretcher {
public:
    static constexpr size_t kWindowSize = 1024; // ~21.3 ms at 48 kHz
    static constexpr size_t kHopOutput  = 256;  // Output hop (75% overlap)

    void reset() {
        inPosition_     = 0.0;
        outputCursor_   = 0;
        accumBuffer_.assign(kWindowSize * 2, 0.0f);
        weightBuffer_.assign(kWindowSize * 2, 0.0f);
    }

    // Process a block of audio stretching or compressing by timeStretchRatio
    // ratio = 1.0 (normal), ratio = 0.5 (double tempo), ratio = 2.0 (half tempo)
    void processSliceBlock(const SampleBuffer& src, size_t sliceStart, size_t sliceEnd,
                           float timeStretchRatio, float* outLeft, float* outRight, size_t numFrames) {
        timeStretchRatio = clampf(timeStretchRatio, 0.35f, 3.0f);
        size_t sliceLen = (sliceEnd > sliceStart) ? (sliceEnd - sliceStart) : 0;
        if (sliceLen < kWindowSize) {
            // Passthrough if slice is shorter than WSOLA window
            for (size_t i = 0; i < numFrames; ++i) {
                size_t frame = sliceStart + (static_cast<size_t>(inPosition_) % std::max<size_t>(1, sliceLen));
                outLeft[i]  = src.getSample(frame, 0);
                outRight[i] = (src.channels() > 1) ? src.getSample(frame, 1) : outLeft[i];
                inPosition_ += timeStretchRatio;
            }
            return;
        }

        double inHop = static_cast<double>(kHopOutput) * timeStretchRatio;

        for (size_t outIdx = 0; outIdx < numFrames; ++outIdx) {
            // If output buffer needs another synthesis grain
            if (outputCursor_ >= kHopOutput) {
                overlapAddGrain(src, sliceStart, sliceLen);
                outputCursor_ = 0;
                inPosition_ += inHop;
                if (inPosition_ >= static_cast<double>(sliceLen - kWindowSize)) {
                    inPosition_ = 0.0; // Loop within slice
                }
            }

            float sampleVal = 0.0f;
            if (weightBuffer_[0] > 0.0001f) {
                sampleVal = accumBuffer_[0] / weightBuffer_[0];
            }
            outLeft[outIdx]  = sampleVal;
            outRight[outIdx] = sampleVal;

            // Shift internal circular buffers by 1 sample
            std::copy(accumBuffer_.begin() + 1, accumBuffer_.end(), accumBuffer_.begin());
            accumBuffer_.back() = 0.0f;
            std::copy(weightBuffer_.begin() + 1, weightBuffer_.end(), weightBuffer_.begin());
            weightBuffer_.back() = 0.0f;

            ++outputCursor_;
        }
    }

private:
    void overlapAddGrain(const SampleBuffer& src, size_t sliceStart, size_t sliceLen) {
        size_t nominalIn = sliceStart + static_cast<size_t>(inPosition_);
        nominalIn = std::min(nominalIn, sliceStart + sliceLen - kWindowSize);

        // Best match search around nominalIn using Cross-Correlation
        size_t bestIn = nominalIn;
        constexpr size_t kSearchRadius = 64;
        if (nominalIn > sliceStart + kSearchRadius && nominalIn + kWindowSize + kSearchRadius < sliceStart + sliceLen) {
            float maxCorr = -1e9f;
            for (int delta = -static_cast<int>(kSearchRadius); delta <= static_cast<int>(kSearchRadius); delta += 4) {
                size_t candidate = static_cast<size_t>(static_cast<int>(nominalIn) + delta);
                float corr = 0.0f;
                for (size_t k = 0; k < 128; k += 4) {
                    corr += src.getSample(candidate + k, 0) * accumBuffer_[k];
                }
                if (corr > maxCorr) {
                    maxCorr = corr;
                    bestIn  = candidate;
                }
            }
        }

        // Overlap-add windowed grain (Hann window)
        for (size_t i = 0; i < kWindowSize; ++i) {
            float win = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(kWindowSize)));
            float s   = src.getSample(bestIn + i, 0) * win;
            accumBuffer_[i]  += s;
            weightBuffer_[i] += win;
        }
    }

    double inPosition_   = 0.0;
    size_t outputCursor_ = kHopOutput;
    std::vector<float> accumBuffer_{std::vector<float>(kWindowSize * 2, 0.0f)};
    std::vector<float> weightBuffer_{std::vector<float>(kWindowSize * 2, 0.0f)};
};

// ============================================================================
// SECTION 5: SAMPLE CHOPPER MANAGER & HARDWARE KNOB ROUTER
// ============================================================================

enum class ChopMode : uint8_t {
    AutoTransient = 0,
    BeatGrid,
    LazyPadChop,
    ManualTrim
};

enum class ChopperKnobBank : uint8_t {
    Bank1_TrimAndStretch = 0, // Knob 1: Start Trim, Knob 2: End Trim, Knob 3: Stretch, Knob 4: Pitch
    Bank2_MixAndChoke         // Knob 1: Gain, Knob 2: Pan, Knob 3: Choke Group, Knob 4: Loop XFade
};

class SampleChopper {
public:
    static constexpr size_t kMaxSlices = 16; // Standard 16 RGB pads per bank

    SampleChopper() {
        slices_.resize(kMaxSlices);
    }

    void loadBuffer(const SampleBuffer& buffer) {
        buffer_ = buffer;
        sliceToGrid(16); // Default 16 equal grid slices
    }

    SampleBuffer&       getBuffer()       { return buffer_; }
    const SampleBuffer& getBuffer() const { return buffer_; }

    // Mode 1: Auto-Transient Chop (detects drum hits, onsets, or acoustic transients)
    size_t autoChopTransients(float sensitivity = 1.65f) {
        TransientSettings cfg;
        cfg.threshold = sensitivity;
        auto onsets = TransientDetector::detectOnsets(buffer_, cfg);

        size_t count = std::min(kMaxSlices, onsets.size());
        for (size_t i = 0; i < count; ++i) {
            slices_[i].startFrame = onsets[i];
            slices_[i].endFrame   = (i + 1 < onsets.size()) ? onsets[i + 1] : buffer_.numFrames();
            slices_[i].direction  = PlaybackDirection::Forward;
            slices_[i].timeStretchRatio = 1.0f;
            slices_[i].pitchSemitones   = 0.0f;
        }
        // Fill remaining slices if onsets < 16
        for (size_t i = count; i < kMaxSlices; ++i) {
            slices_[i] = slices_[count > 0 ? (count - 1) : 0];
        }
        return count;
    }

    // Mode 2: Beat-Grid Slicing (Divides into 4, 8, or 16 musical parts with zero-crossing snap)
    void sliceToGrid(size_t numSlices = 16) {
        size_t count = std::min(kMaxSlices, std::max<size_t>(1, numSlices));
        size_t total = buffer_.numFrames();
        size_t step  = total / count;

        for (size_t i = 0; i < kMaxSlices; ++i) {
            if (i < count) {
                size_t rawStart = i * step;
                size_t rawEnd   = (i + 1 == count) ? total : ((i + 1) * step);
                slices_[i].startFrame = buffer_.findNearestZeroCrossing(rawStart, 256);
                slices_[i].endFrame   = buffer_.findNearestZeroCrossing(rawEnd, 256);
            } else {
                slices_[i] = slices_[count - 1];
            }
            slices_[i].direction        = PlaybackDirection::Forward;
            slices_[i].timeStretchRatio = 1.0f;
            slices_[i].pitchSemitones   = 0.0f;
        }
    }

    // Mode 3: Lazy Chopping (Tap pads live while audio plays to set on-the-fly slice markers)
    void registerLazyChopMarker(uint8_t padIndex, size_t currentPlaybackFrame) {
        if (padIndex >= kMaxSlices) return;
        size_t snapped = buffer_.findNearestZeroCrossing(currentPlaybackFrame, 256);

        slices_[padIndex].startFrame = snapped;
        slices_[padIndex].endFrame   = buffer_.numFrames();

        // Close previous slice end boundary to match new slice's start
        if (padIndex > 0) {
            slices_[padIndex - 1].endFrame = snapped;
        }
    }

    // Direct 4-Knob Hardware Router (Bank A: Trim/Stretch, Bank B: Mix/Choke)
    void applyHardwareKnobs(size_t sliceIndex, ChopperKnobBank bank, int knobIdx, float delta, bool pushHeld = false) {
        if (sliceIndex >= kMaxSlices) return;
        auto& s = slices_[sliceIndex];
        size_t total = buffer_.numFrames();
        if (total == 0) return;

        if (bank == ChopperKnobBank::Bank1_TrimAndStretch) {
            if (knobIdx == 0) {
                // Knob 1: Start Trim
                int64_t change = static_cast<int64_t>(delta * static_cast<float>(total) * 0.1f);
                int64_t newStart = static_cast<int64_t>(s.startFrame) + change;
                newStart = std::max<int64_t>(0, std::min<int64_t>(static_cast<int64_t>(s.endFrame) - 256, newStart));
                s.startFrame = pushHeld ? buffer_.findNearestZeroCrossing(static_cast<size_t>(newStart), 256)
                                        : static_cast<size_t>(newStart);
            } else if (knobIdx == 1) {
                // Knob 2: End Trim
                int64_t change = static_cast<int64_t>(delta * static_cast<float>(total) * 0.1f);
                int64_t newEnd = static_cast<int64_t>(s.endFrame) + change;
                newEnd = std::max<int64_t>(static_cast<int64_t>(s.startFrame) + 256, std::min<int64_t>(static_cast<int64_t>(total), newEnd));
                s.endFrame = pushHeld ? buffer_.findNearestZeroCrossing(static_cast<size_t>(newEnd), 256)
                                      : static_cast<size_t>(newEnd);
            } else if (knobIdx == 2) {
                // Knob 3: WSOLA Time-Stretch Ratio (0.5x = double speed, 2.0x = half speed)
                s.timeStretchRatio = clampf(s.timeStretchRatio + delta * 0.5f, 0.5f, 2.0f);
            } else if (knobIdx == 3) {
                // Knob 4: Pitch Semitones & Detune
                if (pushHeld) {
                    s.fineCents = clampf(s.fineCents + delta * 50.0f, -50.0f, 50.0f);
                } else {
                    s.pitchSemitones = clampf(s.pitchSemitones + std::round(delta * 12.0f), -24.0f, 24.0f);
                }
            }
        } else {
            // Bank B: Mix, Pan, Choke, Loop
            if (knobIdx == 0) s.gainLinear = clampf(s.gainLinear + delta, 0.0f, 2.0f);
            if (knobIdx == 1) s.pan = clampf(s.pan + delta, -1.0f, 1.0f);
            if (knobIdx == 2) {
                int ch = static_cast<int>(s.chokeGroup) + (delta > 0.0f ? 1 : -1);
                s.chokeGroup = static_cast<uint8_t>(std::max(0, std::min(8, ch)));
            }
            if (knobIdx == 3) s.loopCrossfade = clampf(s.loopCrossfade + delta * 0.2f, 0.01f, 0.25f);
        }
    }

    // Creates an instant zero-copy SampleSliceView suitable for AudIO::GranularVoice or AudIO::FM4Voice
    SampleSliceView exportToSliceView(size_t sliceIndex) const {
        if (sliceIndex >= kMaxSlices || buffer_.numFrames() == 0) return {};
        const auto& s = slices_[sliceIndex];
        SampleSliceView view;
        view.data          = buffer_.data();
        view.numFrames     = buffer_.numFrames();
        view.sampleRate    = buffer_.sampleRate();
        view.sliceStart    = s.startFrame;
        view.sliceEnd      = s.endFrame;
        view.loopEnabled   = s.loopEnabled;
        view.loopCrossfade = s.loopCrossfade;
        return view;
    }

    const SampleSlice& getSlice(size_t idx) const { return slices_[idx % kMaxSlices]; }
    SampleSlice&       getSliceMutable(size_t idx) { return slices_[idx % kMaxSlices]; }

private:
    SampleBuffer              buffer_;
    std::vector<SampleSlice>  slices_;
};

} // namespace AudIO
} // namespace BehringerKickCustom

