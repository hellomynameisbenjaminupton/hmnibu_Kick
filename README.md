# Behringer KICK (0700-AAM) Custom Firmware

Custom open-source C++17 firmware extension for the **Behringer KICK (`BE 0700-AAM`)** standalone sampler and music production center, extending [`MusicTribeOpenSource/0700-AAM`](https://github.com/MusicTribeOpenSource/0700-AAM).

## Included Phase 1 Modules
- **`AudIO/CustomEngines/kick_custom_firmware_core.hpp`**:
  - `AudIO::FM4Voice`: 4-Operator, 8-Algorithm FM synthesis engine with Operator 4 feedback, continuous waveform morphing, Sample-FM transient phase modulation, and 4-Knob hardware bank mapping.
  - `AudIO::GranularVoice`: Sample-coupled zero-copy granular synthesis engine reading directly from `AudIO::SampleSliceView` chop buffers.
  - `Kick::ChordScaleEngine`: 16-Pad Scale & Chord Progression engine (Pads 1–12 trigger diatonic and borrowed chords; Pads 13–16 apply live 7th/9th, Sus2/4, Inversion, and Strum/Latch modifiers).
  - `Kick::Arpeggiator`: 4-Knob multi-mode arpeggiator (`Up`, `Down`, `UpDown`, `Converge`, `PinkyThumbPedal`, `ChordRepeat`).
  - `Kick::StepSequencerTrack`: 64-step polyphonic sequencer with 4-Knob step editing, Parameter Locks, and 960 PPQN live pad recording with Input Quantize.
- **`docs/CUSTOM_FIRMWARE_PLAN.md`**: Full architecture and hardware control mapping specification.
- **`tests/test_firmware_core.cpp` & `Makefile`**: Standalone C++17 unit test suite.

## Quick Start in WSL2 (Ubuntu)
```bash
make test
```

