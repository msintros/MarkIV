# Disclaimer

This is very much a work in progress, and mostly I just wanted to see how easy it would be to give Claude an electronics schematic and have it turn it into a working plug-in.  In its current state, the lead and R2 channels sound similar to a real Mark IV but R1 clean is almost useless.  I suspect I will have to have Claude somehow condition the input signal to mimic the correct impedance.  Anyway, if you build this in its current state, don't expect it to knock your socks off.  Also note that while it does have a built-in IR loader, so far I have not attempted to model anything but the pre-amp.  If you actually try this out, I suggest sending it to a dedicated IR loader or speaker sim, or it will sound pretty bad (like most preamps do by themselves).  

# Mark IV Preamp

A VST3 / AU plugin that models the preamp, lead stages and five-band graphic EQ of a Mesa/Boogie Mark IVB guitar amplifier. The model is built directly from the factory schematic. The power amp, reverb and effects loop are left out. A simple cabinet stage is included so the output can be played straight into a DAW.

Written in C++ with [JUCE](https://juce.com). It targets macOS first, but nothing in it is Mac-specific.

## How it works

The circuit is simulated, not approximated with generic filters and waveshapers:

- **Solver:** each part of the amp is a netlist of the actual resistors, capacitors and 12AX7s from the schematic. It is solved per sample with the nodal DK method (`source/dsp/DKSolver.*`). The capacitors and inductors are discretised with the trapezoidal rule. Only the tube equations need Newton iterations.
- **Tubes:** 12AX7 triodes use the Dempwolf–Zölzer model, which includes grid current (`source/dsp/Triode.h`). The plate and grid curves are tabulated for speed.
- **Segments:** the preamp is split into six segments, one per triode (`source/dsp/MarkIVPreamp.cpp`): V1A, V1B, V3A, V4A, V2A and V2B. Each segment includes the network that drives the next tube's grid, plus that tube's grid current. So the tone stacks, coupling caps, blocking distortion and channel switching all come from the real parts.
- **Graphic EQ:** the five bands use the schematic's L-C networks around an ideal op-amp (`source/dsp/GraphicEQ.*`).
- **Channel switching:** LDRs and relays are modelled as resistors that fade between on and off over about 12 ms, as the real ones do.

### Accuracy check

The model's DC operating points match the voltages printed on the schematic:

| Tube | Plate (model / schematic) | Cathode (model / schematic) |
|---|---|---|
| V1A | 216.7 V / 210 V | 1.58 V / 1.6 V |
| V1B | 252.1 V / 255 V | 1.84 V / 1.8 V |
| V3A | 270.7 V / 265 V | 1.98 V / 2.0 V |
| V4A | 222.8 V / 220 V | 1.91 V / 2.0 V |
| V2A | 254.8 V / 257 V | 1.86 V / 2.0 V |
| V2B | 212.3 V / 215 V | 1.39 V / 1.5 V |

`tests/model_tests.cpp` checks this, along with stability at extreme settings and the EQ.

## Controls

The plugin's controls follow the owner's manual:

| Panel | Controls |
|---|---|
| Channel | Rhythm 1, Rhythm 2, Lead |
| Rhythm 1 | Gain (Pull Bright), Treble, Master, Presence |
| R1 + R2 | Bass, Mid (shared, as on the amp) |
| Rhythm 2 | Gain (Pull Fat), Treble, Master, Presence |
| Lead | Gain (Pull Fat), Treble, Bass, Mid, Drive, Lead Bright, Master, Presence, rear-panel Harmonics / Mid Gain |
| Graphic EQ | 80, 240, 750, 2200, 6600 sliders. Out / In / Auto R2, like the rear rocker switch |
| Output | Output Level (the amp's knob), plus plugin input and output trims |
| Cabinet | Off, Built-in, or a loaded impulse response (.wav/.aiff) |
| Oversampling | 2x, 4x (default), 8x |

The plugin treats an input of 0 dBFS as a 1 V peak from the guitar. If your interface is hot or quiet, use Input Trim.

## Installing a build on macOS

1. Open the latest run on the repository's **Actions** tab, then download the `MarkIV-Preamp-macOS` artifact.
2. Unzip it, then copy:
   - `Mark IV Preamp.vst3` to `~/Library/Audio/Plug-Ins/VST3/`
   - `Mark IV Preamp.component` to `~/Library/Audio/Plug-Ins/Components/`
3. The builds are ad-hoc signed, not notarised, so macOS quarantines them after download. Clear the quarantine flag once:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Mark IV Preamp.vst3"
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Mark IV Preamp.component"
   ```
4. Rescan plugins in your DAW. For Logic, you may also need to log out and back in, or run `killall -9 AudioComponentRegistrar`.

## Building from source

You need CMake 3.22 or newer and a C++17 compiler (Xcode command line tools on a Mac). CMake downloads JUCE automatically.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/markiv_tests                 # circuit model checks
./build/markiv_render ./renders      # writes demo WAVs of each channel
./build/markiv_response              # prints EQ and channel frequency responses
```

To build only the DSP and tests, without fetching JUCE, add `-DMARKIV_BUILD_PLUGIN=OFF`.

## Modelling notes and assumptions

`docs/schematic-notes.md` has the full parts transcription. A few things on the scan are ambiguous or fall outside the preamp, so these choices were made:

- **Presence:** Presence and Pull Shift are part of the power amp's negative feedback, which isn't on the preamp pages. The plugin uses a 3 kHz high shelf per channel as an approximation.
- **Lead Bright:** this is read as shorting R226 22K under C311 2.2 uF on V4A's cathode, which adds gain. The LD BRT switch on the schematic could be read the other way round.
- **Mid Gain:** read as switching C344 16 uF across V2A's cathode, in Lead only.
- **Rhythm 2 Treble:** modelled as the R2 Treble pot taking the place of R1 Treble in the shared R1/R2 stack. The 68K resistor near the R1 treble pot is not modelled.
- **Not modelled:** R264 470K on the rhythm gain pots, R241 4.7K (the R2 pad on page 6) and the satellite/loop circuitry. LDR18 on the 6600 Hz band is assumed to be on.
- **Miller capacitance:** each grid's Miller capacitance is a fixed estimate (70 to 110 pF), not computed from the next stage's gain.
- **Graphic EQ band frequencies:** the printed L and C values put three bands at different frequencies from their labels. 240 lands at about 372 Hz, 2200 at about 1.6 kHz and 6600 at about 4.8 kHz. The model uses the printed values.
- **Graphic EQ slider law:** with 50K linear sliders and 3K3 input/feedback resistors, the circuit does almost all its boost or cut in the last part of the slider's travel. The plugin's sliders are calibrated in dB at each band's centre instead. Maximum boost or cut is about ±15 dB on the lower three bands and ±10 dB on the upper two.
- **Supply rails:** the rails are taken from page 3, at 379/375 V in R1 and R2 and 425/423 V in Lead. They are treated as ideal, with no sag.
