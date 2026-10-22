// SLAPPER — the amp. Preamp (asymmetric saturation, 2x oversampled),
// tone (two shelves and a peak), a cabinet of four modal resonators (no IR),
// a soft limiter. Plus the low end the velocity pickup takes away (the sub).
#pragma once
#include <array>
#include <vector>

namespace slapper
{
class Amp
{
public:
    void prepare (double sampleRate);
    void reset();
    float process (float x) noexcept;

    float drive = 0.15f; // SATURATION 0..1
    float tone = 0.0f;  // TIMBRE -1 (dark) .. +1 (bright)
    float sub = 1.0f;   // the low end (0..1): the cabinet's 80 Hz and the shelf under it

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float run (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void clear() noexcept { x1 = x2 = y1 = y2 = 0; }
    };
    static Biquad lowShelf (double fs, double f, double db, double q = 0.707);
    static Biquad highShelf (double fs, double f, double db, double q = 0.707);
    static Biquad peak (double fs, double f, double db, double q);
    static Biquad bandPass (double fs, double f, double q);
    static Biquad lowPass (double fs, double f, double q);
    void design();

    double sr = 48000.0;
    float designedTone = -9.0f, designedSub = -9.0f;
    // 2x oversampling around the saturation: a half-band FIR up and down
    std::vector<float> hb; // its taps
    std::array<float, 64> upHist {}, downHist {};
    int upPos = 0, downPos = 0;
    Biquad subShelf, lowS, highS, mid, cab[4], cabDirect;
    std::array<float, 4> cabGain {};
    float saturate (float x) const noexcept;
};
} // namespace slapper
