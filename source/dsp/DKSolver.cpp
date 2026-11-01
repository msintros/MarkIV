#include "DKSolver.h"

#include <cassert>
#include <cmath>

namespace mk4
{

void Circuit::addDevice (std::shared_ptr<NonlinearDevice> d, std::vector<std::pair<int, int>> portNodes)
{
    assert ((int) portNodes.size() == d->numPorts());
    for (auto& pn : portNodes)
        ports.push_back ({ pn.first, pn.second });
    devices.push_back (std::move (d));
}

namespace
{
    // In-place LU decomposition with partial pivoting. Returns false if singular.
    bool luDecompose (double* a, int n, int* piv)
    {
        for (int k = 0; k < n; ++k)
        {
            int best = k;
            double bestVal = std::abs (a[k * n + k]);
            for (int r = k + 1; r < n; ++r)
            {
                const double val = std::abs (a[r * n + k]);
                if (val > bestVal) { bestVal = val; best = r; }
            }
            piv[k] = best;
            if (bestVal == 0.0)
                return false;
            if (best != k)
                for (int c = 0; c < n; ++c)
                    std::swap (a[k * n + c], a[best * n + c]);

            const double inv = 1.0 / a[k * n + k];
            for (int r = k + 1; r < n; ++r)
            {
                const double f = (a[r * n + k] *= inv);
                if (f != 0.0)
                    for (int c = k + 1; c < n; ++c)
                        a[r * n + c] -= f * a[k * n + c];
            }
        }
        return true;
    }

    // Solves LU x = b for m right-hand sides stored column-wise in b (n x m, row-major).
    void luSolve (const double* lu, int n, const int* piv, double* b, int m)
    {
        for (int k = 0; k < n; ++k)
            if (piv[k] != k)
                for (int c = 0; c < m; ++c)
                    std::swap (b[k * m + c], b[piv[k] * m + c]);

        for (int r = 0; r < n; ++r)
            for (int k = 0; k < r; ++k)
            {
                const double f = lu[r * n + k];
                if (f != 0.0)
                    for (int c = 0; c < m; ++c)
                        b[r * m + c] -= f * b[k * m + c];
            }

        for (int r = n - 1; r >= 0; --r)
        {
            for (int k = r + 1; k < n; ++k)
            {
                const double f = lu[r * n + k];
                if (f != 0.0)
                    for (int c = 0; c < m; ++c)
                        b[r * m + c] -= f * b[k * m + c];
            }
            const double inv = 1.0 / lu[r * n + r];
            for (int c = 0; c < m; ++c)
                b[r * m + c] *= inv;
        }
    }

    // Small dense solve (Gaussian elimination, partial pivoting) for Newton steps.
    void smallSolve (double* a, double* b, int n)
    {
        for (int k = 0; k < n; ++k)
        {
            int best = k;
            for (int r = k + 1; r < n; ++r)
                if (std::abs (a[r * n + k]) > std::abs (a[best * n + k]))
                    best = r;
            if (best != k)
            {
                for (int c = 0; c < n; ++c)
                    std::swap (a[k * n + c], a[best * n + c]);
                std::swap (b[k], b[best]);
            }
            const double d = a[k * n + k];
            if (d == 0.0)
                continue;
            const double inv = 1.0 / d;
            for (int r = k + 1; r < n; ++r)
            {
                const double f = a[r * n + k] * inv;
                if (f == 0.0) continue;
                for (int c = k; c < n; ++c)
                    a[r * n + c] -= f * a[k * n + c];
                b[r] -= f * b[k];
            }
        }
        for (int r = n - 1; r >= 0; --r)
        {
            double acc = b[r];
            for (int c = r + 1; c < n; ++c)
                acc -= a[r * n + c] * b[c];
            const double d = a[r * n + r];
            b[r] = d != 0.0 ? acc / d : 0.0;
        }
    }
} // namespace

void DKSolver::build (const Circuit& c, double sampleRate)
{
    ckt = c;
    T = 1.0 / sampleRate;
    values.clear();
    for (auto& e : ckt.elements)
        values.push_back (e.value);

    nNodes = ckt.numNodes;
    nu = (int) ckt.inputs.size();
    nop = (int) ckt.opamps.size();
    nn = (int) ckt.ports.size();
    ny = (int) ckt.outputs.size();
    N = (nNodes - 1) + nu + nop;

    reactive.clear();
    kappa.clear();
    for (int k = 0; k < (int) ckt.elements.size(); ++k)
    {
        const auto kind = ckt.elements[(size_t) k].kind;
        if (kind == Circuit::Kind::Capacitor) { reactive.push_back (k); kappa.push_back (-1.0); }
        if (kind == Circuit::Kind::Inductor)  { reactive.push_back (k); kappa.push_back (+1.0); }
    }
    nx = (int) reactive.size();
    G.assign ((size_t) nx, 0.0);

    const int m = nx + nu + nn;
    S.assign ((size_t) (N * N), 0.0);
    RHS.assign ((size_t) (N * m), 0.0);
    Kmat.assign ((size_t) (N * m), 0.0);
    piv.assign ((size_t) N, 0);

    A.assign ((size_t) (nx * nx), 0.0);
    B.assign ((size_t) (nx * nu), 0.0);
    C.assign ((size_t) (nx * nn), 0.0);
    D.assign ((size_t) (nn * nx), 0.0);
    E.assign ((size_t) (nn * nu), 0.0);
    F.assign ((size_t) (nn * nn), 0.0);
    Do.assign ((size_t) (ny * nx), 0.0);
    Eo.assign ((size_t) (ny * nu), 0.0);
    Fo.assign ((size_t) (ny * nn), 0.0);

    s.assign ((size_t) nx, 0.0);
    sNew.assign ((size_t) nx, 0.0);
    v.assign ((size_t) nn, 0.0);
    vPrev.assign ((size_t) nn, 0.0);
    i.assign ((size_t) nn, 0.0);
    p.assign ((size_t) nn, 0.0);
    r.assign ((size_t) nn, 0.0);
    delta.assign ((size_t) nn, 0.0);
    J.assign ((size_t) (nn * nn), 0.0);
    Jloc.assign ((size_t) (nn * nn), 0.0);

    // Default port guesses.
    int off = 0;
    for (auto& d : ckt.devices)
    {
        d->initialGuess (v.data() + off);
        off += d->numPorts();
    }

    dirty = true;
    update();
}

void DKSolver::setValue (int element, double value)
{
    if (values[(size_t) element] != value)
    {
        values[(size_t) element] = value;
        dirty = true;
    }
}

void DKSolver::update()
{
    if (! dirty)
        return;
    computeMatrices (false);
    dirty = false;
}

void DKSolver::computeMatrices (bool dcMode)
{
    const int m = nx + nu + nn;
    std::fill (S.begin(), S.end(), 0.0);
    std::fill (RHS.begin(), RHS.end(), 0.0);

    auto stampG = [&] (int a, int b, double g)
    {
        if (a > 0) S[(size_t) ((a - 1) * N + (a - 1))] += g;
        if (b > 0) S[(size_t) ((b - 1) * N + (b - 1))] += g;
        if (a > 0 && b > 0)
        {
            S[(size_t) ((a - 1) * N + (b - 1))] -= g;
            S[(size_t) ((b - 1) * N + (a - 1))] -= g;
        }
    };

    // Tiny conductance to ground keeps every node defined.
    for (int n = 1; n < nNodes; ++n)
        S[(size_t) ((n - 1) * N + (n - 1))] += 1e-12;

    int rIdx = 0;
    for (int k = 0; k < (int) ckt.elements.size(); ++k)
    {
        const auto& e = ckt.elements[(size_t) k];
        const double val = values[(size_t) k];
        switch (e.kind)
        {
            case Circuit::Kind::Resistor:
                stampG (e.a, e.b, 1.0 / std::max (val, 1e-6));
                break;
            case Circuit::Kind::Capacitor:
            {
                const double g = dcMode ? 0.0 : 2.0 * val / T;
                G[(size_t) rIdx] = 2.0 * val / T;
                stampG (e.a, e.b, g);
                ++rIdx;
                break;
            }
            case Circuit::Kind::Inductor:
            {
                const double g = dcMode ? 1e6 : T / (2.0 * val);
                G[(size_t) rIdx] = T / (2.0 * val);
                stampG (e.a, e.b, g);
                ++rIdx;
                break;
            }
        }
    }

    for (int k = 0; k < nu; ++k)
    {
        const int row = nNodes - 1 + k;
        const auto& src = ckt.inputs[(size_t) k];
        if (src.a > 0) { S[(size_t) ((src.a - 1) * N + row)] += 1.0; S[(size_t) (row * N + (src.a - 1))] += 1.0; }
        if (src.b > 0) { S[(size_t) ((src.b - 1) * N + row)] -= 1.0; S[(size_t) (row * N + (src.b - 1))] -= 1.0; }
        RHS[(size_t) (row * m + nx + k)] = 1.0;
    }

    for (int k = 0; k < nop; ++k)
    {
        const int row = nNodes - 1 + nu + k;
        const auto& op = ckt.opamps[(size_t) k];
        if (op.out > 0)   S[(size_t) ((op.out - 1) * N + row)] += 1.0;
        if (op.plus > 0)  S[(size_t) (row * N + (op.plus - 1))] += 1.0;
        if (op.minus > 0) S[(size_t) (row * N + (op.minus - 1))] -= 1.0;
    }

    // Reactive history sources: element current = G v + kappa s  ->  rhs_a -= kappa s.
    for (int j = 0; j < nx; ++j)
    {
        const auto& e = ckt.elements[(size_t) reactive[(size_t) j]];
        const double sign = dcMode ? 0.0 : -kappa[(size_t) j];
        if (e.a > 0) RHS[(size_t) ((e.a - 1) * m + j)] += sign;
        if (e.b > 0) RHS[(size_t) ((e.b - 1) * m + j)] -= sign;
    }

    // Nonlinear port currents leave node a, enter node b.
    for (int k = 0; k < nn; ++k)
    {
        const auto& pt = ckt.ports[(size_t) k];
        if (pt.a > 0) RHS[(size_t) ((pt.a - 1) * m + nx + nu + k)] -= 1.0;
        if (pt.b > 0) RHS[(size_t) ((pt.b - 1) * m + nx + nu + k)] += 1.0;
    }

    Kmat = RHS;
    luDecompose (S.data(), N, piv.data());
    luSolve (S.data(), N, piv.data(), Kmat.data(), m);

    auto diff = [&] (int a, int b, int col) -> double
    {
        double val = 0.0;
        if (a > 0) val += Kmat[(size_t) ((a - 1) * m + col)];
        if (b > 0) val -= Kmat[(size_t) ((b - 1) * m + col)];
        return val;
    };

    for (int row = 0; row < nn; ++row)
    {
        const auto& pt = ckt.ports[(size_t) row];
        for (int c = 0; c < nx; ++c) D[(size_t) (row * nx + c)] = diff (pt.a, pt.b, c);
        for (int c = 0; c < nu; ++c) E[(size_t) (row * nu + c)] = diff (pt.a, pt.b, nx + c);
        for (int c = 0; c < nn; ++c) F[(size_t) (row * nn + c)] = diff (pt.a, pt.b, nx + nu + c);
    }

    for (int row = 0; row < nx; ++row)
    {
        const auto& e = ckt.elements[(size_t) reactive[(size_t) row]];
        const double g2 = 2.0 * G[(size_t) row];
        for (int c = 0; c < nx; ++c) A[(size_t) (row * nx + c)] = g2 * diff (e.a, e.b, c) + (c == row ? kappa[(size_t) row] : 0.0);
        for (int c = 0; c < nu; ++c) B[(size_t) (row * nu + c)] = g2 * diff (e.a, e.b, nx + c);
        for (int c = 0; c < nn; ++c) C[(size_t) (row * nn + c)] = g2 * diff (e.a, e.b, nx + nu + c);
    }

    for (int row = 0; row < ny; ++row)
    {
        const int node = ckt.outputs[(size_t) row];
        for (int c = 0; c < nx; ++c) Do[(size_t) (row * nx + c)] = diff (node, 0, c);
        for (int c = 0; c < nu; ++c) Eo[(size_t) (row * nu + c)] = diff (node, 0, nx + c);
        for (int c = 0; c < nn; ++c) Fo[(size_t) (row * nn + c)] = diff (node, 0, nx + nu + c);
    }
}

bool DKSolver::newton (const double* pv, const double* Fm, int maxIter, double maxStep)
{
    iterations = 0;
    for (int it = 0; it < maxIter; ++it)
    {
        // Evaluate devices.
        std::fill (J.begin(), J.end(), 0.0);
        int off = 0;
        for (auto& d : ckt.devices)
        {
            const int np = d->numPorts();
            double jl[16];
            d->eval (v.data() + off, i.data() + off, jl);
            for (int a = 0; a < np; ++a)
                for (int b = 0; b < np; ++b)
                    Jloc[(size_t) ((off + a) * nn + off + b)] = jl[a * np + b];
            off += np;
        }

        // Residual r = p + F i - v and Jacobian J = F Ji - I.
        double maxR = 0.0;
        for (int row = 0; row < nn; ++row)
        {
            double acc = pv[row] - v[(size_t) row];
            for (int k = 0; k < nn; ++k)
                acc += Fm[row * nn + k] * i[(size_t) k];
            r[(size_t) row] = -acc;
            maxR = std::max (maxR, std::abs (acc));
        }
        if (maxR < 1e-6)
            return true;

        off = 0;
        for (auto& d : ckt.devices)
        {
            const int np = d->numPorts();
            for (int row = 0; row < nn; ++row)
                for (int c = off; c < off + np; ++c)
                {
                    double acc = 0.0;
                    for (int k = off; k < off + np; ++k)
                        acc += Fm[row * nn + k] * Jloc[(size_t) (k * nn + c)];
                    J[(size_t) (row * nn + c)] = acc;
                }
            off += np;
        }
        for (int row = 0; row < nn; ++row)
            J[(size_t) (row * nn + row)] -= 1.0;

        std::copy (r.begin(), r.end(), delta.begin());
        smallSolve (J.data(), delta.data(), nn);

        double maxD = 0.0;
        for (int k = 0; k < nn; ++k)
        {
            const double d = std::max (-maxStep, std::min (maxStep, delta[(size_t) k]));
            v[(size_t) k] += d;
            maxD = std::max (maxD, std::abs (d));
        }
        ++iterations;
        if (maxD < 1e-7)
            break;
    }

    // Make currents consistent with the final voltages.
    int off = 0;
    for (auto& d : ckt.devices)
    {
        double jl[16];
        d->eval (v.data() + off, i.data() + off, jl);
        off += d->numPorts();
    }
    return false;
}

void DKSolver::initDC (const double* u)
{
    computeMatrices (true);

    // p = E u (no state contribution in DC).
    for (int row = 0; row < nn; ++row)
    {
        double acc = 0.0;
        for (int c = 0; c < nu; ++c)
            acc += E[(size_t) (row * nu + c)] * u[c];
        p[(size_t) row] = acc;
    }

    int off = 0;
    for (auto& d : ckt.devices)
    {
        d->initialGuess (v.data() + off);
        off += d->numPorts();
    }
    newton (p.data(), F.data(), 500, 2.0);
    vPrev = v;

    // Node voltages w = Ku u + Kn i  ->  capacitor voltages.
    const int m = nx + nu + nn;
    for (int j = 0; j < nx; ++j)
    {
        const auto& e = ckt.elements[(size_t) reactive[(size_t) j]];
        auto nodeV = [&] (int node) -> double
        {
            if (node <= 0) return 0.0;
            double acc = 0.0;
            for (int c = 0; c < nu; ++c) acc += Kmat[(size_t) ((node - 1) * m + nx + c)] * u[c];
            for (int c = 0; c < nn; ++c) acc += Kmat[(size_t) ((node - 1) * m + nx + nu + c)] * i[(size_t) c];
            return acc;
        };
        const double vc = nodeV (e.a) - nodeV (e.b);
        // At DC a capacitor carries no current: 0 = G v - s  ->  s = G v.
        s[(size_t) j] = kappa[(size_t) j] < 0 ? G[(size_t) j] * vc : 0.0;
    }

    computeMatrices (false);
    dirty = false;
}

void DKSolver::process (const double* u, double* y)
{
    for (int row = 0; row < nn; ++row)
    {
        double acc = 0.0;
        const double* Dr = D.data() + row * nx;
        for (int c = 0; c < nx; ++c) acc += Dr[c] * s[(size_t) c];
        const double* Er = E.data() + row * nu;
        for (int c = 0; c < nu; ++c) acc += Er[c] * u[c];
        p[(size_t) row] = acc;
    }

    if (nn > 0)
    {
        // Linear extrapolation of the previous two solutions as the Newton start.
        for (int k = 0; k < nn; ++k)
        {
            const double prev = v[(size_t) k];
            v[(size_t) k] = 2.0 * prev - vPrev[(size_t) k];
            vPrev[(size_t) k] = prev;
        }
        newton (p.data(), F.data(), 12, 20.0);
    }

    for (int row = 0; row < ny; ++row)
    {
        double acc = 0.0;
        const double* Dr = Do.data() + row * nx;
        for (int c = 0; c < nx; ++c) acc += Dr[c] * s[(size_t) c];
        const double* Er = Eo.data() + row * nu;
        for (int c = 0; c < nu; ++c) acc += Er[c] * u[c];
        const double* Fr = Fo.data() + row * nn;
        for (int c = 0; c < nn; ++c) acc += Fr[c] * i[(size_t) c];
        y[row] = acc;
    }

    for (int row = 0; row < nx; ++row)
    {
        double acc = 0.0;
        const double* Ar = A.data() + row * nx;
        for (int c = 0; c < nx; ++c) acc += Ar[c] * s[(size_t) c];
        const double* Br = B.data() + row * nu;
        for (int c = 0; c < nu; ++c) acc += Br[c] * u[c];
        const double* Cr = C.data() + row * nn;
        for (int c = 0; c < nn; ++c) acc += Cr[c] * i[(size_t) c];
        sNew[(size_t) row] = acc;
    }
    std::swap (s, sNew);
}

} // namespace mk4
