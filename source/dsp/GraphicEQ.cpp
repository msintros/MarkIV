#include "GraphicEQ.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace mk4
{

namespace
{
    constexpr double kPot = 50.0e3;
    constexpr double kRin = 3.3e3, kRf = 3.3e3, kMinR = 10.0;

    struct BandParts { double r, l, c; };
    // Values as printed on the schematic (series resistor, inductor, capacitor).
    constexpr std::array<BandParts, GraphicEQ::numBands> parts { {
        { 470.0,  1.0,   3.3e-6 },    //   80 Hz
        { 470.0,  0.39,  0.47e-6 },   //  240 Hz
        { 470.0,  0.22,  0.22e-6 },   //  750 Hz
        { 1000.0, 0.068, 0.15e-6 },   // 2200 Hz
        { 1000.0, 0.033, 0.033e-6 },  // 6600 Hz
    } };

    // Small-signal gain of the ideal-op-amp EQ at frequency f for wiper
    // positions alpha (0 = input side / full cut, 1 = feedback side / full boost).
    // With the + and - inputs at the same voltage v, each wiper sits at
    // v * g / (g + Y) where g is the conductance of its two pot halves and Y
    // the admittance of its R-L-C; the currents drawn from each side give
    // gain = (1 + Rf * SB) / (1 + Rin * SA).
    double gainDb (const std::array<double, GraphicEQ::numBands>& alpha, double f)
    {
        using cd = std::complex<double>;
        const double w = 2.0 * 3.141592653589793 * f;
        cd SA = 0.0, SB = 0.0;
        for (size_t k = 0; k < parts.size(); ++k)
        {
            const double a = std::max (kMinR, alpha[k] * kPot), b = std::max (kMinR, (1.0 - alpha[k]) * kPot);
            const cd Z = cd (parts[k].r, w * parts[k].l - 1.0 / (w * parts[k].c));
            const cd Y = 1.0 / Z;
            const double g = 1.0 / a + 1.0 / b;
            const cd common = Y / (g + Y);
            SA += common / a;
            SB += common / b;
        }
        return 20.0 * std::log10 (std::abs ((1.0 + kRf * SB) / (1.0 + kRin * SA)));
    }

    double centreFrequency (size_t k)
    {
        return 1.0 / (2.0 * 3.141592653589793 * std::sqrt (parts[k].l * parts[k].c));
    }

    // With 3K3 input/feedback resistors and 50K linear sliders, most of the
    // circuit's action is in the last part of the travel. The plugin instead
    // takes the wanted gain of each band at its centre frequency in dB and
    // finds the wiper position that gives it with the other sliders centred,
    // so each slider feels linear in dB. Bands still interact as they do in
    // the circuit when several are moved.
    std::array<double, GraphicEQ::numBands> wipersForGains (const std::array<double, GraphicEQ::numBands>& db)
    {
        std::array<double, GraphicEQ::numBands> result;
        const double lo0 = kMinR / kPot, hi0 = 1.0 - kMinR / kPot;
        for (size_t k = 0; k < result.size(); ++k)
        {
            std::array<double, GraphicEQ::numBands> alpha;
            alpha.fill (0.5);
            const double f = centreFrequency (k);
            double lo = lo0, hi = hi0;
            for (int it = 0; it < 40; ++it)
            {
                alpha[k] = 0.5 * (lo + hi);
                (gainDb (alpha, f) < db[k] ? lo : hi) = alpha[k];
            }
            result[k] = 0.5 * (lo + hi);
        }
        return result;
    }
}

double GraphicEQ::maxBoostDb (int band)
{
    std::array<double, numBands> alpha;
    alpha.fill (0.5);
    alpha[(size_t) band] = 1.0 - kMinR / kPot;
    return gainDb (alpha, centreFrequency ((size_t) band));
}

void GraphicEQ::prepare (double sampleRate)
{
    const auto gnd = Circuit::ground;
    Circuit c;
    const int in = c.addNode(), plus = c.addNode(), minus = c.addNode(), out = c.addNode();
    c.addInput (in);
    c.addResistor (in, plus, 3.3e3);      // R236
    c.addResistor (out, minus, 3.3e3);    // R234
    c.addResistor (out, gnd, 3.3e3);      // R235 load
    c.addOpAmp (plus, minus, out);

    for (size_t b = 0; b < bands.size(); ++b)
    {
        const int w = c.addNode(), rl = c.addNode(), lc = c.addNode();
        bands[b].toInput = c.addResistor (plus, w, kPot * 0.5);
        bands[b].toFeedback = c.addResistor (w, minus, kPot * 0.5);
        c.addResistor (w, rl, parts[b].r);
        c.addInductor (rl, lc, parts[b].l);
        c.addCapacitor (lc, gnd, parts[b].c);
    }
    c.addOutput (out);
    solver.build (c, sampleRate);
    reset();
}

void GraphicEQ::setGains (const std::array<double, numBands>& gainsDb)
{
    const auto alpha = wipersForGains (gainsDb);
    for (size_t b = 0; b < bands.size(); ++b)
    {
        const double frac = alpha[b];
        solver.setValue (bands[b].toInput, std::max (kMinR, frac * kPot));
        solver.setValue (bands[b].toFeedback, std::max (kMinR, (1.0 - frac) * kPot));
    }
    solver.update();
}

double GraphicEQ::processSample (double x)
{
    double y;
    solver.process (&x, &y);
    return y;
}

void GraphicEQ::reset()
{
    const double zero = 0.0;
    solver.initDC (&zero);
}

} // namespace mk4
