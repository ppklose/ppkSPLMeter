#pragma once
#include <cmath>
#include <complex>

//==============================================================================
/**
    C-weighting IIR filter: one biquad section via bilinear transform.

    Analog prototype:
      H_C(s) = K * s^2 / [(s + p1)(s + p4)]
      p1 = 2pi * 20.598997 Hz,  p4 = 2pi * 12194.217 Hz

    Gain normalised to 0 dB at 1 kHz.
*/
class CWeightingFilter
{
public:
    CWeightingFilter() { reset(); }

    void reset() { s1 = s2 = 0.0; }

    void prepare (double sampleRate)
    {
        const double pi = 3.14159265358979323846;
        const double p1 = 2.0 * pi * 20.598997;
        const double p4 = 2.0 * pi * 12194.217;
        const double Ks = 2.0 * sampleRate;

        double a0 = Ks*Ks + (p1+p4)*Ks + p1*p4;
        b0 = Ks*Ks / a0;
        b1 = -2.0 * Ks*Ks / a0;
        b2 = Ks*Ks / a0;
        a1 = 2.0 * (p1*p4 - Ks*Ks) / a0;
        a2 = (Ks*Ks - (p1+p4)*Ks + p1*p4) / a0;

        // Normalise gain to 0 dB at 1 kHz
        const double omega = 2.0 * pi * 1000.0 / sampleRate;
        std::complex<double> z  = std::exp (std::complex<double> (0.0, omega));
        std::complex<double> zi = 1.0 / z;
        std::complex<double> zi2 = zi * zi;
        std::complex<double> H = (b0 + b1*zi + b2*zi2) / (1.0 + a1*zi + a2*zi2);
        double gain = std::abs (H);
        b0 /= gain;
        b1 /= gain;
        b2 /= gain;

        reset();
    }

    float processSample (float x) noexcept
    {
        double v = static_cast<double> (x);
        double y = b0*v + s1;
        s1 = b1*v - a1*y + s2;
        s2 = b2*v - a2*y;
        return static_cast<float> (y);
    }

private:
    double b0 {}, b1 {}, b2 {};
    double a1 {}, a2 {};
    double s1 {}, s2 {};
};
