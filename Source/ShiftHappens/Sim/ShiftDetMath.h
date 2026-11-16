// Shift Happens - engine-independent numeric core.
// Platform-independent log/exp/pow built only from IEEE basic operations (+,-,*,/),
// frexp and ldexp (both exact). std::pow may differ in the last ulp between C runtimes,
// which could flip a rounding in the vote-weight formula; these helpers cannot.
#pragma once

#include <cmath>

namespace ShiftSim
{

inline double MinOf(double A, double B) { return B < A ? B : A; }
inline double MaxOf(double A, double B) { return A < B ? B : A; }
inline double Clamp01(double X) { return X < 0.0 ? 0.0 : (X > 1.0 ? 1.0 : X); }
inline double ClampRange(double X, double Lo, double Hi) { return X < Lo ? Lo : (X > Hi ? Hi : X); }
inline int MinInt(int A, int B) { return B < A ? B : A; }
inline int RoundToInt(double V) { return static_cast<int>(V < 0.0 ? V - 0.5 : V + 0.5); }
inline int MaxInt(int A, int B) { return A < B ? B : A; }

// Natural log for X > 0.
inline double DetLog(double X)
{
    int Exp2 = 0;
    double M = std::frexp(X, &Exp2); // M in [0.5, 1)
    if (M < 0.70710678118654752440)
    {
        M *= 2.0;
        Exp2 -= 1;
    }
    const double S = (M - 1.0) / (M + 1.0);
    const double S2 = S * S;
    double Term = S;
    double Sum = 0.0;
    for (int K = 0; K < 14; ++K)
    {
        Sum += Term / static_cast<double>(2 * K + 1);
        Term *= S2;
    }
    return 2.0 * Sum + static_cast<double>(Exp2) * 0.69314718055994530942;
}

// e^Y for moderate Y.
inline double DetExp(double Y)
{
    const double Ln2Hi = 0.693147180369123816490;
    const double Ln2Lo = 1.90821492927058770002e-10;
    const double K = std::floor(Y * 1.44269504088896340736 + 0.5);
    const double R = (Y - K * Ln2Hi) - K * Ln2Lo; // |R| <= ~0.35
    double Term = 1.0;
    double Sum = 1.0;
    for (int I = 1; I <= 18; ++I)
    {
        Term *= R / static_cast<double>(I);
        Sum += Term;
    }
    return std::ldexp(Sum, static_cast<int>(K));
}

// X^Y for X > 0.
inline double DetPow(double X, double Y) { return DetExp(Y * DetLog(X)); }

} // namespace ShiftSim
