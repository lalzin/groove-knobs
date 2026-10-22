#include <cmath>
#include <initializer_list>
#include <cstdio>
// a mass hitting a string of impedance 2 (both sides, Z = 1 per side, per sample), spring F = k d^p
int main()
{
    for (double m : { 20.0, 40.0, 80.0, 160.0, 320.0 })
        for (double k : { 0.01, 0.1, 1.0, 10.0, 100.0 })
            for (double p : { 1.5, 2.3 })
            {
                double x = 0, v = 0.01, ys = 0, vs = 0; int contact = 0; bool started = false; int t = 0;
                for (t = 0; t < 2000; ++t)
                {
                    const double d = x - ys; const double F = d > 0 ? k * std::pow (d, p) : 0;
                    v -= F / m; x += v;
                    // the string point: an infinite string, displacement grows by F/2 per sample, no return (the first ms)
                    ys += 0.5 * F;
                    if (F > 0) { ++contact; started = true; } else if (started && v < 0.0) break;
                }
                std::printf ("m %6.1f k %6.2f p %.1f: contact %4d samples (%.2f ms)\n", m, k, p, contact, contact / 48.0);
            }
}
