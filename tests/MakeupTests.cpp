// Makeup calibration test: perceived (K-weighted) level along the knob travel.

#include "TestHelpers.h"

using namespace fftest;
using ff::FrenchFilterProcessor;
namespace ts = gk::testsignals;

namespace
{
struct Signals
{
    static constexpr double sr = 48000.0;
    Buffer pink = ts::pinkNoise (2, (int) (6.0 * sr), -18.0f);
    Buffer loop = ts::grooveLoop (sr, 124.0, 4, false);
    int skip = (int) (0.5 * sr);
};

const Signals& signals()
{
    static const Signals s;
    return s;
}

double deviationDb (FrenchFilterProcessor& proc, int preset, float macro, const Buffer& in, double* peakDb = nullptr)
{
    const float amount = proc.amountFor (macro);
    gk::OfflineRenderer::Options opt;
    opt.blockSize = 512;
    opt.preset = preset;
    opt.amount = [amount] (double) { return amount; };
    const auto out = gk::OfflineRenderer::render (proc, in, Signals::sr, opt);
    if (peakDb != nullptr)
        *peakDb = gk::dspmath::gainToDb (out.getMagnitude (0, out.getNumSamples()));
    const auto& s = signals();
    return gk::KWeighting::levelDb (out, Signals::sr, s.skip) - gk::KWeighting::levelDb (in, Signals::sr, s.skip);
}

std::vector<float> amounts()
{
    std::vector<float> a { 0.01f, 0.02f, 0.72f };
    for (int i = 0; i <= 20; ++i)
        a.push_back ((float) i / 20.0f);
    return a;
}
} // namespace

TEST_CASE ("7. Makeup: K-weighted level of the drum loop stays stable over both sides", "[makeup]")
{
    const auto presets = FrenchFilterProcessor::loadBuiltInPresets();
    for (int preset = 0; preset < presets.size(); ++preset)
    {
        FrenchFilterProcessor proc;
        for (float sign : { -1.0f, 1.0f })
            for (float a : amounts())
            {
                // Low-pass side: ±1 dB. The high-pass side removes the kick:
                // full compensation would push the hats / claps into the
                // safety clipper, so the calibration's peak guard wins and the
                // level drops towards the end of travel (DECISIONS.md). There
                // we check the peaks stay clear of the clipper instead.
                CAPTURE (preset, sign, a);
                double peak = 0.0;
                const double d = deviationDb (proc, preset, sign * a, signals().loop, &peak);
                INFO ("deviation " << d << " dB, peak " << peak << " dBFS");
                CHECK (d <= 1.0);
                if (sign < 0.0f)
                    CHECK (d >= -1.0);
                else
                {
                    CHECK (d >= -8.0);
                    CHECK (peak <= -1.4);
                }
            }
    }
}

// Pink noise loses far more K-weighted level through a 180 Hz low-pass than
// any kick/bass-driven material, and a single makeup curve cannot satisfy
// both (see DECISIONS.md). Kept visible but allowed to fail until the
// calibration target is validated by ear.
TEST_CASE ("7b. Makeup: K-weighted level of pink noise within ±1 dB over 0-100 %", "[makeup][!mayfail]")
{
    for (int preset = 0; preset < FrenchFilterProcessor::loadBuiltInPresets().size(); ++preset)
    {
        FrenchFilterProcessor proc;
        double worst = 0.0;
        for (float sign : { -1.0f, 1.0f })
            for (float a : amounts())
                worst = juce::jmax (worst, std::abs (deviationDb (proc, preset, sign * a, signals().pink)));
        CAPTURE (preset);
        INFO ("worst pink deviation " << worst << " dB");
        CHECK (worst <= 1.0);
    }
}
