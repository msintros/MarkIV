// Mesa/Boogie Mark IVB preamp model (V1A, tone stacks, V1B, lead stages
// V3A/V4A, V2A, masters, V2B recovery and Output Level), built from the
// factory schematic. See docs/schematic-notes.md for the part values and the
// assumptions made where the scan is ambiguous.
#pragma once

#include "DKSolver.h"

#include <cmath>
#include <vector>

namespace mk4
{

enum class Channel { Rhythm1 = 0, Rhythm2 = 1, Lead = 2 };

// All knobs are 0..1 (panel 0..10).
struct PreampParams
{
    Channel channel = Channel::Rhythm1;

    double r1Gain = 0.6, r1Treble = 0.6, r1Master = 0.5;
    bool r1Bright = false;

    double bass = 0.25, mid = 0.6;   // shared by R1 and R2

    double r2Gain = 0.8, r2Treble = 0.6, r2Master = 0.5;
    bool r2Fat = false;

    double leadGain = 0.75, leadTreble = 0.6, leadBass = 0.25, leadMid = 0.6;
    double leadDrive = 0.6, leadMaster = 0.5;
    bool leadFat = false, leadBright = false;

    bool midGain = false;            // rear panel Harmonics (false) / Mid Gain (true)

    double outputLevel = 0.5;
};

class MarkIVPreamp
{
public:
    // Supply rails (page 3). Lead mode raises C and D by switching out R401.
    static constexpr double railC_Rhythm = 379.0, railC_Lead = 425.0;
    static constexpr double railD_Rhythm = 375.0, railD_Lead = 423.0;

    void prepare (double sampleRate);
    void setParams (const PreampParams& p);   // may rebuild matrices; call at control rate
    double processSample (double inVolts);    // returns volts at the Output Level wiper
    void reset();                             // back to the DC operating point
    // Advances the LDR/relay fades. Call once per block of numSamples.
    void advance (int numSamples, bool snap = false);

    // Diagnostics for tests.
    struct OperatingPoint { double vk, vp; };
    OperatingPoint operatingPoint (int stage) const;  // 0..5 = V1A, V1B, V3A, V4A, V2A, V2B

private:
    struct Pot { int upper = -1, lower = -1; double total = 0; };

    void buildSegments (double fs);
    void applyParams();
    void setPot (DKSolver& s, const Pot& p, double fractionFromBottom);
    void setSwitch (DKSolver& s, int id, bool on);
    void setRheostat (DKSolver& s, int id, double total, double fraction);
    void computeDC();
    double switchAmount (DKSolver& s, int id) const;

    struct Switch { DKSolver* solver; int id; double amount, target; };
    std::vector<Switch> switches;
    double leadMix = 0.0;

    PreampParams params;
    double fs = 48000.0;
    bool built = false;

    // Segments, one triode each (see MarkIVPreamp.cpp for the split).
    DKSolver s1, s2, s3, s4, s5, s6;

    // Element ids.
    struct
    {
        int ldr7 = -1;
        Pot leadTreble, rhyTreble, leadGain, rhyGain;
        int leadBass = -1, leadMid = -1, rhyBass = -1, rhyMid = -1;
        int leadFatSw = -1, rhyFatSw = -1, brightSw = -1, leadSel = -1, rhySel = -1;
    } e1;
    struct { int ry2a = -1, rv2 = -1, ldr9 = -1, ldr8 = -1; Pot drive; } e2;
    struct { int ldBright = -1, ldr11 = -1, muteZ = -1; } e4;
    struct { int rv1 = -1, rv2 = -1, midGain = -1, master = -1; } e5;
    struct { Pot level; } e6;

    // Inter-segment state.
    double railC = railC_Rhythm, railD = railD_Rhythm;
    double targetC = railC_Rhythm, targetD = railD_Rhythm, railCoeff = 0.0;
    double vk[6] {};       // cathode voltages from the previous sample
    double vp[6] {};       // plate voltages from the previous sample
    double x2 = 0;         // V1B coupling node (feeds the R102 path into the lead return)
};

// Audio-taper ("A") pot law: 10% resistance at half rotation.
inline double audioTaper (double x)
{
    x = x < 0 ? 0 : (x > 1 ? 1 : x);
    return (std::pow (100.0, x) - 1.0) / 99.0;
}

} // namespace mk4
