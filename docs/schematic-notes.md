# Mesa/Boogie Mark IVB: what I extracted from pages 3 to 7

Values are as read from the scan. Items marked (?) were hard to read or ambiguous and should be checked against a cleaner copy or a real amp.

## 1. Supply rails (page 3)

| Node | Voltage | Source / filtering | Feeds |
|---|---|---|---|
| A | 460 V | 350 VAC full-wave, 2x220uF/300V + 2x150K balance, standby | Power amp (ignored) |
| B | 460 V | After choke, C453 30uF/500V | Reverb driver V4b |
| C | 425 V (Lead) / 379 V (R1, R2) | R401 8K2/2W, bypassed by Q10 (2N5401) + LDR27 in Lead mode; C452 30uF | V2A, V2B, V3A, V4A, V3b |
| D | 423 V (Lead) / 375 V (R1, R2) | R314 1K from C, C451 30uF | V1A, V1B |
| E / F | 15 V / 12 V DC | 12 VAC bridge (D7 to D10), R124 6.7, 10,000uF | 12AX7 heaters (DC), 5 V reg for LDR logic |
| Bias | -57 V | D15, R233 3K3, 47uF | Power tubes (ignored) |
| EQ supply | -23 V | D16, R342 47 (?), R256 1K5, C432 220uF, C431 | Graphic EQ transistors |

Modeling note: Lead mode raises the preamp rails by about 45 V. That is a real (small) part of the Lead voicing, so the model will switch B+ per channel. Sag is negligible for the preamp, so I treat rails as ideal DC sources.

## 2. Preamp, section 1 (page 4)

**V1A (input stage)**: grid leak R391 1M. Plate R201 150K to D (210 V measured). Cathode R261 1.5K || C321 0.47uF, plus C331 2.2uF in series with R222 22K to ground (R222 shorted by LDR7 in R2, so R2 gets full fat-boost bypass). Cathode 1.6 V.

**Tone stacks (two of them, both driven from V1A plate)**: Mark-style passive stack placed *before* the gain control, which is the defining Boogie topology.
- Lead stack: treble cap C11 250pF, plus C31 750pF || R361 10M switched by LDR1 (LD); slope resistor R211 100K; bass cap C242 0.1uF; mid cap C232 0.047uF; Treble 250K, Bass 250K, Mid 10K. Treble wiper drives Gain LD 1M.
- R1/R2 stack: C12 250pF, C32 750pF || R362 10M (LDR3, R2); R212 100K; C241 0.1uF; C231 0.047uF; Treble 250K (R1), separate R2 Treble 250K via 68K (?) and LDR2; Bass 250K; Mid 10K; Mid bottom switched by LDR6 (R2) / RV1 (R1).
- Gains: R1 Gain 1M with BRT 120pF bright cap; R2 Gain 1M; R264 470K. LDR4/LDR5/LDR10 pick which wiper reaches V1B.

**V1B**: plate R213 100K to D (255 V). Cathode R252 1.5K || C343 15uF (1.8 V). Output C233 0.047uF.
- To V2A: R102 3.3M || C2 20pF series, then shunt C1 47pF and R362 680K (switched by RV2), R214 100K.
- Lead send: LDR8 (L), R361 680K, C223 0.02uF to the Lead Drive pot.

**V2A**: plate R215 100K to C (257 V). Cathode R263 1.5K, C344 15uF, plus C332 2.2uF via RV1 and R223 22K via RV2 (the "Mid Gain" / channel cathode tweaks). Cathode 2 V. Output C234 0.047uF to page 6. In Lead mode V2A's grid is fed from the lead stages (LDR11/13/14).

## 3. Lead stages (page 5, lower half; reverb ignored)

**Lead Drive**: 1M pot, input from C223.

**V3A**: grid R264 470K (?). Plate R282 82K to C (265 V). Cathode R265 1K5 || C333 2.2uF (2 V); 180pF across the stage (?, likely grid or plate to cathode).
- Interstage: C222 0.02uF + R275 270K series into V4A grid, shunted by R332 68K || 750pF. This is the classic Boogie lead attenuator/low-pass.

**V4A**: plate R276 270K to C (220 V). Cathode R238 3K3 || 250pF, plus C311 2.2uF + R226 22K switched by LD BRT. Output C236 0.047uF, then R323 220K || C13 250pF series, R314 100K || C21 500pF shunt, out to V2A.

## 4. Master / loop / recovery (page 6, simplified)

C234 → R372 47K → per-channel Masters (R1, R2, LD, each 100K, LDR15/16/17) → R203 150K → (reverb tap, ignored) → R295 39 → R241 4.7K (R2 pad, LDR12) → loop LDRs → V2B grid (R371 47K).

**V2B recovery stage**: plate R103 120K to C (215 V), cathode R311 1K || C345 15uF (1.5 V). Out C235 0.047uF → R224 22K → Output Level 250KA with 250pF → EQ in.

Simplification: loop removed (LDR20 path treated as a wire), Q5 sat-send ignored, reverb mix ignored.

## 5. Graphic EQ (page 7)

Active 5-band design: Q1 MPSA63 (darlington, input), Q2/Q3 MPSA70 differential pair, Q4 MPSA20 output, -23 V supply, C51 0.1uF in, C411 1uF out, 10pF compensation (C41, C42), R234/R235/R237 3K3. Functionally it is an op-amp style graphic EQ: each 50K slider sits between the amp's input and feedback side, with a series R-L-C to ground from the wiper. Real inductors, not gyrators.

| Label | R | L | C | LC resonance |
|---|---|---|---|---|
| 80 Hz | 470 | 1.0 H | 3.3uF | 88 Hz |
| 240 Hz | 470 | 0.39 H | 0.47uF | 372 Hz |
| 750 Hz | 470 | 0.22 H | 0.22uF | 723 Hz |
| 2200 Hz | 1K | 0.068 H | 0.15uF | 1576 Hz |
| 6600 Hz | 1K | 0.033 H | 0.033uF | 4823 Hz |

The 240, 2200 and 6600 bands do not resonate at their labeled frequency with the values as printed. Either the scan's cap labels are off or Mesa's labels are nominal. I'll model the printed values and let you A/B against the labels. LDR18 + R373 47K on the 6600 band is a switched treble tweak.

## 6. Open questions in the schematic

- R342 value (reads "476", probably 47 ohm) only affects the EQ supply, irrelevant to sound.
- Exact placement of V3A's 180pF.
- The 68K near the R1 treble pot and how R1/R2 share Bass/Mid (likely dual-gang pots behind one knob).
- The full LDR truth table per channel is not on these pages; I'll infer it from the (R1)/(R2)/(L) labels.

## 7. Front/rear panel controls mapped to the schematic (from the owner's manual)

| Plugin control | Manual name | Circuit element |
|---|---|---|
| Channel: R1 / R2 / Lead | Mode select / footswitch | LDR and relay (RV) switching on pages 4 to 6; also selects rail C/D voltage (Lead raises it) |
| R1 Gain (+ Pull Bright) | RHY1 GAIN, PULL BRIGHT | R1 Gain 1M; BRT 120pF across the pot |
| R1 Treble | R1 TREBLE | R1 stack treble 250K |
| R1+R2 Bass, R1+R2 Mid | shared Bass / Mid | R1/R2 stack bass 250K, mid 10K (shared knob) |
| R2 Gain (+ Pull Fat) | RHY2 GAIN, PULL FAT | R2 Gain 1M; Fat = C32 750pF treble-cap shift via LDR3 ("lowers the treble frequency, boosts low mids") |
| R2 Treble | R2 TREBLE | separate R2 treble 250K (via 68K, LDR2) |
| Lead Gain (+ Pull Fat) | LEAD GAIN, PULL FAT | Gain LD 1M; Fat = C31 750pF shift via LDR1 |
| Lead Treble / Bass / Mid | LEAD TREBLE / BASS / MID | upper (lead) stack |
| Lead Drive | LEAD DRIVE | 1M pot into V3A |
| Lead Bright | LEAD BRIGHT (pull) | LD BRT: C311 2.2uF + R226 22K on V4A cathode |
| Harmonics / Mid Gain | rear LEAD VOICING switch | MID GAIN switch on V2A cathode bypass (C344/C332), Lead only |
| R1 / R2 / Lead Master | MASTERS | 100K masters, LDR15/16/17 |
| EQ In / Out / Auto R2 | EQ rocker | bypasses the graphic EQ (Auto = on only in R2) |
| 80/240/750/2200/6600 | Graphic EQ sliders | 50K sliders on page 7 |
| Output Level | OUTPUT LEVEL | 250KA pot before the EQ |

Out of scope (power amp, per the manual): Presence and Pull Shift on all three channels, Triode/Pentode, Simul-Class/Class A, Tweed power, reverb, loops, recording out. Presence is worth noting: the manual treats it as a key tone control, but it lives in the power amp's negative feedback, which isn't on pages 3 to 7. I can add an approximated presence shelf per channel if wanted.
