// Measures steady-state sine gain of the EQ and the small-signal preamp response.
#include "GraphicEQ.h"
#include "MarkIVPreamp.h"
#include <cmath>
#include <cstdio>
#include <functional>
using namespace mk4;

static double gainAt (double f, double fs, double amp, const std::function<double (double)>& proc)
{
    int settle = (int) (fs * 0.15), meas = (int) std::max (fs * 0.05, 4 * fs / f);
    double sc = 0, ss = 0;
    for (int i = 0; i < settle + meas; ++i)
    {
        double x = amp * std::sin (2 * M_PI * f * i / fs);
        double y = proc (x);
        if (i >= settle) { sc += y * std::cos (2 * M_PI * f * i / fs); ss += y * std::sin (2 * M_PI * f * i / fs); }
    }
    return 2 * std::sqrt (sc * sc + ss * ss) / meas / amp;
}

int main()
{
    const double fs = 96000;
    const double freqs[] = { 40, 80, 150, 240, 372, 500, 750, 1000, 1576, 2200, 3300, 4823, 6600, 10000 };
    printf ("EQ (dB):        ");
    for (double f : freqs) printf ("%7.0f", f);
    printf ("\n");
    const char* names[] = { "flat", "80+", "240+", "750+", "2200+", "6600+", "80-", "750-", "classicV" };
    for (int cfg = 0; cfg < 9; ++cfg)
    {
        GraphicEQ eq; eq.prepare (fs);
        std::array<double, 5> s {};
        if (cfg >= 1 && cfg <= 5) s[(size_t) cfg - 1] = 1;
        if (cfg == 6) s[0] = -1;
        if (cfg == 7) s[2] = -1;
        if (cfg == 8) s = { 0.8, 0.4, -0.6, 0.4, 0.8 };
        for (auto& v : s) v *= 15.0;
        eq.setGains (s);
        printf ("  %-12s ", names[cfg]);
        for (double f : freqs)
        {
            eq.reset();
            printf ("%7.1f", 20 * std::log10 (gainAt (f, fs, 1.0, [&] (double x) { return eq.processSample (x); })));
        }
        printf ("\n");
    }

    printf ("\nPreamp small-signal (dB re 1V in), tone controls at defaults:\n");
    for (int ch = 0; ch < 3; ++ch)
    {
        for (int trebleCase = 0; trebleCase < 3; ++trebleCase)
        {
            MarkIVPreamp pre;
            PreampParams p; p.channel = (Channel) ch;
            double tr = trebleCase == 0 ? 0.0 : (trebleCase == 1 ? 0.5 : 1.0);
            p.r1Treble = p.r2Treble = p.leadTreble = tr;
            p.r1Gain = 0.3; p.r2Gain = 0.3; p.leadGain = 0.3; p.leadDrive = 0.2;
            pre.setParams (p);
            pre.prepare (fs);
            printf ("  ch%d treble %.1f ", ch, tr);
            for (double f : freqs)
            {
                pre.reset();
                printf ("%7.1f", 20 * std::log10 (gainAt (f, fs, 1e-4, [&] (double x) { return pre.processSample (x); })));
            }
            printf ("\n");
        }
    }
}
