// quick look at one voice: fret contact, peak, level over time, per attack and note
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
            for (float vel : { 0.3f, 0.9f })
            {
                Voice v; v.prepare (sr); v.subLevel = 0.0f; v.noteOn (hz, vel, (Attack) a);
                std::vector<float> x; float pk = 0;
                for (int i = 0; i < 2 * sr; ++i) { x.push_back (v.process()); pk = std::fmax (pk, std::fabs (x.back())); if (! std::isfinite (x.back())) { std::printf ("NaN!\n"); return 1; } }
                auto rms = [&] (double t0, double t1) { double s = 0; int n = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i) { s += x[i] * x[i]; ++n; } return std::sqrt (s / n); };
                std::printf ("%-5s %5.1f Hz v%.1f: fret contact %4d samples, peak force %.2e, peak %.3f, rms 0-0.1 s %.4f, -%.1f dB at 1 s\n", names[a], hz, vel, v.contactSamples, v.peakForce, pk, rms (0, 0.1), 20 * std::log10 (rms (0, 0.1) / rms (0.9, 1.0)));
            }
}
