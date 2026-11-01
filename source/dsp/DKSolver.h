// Nodal DK-method circuit solver.
//
// A circuit is described as a netlist of resistors, capacitors, inductors,
// ideal voltage-source inputs, ideal op-amps (nullors) and nonlinear devices
// (exposed as "ports": a voltage across two nodes and a current through them).
// Reactive elements are discretised with the trapezoidal rule, the linear part
// is pre-solved with modified nodal analysis, and only the (small) nonlinear
// port equations are solved per sample with Newton's method.
//
// Reference: D. Yeh, "Automated physical modeling of nonlinear audio circuits
// for real-time audio effects" (IEEE TASLP 2012) and M. Holters & U. Zölzer,
// "Physical modelling of a wah-wah effect pedal as a case study for
// application of the nodal DK method to circuits with variable parts" (DAFx 2011).
#pragma once

#include <memory>
#include <utility>
#include <vector>

namespace mk4
{

class NonlinearDevice
{
public:
    virtual ~NonlinearDevice() = default;
    virtual int numPorts() const = 0;
    // v: port voltages. Writes port currents i and the local Jacobian
    // J (numPorts x numPorts, row-major, J[r*n+c] = d i_r / d v_c).
    virtual void eval (const double* v, double* i, double* J) const = 0;
    // A sensible starting point for the DC operating point search.
    virtual void initialGuess (double* v) const = 0;
};

class Circuit
{
public:
    enum class Kind { Resistor, Capacitor, Inductor };
    struct Element { Kind kind; int a, b; double value; };
    struct Port    { int a, b; };
    struct OpAmp   { int plus, minus, out; };

    static constexpr int ground = 0;

    int addNode() { return numNodes++; }
    int addResistor  (int a, int b, double ohms)   { return addElement (Kind::Resistor,  a, b, ohms); }
    int addCapacitor (int a, int b, double farads) { return addElement (Kind::Capacitor, a, b, farads); }
    int addInductor  (int a, int b, double henrys) { return addElement (Kind::Inductor,  a, b, henrys); }

    // Ideal voltage source between a (+) and b (-). Returns the input index.
    int addInput (int a, int b = ground) { inputs.push_back ({ a, b }); return (int) inputs.size() - 1; }
    void addOpAmp (int plus, int minus, int out) { opamps.push_back ({ plus, minus, out }); }
    // Adds a device; portNodes gives (a, b) for each port. Current flows a -> b through the device.
    void addDevice (std::shared_ptr<NonlinearDevice> d, std::vector<std::pair<int, int>> portNodes);
    int addOutput (int node) { outputs.push_back (node); return (int) outputs.size() - 1; }

    int numNodes = 1;
    std::vector<Element> elements;
    std::vector<Port> inputs;
    std::vector<OpAmp> opamps;
    std::vector<Port> ports;
    std::vector<std::shared_ptr<NonlinearDevice>> devices;
    std::vector<int> outputs;

private:
    int addElement (Kind k, int a, int b, double v) { elements.push_back ({ k, a, b, v }); return (int) elements.size() - 1; }
};

class DKSolver
{
public:
    void build (const Circuit& c, double sampleRate);

    // Change an element value (e.g. a pot section). Takes effect on update().
    void setValue (int element, double value);
    double getValue (int element) const { return values[(size_t) element]; }
    // Recomputes the system matrices if any value changed. Allocation free.
    void update();

    // Finds the DC operating point for constant inputs u and sets the state to it.
    void initDC (const double* u);

    // Processes one sample. u: numInputs values, y: numOutputs values.
    void process (const double* u, double* y);

    int numInputs() const  { return nu; }
    int numOutputs() const { return ny; }
    int lastIterations() const { return iterations; }
    const double* portVoltages() const { return v.data(); }

private:
    void computeMatrices (bool dcMode);
    bool newton (const double* p, const double* Fm, int maxIter, double maxStep);

    Circuit ckt;
    std::vector<double> values;
    double T = 1.0 / 48000.0;
    bool dirty = true;

    int nNodes = 0, N = 0, nx = 0, nu = 0, nn = 0, ny = 0, nop = 0;
    std::vector<int> reactive;        // element indices of C and L
    std::vector<double> kappa, G;     // per reactive element

    // Work matrices (row-major).
    std::vector<double> S, RHS, Kmat;   // N x N, N x (nx+nu+nn)
    std::vector<int> piv;

    // DK matrices.
    std::vector<double> A, B, C, D, E, F, Do, Eo, Fo;

    // State and work vectors.
    std::vector<double> s, sNew, v, vPrev, i, p, r, J, Jloc, delta;
    int iterations = 0;
};

} // namespace mk4
