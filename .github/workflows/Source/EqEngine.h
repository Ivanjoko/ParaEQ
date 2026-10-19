#pragma once
// Motor de ecualización en doble precisión, sin dependencias de JUCE.
// Los filtros reproducen la respuesta analógica (poles por matched-z y ceros
// ajustados en DC, frecuencia central y Nyquist), de modo que no hay
// "cramping" cerca de Nyquist como en los biquads RBJ/bilineales.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace EqMath
{
    constexpr double pi = 3.14159265358979323846;
    constexpr int numBands  = 6;
    constexpr int maxStages = 4;                 // 4 x 12 dB/oct = 48 dB/oct

    enum BandType { Bell = 0, LowShelf, HighShelf, Notch };

    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };   // a0 = 1

    // Función de transferencia analógica con frecuencia normalizada (w0 = 1)
    struct Analog { double n2, n1, n0, d2, d1, d0; };

    inline double sqr (double x) { return x * x; }

    inline double analogMag2 (const Analog& a, double W)
    {
        const double W2 = W * W;
        const double nr = a.n0 - a.n2 * W2, ni = a.n1 * W;
        const double dr = a.d0 - a.d2 * W2, di = a.d1 * W;
        return (nr * nr + ni * ni) / (dr * dr + di * di);
    }

    inline Coef matchedDesign (const Analog& a, double w0)
    {
        Coef c;

        // --- Polos: mapeo exacto z = exp(s T)
        const double p = a.d1 / a.d2, q = a.d0 / a.d2;
        const double disc = p * p - 4.0 * q;
        if (disc < 0.0)
        {
            const double r = std::exp (-0.5 * p * w0);
            c.a1 = -2.0 * r * std::cos (0.5 * std::sqrt (-disc) * w0);
            c.a2 = r * r;
        }
        else
        {
            const double sq = std::sqrt (disc);
            const double z1 = std::exp (0.5 * (-p + sq) * w0);
            const double z2 = std::exp (0.5 * (-p - sq) * w0);
            c.a1 = -(z1 + z2);
            c.a2 = z1 * z2;
        }

        // --- Ceros: se iguala |H| en DC, en la frecuencia central y en Nyquist
        const double A0 = sqr (1.0 + c.a1 + c.a2);
        const double A1 = sqr (1.0 - c.a1 + c.a2);
        const double A2 = -4.0 * c.a2;

        const double B0 = A0 * analogMag2 (a, 0.0);
        const double B1 = A1 * analogMag2 (a, pi / w0);

        const double s2   = sqr (std::sin (0.5 * w0));
        const double phi1 = s2, phi0 = 1.0 - s2, phi2 = 4.0 * phi0 * phi1;
        const double den  = A0 * phi0 + A1 * phi1 + A2 * phi2;
        const double B2   = (analogMag2 (a, 1.0) * den - B0 * phi0 - B1 * phi1) / phi2;

        const double r0 = std::sqrt (std::max (0.0, B0));
        const double r1 = std::sqrt (std::max (0.0, B1));
        const double W  = 0.5 * (r0 + r1);
        const double rt = std::sqrt (std::max (0.0, W * W + B2));

        c.b0 = 0.5 * (W + rt);
        c.b1 = 0.5 * (r0 - r1);
        c.b2 = 0.5 * (W - rt);

        if (! (std::isfinite (c.b0) && std::isfinite (c.b1) && std::isfinite (c.b2)
               && std::isfinite (c.a1) && std::isfinite (c.a2)))
            return Coef {};                          // paso directo si algo falla

        return c;
    }

    inline double clampFreq (double f, double sr) { return std::min (std::max (f, 5.0), 0.45 * sr); }

    // Biquad bilineal (RBJ) con prewarping: exacto en la frecuencia indicada
    inline Coef rbj (int type, double w0, double A, double q)
    {
        const double cw = std::cos (w0), sw = std::sin (w0), al = sw / (2.0 * q), sA = std::sqrt (A);
        double b0, b1, b2, a0, a1, a2;

        switch (type)
        {
            case LowShelf:
                b0 = A * ((A + 1) - (A - 1) * cw + 2 * sA * al);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - 2 * sA * al);
                a0 = (A + 1) + (A - 1) * cw + 2 * sA * al;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - 2 * sA * al;
                break;
            case HighShelf:
                b0 = A * ((A + 1) + (A - 1) * cw + 2 * sA * al);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - 2 * sA * al);
                a0 = (A + 1) - (A - 1) * cw + 2 * sA * al;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - 2 * sA * al;
                break;
            default: // Notch
                b0 = 1; b1 = -2 * cw; b2 = 1;
                a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
                break;
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    // Campana: ajustada a la respuesta analogica (menos error que el biquad clasico en agudos).
    // Shelves y notch: bilineal con prewarping.
    inline Coef designBand (int type, double sr, double f, double gainDb, double q)
    {
        f = clampFreq (f, sr);
        q = std::min (std::max (q, 0.05), 50.0);
        const double w0 = 2.0 * pi * f / sr;
        const double A  = std::pow (10.0, gainDb / 40.0);

        if (type == LowShelf || type == HighShelf || type == Notch)
            return rbj (type, w0, A, q);

        return matchedDesign ({ 1.0, A / q, 1.0, 1.0, 1.0 / (A * q), 1.0 }, w0);
    }

    // Filtros de corte Butterworth (bilineal con prewarping: -3 dB exactos en fc).
    // slope: 0 = 12, 1 = 24, 2 = 36, 3 = 48 dB/oct. Devuelve el numero de etapas.
    inline int designCut (bool highPass, double sr, double f, int slope, Coef* out)
    {
        const int stages = std::min (std::max (slope, 0), maxStages - 1) + 1;
        const int order  = 2 * stages;
        const double w0  = 2.0 * pi * clampFreq (f, sr) / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);

        for (int k = 0; k < stages; ++k)
        {
            const double q  = 1.0 / (2.0 * std::cos ((2.0 * (k + 1) - 1.0) * pi / (2.0 * order)));
            const double al = sw / (2.0 * q), a0 = 1.0 + al;
            const double bb = highPass ? (1.0 + cw) * 0.5 : (1.0 - cw) * 0.5;
            out[k] = { bb / a0, (highPass ? -2.0 : 2.0) * bb / a0, bb / a0, -2.0 * cw / a0, (1.0 - al) / a0 };
        }
        return stages;
    }

    inline double magnitude (const Coef& c, double f, double sr)
    {
        const double w = 2.0 * pi * f / sr;
        const double cw = std::cos (w), c2 = std::cos (2.0 * w), sw = std::sin (w), s2 = std::sin (2.0 * w);
        const double nr = c.b0 + c.b1 * cw + c.b2 * c2, ni = -(c.b1 * sw + c.b2 * s2);
        const double dr = 1.0 + c.a1 * cw + c.a2 * c2,  di = -(c.a1 * sw + c.a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
}

//==============================================================================
struct BandParams { bool on = true; int type = EqMath::Bell; double freq = 1000.0, gainDb = 0.0, q = 1.0; };
struct CutParams  { bool on = false; double freq = 100.0; int slope = 1; };

struct EqParams
{
    double inDb = 0.0, outDb = 0.0;
    bool bypass = false;
    CutParams hpf, lpf;
    std::array<BandParams, EqMath::numBands> band;
};

//==============================================================================
class EqEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        blockK  = std::exp (-(double) subBlock / (0.02 * sr));   // suavizado de 20 ms
        mixStep = 1.0 / (0.01 * sr);                              // fundidos de 10 ms
        initialised = false;
        hpf = {}; lpf = {};
        for (auto& b : bands) b = {};
        bypassMix = 1.0;
        peakIn = peakOut = 0.0;
    }

    void setParams (const EqParams& p)
    {
        const bool first = ! initialised;
        initialised = true;

        setSmooth (inDb,  p.inDb,  first);
        setSmooth (outDb, p.outDb, first);
        bypassOn = p.bypass;
        if (first) { bypassMix = bypassOn ? 0.0 : 1.0; giCur = dbToGain (inDb.cur); goCur = dbToGain (outDb.cur); }

        setCut (hpf, p.hpf, true,  first);
        setCut (lpf, p.lpf, false, first);

        for (int i = 0; i < EqMath::numBands; ++i)
        {
            auto& b = bands[(size_t) i];
            const auto& q = p.band[(size_t) i];
            const bool typeChanged = (q.type != b.type);

            setSmooth (b.lf, std::log (EqMath::clampFreq (q.freq, sr)), first || typeChanged);
            setSmooth (b.g,  q.gainDb, first || typeChanged);
            setSmooth (b.lq, std::log (std::min (std::max (q.q, 0.05), 50.0)), first || typeChanged);

            b.type = q.type;
            b.on = q.on;
            if (first) b.mix = q.on ? 1.0 : 0.0;
            if (first || typeChanged) b.dirty = true;
        }
    }

    template <typename T>
    void process (T* l, T* r, int n)
    {
        peakIn = peakOut = 0.0;
        for (int pos = 0; pos < n; pos += subBlock)
        {
            const int len = std::min (subBlock, n - pos);
            updateBlock();
            runBlock (l + pos, r + pos, len);
        }
    }

    double getPeakIn()  const { return peakIn; }
    double getPeakOut() const { return peakOut; }

private:
    static constexpr int subBlock = 32;

    struct Biquad
    {
        double s1 = 0, s2 = 0;
        inline double process (double x, const EqMath::Coef& c)
        {
            const double y = c.b0 * x + s1;
            s1 = c.b1 * x - c.a1 * y + s2;
            s2 = c.b2 * x - c.a2 * y;
            return y;
        }
        void reset() { s1 = s2 = 0.0; }
    };

    struct Smooth
    {
        double cur = 0, target = 0;
        bool active = false;
        void snap (double v) { cur = target = v; active = false; }
        void set (double t)  { target = t; active = (cur != target); }
        bool advance (double k)
        {
            if (! active) return false;
            cur = target + (cur - target) * k;
            if (std::fabs (cur - target) < 1e-9) { cur = target; active = false; }
            return true;
        }
    };

    struct BandState
    {
        Smooth lf, g, lq;
        int type = 0;
        bool on = true, dirty = true, live = false;
        double mix = 1.0;
        EqMath::Coef c;
        Biquad f[2];
    };

    struct CutState
    {
        Smooth lf;
        int slope = 1, stages = 2;
        bool on = false, hp = true, dirty = true, live = false;
        double mix = 0.0;
        EqMath::Coef c[EqMath::maxStages];
        Biquad f[2][EqMath::maxStages];
    };

    static double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

    static void setSmooth (Smooth& s, double target, bool snap)
    {
        if (snap) s.snap (target); else s.set (target);
    }

    void setCut (CutState& s, const CutParams& p, bool isHp, bool first)
    {
        const bool slopeChanged = (p.slope != s.slope);
        s.hp = isHp;
        setSmooth (s.lf, std::log (EqMath::clampFreq (p.freq, sr)), first || slopeChanged);
        s.slope = p.slope;
        s.on = p.on;
        if (first) s.mix = p.on ? 1.0 : 0.0;
        if (first || slopeChanged) s.dirty = true;
    }

    static void approach (double& m, double target, double step)
    {
        if (m < target)      m = std::min (target, m + step);
        else if (m > target) m = std::max (target, m - step);
    }

    void updateBlock()
    {
        const bool inMoved  = inDb.advance (blockK);
        const bool outMoved = outDb.advance (blockK);
        const double giNew = inMoved  ? dbToGain (inDb.cur)  : dbToGain (inDb.target);
        const double goNew = outMoved ? dbToGain (outDb.cur) : dbToGain (outDb.target);
        giStep = (giNew - giCur) / (double) subBlock;
        goStep = (goNew - goCur) / (double) subBlock;
        giEnd = giNew; goEnd = goNew;

        updateCut (hpf);
        updateCut (lpf);

        for (auto& b : bands)
        {
            const bool moved = b.lf.advance (blockK) | b.g.advance (blockK) | b.lq.advance (blockK);
            if (moved || b.dirty)
            {
                b.c = EqMath::designBand (b.type, sr, std::exp (b.lf.cur), b.g.cur, std::exp (b.lq.cur));
                b.dirty = false;
            }
        }
    }

    void updateCut (CutState& s)
    {
        const bool moved = s.lf.advance (blockK);
        if (moved || s.dirty)
        {
            s.stages = EqMath::designCut (s.hp, sr, std::exp (s.lf.cur), s.slope, s.c);
            s.dirty = false;
        }
    }

    static void resetCut (CutState& s)
    {
        for (auto& ch : s.f) for (auto& f : ch) f.reset();
        s.live = false;
    }

    inline void applyCut (CutState& s, double& x0, double& x1)
    {
        approach (s.mix, s.on ? 1.0 : 0.0, mixStep);
        if (s.mix <= 0.0)
        {
            if (s.live) resetCut (s);
            return;
        }
        s.live = true;

        double y0 = x0, y1 = x1;
        for (int k = 0; k < s.stages; ++k)
        {
            y0 = s.f[0][k].process (y0, s.c[k]);
            y1 = s.f[1][k].process (y1, s.c[k]);
        }
        x0 += s.mix * (y0 - x0);
        x1 += s.mix * (y1 - x1);
    }

    inline void applyBand (BandState& b, double& x0, double& x1)
    {
        approach (b.mix, b.on ? 1.0 : 0.0, mixStep);
        if (b.mix <= 0.0)
        {
            if (b.live) { b.f[0].reset(); b.f[1].reset(); b.live = false; }
            return;
        }
        b.live = true;

        const double y0 = b.f[0].process (x0, b.c);
        const double y1 = b.f[1].process (x1, b.c);
        x0 += b.mix * (y0 - x0);
        x1 += b.mix * (y1 - x1);
    }

    template <typename T>
    void runBlock (T* l, T* r, int len)
    {
        if (bypassOn && bypassMix <= 0.0)
        {
            for (int i = 0; i < len; ++i)
            {
                const double a = std::fabs ((double) l[i]), b = std::fabs ((double) r[i]);
                peakIn = std::max (peakIn, std::max (a, b));
            }
            peakOut = peakIn;
            resetCut (hpf); resetCut (lpf);
            for (auto& b : bands) { b.f[0].reset(); b.f[1].reset(); b.live = false; }
            giCur = giEnd; goCur = goEnd;
            return;
        }

        for (int i = 0; i < len; ++i)
        {
            const double d0 = (double) l[i], d1 = (double) r[i];
            approach (bypassMix, bypassOn ? 0.0 : 1.0, mixStep);

            giCur += giStep;  goCur += goStep;
            double x0 = d0 * giCur, x1 = d1 * giCur;
            peakIn = std::max (peakIn, std::max (std::fabs (x0), std::fabs (x1)));

            applyCut (hpf, x0, x1);
            for (auto& b : bands) applyBand (b, x0, x1);
            applyCut (lpf, x0, x1);

            x0 *= goCur;  x1 *= goCur;

            if (bypassMix < 1.0)
            {
                x0 = d0 + bypassMix * (x0 - d0);
                x1 = d1 + bypassMix * (x1 - d1);
            }

            peakOut = std::max (peakOut, std::max (std::fabs (x0), std::fabs (x1)));
            l[i] = (T) x0;  r[i] = (T) x1;
        }
        giCur = giEnd; goCur = goEnd;
    }

    double sr = 44100.0, blockK = 0.9, mixStep = 0.01;
    bool initialised = false, bypassOn = false;
    double bypassMix = 1.0;
    Smooth inDb, outDb;
    double giCur = 1.0, goCur = 1.0, giStep = 0.0, goStep = 0.0, giEnd = 1.0, goEnd = 1.0;
    double peakIn = 0.0, peakOut = 0.0;
    CutState hpf, lpf;
    std::array<BandState, EqMath::numBands> bands;
};
