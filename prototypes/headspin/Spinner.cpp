#include "Spinner.h"
#include <algorithm>

namespace headspin
{
void Spinner::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto* s : { &sMix, &sWidth, &sDepth, &sRot, &sRate, &sDist, &sLevel })
        s->prepare (sr, smoothSeconds);
    shape.build (ShapeTable::circle);
    delayL.prepare ((int) std::ceil (maxItdSeconds * sr) + 4);
    delayR.prepare ((int) std::ceil (maxItdSeconds * sr) + 4);
    room.prepare (sr);
    reset();
}

void Spinner::reset()
{
    for (auto* s : { &sMix, &sWidth, &sDepth, &sRot, &sRate, &sDist, &sLevel })
        s->init = false;
    delayL.prepare ((int) std::ceil (maxItdSeconds * sr) + 4);
    delayR.prepare ((int) std::ceil (maxItdSeconds * sr) + 4);
    room.prepare (sr);
    shadowL = shadowR = backL = backR = 0.0;
    started = false;
}

Position Spinner::positionAt (double u) const
{
    const auto p = shape.at (u);
    // stretch, then rotate (clockwise seen from above)
    const double x = p.x * width, y = p.y * depth;
    const double c = std::cos (rotation), s = std::sin (rotation);
    Position out;
    out.x = x * c + y * s;
    out.y = -x * s + y * c;
    out.radius = std::hypot (out.x, out.y);
    out.azimuthDeg = std::atan2 (out.x, out.y) * 180.0 / M_PI;
    return out;
}

Position Spinner::position() const
{
    return positionAt (phase + target.startDeg / 360.0);
}

void Spinner::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    if (! started)
    {
        phase = 0.0;
        started = true;
    }
    const double maxItd = maxItdSeconds * sr;
    const double twoPiOverSr = 2.0 * M_PI / sr;
    const double backCoeff = 1.0 - std::exp (-2.0 * M_PI * 4000.0 / sr); // behind the head: 4 kHz one-pole

    for (int i = 0; i < n; ++i)
    {
        const double mix = sMix.next (target.mix);
        width = sWidth.next (target.width);
        depth = sDepth.next (target.depth);
        rotation = sRot.next (target.rotationDeg) * M_PI / 180.0;
        const double rate = sRate.next (target.rateHz);
        const double dist = sDist.next (target.distance);
        const double level = std::pow (10.0, sLevel.next (target.levelDb) / 20.0);

        // Where the sound is: the phase moves at the rate, in the direction
        // (a direction change keeps the position, only the way it goes flips)
        const auto pos = positionAt (phase + target.startDeg / 360.0);
        phase += (target.leftToRight ? 1.0 : -1.0) * rate / sr;
        phase -= std::floor (phase);

        const double th = pos.azimuthDeg * M_PI / 180.0;
        const double side = std::sin (th);   // -1 left .. +1 right
        const double front = std::cos (th);  // +1 front .. -1 back

        const float xL = inL[i], xR = inR[i];
        const float mono = 0.5f * (xL + xR);

        // 1. Constant-power gains from sin θ
        const double pan = 0.25 * M_PI * (1.0 + side);
        double gL = std::cos (pan), gR = std::sin (pan);

        // 2. Interaural delay on the far ear (the near ear reads the input itself)
        delayL.push (mono);
        delayR.push (mono);
        const double itd = maxItd * std::abs (side);
        double l = delayL.read (side > 0.0 ? itd : 0.0);
        double r = delayR.read (side < 0.0 ? itd : 0.0);

        // 3. Head shadow: the far ear loses its highs (20 kHz → 3 kHz at the side)
        const double fc = shadowMaxHz * std::pow (shadowMinHz / shadowMaxHz, std::abs (side));
        const double a = 1.0 - std::exp (-twoPiOverSr * std::min (fc, 0.45 * sr));
        shadowL += a * (l - shadowL);
        shadowR += a * (r - shadowR);
        // (faded in over the first few degrees off the centre line, so the
        // far ear changing sides in front / behind never jumps)
        const double shade = std::min (1.0, std::abs (side) * 8.0);
        if (side > 0.0)
            l += shade * (shadowL - l);
        else if (side < 0.0)
            r += shade * (shadowR - r);

        // 4. Behind the head: highs down a little (up to half of what is
        // above ~4 kHz) on both ears
        const double behind = std::max (0.0, -front);
        backL += backCoeff * (l - backL);
        backR += backCoeff * (r - backR);
        l += 0.5 * behind * (backL - l);
        r += 0.5 * behind * (backR - r);

        // 5. Distance: 1/r gain (r = 1 on the ears' circle; limited to +-6 dB),
        // DISTANCE sets how strongly; and a send to the room that grows with
        // the distance and behind the head
        const double rr = std::max (0.05, pos.radius);
        const double gDist = std::clamp (std::pow (1.0 / rr, dist), 0.5, 2.0);
        const double send = dist * (0.10 + 0.25 * std::min (1.0, rr) + 0.20 * behind);
        float roomL = 0.0f, roomR = 0.0f;
        room.process ((float) (mono * send), roomL, roomR);

        const double wetL = gDist * gL * l + roomL, wetR = gDist * gR * r + roomR;

        // MIX: constant-power dry / wet (MIX 0 = the input, exactly)
        const double dryG = std::cos (0.5 * M_PI * mix), wetG = std::sin (0.5 * M_PI * mix);
        outL[i] = (float) (level * (dryG * xL + wetG * wetL));
        outR[i] = (float) (level * (dryG * xR + wetG * wetR));
    }
}
} // namespace headspin
