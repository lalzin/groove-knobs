#include "SlapperVoice.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace slapper;
int main()
{
    const int sr = 48000;
    const char* names[] { "slap", "pop", "doigt" };
    for (int a = 0; a < 3; ++a)
        for (double hz : { 41.2, 110.0 })
        {
            Voice v; v.prepare (sr); v.noteOn (hz, 0.85f, (Attack) a);
            std::vector<float> x; for (int i = 0; i < 2 * sr; ++i) x.push_back (v.process());
            // brightness: energy of the first difference over the energy, first 60 ms (a crude centroid)
            double e = 0, d = 0; for (int i = 1; i < (int) (0.06 * sr); ++i) { e += x[i] * x[i]; d += (x[i] - x[i-1]) * (x[i] - x[i-1]); }
            const double centroid = std::sqrt (d / (e + 1e-30)) * sr / (2 * 3.14159);
            auto rms = [&] (double t0, double t1) { double s = 0; int n = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i) { s += x[i] * x[i]; ++n; } return std::sqrt (s / n); };
            const double r0 = rms (0.0, 0.1), r1 = rms (0.9, 1.0), r2 = rms (1.8, 1.9);
            std::printf ("%-5s %5.1f Hz: contact %3d samples (%.2f ms), peak force %.2e, attack rms %.4f, brightness ~%5.0f Hz, -%.1f dB at 1 s, -%.1f dB at 1.8 s\n", names[a], hz, v.contactSamples, v.contactSamples * 1000.0 / sr, v.peakForce, r0, centroid, 20 * std::log10 (r0 / r1), 20 * std::log10 (r0 / r2));
        }
}
