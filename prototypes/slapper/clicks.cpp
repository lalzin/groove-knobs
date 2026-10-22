// the first ms of each attack and each note change: the largest sample step there against the note's own steps
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
    {
        Voice v; v.prepare (sr);
        std::vector<float> x;
        for (double hz : { 41.2, 55.0, 110.0, 41.2 })
        {
            v.noteOn (hz, 0.8f, (Attack) a);
            for (int i = 0; i < (int) (0.25 * sr); ++i) x.push_back (v.process());
        }
        const int L = (int) (0.25 * sr);
        for (int n = 0; n < 4; ++n)
        {
            double first = 0, rest = 0;
            for (int i = n * L + 1; i < n * L + L; ++i)
                (i < n * L + sr / 1000 ? first : rest) = std::fmax (i < n * L + sr / 1000 ? first : rest, std::fabs (x[(size_t) i] - x[(size_t) i - 1]));
            std::printf ("%-5s note %d: largest step, first ms %.4f, rest %.4f\n", names[a], n, first, rest);
        }
    }
}
