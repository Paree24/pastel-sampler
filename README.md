# Pastel Sampler — Chromatic Single-Sample Instrument (VST3 + Standalone)

> **Disclaimer:** this project is vibe-coded for personal use. It is provided
> as-is, without warranty of any kind. The author is not responsible for
> anything — Use at your own risk.

Pastel Sampler is a chromatic single-sample instrument (VST3 + Standalone,
JUCE 8). Load one audio file; the root note is detected automatically and the
sample is spread across the keyboard. The Engines card has a tab per engine
(SAMPLER, ADDITIVE, SPECTRAL, GRANULAR, BLEND). Clicking a tab plays that
engine and shows its settings. **BLEND** plays all four, mixed with the
vertical sliders in that tab; a dot marks the engines in the mix. The Blend
levels only apply in Blend, so a single engine always plays at full level. A
fader at its left end turns that engine off. Notes that are still ringing keep
the engine they started with. The engines are phase-aligned, so blending them
doesn't comb-filter.

- **Sampler**: classic repitching with Linear / Cubic / windowed-sinc HQ
  interpolation (anti-aliased when transposing up), loop modes Off / Forward /
  Ping-pong / Sustain, a baked equal-power loop crossfade, and a start offset.
- **Additive**: time-varying harmonic analysis (up to 128 partials, measured
  inharmonicity, phase-aligned starts), resynthesised at any pitch. Partials,
  tilt and noise are knobs, and you can drag on the partial display to redraw
  the spectrum.
- **Spectral**: pitch shifting via a phase-vocoder time stretch followed by
  windowed-sinc resampling, so every partial lands at exactly ratio × its
  frequency, as with the sampler, and nothing is rounded to FFT bins. Phase
  locking works per peak region, plus harmonic phase locking that keeps the
  waveform shape for pitched samples. It resynchronises on loop wraps, resets
  phase on onsets, band-limits in the spectral domain (no aliasing), runs in
  stereo, and pre-rolls so attacks are kept. At the root it reconstructs real
  recordings at 25–53 dB SNR. Optional formant preservation uses a "true
  envelope" (iterative cepstral) estimate and is off by default (FORMANT in
  the Spectral tab). **Shift** moves formants without changing pitch.
  **Smear** blurs the spectrum over time (0–2 s) into a pad. **Freeze** slows
  the sound to a standstill after the attack; at 100% it holds the tone.
- **Granular**: windowed grains read from the sample around a moving playhead.
  Each grain is resampled to the played key, so pitch follows the keyboard and
  length doesn't. New grains start a whole number of pitch periods from the
  previous one, so overlapping grains stay in phase (clean pitch, no comb
  filtering). Controls: Position, Scan (1 = real time, 0 = freeze, negative =
  backwards), Size, Density, Spray, pitch Jitter, stereo Width and window
  Shape. The playhead loops inside the loop region, or holds at the end as a
  sustained texture. Live grains are drawn on the waveform.

With **Keep length** on, Additive and Spectral keep the original duration on
every key. The **Filter** works per voice and goes up to Q 25 (sharp). Its
level is held by a make-down that scales with Q, plus a soft limiter above
unity.

**Master:** a lookahead peak limiter (1.5 ms, stereo-linked, −0.3 dBFS
ceiling). It is transparent below the ceiling, and its latency is reported to
the host. The **LIMITER** switch sits next to Volume.

**Sample editing:** **LOOP** switches looping on or off; the loop type
(Forward, Ping-pong, Sustain) is in the Sampler tab. Drag across the waveform,
or drag the S / E flags, to select the region. **TRIM** cuts the sample to the
selection and **RESTORE** undoes it. **FIND LOOP** places a seamless loop
inside the selection. Root detection follows the selection. The **EFFECTS**
page has the Pastel player's chain: chorus, phaser, flanger, distortion,
saturation, reverb, delay (tempo sync) and master volume / limiter / mono.

**MOD page:** two LFOs (sine, triangle, saw, square, S&H; free Hz or synced to
the host tempo) and an 8-slot mod matrix. Sources: LFO 1/2, mod wheel,
aftertouch, velocity, key and a per-note random value. Destinations: pitch,
cutoff, resonance, level, pan, grain position / size / density / spray, formant
shift, smear, freeze and additive tilt, applied per voice every 32 samples.
The **arpeggiator** (up, down, up-down, random; 1/4 to 1/32 with dotted and
triplet steps, 1-4 octaves, gate, swing, latch, played velocity) locks to the
host grid while the transport runs. **RANDOMIZE** / **MUTATE** change only the
unlocked groups (Sample and Root are locked by default); each is one undo step.

**Presets:** the PRESETS browser lists factory sounds, which play on the loaded
sample and keep its root, plus your own presets (sample + all settings) in
Documents/Pastel Sampler/Presets. **UNDO** / **REDO** (Ctrl+Z / Ctrl+Shift+Z)
cover knob moves, sample edits and loads, presets and randomize.

**Root detection:** YIN, HPS, or both combined, then refined on the harmonic
peaks of a long FFT. It runs on an automatic analysis window (the steady
part after the attack, inside the S..E selection) and shows a confidence
value. The window is not draggable: moving the S / E flags re-places it. You can override the root note and fine tune, set it with
MIDI learn, or run the detection again. The browser detects the root of every
file in the current folder in the background.

The UI follows the design language of the Pastel Soundfont Player, with a
`#9EB9F3` accent.

## License

GPL version 3 — see [LICENSE](LICENSE). This also satisfies the JUCE
framework's licensing terms for this project.

Embedded typefaces (Lato) are SIL Open Font License 1.1; see
[Assets/OFL-NOTICE.txt](Assets/OFL-NOTICE.txt).

## Building

Requirements: CMake 3.22+, a C++17 compiler, Ninja (or Make), Git (JUCE is
fetched automatically), and JUCE's Linux dependencies (ALSA, X11, freetype,
etc.) — see the JUCE docs for the full list.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Outputs:

- VST3: `build/PastelSampler_artefacts/Release/VST3/Pastel Sampler.vst3`
- Standalone: `build/PastelSampler_artefacts/Release/Standalone/Pastel Sampler`
- Headless checks: `build/PastelSamplerHarness_artefacts/Release/PastelSamplerHarness`

Install the VST3 by copying `Pastel Sampler.vst3` to your plugin folder:

| OS      | VST3 folder                          | Notes                                        |
|---------|--------------------------------------|----------------------------------------------|
| Linux   | `~/.vst3/`                           | Rescan plugins in your DAW afterwards        |
| macOS   | `~/Library/Audio/Plug-Ins/VST3/`     | Unsigned build: right-click → Open once      |
| Windows | `C:\Program Files\Common Files\VST3\`| Rescan plugins in your DAW afterwards        |

**Formats:** WAV, AIFF, FLAC, OGG and MP3 (JUCE's built-in readers). Files are
read up to 120 s.

### OS-specific notes

> Only the Linux build has actually been compiled and tested here. The macOS
> and Windows steps below follow the standard JUCE/CMake flow and *should*
> work, but if you hit an OS-specific snag, please file an issue with the
> failing command and its full output.

#### Linux (verified)

1. Install dependencies (Debian/Ubuntu shown; Fedora/Arch: equivalent
   `-devel` packages):
   ```sh
   sudo apt install build-essential cmake ninja-build git pkg-config \
     libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev \
     libxinerama-dev libxrandr-dev libfreetype6-dev libfontconfig1-dev \
     libcurl4-openssl-dev libwebkit2gtk-4.1-dev
   ```
2. Configure + build (JUCE is fetched automatically — needs network access):
   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```
3. Run the headless checks:
   ```sh
   ./build/PastelSamplerHarness_artefacts/Release/PastelSamplerHarness
   ```
4. Install: copy `build/PastelSampler_artefacts/Release/VST3/Pastel Sampler.vst3`
   to `~/.vst3/`, then rescan plugins in your DAW.

#### macOS (not yet built here)

1. Install Xcode command-line tools, CMake, Ninja and Git:
   ```sh
   xcode-select --install
   brew install cmake ninja git
   ```
2. Same configure + build as Linux. For a universal (Intel + Apple Silicon)
   binary, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` to the configure step.
3. Run `./build/PastelSamplerHarness_artefacts/Release/PastelSamplerHarness`.
4. Install: copy `Pastel Sampler.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.
   These are unsigned personal builds: if macOS refuses to load them, ad-hoc
   sign with `codesign --force --deep -s - <path>` and/or strip the quarantine
   flag with `xattr -dr com.apple.quarantine <path>`. Codesigning/notarisation
   for distribution is out of scope for this project.

#### Windows (not yet built here)

1. Install Visual Studio 2022 with the **Desktop development with C++**
   workload (MSVC v143 or newer), plus CMake, Ninja and Git
   (`winget install Kitware.CMake Ninja-build.Ninja Git.Git` works).
2. Open an **x64 Native Tools Command Prompt** (so MSVC is on `PATH`), then
   the same configure + build commands as Linux. Alternatively use the
   Visual Studio generator instead of Ninja:
   ```bat
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```
3. Run `build\PastelSamplerHarness_artefacts\Release\PastelSamplerHarness.exe`.
4. Install: copy `Pastel Sampler.vst3` to
   `C:\Program Files\Common Files\VST3\`, then rescan plugins in your DAW. No
   ASIO SDK or extra setup required.

## Repository layout

- `Source/` — plugin DSP, UI, engine backends
  (`DSPEngine.*` sampler/additive/spectral/granular voices, `SynthEngine.*`,
  `Analysis.*` root detection, `FileLibrary.*` browser, `PluginProcessor.*`,
  `PluginEditor.*`, `PastelLookAndFeel.*`, `PianoStrip.*`, `Widgets.*`,
  `Params.*`, `Limiter.h`, `VoiceFilter.h`)
- `Assets/` — embedded fonts + OFL notice
- `Test/Harness.cpp` — headless checks
- `ui_mockup.svg` — UI mockup

## Dev tools

`PASTEL_EVAL_FILES="a.wav;b.wav" PASTEL_EVAL_OUT=dir` makes the harness render
real files through the Sampler and Spectral engines (unity, +7, +12 and −12 st)
and report unity SNR. `PASTEL_SNAPSHOT_DIR=dir` renders the editor pages to PNG.
`PastelSamplerHostCheck <Pastel Sampler.vst3>` loads the built VST3 like a DAW
(buses, latency, a sample load and note, state, editor).

**Dev hook:** `PASTEL_AUTOLOAD=/path/to/file.wav` preloads a sample at startup.
`PASTEL_SETTINGS_FOLDER` switches to a separate settings folder.
