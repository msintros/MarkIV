// Sanity checks for the circuit model. Runs without JUCE: `ctest` or ./markiv_tests
#include "GraphicEQ.h"
#include "MarkIVPreamp.h"

#include <cmath>
#include <cstdio>
#include <functional>

using namespace mk4;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static double sineGain (double f, double fs, double amp, const std::function<double (double)>& proc)
{
    const int settle = (int) (fs * 0.2), meas = (int) (fs * 0.1);
    double sc = 0, ss = 0;
    for (int i = 0; i < settle + meas; ++i)
    {
        const double ph = 2 * M_PI * f * i / fs;
        const double y = proc (amp * std::sin (ph));
        if (i >= settle) { sc += y * std::cos (ph); ss += y * std::sin (ph); }
    }
    return 2 * std::sqrt (sc * sc + ss * ss) / meas / amp;
}

int main()
{
    // 1. DC operating points vs the voltages printed on the schematic (rhythm rails).
    {
        const char* names[6] = { "V1A", "V1B", "V3A", "V4A", "V2A", "V2B" };
        const double vk[6] = { 1.6, 1.8, 2.0, 2.0, 2.0, 1.5 };
        const double vp[6] = { 210, 255, 265, 220, 257, 215 };
        MarkIVPreamp pre;
        pre.prepare (96000.0);
        std::printf ("DC operating points (model vs schematic):\n");
        for (int s = 0; s < 6; ++s)
        {
            const auto op = pre.operatingPoint (s);
            std::printf ("  %s  Vk %5.2f V (%.1f)   Vp %5.1f V (%.0f)\n", names[s], op.vk, vk[s], op.vp, vp[s]);
            CHECK (std::abs (op.vp - vp[s]) / vp[s] < 0.10, "%s plate %.1f V vs %.0f V", names[s], op.vp, vp[s]);
            CHECK (std::abs (op.vk - vk[s]) / vk[s] < 0.20, "%s cathode %.2f V vs %.1f V", names[s], op.vk, vk[s]);
        }
    }

    // 2. Every channel stays finite and bounded with a hot input, including channel switches.
    {
        MarkIVPreamp pre;
        PreampParams p;
        pre.prepare (96000.0);
        double peak = 0;
        bool finite = true;
        for (int ch = 0; ch < 3; ++ch)
        {
            p.channel = (Channel) ch;
            p.r1Gain = p.r2Gain = p.leadGain = p.leadDrive = 1.0;
            p.r1Treble = p.r2Treble = p.leadTreble = 1.0;
            p.r1Master = p.r2Master = p.leadMaster = 1.0;
            p.outputLevel = 1.0;
            p.r1Bright = p.r2Fat = p.leadFat = p.leadBright = p.midGain = true;
            pre.setParams (p);
            for (int i = 0; i < 96000; ++i)
            {
                if (i % 32 == 0) pre.advance (32);
                const double y = pre.processSample (2.0 * std::sin (2 * M_PI * 110 * i / 96000.0));
                finite = finite && std::isfinite (y);
                peak = std::max (peak, std::abs (y));
            }
        }
        std::printf ("Max output, everything on 10 with a 2 V input: %.1f V\n", peak);
        CHECK (finite, "non-finite output");
        CHECK (peak < 200.0, "output peak %.1f V is implausible", peak);
    }

    // 3. Channel gain ordering at the same knob settings: R1 < Lead, R1 < R2.
    {
        double g[3];
        for (int ch = 0; ch < 3; ++ch)
        {
            MarkIVPreamp pre;
            PreampParams p;
            p.channel = (Channel) ch;
            pre.setParams (p);
            pre.prepare (96000.0);
            g[ch] = sineGain (500, 96000.0, 1e-4, [&] (double x) { return pre.processSample (x); });
        }
        std::printf ("Small-signal gain at 500 Hz: R1 %.1f dB, R2 %.1f dB, Lead %.1f dB\n",
                     20 * std::log10 (g[0]), 20 * std::log10 (g[1]), 20 * std::log10 (g[2]));
        CHECK (g[0] < g[1] && g[0] < g[2], "channel gain ordering");
    }

    // 4. Graphic EQ: flat when centred, boosts/cuts by about the max at band centre.
    {
        const double fs = 48000.0;
        GraphicEQ eq;
        eq.prepare (fs);
        eq.setGains ({ 0, 0, 0, 0, 0 });
        for (double f : { 80.0, 750.0, 6600.0 })
        {
            eq.reset();
            const double db = 20 * std::log10 (sineGain (f, fs, 1.0, [&] (double x) { return eq.processSample (x); }));
            CHECK (std::abs (db) < 0.1, "EQ not flat at %.0f Hz (%.2f dB)", f, db);
        }
        for (int b = 0; b < 5; ++b)
            std::printf ("EQ band %s: max boost %.1f dB\n", GraphicEQ::labels[(size_t) b], GraphicEQ::maxBoostDb (b));
        eq.setGains ({ 0, 0, -10, 0, 0 });
        eq.reset();
        const double cut = 20 * std::log10 (sineGain (723, fs, 1.0, [&] (double x) { return eq.processSample (x); }));
        std::printf ("EQ 750 set to -10 dB, measured at its resonance (723 Hz): %.1f dB\n", cut);
        CHECK (std::abs (cut + 10.0) < 1.0, "750 band gain law off: %.1f dB", cut);
    }

    std::printf (failures == 0 ? "All checks passed.\n" : "%d check(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
