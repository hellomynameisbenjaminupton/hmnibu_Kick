# Behringer KICK (BE 0700-AAM) Custom Firmware Extension

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-red.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![Upstream](https://img.shields.io/badge/Upstream-MusicTribeOpenSource%2F0700--AAM-orange.svg)](https://github.com/MusicTribeOpenSource/0700-AAM)

Open-source high-performance C++17 custom firmware extension for the **Behringer KICK (`BE 0700-AAM`)** standalone sampling workstation and groovebox, extending the upstream [MusicTribeOpenSource/0700-AAM](https://github.com/MusicTribeOpenSource/0700-AAM) codebase.

---

## Architecture & Completed Modules

This firmware extension eliminates menu diving by mapping synthesis, sampling, chord generation, and sequencing directly across the hardware's 4 endless rotary encoders, 16 RGB velocity/aftertouch pads, capacitive touch strip, and jog wheel.

```
                          BEHRINGER KICK HARDWARE (0700-AAM)
         [ 16 RGB Pads ]   [ 4 Encoders ]   [ Touch Strip ]   [ VL53L5CX AI Cam ]
               │                 │                 │                  │
               └─────────────────┴────────┬────────┴──────────────────┘
                                          ▼
                      Kick::KickHardwareBridge (Phase 3)
                      ├─ ComProtocol Event Demux & MIDI Router
                      ├─ Elektron-style Parameter Lock (P-Lock) Latch
                      └─ Capacitive Strip Scene A/B Morph Engine
                                          │
            ┌─────────────────────────────┼─────────────────────────────┐
            ▼                             ▼                             ▼
    AudIO::FM4Voice             AudIO::GranularVoice          AudIO::SampleChopper
       (Phase 1)                     (Phase 1)                     (Phase 2)
  4-Op, 8-Algorithm FM        Sample-Coupled Granular       Auto-Transient Detection
  Sample-FM Phase Mod         Direct RAM Slice Streaming    WSOLA Time-Stretching
  Continuous Waveform Morph   Touch Strip Grain Scrub       Choke Groups & Lazy Chop
            │                             │                             │
            └─────────────────────────────┼─────────────────────────────┘
                                          ▼
                      Kick::BehringerKickFirmwareApp (Phase 4)
                      ├─ 8-Voice FM / 4-Voice Granular / 8-Voice Chop Voice Pools
                      ├─ Master Effects Bus (SVF Filter, Saturation, Lookahead Limiter)
                      ├─ Real-Time 48 kHz Stereo Block Audio Callback
                      └─ 7" Framebuffer UI State Telemetry Exporter
```

---

### Phase 1: Core Sound Engines & Sequencing
Located in `AudIO/CustomEngines/kick_custom_firmware_core.hpp`:
- **`AudIO::FM4Voice`**: 4-Operator, 8-Algorithm FM synthesis engine with Operator 4 self-feedback, continuous waveform morphing (sine, triangle, saw, pulse), Sample-FM transient phase modulation, and dedicated 4-knob hardware bank mapping.
- **`AudIO::GranularVoice`**: Zero-copy sample-coupled granular synth streaming directly from shared RAM sample/chop buffers (`AudIO::SampleSliceView`) with window shape morphing, grain density, spray, and live capacitive touch-strip scrubbing.
- **`Kick::ChordScaleEngine`**: 16-Pad Scale & Chord Progression engine. Pads 1–12 trigger diatonic and borrowed chords within selected scale; Pads 13–16 apply real-time modifiers (7th/9th extensions, Sus2/Sus4, voice-leading inversions, and arpeggiator latch/strum).
- **`Kick::Arpeggiator`**: 4-Knob arpeggiator supporting `Up`, `Down`, `UpDown`, `Converge`, `PinkyThumbPedal`, and `ChordRepeat` modes with octave span, gate length, swing, and rate division.
- **`Kick::StepSequencerTrack`**: 64-step polyphonic sequencer with 4-knob step editing (pitch, velocity, gate length, microtiming/ratchet), per-step Parameter Locks (P-Locks), and live 960 PPQN pad recording with Input Quantize.

### Phase 2: Sample Chopper, Trimmer & WSOLA Time-Stretching
Located in `AudIO/CustomEngines/kick_sample_chopper.hpp`:
- **`AudIO::SampleBuffer`**: High-precision 32-bit floating point stereo sample buffer management with peak normalization, gain trimming, and zero-crossing detection.
- **`AudIO::TransientDetector`**: Short-Time Energy (STE) and high-frequency Spectral Flux onset detection for automatic, click-free transient slicing.
- **Grid & Lazy Slicing**: Musical beat-grid slicing (1/4, 1/8, 1/16, 1/32) and real-time manual pad tapping ("lazy chop") mode.
- **`AudIO::WSOLATimeStretcher`**: Waveform Similarity Overlap-Add time-stretching algorithm enabling independent tempo adjustments (0.5x to 2.0x) without altering sample pitch or introducing metallic phase artifacts.

### Phase 3: Hardware/UI Bridge & Event Routing
Located in `Kick/CustomSequencer/kick_hardware_bridge.hpp`:
- **`ComProtocol` Peripheral Demuxer**: Direct event decoding for 16 velocity/polyphonic aftertouch pads, 4 endless rotary encoders, jog wheel, and front-panel transport buttons.
- **Capacitive Touch Strip Scene Morphing**: Bi-directional crossfading between Scene A and Scene B parameter snapshots across all active engines.
- **Elektron-style Parameter Locks**: Holding any step button while turning an encoder latches custom parameters (FM ratios, filter cutoff, sample pitch) directly to that individual step.
- **VL53L5CX Optical / AI Camera Integration**: Maps vertical hand distance and gestures to filter cutoffs, granular grain density, and master send levels.
- **Dynamic RGB LED Matrix**: Real-time visual feedback across all 16 pads for scale root notes, chord extensions, sequencer step playheads, and sample slice triggers.

### Phase 4: Master Real-Time Audio Mixer, FX Bus & UI Exporter
Located in `Kick/CustomSequencer/kick_firmware_app.hpp`:
- **`Kick::BehringerKickFirmwareApp`**: Unified application coordinator executing the high-priority real-time 48 kHz stereo audio callback (`renderAudioBlock`).
- **Polyphonic Voice Pools**:
  - 8-Voice Polyphonic FM Synthesis Pool with dynamic voice stealing.
  - 4-Voice Polyphonic Granular Synth Pool with real-time grain streaming.
  - 8-Voice Sample Chopper Pool with 4 mutually exclusive choke groups (e.g. open/closed hi-hat cutoffs).
- **Master Effects Bus**:
  - 2-pole State Variable Filter (SVF) with smooth 20 Hz – 20 kHz cutoff and resonance.
  - Analog-modeled soft saturation drive (`std::tanh`).
  - 1 ms lookahead peak safety limiter preventing output clipping.
- **7" Framebuffer UI Telemetry Exporter (`UIFrameState`)**:
  - High-speed zero-copy telemetry serialization exporting live waveforms, slice markers, active pad triggers, and encoder readout strings for the 1024×600 multi-touch display.

---

## In-Browser Hardware Simulator

Test and play the complete custom firmware before physical hardware release:
- **File**: `behringer_kick_virtual_sampler.html`
- **Features**: Complete WebAudio implementation of the 4-Op FM engine, Granular engine, Sample Chopper with drag-and-drop audio loading, 16 RGB playable pads, 4 interactive Q-Link knobs, capacitive touch strip, and live 64-step polyphonic sequencer.
- **How to Run**: Simply open `behringer_kick_virtual_sampler.html` in any modern web browser (Chrome, Edge, Firefox, Safari).

---

## Repository Directory Structure

```
.
├── AudIO/
│   └── CustomEngines/
│       ├── kick_custom_firmware_core.hpp   # Phase 1: FM4, Granular, Chord & Arp
│       └── kick_sample_chopper.hpp         # Phase 2: Transient Slicing & WSOLA
├── Kick/
│   └── CustomSequencer/
│       ├── kick_hardware_bridge.hpp        # Phase 3: ComProtocol & P-Lock Bridge
│       └── kick_firmware_app.hpp           # Phase 4: Master Mixer, Voice Pools & FX
├── docs/
│   └── CUSTOM_FIRMWARE_PLAN.md             # Complete Architecture Specification
├── tests/
│   ├── test_firmware_core.cpp              # Phase 1 verification suite
│   ├── test_phase2_chopper.cpp             # Phase 2 verification suite
│   ├── test_phase3_bridge.cpp              # Phase 3 verification suite
│   └── test_phase4_app.cpp                 # Phase 4 verification suite
├── behringer_kick_virtual_sampler.html     # In-browser virtual hardware simulator
├── Makefile                                # Unified C++17 build & test runner
└── README.md                               # Project documentation
```

---

## Quick Start in WSL2 (Ubuntu)

### 1. Prerequisites
Ensure standard C++17 build tools are installed:
```bash
sudo apt update && sudo apt install -y build-essential
```

### 2. Build & Run All Verification Test Suites
Run the entire test suite across all 4 phases:
```bash
make test
```

Or test individual modules:
```bash
make test_phase1   # Test Phase 1 Core Engines (FM, Granular, Chords, Sequencer)
make test_phase2   # Test Phase 2 Chopper, Transients & WSOLA Stretcher
make test_phase3   # Test Phase 3 Hardware Bridge, Pads & P-Locks
make test_phase4   # Test Phase 4 Master Audio Mixer, Voice Pools & Framebuffer
```

---

## Upstream Compatibility & Licensing

This project is licensed under the **GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later)** to remain strictly compatible with Behringer / Music Tribe's open-source release ([`MusicTribeOpenSource/0700-AAM`](https://github.com/MusicTribeOpenSource/0700-AAM)).

All custom code is architected to link seamlessly with the stock `ComProtocol` and `AudIO` hardware abstraction layers without modifying upstream core kernel binaries.
