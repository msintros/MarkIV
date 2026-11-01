// Mark IVB five-band graphic EQ (schematic page 7).
//
// The discrete op-amp (Q1 input buffer, Q2/Q3 differential pair, Q4 output)
// is modelled as an ideal op-amp: Q1's emitter feeds the + input through
// R236 3K3, R234 3K3 is the feedback resistor to the - input, and each 50K
// slider sits between the two inputs with its wiper loaded by a series
// R-L-C to ground. Moving a wiper toward the feedback side boosts that band,
// toward the input side cuts it.
#pragma once

#include "DKSolver.h"

#include <array>

namespace mk4
{

class GraphicEQ
{
public:
    static constexpr int numBands = 5;
    static constexpr std::array<const char*, numBands> labels { "80", "240", "750", "2200", "6600" };

    void prepare (double sampleRate);
    // Boost at a band's centre with its slider all the way up.
    static double maxBoostDb (int band);
    // Wanted gain of each band at its centre, in dB (0 = slider centred).
    // Values beyond what the circuit can do are clamped.
    void setGains (const std::array<double, numBands>& gainsDb);
    double processSample (double x);
    void reset();

private:
    struct Band { int toInput = -1, toFeedback = -1; };
    DKSolver solver;
    std::array<Band, numBands> bands;
};

} // namespace mk4
