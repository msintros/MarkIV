// Renders short demo clips of each channel to WAV (no JUCE needed).
// Usage: markiv_render <output-dir>
#include "GraphicEQ.h"
#include "MarkIVPreamp.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace mk4;

namespace
{
// Plucked-string (Karplus-Strong) riff standing in for a guitar DI, ~0.3 V peaks.
std::vector<double> makeRiff (double fs)
{
    struct Note { double t, f, dur; };
    std::vector<Note> notes;
    const double E2 = 82.41, A2 = 110.0, D3 = 146.83, G3 = 196.0, B3 = 246.94, E4 = 329.63;
    // Palm-muted chug, then an open chord, then a short lead line.
    for (int i = 0; i < 8; ++i) notes.push_back ({ i * 0.18, E2, 0.15 });
    for (double f : { E2, 123.47, E2 * 2, G3, B3, E4 }) notes.push_back ({ 1.6, f, 1.8 });
    double t = 3.6;
    for (double f : { 392.0, 440.0, 493.88, 587.33, 659.25, 587.33, 493.88, 440.0 }) { notes.push_back ({ t, f, 0.3 }); t += 0.25; }
    notes.push_back ({ t, 329.63 * 2, 1.5 });
    (void) A2; (void) D3;

    const int total = (int) ((t + 2.0) * fs);
    std::vector<double> out ((size_t) total, 0.0);
    std::mt19937 rng (1);
    std::uniform_real_distribution<double> noise (-1.0, 1.0);
    for (auto& n : notes)
    {
        const int len = (int) (fs / n.f);
        std::vector<double> buf ((size_t) len);
        for (auto& b : buf) b = noise (rng);
        // Soften the pluck.
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 1; i < len; ++i) buf[(size_t) i] = 0.5 * (buf[(size_t) i] + buf[(size_t) i - 1]);
        const int start = (int) (n.t * fs), dur = (int) (n.dur * fs);
        const double decay = n.dur < 0.2 ? 0.990 : 0.9985;
        int idx = 0;
        for (int i = 0; i < dur && start + i < total; ++i)
        {
            const int nxt = (idx + 1) % len;
            const double y = buf[(size_t) idx];
            buf[(size_t) idx] = decay * 0.5 * (buf[(size_t) idx] + buf[(size_t) nxt]);
            idx = nxt;
            const double env = i > dur - 400 ? (dur - i) / 400.0 : 1.0;
            out[(size_t) (start + i)] += 0.12 * y * env;
        }
    }
    return out;
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    static Biquad peak (double fs, double f, double q, double db)
    {
        const double A = std::pow (10, db / 40), w = 2 * M_PI * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        const double a0 = 1 + al / A;
        return { (1 + al * A) / a0, -2 * c / a0, (1 - al * A) / a0, -2 * c / a0, (1 - al / A) / a0 };
    }
    static Biquad lowpass (double fs, double f, double q)
    {
        const double w = 2 * M_PI * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        return { (1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0 };
    }
    static Biquad highpass (double fs, double f, double q)
    {
        const double w = 2 * M_PI * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        return { (1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0 };
    }
};

void writeWav (const std::string& path, const std::vector<double>& x, int fs)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    const uint32_t bytes = (uint32_t) x.size() * 2;
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (1); w32 ((uint32_t) fs); w32 ((uint32_t) fs * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    for (double v : x) { const int16_t s = (int16_t) std::lround (std::max (-1.0, std::min (1.0, v)) * 32767); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

// Decimate by 4 with a simple windowed-sinc FIR (offline, so cost doesn't matter).
std::vector<double> decimate4 (const std::vector<double>& x)
{
    const int taps = 127;
    std::vector<double> h (taps);
    for (int i = 0; i < taps; ++i)
    {
        const double m = i - (taps - 1) / 2.0, fc = 0.11;
        const double sinc = m == 0 ? 2 * fc : std::sin (2 * M_PI * fc * m) / (M_PI * m);
        h[(size_t) i] = sinc * (0.42 - 0.5 * std::cos (2 * M_PI * i / (taps - 1)) + 0.08 * std::cos (4 * M_PI * i / (taps - 1)));
    }
    std::vector<double> y;
    for (size_t n = 0; n < x.size(); n += 4)
    {
        double acc = 0;
        for (int k = 0; k < taps; ++k)
            if (n >= (size_t) k) acc += h[(size_t) k] * x[n - (size_t) k];
        y.push_back (acc);
    }
    return y;
}

std::vector<double> upsample4 (const std::vector<double>& x)
{
    std::vector<double> z (x.size() * 4, 0.0);
    for (size_t i = 0; i < x.size(); ++i) z[i * 4] = 4 * x[i];
    auto y = decimate4 (z);   // reuse the same lowpass design
    (void) y;
    // Filter at the high rate (no decimation).
    const int taps = 127;
    std::vector<double> h (taps), out (z.size(), 0.0);
    for (int i = 0; i < taps; ++i)
    {
        const double m = i - (taps - 1) / 2.0, fc = 0.11;
        const double sinc = m == 0 ? 2 * fc : std::sin (2 * M_PI * fc * m) / (M_PI * m);
        h[(size_t) i] = sinc * (0.42 - 0.5 * std::cos (2 * M_PI * i / (taps - 1)) + 0.08 * std::cos (4 * M_PI * i / (taps - 1)));
    }
    for (size_t n = 0; n < z.size(); ++n)
    {
        double acc = 0;
        for (int k = 0; k < taps; k += 1)
            if (n >= (size_t) k && z[n - (size_t) k] != 0.0) acc += h[(size_t) k] * z[n - (size_t) k];
        out[n] = acc;
    }
    return out;
}
} // namespace

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const int fs = 48000;
    const auto di = makeRiff (fs);
    const auto diUp = upsample4 (di);
    writeWav (dir + "/00_di_input.wav", di, fs);

    struct Setting { const char* name; PreampParams p; bool eq; std::array<double, 5> sliders; double trim; };
    std::vector<Setting> settings;
    { PreampParams p; p.channel = Channel::Rhythm1; p.r1Gain = 0.6; p.r1Treble = 0.65; p.r1Master = 0.6; p.outputLevel = 0.6;
      settings.push_back ({ "01_rhythm1_clean", p, false, {}, 0.0 }); }
    { PreampParams p; p.channel = Channel::Rhythm1; p.r1Gain = 0.4; p.r1Bright = true; p.r1Treble = 0.6; p.r1Master = 0.6; p.outputLevel = 0.6;
      settings.push_back ({ "02_rhythm1_bright", p, false, {}, 0.0 }); }
    { PreampParams p; p.channel = Channel::Rhythm2; p.r2Gain = 0.9; p.r2Fat = true; p.r2Treble = 0.6; p.outputLevel = 0.6;
      settings.push_back ({ "03_rhythm2_crunch_fat", p, false, {}, 0.0 }); }
    { PreampParams p; p.channel = Channel::Lead; p.leadGain = 0.75; p.leadDrive = 0.6; p.leadTreble = 0.65; p.outputLevel = 0.6;
      settings.push_back ({ "04_lead", p, false, {}, 0.0 }); }
    { PreampParams p; p.channel = Channel::Lead; p.leadGain = 0.75; p.leadDrive = 0.6; p.leadTreble = 0.65; p.outputLevel = 0.6;
      settings.push_back ({ "05_lead_classic_v_eq", p, true, { 9, 5, -8, 5, 9 }, 0.0 }); }
    { PreampParams p; p.channel = Channel::Lead; p.leadGain = 0.8; p.leadDrive = 0.7; p.leadTreble = 0.7; p.leadFat = true; p.leadBright = true; p.midGain = true; p.outputLevel = 0.6;
      settings.push_back ({ "06_lead_fat_bright_midgain", p, false, {}, 0.0 }); }

    for (auto& s : settings)
    {
        MarkIVPreamp pre;
        pre.setParams (s.p);
        pre.prepare (fs * 4.0);
        std::vector<double> up (diUp.size());
        for (size_t i = 0; i < diUp.size(); ++i)
            up[i] = pre.processSample (diUp[i]);
        auto y = decimate4 (up);

        GraphicEQ eq; eq.prepare (fs); eq.setGains (s.sliders);
        auto hp = Biquad::highpass (fs, 75, 0.8), lr = Biquad::peak (fs, 110, 1.2, 4), bx = Biquad::peak (fs, 450, 1.0, -4);
        auto um = Biquad::peak (fs, 2400, 1.5, 4), lp1 = Biquad::lowpass (fs, 4800, 0.9), lp2 = Biquad::lowpass (fs, 6500, 0.6);
        double peak = 1e-9;
        for (auto& v : y)
        {
            double x = v;
            if (s.eq) x = eq.processSample (x);
            x = lp2.process (lp1.process (um.process (bx.process (lr.process (hp.process (x))))));
            v = x;
            peak = std::max (peak, std::abs (x));
        }
        for (auto& v : y) v *= 0.7 / peak;   // normalise each clip for listening
        writeWav (dir + "/" + s.name + ".wav", y, fs);
        std::printf ("%s: raw peak %.2f V\n", s.name, peak);
    }
}
