#include "MarkIVPreamp.h"

#include "Triode.h"

#include <algorithm>
#include <memory>

// How the schematic is split into solver segments
// ------------------------------------------------
// Each segment holds one triode plus the passive network from its plate to the
// next tube's grid. The next grid is modelled with a GridDiode referenced to
// that tube's cathode voltage (fed back from the previous sample), so grid
// current loads the coupling network the way it does in the amp (blocking
// distortion, bias shift). The next segment then sees its grid as an ideal
// voltage source.
//
//   S1  V1A (input stage) + both tone stacks + gain pots + channel select -> V1B grid
//   S2  V1B + C233/R214 + R102 path to V2A grid (R1/R2) + lead send -> Lead Drive -> V3A grid
//   S3  V3A + C222/R275 + R332/750p attenuator -> V4A grid                       (Lead only)
//   S4  V4A + lead return network (C236, R323||C13, R314||C21) -> V2A grid       (Lead only)
//   S5  V2A + C234/R372 + channel master + R203/R295 -> V2B grid
//   S6  V2B recovery + C235/R224/250p + Output Level pot
//
// LDRs and relay contacts are modelled as resistors switched between 10 ohms
// and 1 Gohm so that every segment keeps a fixed structure for all channels.

namespace mk4
{

namespace
{
    constexpr double kSwitchOn = 10.0;
    constexpr double kSwitchOff = 1.0e9;
    constexpr double kMinPot = 20.0;

    constexpr double k = 1.0e3, M = 1.0e6, u = 1.0e-6, pF = 1.0e-12;

    // Input (Miller) capacitance estimates: Cgk + Cgp * (1 + stage gain).
    constexpr double kMillerBypassed = 110.0 * pF;    // fully bypassed cathode, gain ~60
    constexpr double kMillerPartial = 70.0 * pF;      // partly bypassed, gain ~35-40
}

void MarkIVPreamp::prepare (double sampleRate)
{
    fs = sampleRate;
    railCoeff = 1.0 - std::exp (-1.0 / (0.08 * fs));   // ~80 ms rail settling
    buildSegments (fs);
    switches.clear();
    built = true;
    applyParams();
    reset();
}

void MarkIVPreamp::buildSegments (double sr)
{
    const auto gnd = Circuit::ground;

    // ---------------------------------------------------------------- S1
    {
        Circuit c;
        const int g1 = c.addNode(), k1 = c.addNode(), p1 = c.addNode(), rail = c.addNode(), kref = c.addNode();
        c.addInput (g1);      // u0: guitar
        c.addInput (rail);    // u1: rail D
        c.addInput (kref);    // u2: V1B cathode

        c.addDevice (std::make_shared<Triode>(), { { g1, k1 }, { p1, k1 } });

        // V1A cathode: R261 1.5K || C321 0.47u || (C331 2.2u + R222 22K, R222 shorted by LDR7 in R2).
        c.addResistor (k1, gnd, 1.5 * k);
        c.addCapacitor (k1, gnd, 0.47 * u);
        const int c331 = c.addNode();
        c.addCapacitor (k1, c331, 2.2 * u);
        c.addResistor (c331, gnd, 22 * k);
        e1.ldr7 = c.addResistor (c331, gnd, kSwitchOff);

        c.addResistor (rail, p1, 150 * k);   // R201

        // Mark-style passive stack. Returns the treble wiper node.
        auto stack = [&] (Pot& treble, int& bassId, int& midId, int& fatSw,
                          double slope, double cBass, double cMid)
        {
            const int top = c.addNode(), wiper = c.addNode(), bot = c.addNode(), midTop = c.addNode();
            const int junction = c.addNode(), fat = c.addNode();
            c.addCapacitor (p1, top, 250 * pF);                 // C11 / C12 treble cap
            c.addCapacitor (p1, fat, 750 * pF);                 // C31 / C32 "Fat" shift cap
            fatSw = c.addResistor (fat, top, kSwitchOff);       // LDR1 / LDR3 + pull switch
            c.addResistor (fat, top, 10 * M);                   // R361 / R362 anti-pop bleeder
            treble.upper = c.addResistor (top, wiper, 125 * k);
            treble.lower = c.addResistor (wiper, bot, 125 * k);
            treble.total = 250 * k;
            bassId = c.addResistor (bot, midTop, 50 * k);       // Bass 250K rheostat
            midId = c.addResistor (midTop, gnd, 5 * k);         // Mid 10K rheostat
            c.addResistor (p1, junction, slope);                // R211 / R212 slope resistor
            c.addCapacitor (junction, bot, cBass);              // C242 / C241
            c.addCapacitor (junction, midTop, cMid);            // C232 / C231
            return wiper;
        };

        const int g2 = c.addNode();
        c.addCapacitor (g2, gnd, kMillerBypassed);              // V1B input capacitance

        // Lead stack -> Lead Gain (1M) -> LDR4 -> V1B grid.
        const int leadWiper = stack (e1.leadTreble, e1.leadBass, e1.leadMid, e1.leadFatSw, 100 * k, 0.1 * u, 0.047 * u);
        const int lgw = c.addNode();
        e1.leadGain.upper = c.addResistor (leadWiper, lgw, 500 * k);
        e1.leadGain.lower = c.addResistor (lgw, gnd, 500 * k);
        e1.leadGain.total = 1 * M;
        e1.leadSel = c.addResistor (lgw, g2, kSwitchOff);

        // R1/R2 stack -> R1 or R2 Gain (1M, R1 Bright 120p) -> LDR5/LDR10 -> V1B grid.
        const int rhyWiper = stack (e1.rhyTreble, e1.rhyBass, e1.rhyMid, e1.rhyFatSw, 100 * k, 0.1 * u, 0.047 * u);
        const int rgw = c.addNode(), brt = c.addNode();
        e1.rhyGain.upper = c.addResistor (rhyWiper, rgw, 500 * k);
        e1.rhyGain.lower = c.addResistor (rgw, gnd, 500 * k);
        e1.rhyGain.total = 1 * M;
        c.addCapacitor (rhyWiper, brt, 120 * pF);               // BRT cap
        e1.brightSw = c.addResistor (brt, rgw, kSwitchOff);
        e1.rhySel = c.addResistor (rgw, g2, kSwitchOff);

        c.addDevice (std::make_shared<GridDiode>(), { { g2, kref } });

        c.addOutput (g2);
        c.addOutput (k1);
        c.addOutput (p1);
        s1.build (c, sr);
    }

    // ---------------------------------------------------------------- S2
    {
        Circuit c;
        const int g2 = c.addNode(), k2 = c.addNode(), p2 = c.addNode(), rail = c.addNode();
        const int krefV2A = c.addNode(), krefV3A = c.addNode();
        c.addInput (g2);        // u0
        c.addInput (rail);      // u1: rail D
        c.addInput (krefV2A);   // u2
        c.addInput (krefV3A);   // u3

        c.addDevice (std::make_shared<Triode>(), { { g2, k2 }, { p2, k2 } });
        c.addResistor (k2, gnd, 1.5 * k);    // R252
        c.addCapacitor (k2, gnd, 15 * u);    // C343
        c.addResistor (rail, p2, 100 * k);   // R213

        const int x = c.addNode(), y = c.addNode();
        c.addCapacitor (p2, x, 0.047 * u);   // C233
        c.addResistor (x, gnd, 100 * k);     // R214
        c.addResistor (x, y, 3.3 * M);       // R102
        c.addCapacitor (x, y, 20 * pF);      // C2
        e2.ry2a = c.addResistor (x, y, kSwitchOff);   // RY2a shorts R102 in R2
        const int a = c.addNode(), b = c.addNode();
        c.addResistor (y, a, 680 * k);       // R362
        e2.rv2 = c.addResistor (a, gnd, kSwitchOff);  // RV2 (R1, L)
        c.addCapacitor (y, b, 47 * pF);      // C1
        e2.ldr9 = c.addResistor (b, gnd, kSwitchOff); // LDR9 (LD)
        c.addCapacitor (y, gnd, kMillerPartial);      // V2A input capacitance
        c.addDevice (std::make_shared<GridDiode>(), { { y, krefV2A } });

        // Lead send: LDR8 -> R361 680K -> C223 .02 -> Lead Drive 1M -> V3A grid (R264 470K, 180p).
        const int ls = c.addNode(), lc = c.addNode(), ld = c.addNode(), lw = c.addNode();
        e2.ldr8 = c.addResistor (x, ls, kSwitchOff);
        c.addResistor (ls, lc, 680 * k);
        c.addCapacitor (lc, ld, 0.02 * u);
        e2.drive.upper = c.addResistor (ld, lw, 500 * k);
        e2.drive.lower = c.addResistor (lw, gnd, 500 * k);
        e2.drive.total = 1 * M;
        c.addResistor (lw, gnd, 470 * k);              // R264
        c.addCapacitor (lw, krefV3A, 180 * pF);        // 180p at V3A
        c.addCapacitor (lw, gnd, kMillerBypassed);
        c.addDevice (std::make_shared<GridDiode>(), { { lw, krefV3A } });

        c.addOutput (y);
        c.addOutput (lw);
        c.addOutput (k2);
        c.addOutput (p2);
        c.addOutput (x);
        s2.build (c, sr);
    }

    // ---------------------------------------------------------------- S3 (V3A)
    {
        Circuit c;
        const int g = c.addNode(), kk = c.addNode(), pp = c.addNode(), rail = c.addNode(), kref = c.addNode();
        c.addInput (g);
        c.addInput (rail);   // rail C
        c.addInput (kref);   // V4A cathode

        c.addDevice (std::make_shared<Triode>(), { { g, kk }, { pp, kk } });
        c.addResistor (kk, gnd, 1.5 * k);    // R265
        c.addCapacitor (kk, gnd, 2.2 * u);   // C333
        c.addResistor (rail, pp, 82 * k);    // R282

        const int a = c.addNode(), m = c.addNode();
        c.addCapacitor (pp, a, 0.02 * u);    // C222
        c.addResistor (a, m, 270 * k);       // R275
        c.addResistor (m, gnd, 68 * k);      // R332
        c.addCapacitor (m, gnd, 750 * pF);
        c.addCapacitor (m, gnd, kMillerPartial);
        c.addDevice (std::make_shared<GridDiode>(), { { m, kref } });

        c.addOutput (m);
        c.addOutput (kk);
        c.addOutput (pp);
        s3.build (c, sr);
    }

    // ---------------------------------------------------------------- S4 (V4A + lead return)
    {
        Circuit c;
        const int g = c.addNode(), kk = c.addNode(), pp = c.addNode(), rail = c.addNode();
        const int xin = c.addNode(), kref = c.addNode();
        c.addInput (g);
        c.addInput (rail);   // rail C
        c.addInput (xin);    // V1B coupling node (R102 path)
        c.addInput (kref);   // V2A cathode

        c.addDevice (std::make_shared<Triode>(), { { g, kk }, { pp, kk } });
        c.addResistor (kk, gnd, 3.3 * k);    // R238
        c.addCapacitor (kk, gnd, 250 * pF);
        const int e = c.addNode();
        c.addCapacitor (kk, e, 2.2 * u);     // C311
        c.addResistor (e, gnd, 22 * k);      // R226
        e4.ldBright = c.addResistor (e, gnd, kSwitchOff);   // LD BRT
        c.addResistor (rail, pp, 270 * k);   // R276

        const int r = c.addNode(), z = c.addNode(), y = c.addNode();
        c.addCapacitor (pp, r, 0.047 * u);   // C236
        c.addResistor (r, z, 220 * k);       // R323
        c.addCapacitor (r, z, 250 * pF);     // C13
        c.addResistor (z, gnd, 100 * k);     // R314
        c.addCapacitor (z, gnd, 500 * pF);   // C21
        e4.ldr11 = c.addResistor (z, y, kSwitchOn);         // LDR11 (L)
        e4.muteZ = c.addResistor (z, gnd, kSwitchOff);      // LDR13/14 mute outside Lead

        // V2A grid node as seen in Lead mode.
        c.addResistor (xin, y, 3.3 * M);     // R102
        c.addCapacitor (xin, y, 20 * pF);    // C2
        c.addResistor (y, gnd, 680 * k);     // R362 via RV2 (L)
        c.addCapacitor (y, gnd, 47 * pF);    // C1 via LDR9 (LD)
        c.addCapacitor (y, gnd, kMillerPartial);
        c.addDevice (std::make_shared<GridDiode>(), { { y, kref } });

        c.addOutput (y);
        c.addOutput (kk);
        c.addOutput (pp);
        s4.build (c, sr);
    }

    // ---------------------------------------------------------------- S5 (V2A + masters)
    {
        Circuit c;
        const int g = c.addNode(), kk = c.addNode(), pp = c.addNode(), rail = c.addNode(), kref = c.addNode();
        c.addInput (g);
        c.addInput (rail);   // rail C
        c.addInput (kref);   // V2B cathode

        c.addDevice (std::make_shared<Triode>(), { { g, kk }, { pp, kk } });
        c.addResistor (kk, gnd, 1.5 * k);    // R263
        const int c1 = c.addNode(), c2 = c.addNode(), mg = c.addNode();
        c.addCapacitor (kk, c1, 2.2 * u);    // C332
        e5.rv1 = c.addResistor (c1, c2, kSwitchOff);    // RV1 (R2, L)
        c.addResistor (c2, gnd, 22 * k);     // R223
        e5.rv2 = c.addResistor (c2, gnd, kSwitchOff);   // RV2 (R2) shorts R223
        c.addCapacitor (kk, mg, 16 * u);     // C344
        e5.midGain = c.addResistor (mg, gnd, kSwitchOff);  // MID GAIN switch (Lead)
        c.addResistor (rail, pp, 100 * k);   // R215

        const int o = c.addNode(), nn = c.addNode(), q = c.addNode(), pg = c.addNode();
        c.addCapacitor (pp, o, 0.047 * u);   // C234
        c.addResistor (o, nn, 47 * k);       // R372
        e5.master = c.addResistor (nn, gnd, 50 * k);   // channel master, 100K rheostat
        c.addResistor (nn, q, 150 * k);      // R203
        c.addResistor (q, pg, 39.0);         // R295
        c.addResistor (pg, gnd, 47 * k);     // R371
        c.addCapacitor (pg, gnd, kMillerBypassed);
        c.addDevice (std::make_shared<GridDiode>(), { { pg, kref } });

        c.addOutput (pg);
        c.addOutput (kk);
        c.addOutput (pp);
        s5.build (c, sr);
    }

    // ---------------------------------------------------------------- S6 (V2B recovery)
    {
        Circuit c;
        const int g = c.addNode(), kk = c.addNode(), pp = c.addNode(), rail = c.addNode();
        c.addInput (g);
        c.addInput (rail);   // rail C

        c.addDevice (std::make_shared<Triode>(), { { g, kk }, { pp, kk } });
        c.addResistor (kk, gnd, 1 * k);      // R311
        c.addCapacitor (kk, gnd, 15 * u);    // C345
        c.addResistor (rail, pp, 120 * k);   // R103

        const int o = c.addNode(), q = c.addNode(), w = c.addNode();
        c.addCapacitor (pp, o, 0.047 * u);   // C235
        c.addResistor (o, q, 22 * k);        // R224
        c.addCapacitor (q, gnd, 250 * pF);
        e6.level.upper = c.addResistor (q, w, 125 * k);   // Output Level 250KA
        e6.level.lower = c.addResistor (w, gnd, 125 * k);
        e6.level.total = 250 * k;

        c.addOutput (w);
        c.addOutput (kk);
        c.addOutput (pp);
        s6.build (c, sr);
    }
}

void MarkIVPreamp::setPot (DKSolver& s, const Pot& p, double fractionFromBottom)
{
    const double lower = std::clamp (fractionFromBottom * p.total, kMinPot, p.total - kMinPot);
    s.setValue (p.lower, lower);
    s.setValue (p.upper, p.total - lower);
}

void MarkIVPreamp::setSwitch (DKSolver& s, int id, bool on)
{
    for (auto& sw : switches)
        if (sw.solver == &s && sw.id == id)
        {
            sw.target = on ? 1.0 : 0.0;
            return;
        }
    switches.push_back ({ &s, id, on ? 1.0 : 0.0, on ? 1.0 : 0.0 });
    s.setValue (id, on ? kSwitchOn : kSwitchOff);
}

double MarkIVPreamp::switchAmount (DKSolver& s, int id) const
{
    for (auto& sw : switches)
        if (sw.solver == &s && sw.id == id)
            return sw.amount;
    return 0.0;
}

void MarkIVPreamp::advance (int numSamples, bool snap)
{
    // LDRs and relays don't switch instantly; fading the resistance over ~12 ms
    // also avoids clicks when changing channel.
    const double step = snap ? 1.0 : numSamples / (0.012 * fs);
    bool moved = false;
    for (auto& sw : switches)
    {
        if (sw.amount == sw.target)
            continue;
        sw.amount = sw.target > sw.amount ? std::min (sw.target, sw.amount + step)
                                          : std::max (sw.target, sw.amount - step);
        const double logR = std::log (kSwitchOff) + sw.amount * (std::log (kSwitchOn) - std::log (kSwitchOff));
        sw.solver->setValue (sw.id, std::exp (logR));
        moved = true;
    }
    if (moved)
        for (auto* sv : { &s1, &s2, &s3, &s4, &s5, &s6 })
            sv->update();
    leadMix = switchAmount (s4, e4.ldr11);
}

void MarkIVPreamp::setRheostat (DKSolver& s, int id, double total, double fraction)
{
    s.setValue (id, std::max (kMinPot, fraction * total));
}

void MarkIVPreamp::setParams (const PreampParams& p)
{
    params = p;
    if (built)
        applyParams();
}

void MarkIVPreamp::applyParams()
{
    const auto& p = params;
    const bool r1 = p.channel == Channel::Rhythm1;
    const bool r2 = p.channel == Channel::Rhythm2;
    const bool ld = p.channel == Channel::Lead;

    // S1
    setSwitch (s1, e1.ldr7, r2);
    setPot (s1, e1.leadTreble, audioTaper (p.leadTreble));
    setRheostat (s1, e1.leadBass, 250 * k, audioTaper (p.leadBass));
    setRheostat (s1, e1.leadMid, 10 * k, audioTaper (p.leadMid));
    setSwitch (s1, e1.leadFatSw, ld && p.leadFat);
    setPot (s1, e1.leadGain, audioTaper (p.leadGain));
    setSwitch (s1, e1.leadSel, ld);

    setPot (s1, e1.rhyTreble, audioTaper (r2 ? p.r2Treble : p.r1Treble));
    setRheostat (s1, e1.rhyBass, 250 * k, audioTaper (p.bass));
    setRheostat (s1, e1.rhyMid, 10 * k, audioTaper (p.mid));
    setSwitch (s1, e1.rhyFatSw, r2 && p.r2Fat);
    setPot (s1, e1.rhyGain, audioTaper (r2 ? p.r2Gain : p.r1Gain));
    setSwitch (s1, e1.brightSw, r1 && p.r1Bright);
    setSwitch (s1, e1.rhySel, ! ld);

    // S2
    setSwitch (s2, e2.ry2a, r2);
    setSwitch (s2, e2.rv2, r1 || ld);
    setSwitch (s2, e2.ldr9, ld);
    setSwitch (s2, e2.ldr8, ld);
    setPot (s2, e2.drive, audioTaper (p.leadDrive));

    // S4
    setSwitch (s4, e4.ldBright, p.leadBright);
    setSwitch (s4, e4.ldr11, ld);
    setSwitch (s4, e4.muteZ, ! ld);

    // S5
    setSwitch (s5, e5.rv1, r2 || ld);
    setSwitch (s5, e5.rv2, r2);
    setSwitch (s5, e5.midGain, ld && p.midGain);
    const double master = r1 ? p.r1Master : (r2 ? p.r2Master : p.leadMaster);
    setRheostat (s5, e5.master, 100 * k, audioTaper (master));

    // S6
    setPot (s6, e6.level, audioTaper (p.outputLevel));

    for (auto* s : { &s1, &s2, &s3, &s4, &s5, &s6 })
        s->update();
    advance (0, false);

    targetC = ld ? railC_Lead : railC_Rhythm;
    targetD = ld ? railD_Lead : railD_Rhythm;
}

void MarkIVPreamp::reset()
{
    advance (0, true);
    railC = targetC;
    railD = targetD;
    computeDC();
}

void MarkIVPreamp::computeDC()
{
    // Initial guesses for the cathode references.
    const double vkGuess[6] = { 1.6, 1.8, 2.0, 2.0, 2.0, 1.5 };
    std::copy (vkGuess, vkGuess + 6, vk);

    double y1[3], y2[5], y3[3], y4[3], y5[3], y6[3];
    for (int pass = 0; pass < 4; ++pass)
    {
        const double u1[3] = { 0.0, railD, vk[1] };
        s1.initDC (u1);
        s1.process (u1, y1);

        const double u2[4] = { y1[0], railD, vk[4], vk[2] };
        s2.initDC (u2);
        s2.process (u2, y2);

        const double u3[3] = { y2[1], railC, vk[3] };
        s3.initDC (u3);
        s3.process (u3, y3);

        const double u4[4] = { y3[0], railC, y2[4], vk[4] };
        s4.initDC (u4);
        s4.process (u4, y4);

        const double yIn = leadMix * y4[0] + (1.0 - leadMix) * y2[0];
        const double u5[3] = { yIn, railC, vk[5] };
        s5.initDC (u5);
        s5.process (u5, y5);

        const double u6[2] = { y5[0], railC };
        s6.initDC (u6);
        s6.process (u6, y6);

        vk[0] = y1[1]; vp[0] = y1[2];
        vk[1] = y2[2]; vp[1] = y2[3];
        vk[2] = y3[1]; vp[2] = y3[2];
        vk[3] = y4[1]; vp[3] = y4[2];
        vk[4] = y5[1]; vp[4] = y5[2];
        vk[5] = y6[1]; vp[5] = y6[2];
        x2 = y2[4];
    }

    // Let the coupling networks settle completely.
    for (int i = 0; i < (int) (0.05 * fs); ++i)
        processSample (0.0);
}

double MarkIVPreamp::processSample (double in)
{
    railC += (targetC - railC) * railCoeff;
    railD += (targetD - railD) * railCoeff;

    double y1[3], y2[5], y3[3], y4[3], y5[3], y6[3];

    const double u1[3] = { in, railD, vk[1] };
    s1.process (u1, y1);

    const double u2[4] = { y1[0], railD, vk[4], vk[2] };
    s2.process (u2, y2);

    double v2aGrid = y2[0];
    if (params.channel == Channel::Lead || leadMix > 0.0)
    {
        const double u3[3] = { y2[1], railC, vk[3] };
        s3.process (u3, y3);
        const double u4[4] = { y3[0], railC, y2[4], vk[4] };
        s4.process (u4, y4);
        v2aGrid = leadMix * y4[0] + (1.0 - leadMix) * y2[0];
        vk[2] = y3[1]; vp[2] = y3[2];
        vk[3] = y4[1]; vp[3] = y4[2];
    }

    const double u5[3] = { v2aGrid, railC, vk[5] };
    s5.process (u5, y5);

    const double u6[2] = { y5[0], railC };
    s6.process (u6, y6);

    vk[0] = y1[1]; vp[0] = y1[2];
    vk[1] = y2[2]; vp[1] = y2[3];
    vk[4] = y5[1]; vp[4] = y5[2];
    vk[5] = y6[1]; vp[5] = y6[2];
    x2 = y2[4];

    return y6[0];
}

MarkIVPreamp::OperatingPoint MarkIVPreamp::operatingPoint (int stage) const
{
    return { vk[stage], vp[stage] };
}

} // namespace mk4
