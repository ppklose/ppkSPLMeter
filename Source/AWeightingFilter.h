#pragma once
#include <cmath>
#include <complex>

//==============================================================================
/**
    A-weighting IIR filter implemented as three cascaded biquad sections.

    Analog prototype:
      H_A(s) = K * s^4 / [(s+p1)(s+p2)^2(s+p3)(s+p4)^2]
      p1 = 2pi*20.598997, p2 = 2pi*107.65265,
      p3 = 2pi*737.86223, p4 = 2pi*12194.217

    Factored into three biquads:
      H1(s) = s^2 / [(s+p1)(s+p3)]         -- HP, real poles at p1, p3
      H2(s) = s^2 / (s+p2)^2               -- HP, double real pole at p2
      H3(s) = 1  / (s+p4)^2                -- LP, double real pole at p4
    with an overall gain so that |H_A(j*2pi*1000)| = 1 (0 dB at 1 kHz).

    Bilinear transform used with K_s = 2 * sampleRate.
*/
class AWeightingFilter
{
public:
    AWeightingFilter() { reset(); }

    void reset()
    {
        for (int i = 0; i < 3; ++i)
            s1[i] = s2[i] = 0.0;
    }

    /** Call once after the sample rate is known. */
    void prepare (double sampleRate)
    {
        const double pi = 3.14159265358979323846;
        const double p1 = 2.0 * pi * 20.598997;
        const double p2 = 2.0 * pi * 107.65265;
        const double p3 = 2.0 * pi * 737.86223;
        const double p4 = 2.0 * pi * 12194.217;
        const double Ks = 2.0 * sampleRate;   // bilinear warp constant

        // --- Section 0: H1(s) = s^2 / [(s+p1)(s+p3)] ---
        {
            double a0 = Ks*Ks + (p1+p3)*Ks + p1*p3;
            b[0][0] = Ks*Ks / a0;
            b[1][0] = -2.0 * Ks*Ks / a0;
            b[2][0] = Ks*Ks / a0;
            a[1][0] = 2.0 * (p1*p3 - Ks*Ks) / a0;
            a[2][0] = (Ks*Ks - (p1+p3)*Ks + p1*p3) / a0;
        }

        // --- Section 1: H2(s) = s^2 / (s+p2)^2 ---
        {
            double a0 = Ks*Ks + 2.0*p2*Ks + p2*p2;
            b[0][1] = Ks*Ks / a0;
            b[1][1] = -2.0 * Ks*Ks / a0;
            b[2][1] = Ks*Ks / a0;
            a[1][1] = 2.0 * (p2*p2 - Ks*Ks) / a0;
            a[2][1] = (Ks*Ks - 2.0*p2*Ks + p2*p2) / a0;
        }

        // --- Section 2: H3(s) = 1 / (s+p4)^2 ---
        {
            double a0 = Ks*Ks + 2.0*p4*Ks + p4*p4;
            b[0][2] = 1.0 / a0;
            b[1][2] = 2.0 / a0;
            b[2][2] = 1.0 / a0;
            a[1][2] = 2.0 * (p4*p4 - Ks*Ks) / a0;
            a[2][2] = (Ks*Ks - 2.0*p4*Ks + p4*p4) / a0;
        }

        // --- Compute gain at 1 kHz and normalise section 0 ---
        gainAt1k = computeGainAt1kHz (sampleRate);
        // Absorb normalisation into section 0 numerator coefficients
        b[0][0] /= gainAt1k;
        b[1][0] /= gainAt1k;
        b[2][0] /= gainAt1k;

        reset();
    }

    /** Process one sample, returns A-weighted sample. */
    float processSample (float x) noexcept
    {
        double v = static_cast<double> (x);
        for (int i = 0; i < 3; ++i)
        {
            double y = b[0][i] * v + s1[i];
            s1[i] = b[1][i] * v - a[1][i] * y + s2[i];
            s2[i] = b[2][i] * v - a[2][i] * y;
            v = y;
        }
        return static_cast<float> (v);
    }

private:
    double b[3][3] {};   // b[coeff][section]  (b0, b1, b2)
    double a[3][3] {};   // a[coeff][section]  (a0 unused, a1, a2)
    double s1[3] {}, s2[3] {};   // DFII-T state per section
    double gainAt1k = 1.0;

    /** Evaluate |H(e^{j*omega})| at f = 1000 Hz using all 3 sections. */
    double computeGainAt1kHz (double sampleRate)
    {
        const double pi = 3.14159265358979323846;
        const double omega = 2.0 * pi * 1000.0 / sampleRate;
        std::complex<double> z = std::exp (std::complex<double> (0.0, omega));
        std::complex<double> zinv = 1.0 / z;
        std::complex<double> zinv2 = zinv * zinv;

        std::complex<double> total (1.0, 0.0);
        for (int i = 0; i < 3; ++i)
        {
            std::complex<double> num = b[0][i] + b[1][i]*zinv + b[2][i]*zinv2;
            std::complex<double> den = 1.0 + a[1][i]*zinv + a[2][i]*zinv2;
            total *= num / den;
        }
        return std::abs (total);
    }
};
