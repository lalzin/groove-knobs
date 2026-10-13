// Boogie Trio makeup: K-weighted level of the pad stable over the knob.

#include "BtTestHelpers.h"

using namespace bttest;
namespace ts = gk::testsignals;

TEST_CASE ("BT 8. Makeup: K-weighted level of the pad stays close to dry, every preset and chip combination", "[bt][makeup]")
{
    const double sr = 48000.0;
    const auto pad = ts::chordPad (sr, 6.0, -3.0f);
    const int skip = (int) (0.5 * sr);
    const double dry = gk::KWeighting::levelDb (pad, sr, skip);
    for (int preset = 0; preset < numPresets; ++preset)
        for (int combo = 0; combo < 4; ++combo)
        {
            BoogieTrioProcessor proc;
            for (float a : { 0.01f, 0.02f, 0.1f, 0.25f, 0.4f, 0.5f, 0.6f, 0.75f, 0.9f, 1.0f })
            {
                CAPTURE (preset, combo, a);
                gk::OfflineRenderer::Options opt;
                opt.blockSize = 512;
                opt.preset = preset;
                opt.amount = [a] (double) { return a; };
                setParam (proc.getPresetParameter(), (float) preset);
                setParam (proc.getVintageParameter(), (float) (combo & 1));
                setParam (proc.getWideParameter(), (float) (combo >> 1));
                const auto out = gk::OfflineRenderer::render (proc, pad, sr, opt);
                const double d = gk::KWeighting::levelDb (out, sr, skip) - dry;
                INFO ("deviation " << d << " dB");
                // Never louder than +1 dB. The "Wider" side raises the channel
                // peaks, so the calibration's peak guard can leave a wide
                // setting up to ~4 dB under the dry level (DECISIONS.md).
                CHECK (d <= 1.0);
                CHECK (d >= -4.0);
            }
        }
}
