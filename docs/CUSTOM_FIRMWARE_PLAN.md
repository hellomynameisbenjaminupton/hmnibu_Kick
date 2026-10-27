# Behringer KICK (0700-AAM) Custom Firmware Architecture & Implementation Plan

## Forking and Extending the Official 0700-AAM Codebase Preserves Hardware Drivers and Community Compatibility

Extending the official [MusicTribeOpenSource/0700-AAM](https://github.com/MusicTribeOpenSource/0700-AAM) repository rather than writing a bare-metal OS from scratch allows our custom firmware to inherit Behringer's board-support packages, ALSA/Linux audio routing, hardware peripheral communication, and 300+ stock effects while remaining compatible with the broader [Music Tribe Open Source](https://github.com/MusicTribeOpenSource) ecosystem. Because the repository is structured under an **AGPL-3.0** license with modular C++ boundaries, we can isolate our custom synthesis and sequencing modules cleanly so upstream fixes from Behringer or community pull requests merge without conflict.

| Repository Layer | Stock Role in `0700-AAM` | Our Custom Firmware Extension Strategy |
| :--- | :--- | :--- |
| **`Linux/`** | Buildroot embedded Linux image, storage mounts, and overlay boot units ([`0700-AAM`](https://github.com/MusicTribeOpenSource/0700-AAM)) | **Keep Unmodified**: Retain stock Buildroot configuration, realtime audio thread priorities, and SD/USB filesystem mounts. |
| **`ComProtocol/`** | Hardware communication bridge for 16 RGB pads, 4 rotary encoders, jog wheel, touch strip, 4x CV/Gate jacks, and AI gesture camera ([`0700-AAM`](https://github.com/MusicTribeOpenSource/0700-AAM)) | **Keep Unmodified (Hook Events)**: Subscribe to existing pad velocity/aftertouch, encoder delta, and touch-strip coordinate streams. |
| **`AudIO/`** | Real-time C++ audio engine library, sample playback voices, looper, and 300+ built-in effects ([`0700-AAM`](https://github.com/MusicTribeOpenSource/0700-AAM)) | **Extend with New DSP Voices**: Add `AudIO/Engines/FM4Voice.cpp` (4-Op FM) and `AudIO/Engines/GranularVoice.cpp` (Sample-Coupled Granular) alongside the stock sampler engine. |
| **`Kick/`** | Top-level C++ workstation application, 7" touchscreen UI, sample editor, and sequencer ([`0700-AAM`](https://github.com/MusicTribeOpenSource/0700-AAM)) | **Extend UI & Sequencer**: Add the 4-Knob hardware page router, Interactive Step/Piano-Roll Sequencer, 1-Pad Diatonic Chord Engine, and Arpeggiator. |

## Hardware-Mapped 4-Operator FM Synthesis Eliminates Menu Diving via Four Dedicated Knob Banks

Inspired by the Elektron Digitone, the `DT-FM` Digitakt mod ([`Lapin/awesome-music-gear-firmware`](https://github.com/Lapin/awesome-music-gear-firmware)), and the 4-op `DIGITAL` engine in the M-VAVE FM-1 `Felucca` firmware ([`aroum/fm1-custom-fw`](https://github.com/aroum/fm1-custom-fw)), our **4-Operator FM Engine (`FM4`)** maps every core synthesis parameter directly to the **4 physical rotary knobs** and **Jog Wheel**. Pressing one of the 4 contextual screen tabs (or hardware bank buttons) switches the active knob assignment with live visual algorithm and waveform feedback on the 7" touchscreen.

| Control Bank | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Jog Wheel + Push Function |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Bank 1: Ratios & Tuning** | **Op 1 Ratio** (Carrier base: `0.5x` – `16.0x`; push+turn for fine detune) | **Op 2 Ratio** (Coarse harmonic integer/half; push+turn for fine detune) | **Op 3 Ratio** (Coarse harmonic integer/half; push+turn for fine detune) | **Op 4 Ratio** (Coarse harmonic integer/half; push+turn for fine detune) | **Turn**: Selects **Algorithm (1–8)**.<br>**Push+Turn**: Global Transpose / Micro-detune spread. |
| **Bank 2: Levels & Feedback** | **Op 1 Level** (Carrier output / sub-mix level) | **Op 2 Level** (Modulation index into target operator) | **Op 3 Level** (Modulation index into target operator) | **Op 4 Level** (Modulation index into target operator) | **Turn**: **Op 4 Feedback** (`0%` sine to `100%` chaotic saw/noise).<br>**Push+Turn**: **Sample-FM Depth** (modulates Op 4 phase using the track's loaded sample transient). |
| **Bank 3: Waveforms & Envelopes** | **Mod Attack** (Simultaneous attack time for Modulator Operators) | **Mod Decay / Punch** (Controls harmonic pluck and metallic decay) | **Carrier Attack** (Amplitude fade-in from percussive snap to pad swell) | **Carrier Release** (Amplitude tail length) | **Turn**: Cycles **Operator Waveform Morph** (Pure Sine → Triangle → Digitone Harmonics → Wavefolded Sine).<br>**Push**: Toggles individual Op 1–4 envelope editing on the 7" touchscreen. |
| **Bank 4: Filter, Drive & Motion** | **Filter Cutoff** (Resonant 12/24 dB LPF, BPF, HPF) | **Filter Resonance & Env Depth** (Turn for Res; push+turn for Env Depth) | **Harmonic Drive / Bitcrush** (Warm saturation to lo-fi DAC reduction) | **LFO Depth / Rate** (Turn for Depth; push+turn for Tempo-Synced Rate) | **Turn**: Selects LFO Target (Op Ratios, Mod Levels, Filter, Pitch).<br>**Touch Strip**: Smoothly morphs between current FM state and a Snapshot state. |

### Eight Curated 4-Operator Algorithms Cover Classic Bass, Bell, Brass, and Pad Topologies

1. **Algorithm 1 (`4 → 3 → 2 → 1`)**: Deep 4-operator serial stack for aggressive neuro/industrial bass, complex metallic percussion, and evolving timbres.
2. **Algorithm 2 (`(3 + 4) → 2 → 1`)**: Dual modulators feeding an intermediate modulator into a single carrier for woody marimbas, slap bass, and vocal formants.
3. **Algorithm 3 (`4 → 3 → 1` + `2 → 1`)**: Three-op stack plus an independent parallel modulator into Carrier 1 for electric pianos with separate tine click and warm body.
4. **Algorithm 4 (`(2 + 3 + 4) → 1`)**: Three parallel modulators feeding a single carrier for complex inharmonic bells, gongs, and clangorous chimes.
5. **Algorithm 5 (`(4 → 3) + (2 → 1)`)**: Two independent 2-operator stacks mixed in parallel (classic dual-timbre FM for layered sub-bass + top pluck or detuned brass).
6. **Algorithm 6 (`4 → (1 + 2 + 3)`)**: Single master modulator driving three parallel carriers for lush, chorus-like FM strings and organ drawbar textures.
7. **Algorithm 7 (`(4 → 3) + 2 + 1`)**: One 2-op pair plus two pure additive carriers for warm hybrid pads with sub-octave foundation.
8. **Algorithm 8 (`1 + 2 + 3 + 4`)**: Four parallel carriers with waveform morphing and Op 4 feedback/Sample-FM for 4-oscillator virtual-analog/additive chords and drones.

## Sample-Coupled Granular Synthesis Turns Any Chopped Pad or Live Input into an Interactive Cloud

Rather than treating the sampler and synthesizer as walled-off modes, our **Sample-Coupled Granular Engine (`GRAIN`)**—informed by [Mutable Instruments Plaits](https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/), the Digitakt `digigrain` mod ([`Lapin/awesome-music-gear-firmware`](https://github.com/Lapin/awesome-music-gear-firmware)), and [Layer Engine](https://github.com/samplaman/layerengine)—shares the exact same in-memory audio buffers as the KICK's sampler. Any recorded loop, individual slice from a chopped breakbeat, or live stereo input stream can be switched into Granular mode instantaneously without duplicating RAM.

| Control Bank | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Touchscreen & Touch Strip Integration |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Bank 1: Grain Core** | **Grain Position** (`0.0%` – `100.0%` through sample or active chop slice) | **Grain Size** (`5 ms` pulsar micro-grains up to `1,000 ms` ambient clouds) | **Density / Trigger Rate** (Free cloud overlap up to 32 grains, or tempo-synced `1/4` to `1/64` rhythmic bursts) | **Grain Pitch** (`-24` to `+24` semitones; push+turn for fine cents) | **7" Touchscreen**: Drag finger across the live waveform to scrub grain position and pinch-zoom slice bounds.<br>**Touch Strip**: Acts as a ribbon controller for manual time-scrubbing or pitch-bending the cloud. |
| **Bank 2: Spray, Window & Space** | **Position Spray** (Randomizes grain start offset around the playhead for organic motion) | **Pitch Spray / Shimmer** (Quantized random pitch scatter: Unison, `+1 Oct`, `+5th`, or active Scale notes) | **Grain Window Envelope** (Morphs from sharp percussive ramp → Tukey → smooth Hann → Gaussian bell) | **Reverse Probability & Pan Spread** (`0%` forward to `100%` reverse grains with stereo widening) | **AI Gesture Camera (Optional Toggle)**: Hand height over the sensor modulates Grain Position or Freeze state for hands-free live performance. |

### Core Sampler & Slicing Workflow Bridges Traditional Chopping and Granular Resampling

* **Three Slicing Modes**: Supports **Auto-Transient Chopping** (adjustable sensitivity with zero-crossing snap), **Beat-Grid Slicing** (`1/4`, `1/8`, `1/16` equal divisions), and **Live Lazy Chopping** (tap consecutive RGB pads while the sample plays to drop slice markers on the fly).
* **Per-Slice Non-Destructive Parameters**: Following the `PIGEON` sampler model on the FM-1 ([`aroum/fm1-custom-fw`](https://github.com/aroum/fm1-custom-fw)), each of the 16 pads/slices across 8 banks retains independent start/end markers, loop crossfade, reverse toggle, pitch/time-stretch ratio, filter cutoff, choke group, and a **1-Tap "Granularize Slice" toggle**.
* **Internal Resampling Bus**: Sample directly from the stereo line input, USB audio input, or **internal master/track bounce**—letting you play a 4-Op FM chord progression, resample it into a pad slot in two seconds, and immediately slice or granularize it.

## Step Sequencer, Live Pad Recording, 1-Pad Diatonic Chord Mode, and Arpeggiator Unite Groovebox Speed with Polyphonic Expression

To make sequencing fast, tactile, and musical without tedious piano-roll menu diving, `Kick/Sequencer/` combines Elektron-style step editing (`Skiff` on [`aroum/fm1-custom-fw`](https://github.com/aroum/fm1-custom-fw)) with MPC-style live pad capture and a dedicated **1-Pad Chord & Arpeggiator system** inspired by `ChoralRoot` and `FoMni` ([Drey Andersson](https://dreyandersson.com/blog/m-vave-fm-1-custom-firmware/)).

### 1. Tactile Step Sequencing & Live Pad Recording

* **Direct Step Entry + 4-Knob Note Shaping**: When in Step Edit mode, the 16 RGB pads represent steps `1–16` (Across 4 page banks for up to 64 steps per pattern), while the 7" touchscreen shows the full polyphonic Piano Roll / Step Grid. Tapping a pad or touchscreen cell places a note; holding any step pad (or selecting a note on screen) immediately binds the **4 physical knobs** to that step's musical attributes:
  * **Knob 1 — Pitch / Note**: Scrolls semitones (or snaps to the active Scale) and updates the note vertically on screen.
  * **Knob 2 — Velocity (`1–127`)**: Sets note dynamics with proportional pad LED brightness and on-screen bar height.
  * **Knob 3 — Note Length / Gate**: Adjusts duration from `0.1` step staccato blips up to `64` steps for tied drones and sustained pads.
  * **Knob 4 — Microtiming Nudge & Ratchets**: Turn to nudge timing `-49%` to `+49%` off-grid for human pocket; push+turn to add `2x`, `3x`, `4x`, or `8x` step ratchets and probability conditions (`10%–100%` or `A:B` loop iteration).
* **Parameter Locks (P-Locks)**: While holding any step pad, turning any FM or Granular parameter on screen (such as FM Algorithm, Op 2 Level, or Grain Size) locks that exact parameter value to that single step.
* **Live Pad Recording with Flexible Quantize**: Hit `REC + PLAY` to finger-drum or play chords live on the 16 velocity-sensitive pads into the sequencer at 960 PPQN resolution. A top-bar **Quantize button** cycles between:
  * **Off (Unquantized)**: Captures exact microtiming nuances.
  * **Smart Humanize (`50%–85%` Strength)**: Pulls notes closer to the grid while preserving pocket feel and swing (`50%–75%`).
  * **Hard Grid Snap (`100%`)**: Quantizes note starts to `1/16`, `1/16T`, or `1/32` steps immediately on input.

### 2. 1-Pad Scale & Chord Progression Mode (12 Progression Chords + 4 Real-Time Modifiers)

When **Chord Mode** is active on any FM, Granular, or polyphonic Sample track, you select a **Root Note** (`C` through `B`) and **Scale / Mood** (Major, Natural Minor, Dorian, Phrygian, Lydian, Mixolydian, Harmonic Minor, Melodic Minor, Lo-Fi Jazz, or Dark Cinematic) from the header bar. The 16 RGB pads then split into **12 Single-Finger Chord Pads (Pads 1–12)** and **4 Real-Time Voicing Modifiers (Pads 13–16)**:

| Pad Row (Physical Layout) | Pad Number & LED Color | Default Musical Function (Example in C Minor / Aeolian) | Why It Belongs in the 12-Chord Palette |
| :--- | :--- | :--- | :--- |
| **Row 4 (Top Row: Real-Time Chord Modifiers)** | **Pad 13** (Cyan) | **Hold: Add 7th / 9th** (Cycles Triad → 7th → 9th → 11th) | Instantly enriches any held chord pad into lush jazz/house extensions. |
| **Row 4 (Top Row: Real-Time Chord Modifiers)** | **Pad 14** (Cyan) | **Hold: Sus2 / Sus4 Toggle** | Replaces the 3rd with a 2nd or 4th for tension and electronic techno/ambient chords. |
| **Row 4 (Top Row: Real-Time Chord Modifiers)** | **Pad 15** (Purple) | **Hold: Inversion & Open Drop-2 Spread** | Smooths voice-leading so adjacent chords stay in the same register instead of jumping. |
| **Row 4 (Top Row: Real-Time Chord Modifiers)** | **Pad 16** (Gold) | **Tap: Latch / Hold: Strum & Arp Trigger** | Latches chords hands-free or strums notes across the Touch Strip. |
| **Row 3 (Borrowed & Turnaround Chords: Pads 9–12)** | **Pad 9** (Amber) | **Borrowed `IV` Major / Dorian Lift** (`F Maj7` in C Minor) | Classic soulful/electronic modal interchange chord. |
| **Row 3 (Borrowed & Turnaround Chords: Pads 9–12)** | **Pad 10** (Amber) | **Harmonic Dominant `V7` / `V7b9`** (`G7` / `G7b9` in C Minor) | Provides strong cadential pull back to the root `i` chord. |
| **Row 3 (Borrowed & Turnaround Chords: Pads 9–12)** | **Pad 11** (Amber) | **Neapolitan / Phrygian `bII Maj7`** (`Db Maj7` in C Minor) | Adds dark cinematic, techno, or soundtrack tension. |
| **Row 3 (Borrowed & Turnaround Chords: Pads 9–12)** | **Pad 12** (Amber) | **Secondary Dominant `V7/iv` or Passing Dim7** (`C7` → `Fm` in C Minor) | Creates smooth chromatic movement into the subdominant. |
| **Row 2 (Upper Diatonic & Cadential Chords: Pads 5–8)** | **Pad 5** (Green) | **Scale Degree `v` (Minor Dominant)** (`G m7`) | Natural minor dominant chord. |
| **Row 2 (Upper Diatonic & Cadential Chords: Pads 5–8)** | **Pad 6** (Green) | **Scale Degree `bVI` (Submediant)** (`Ab Maj7`) | Essential epic/emotional progression step. |
| **Row 2 (Upper Diatonic & Cadential Chords: Pads 5–8)** | **Pad 7** (Green) | **Scale Degree `bVII` (Subtonic)** (`Bb Maj` / `Bb7`) | Staple electronic, pop, and house cadence chord. |
| **Row 2 (Upper Diatonic & Cadential Chords: Pads 5–8)** | **Pad 8** (Blue) | **Tonic Octave / Add9 Voicing (`i9`)** (`C m9` higher voicing) | Gives the 8th pad in the diatonic group an expressive high tonic resolution. |
| **Row 1 (Bottom Row: Core Diatonic Degrees: Pads 1–4)** | **Pad 1** (Bright White/Blue) | **Scale Degree `i` (Root Tonic)** (`C m`) | Home base chord under your left index finger. |
| **Row 1 (Bottom Row: Core Diatonic Degrees: Pads 1–4)** | **Pad 2** (Green) | **Scale Degree `ii°` / `ii m7b5`** (`D m7b5`) | Pre-dominant tension chord. |
| **Row 1 (Bottom Row: Core Diatonic Degrees: Pads 1–4)** | **Pad 3** (Green) | **Scale Degree `bIII` (Relative Major)** (`Eb Maj7`) | Instant lift to the relative major key center. |
| **Row 1 (Bottom Row: Core Diatonic Degrees: Pads 1–4)** | **Pad 4** (Green) | **Scale Degree `iv` (Subdominant)** (`F m7`) | Warm minor subdominant foundation. |

Every single-pad chord press can be **recorded straight into the Step Sequencer** (either as a unified Chord Event that follows future scale changes, or exploded into individual editable polyphonic notes on the touchscreen grid).

### 3. Integrated Multi-Mode Arpeggiator

The **Arpeggiator (`ARP`)** sits directly between the 1-Pad Chord Engine and the Sequencer/Audio Engine, meaning you can hold down a single chord pad (e.g., Pad 1 `Cm` + Pad 13 `9th`) and immediately hear an evolving arpeggio—and optionally **bake the generated arp notes straight into the step sequencer pattern**:

* **Knob 1 — Arp Direction & Pattern**: Cycles through `Up`, `Down`, `Up & Down`, `Converge (Outside-In)`, `Diverge (Inside-Out)`, `Pinky/Thumb (Root Pedal + Moving Top Note)`, `Random / Mutate`, and `Polyphonic Chord Repeat`.
* **Knob 2 — Rate & Timing Division**: Tempo-synced `1/4`, `1/8`, `1/8T`, `1/16`, `1/16T`, `1/32`, or polymorphic Euclidean trigger masks (e.g., `5 hits over 16 steps`).
* **Knob 3 — Octave Range & Voicing Span**: Spans `1` to `4` octaves, with a push-turn mode that shifts through chord inversions on each octave pass rather than simple +12 semitone jumps.
* **Knob 4 — Gate Length & Accent Groove**: Adjusts note length (`5%` plucks to `120%` overlapping legato) and applies velocity accent patterns.

## Four-Phase Implementation Roadmap Delivers Testable Modules Incrementally

1. **Phase 1 — Repository Fork & Core C++ Engine Scaffolding (`AudIO/` + `Kick/` Headers)**:
   * Fork [`MusicTribeOpenSource/0700-AAM`](https://github.com/MusicTribeOpenSource/0700-AAM) and define the shared `ITrackSynthVoice` interface in `AudIO/` plus the `ChordScaleEngine`, `Arpeggiator`, and `StepSequencerTrack` data structures in `Kick/`.
   * Build a lightweight desktop/simulator test harness (similar to [`m-vave/fm1_open`](https://github.com/m-vave/fm1_open)) so we can audition the FM, Granular, Chord, and Arp engines on your computer before flashing hardware.
2. **Phase 2 — 4-Operator FM & Sample-Coupled Granular DSP (`AudIO/`)**:
   * Implement the 8-algorithm 4-Operator FM engine (`FM4Voice.hpp/.cpp`) with 4-knob bank mapping, operator waveform morphing, and Sample-FM phase modulation.
   * Implement the zero-copy Granular engine (`GranularVoice.hpp/.cpp`) reading directly from `AudIO` sample/slice buffers with variable window shapes, pitch quantization, and Touch Strip scrub hooks.
3. **Phase 3 — Step Sequencer, 1-Pad Chord Engine & Arpeggiator (`Kick/Sequencer/`)**:
   * Implement the 64-step polyphonic sequencer with 4-knob step editing (Pitch, Velocity, Length, Microtiming/Ratchet), Parameter Locks, and 960 PPQN live pad recording with input quantization.
   * Implement the 16-pad Scale/Chord Progression mapper (12 diatonic/borrowed chords + 4 live modifiers) feeding the Arpeggiator and Step Recorder.
4. **Phase 4 — 7" Touchscreen UI Views & Hardware Controller Binding (`ComProtocol/` + `Kick/UI/`)**:
   * Build the high-contrast touchscreen views (4-Op FM Matrix + Live Waveform Preview, Granular Cloud Scrubber, and Step/Chord Grid) and bind the 16 RGB pad LEDs, 4 encoders, Jog Wheel, Touch Strip, and AI Camera gesture stream.

