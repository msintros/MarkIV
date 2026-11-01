// 12AX7 triode model (K. Dempwolf & U. Zölzer, "A physically-motivated triode
// model for circuit simulations", DAFx 2011). Smooth everywhere, which keeps
// Newton's method well behaved.
#pragma once

#include "DKSolver.h"

#include <cmath>
#include <vector>

namespace mk4
{

struct TriodeParams
{
    // 12AX7 fit from the paper.
    double G = 2.242e-3, mu = 103.2, gamma = 1.26, C = 3.40;
    double Gg = 6.177e-4, xi = 1.314, Cg = 9.901;
};

namespace tube
{
    inline double softplus (double x) { return x > 30.0 ? x : (x < -30.0 ? std::exp (x) : std::log1p (std::exp (x))); }
    inline double sigmoid (double x)  { return x >= 0.0 ? 1.0 / (1.0 + std::exp (-x)) : std::exp (x) / (1.0 + std::exp (x)); }

    // Grid current and its derivative w.r.t. Vgk.
    inline void gridCurrent (const TriodeParams& t, double vgk, double& ig, double& dig)
    {
        const double sp = softplus (t.Cg * vgk) / t.Cg;
        if (sp <= 0.0) { ig = 0.0; dig = 0.0; return; }
        const double pw = std::pow (sp, t.xi - 1.0);
        ig = t.Gg * pw * sp;
        dig = t.Gg * t.xi * pw * sigmoid (t.Cg * vgk);
    }

    // Cathode current and its derivative w.r.t. a = Vpk/mu + Vgk.
    inline void cathodeCurrent (const TriodeParams& t, double vgk, double vpk, double& ik, double& dik)
    {
        const double a = vpk / t.mu + vgk;
        const double sp = softplus (t.C * a) / t.C;
        if (sp <= 0.0) { ik = 0.0; dik = 0.0; return; }
        const double pw = std::pow (sp, t.gamma - 1.0);
        ik = t.G * pw * sp;
        dik = t.G * t.gamma * pw * sigmoid (t.C * a);
    }
}

// The two characteristic curves are each a function of a single variable
// (Ig of Vgk, Ik of Vpk/mu + Vgk), so they are tabulated once with cubic
// Hermite interpolation. That keeps the per-sample Newton solve free of
// exp/log/pow calls; outside the table range the exact formula is used.
class TubeTable
{
public:
    static const TubeTable& get (const TriodeParams& t = {})
    {
        static const TubeTable table (t);
        return table;
    }

    void grid (double vgk, double& ig, double& dig) const
    {
        if (vgk <= gx0) { ig = 0.0; dig = 0.0; return; }
        if (vgk >= gx1) { tube::gridCurrent (p, vgk, ig, dig); return; }
        interp (gf, gd, gx0, vgk, ig, dig);
    }

    void cathode (double vgk, double vpk, double& ik, double& dik) const
    {
        const double a = vpk / p.mu + vgk;
        if (a <= kx0) { ik = 0.0; dik = 0.0; return; }
        if (a >= kx1) { tube::cathodeCurrent (p, vgk, vpk, ik, dik); return; }
        interp (kf, kd, kx0, a, ik, dik);
    }

    const TriodeParams p;

private:
    static constexpr int size = 8192;
    static constexpr double gx0 = -4.0, gx1 = 12.0, kx0 = -6.0, kx1 = 14.0;
    double gh, kh, gInv, kInv;
    std::vector<double> gf, gd, kf, kd;

    explicit TubeTable (const TriodeParams& t) : p (t)
    {
        gh = (gx1 - gx0) / (size - 1); gInv = 1.0 / gh;
        kh = (kx1 - kx0) / (size - 1); kInv = 1.0 / kh;
        gf.resize (size); gd.resize (size); kf.resize (size); kd.resize (size);
        for (int i = 0; i < size; ++i)
        {
            tube::gridCurrent (p, gx0 + i * gh, gf[(size_t) i], gd[(size_t) i]);
            // Cathode current as a function of a: evaluate with vpk = 0, vgk = a.
            tube::cathodeCurrent (p, kx0 + i * kh, 0.0, kf[(size_t) i], kd[(size_t) i]);
        }
    }

    void interp (const std::vector<double>& f, const std::vector<double>& d, double x0, double x,
                 double& y, double& dy) const
    {
        const bool isGrid = (&f == &gf);
        const double inv = isGrid ? gInv : kInv;
        const double h = isGrid ? gh : kh;
        const double pos = (x - x0) * inv;
        int i = (int) pos;
        if (i > size - 2) i = size - 2;
        const double t = pos - i;
        const double f0 = f[(size_t) i], f1 = f[(size_t) i + 1];
        const double d0 = d[(size_t) i] * h, d1 = d[(size_t) i + 1] * h;
        const double t2 = t * t, t3 = t2 * t;
        y = (2 * t3 - 3 * t2 + 1) * f0 + (t3 - 2 * t2 + t) * d0 + (-2 * t3 + 3 * t2) * f1 + (t3 - t2) * d1;
        dy = ((6 * t2 - 6 * t) * f0 + (3 * t2 - 4 * t + 1) * d0 + (-6 * t2 + 6 * t) * f1 + (3 * t2 - 2 * t) * d1) * inv;
    }
};

// Two ports: 0 = grid -> cathode (Vgk, Ig), 1 = plate -> cathode (Vpk, Ip).
class Triode : public NonlinearDevice
{
public:
    Triode() : table (TubeTable::get()), t (table.p) {}
    int numPorts() const override { return 2; }

    void eval (const double* v, double* i, double* J) const override
    {
        double ig, dig, ik, dik;
        table.grid (v[0], ig, dig);
        table.cathode (v[0], v[1], ik, dik);
        i[0] = ig;
        i[1] = ik - ig;
        J[0] = dig;              J[1] = 0.0;
        J[2] = dik - dig;        J[3] = dik / t.mu;
    }

    void initialGuess (double* v) const override { v[0] = -1.5; v[1] = 150.0; }

private:
    const TubeTable& table;
    const TriodeParams t;
};

// Grid-cathode diode only, used where a stage's grid is driven from the
// previous segment: it adds the grid-current loading (blocking, clipping
// asymmetry) to the network that drives the grid.
class GridDiode : public NonlinearDevice
{
public:
    GridDiode() : table (TubeTable::get()) {}
    int numPorts() const override { return 1; }

    void eval (const double* v, double* i, double* J) const override
    {
        double ig, dig;
        table.grid (v[0], ig, dig);
        i[0] = ig;
        J[0] = dig;
    }

    void initialGuess (double* v) const override { v[0] = -1.5; }

private:
    const TubeTable& table;
};

} // namespace mk4
