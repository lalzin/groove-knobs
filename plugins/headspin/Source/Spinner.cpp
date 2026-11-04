#include "Spinner.h"
#include <algorithm>

namespace headspin
{
namespace
{
    double wrapPi (double a) { return a - 2.0 * pi * std::floor ((a + pi) / (2.0 * pi)); }

    /** From p to q (f 0..1) round the head: the angle the short way, the
        distance in between (a straight line could cross the head). */
    Point polarLerp (Point p, Point q, double f)
    {
        const double rp = std::hypot (p.x, p.y), rq = std::hypot (q.x, q.y);
        const double ap = std::atan2 (p.x, p.y), aq = std::atan2 (q.x, q.y);
        const double a = ap + f * wrapPi (aq - ap), r = rp + f * (rq - rp);
        return { r * std::sin (a), r * std::cos (a) };
    }
    constexpr double panDepth = 0.85; // the far channel keeps -10 dB of the (delayed, shadowed) source at the side
    constexpr double pinnaHz = 4000.0, airHz = 8000.0;
} // namespace

double Spinner::earDelaySeconds (double phi) noexcept
{
    // Woodworth's ray-tracing formula as written by Brown & Duda: the wave
    // reaches an ear facing it a/c early, one behind the head travels round it
    const double ac = headRadius / speedOfSound;
    return phi < 0.5 * pi ? -ac * std::cos (phi) : ac * (phi - 0.5 * pi);
}

double Spinner::shadowAlpha (double phi) noexcept
{
    // Brown & Duda: alpha_min 0.1 at theta_min 150°, 2 facing the ear (+6 dB
    // above the corner), a little "bright spot" right behind it
    constexpr double alphaMin = 0.1, thetaMin = 150.0 * pi / 180.0;
    return (1.0 + 0.5 * alphaMin) + (1.0 - 0.5 * alphaMin) * std::cos (phi / thetaMin * pi);
}

void Spinner::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto* s : { &sMix, &sWidth, &sDepth, &sRot, &sRate, &sDist, &sLevel, &sDoppler })
        s->prepare (sr, smoothSeconds);
    shapeFadeStep = 1.0 / (shapeFadeSeconds * sr);
    glideStep = 1.0 / (jumpGlideSeconds * sr);
    motion.prepare (sr);
    // head shadow: the pole at 2 c / a (~1.25 kHz), bilinear, prewarped at the pole
    const double wp = 2.0 * speedOfSound / headRadius;
    hsG = 1.0 / std::tan (0.5 * wp / sr);
    // pinna band-pass (TPT state-variable filter, Q ~ 1.2)
    svfG = std::tan (pi * pinnaHz / sr);
    svfK = 1.0 / 1.2;
    svfA1 = 1.0 / (1.0 + svfG * (svfG + svfK));
    svfA2 = svfG * svfA1;
    svfA3 = svfG * svfA2;
    airCoeff = 1.0 - std::exp (-2.0 * pi * airHz / sr);
    reset();
}

void Spinner::reset()
{
    for (auto* s : { &sMix, &sWidth, &sDepth, &sRot, &sRate, &sDist, &sLevel, &sDoppler })
        s->init = false;
    const double ac = headRadius / speedOfSound;
    delay.prepare ((int) std::ceil ((maxDopplerSeconds + ac * (1.0 + 0.5 * pi)) * sr) + 8);
    room.prepare (sr);
    hsXL = hsYL = hsXR = hsYR = 0.0;
    pinnaL = pinnaR = {};
    airL = airR = 0.0;
    started = false;
    motion.reset();
    glide = 1.0;
    shapeFade = 1.0;
}

Point Spinner::pathPoint (double u) const
{
    const auto p = store.get (currentSlot).at (u);
    if (previousSlot < 0 || shapeFade >= 1.0)
        return p;
    // a new shape arrived: crossfade the points (smoothly)
    const auto q = store.get (previousSlot).at (u);
    return polarLerp (q, p, shapeFade * shapeFade * (3.0 - 2.0 * shapeFade));
}

Point Spinner::glidedPoint (double u) const
{
    auto p = pathPoint (u);
    if (glide < 1.0)
    {
        // after a jump (transport start, retrigger): glide from where the sound was
        p = polarLerp (glideFrom, p, glide * glide * (3.0 - 2.0 * glide));
    }
    return p;
}

Position Spinner::transform (Point p) const
{
    // stretch, then rotate (clockwise seen from above)
    const double x = p.x * width, y = p.y * depth;
    const double c = std::cos (rotation), s = std::sin (rotation);
    Position out;
    out.x = x * c + y * s;
    out.y = -x * s + y * c;
    out.radius = std::hypot (out.x, out.y);
    out.azimuthDeg = std::atan2 (out.x, out.y) * 180.0 / pi;
    return out;
}

Position Spinner::positionAt (double u) const
{
    return transform (glidedPoint (u));
}

Position Spinner::position() const
{
    return positionAt (phase);
}

void Spinner::process (const float* inL, const float* inR, float* outL, float* outR, int n, const Transport& transport, const int* noteOns,
                        int numNoteOns, const float* mixPerSample)
{
    started = true;
    // a new shape table: crossfade to it (both slots stay reserved meanwhile)
    if (const int latest = store.latest(); latest != currentSlot && (previousSlot < 0 || shapeFade >= 1.0))
    {
        previousSlot = currentSlot;
        currentSlot = latest;
        shapeFade = 0.0;
    }
    store.reserve (currentSlot, previousSlot);

    Motion::Settings ms;
    ms.sync = target.sync;
    ms.division = target.division;
    ms.rateHz = target.rateHz;
    ms.leftToRight = target.leftToRight;
    ms.startTurns = target.startDeg / 360.0;
    ms.groove = target.groove;
    ms.retrigger = target.retrigger;
    motion.beginBlock (ms, transport);
    int nextNote = 0;
    // the path's nearest and farthest points (the Doppler delay is 0 at the
    // nearest), once per block
    double farthest = 0.0;
    {
        double m = 1.0e9;
        for (int k = 0; k < 256; ++k)
        {
            const double rad = transform (pathPoint (k / 256.0)).radius;
            m = std::min (m, rad);
            farthest = std::max (farthest, rad);
        }
        nearestRadius = m;
    }
    // Doppler kept musical: the radial speed (~ 2 pi rate (far - near) scale)
    // never shifts the pitch by more than maxDopplerShift
    const double radialSpan = std::max (1.0e-6, farthest - nearestRadius);
    const double dopplerScale = std::min (pathScale, maxDopplerShift * speedOfSound / (2.0 * pi * std::max (0.01, motion.turnsPerSecond()) * radialSpan));
    const double maxDelaySamples = (double) delay.mask - 4.0;
    const double frontPinnaDb = 1.5, backPinnaDb = -8.0;

    for (int i = 0; i < n; ++i)
    {
        // (MIX per sample from the host's ramp when given, else smoothed here)
        const double mix = mixPerSample != nullptr ? (double) mixPerSample[i] : sMix.next (target.mix);
        width = sWidth.next (target.width);
        depth = sDepth.next (target.depth);
        rotation = sRot.next (target.rotationDeg) * pi / 180.0;
        const double dist = sDist.next (target.distance);
        const double level = std::pow (10.0, sLevel.next (target.levelDb) / 20.0);

        // Where the sound is (Motion: rate or tempo, direction, groove, retrigger)
        bool note = false;
        while (nextNote < numNoteOns && noteOns[nextNote] <= i)
        {
            note = note || noteOns[nextNote] == i;
            ++nextNote;
        }
        phase = motion.next (0.5f * (inL[i] + inR[i]), note);
        if (motion.takeJump())
        {
            glideFrom = lastPoint;
            glide = 0.0;
        }
        // (the glide and the shape crossfade work on the path itself, before
        // stretch and rotation)
        lastPoint = glidedPoint (phase);
        const auto pos = transform (lastPoint);
        glide = std::min (1.0, glide + glideStep);
        if (previousSlot >= 0)
        {
            shapeFade = std::min (1.0, shapeFade + shapeFadeStep);
            if (shapeFade >= 1.0)
            {
                previousSlot = -1;
                store.reserve (currentSlot, -1);
            }
        }

        const double th = pos.azimuthDeg * pi / 180.0;
        const double side = std::sin (th);  // -1 left .. +1 right
        const double front = std::cos (th); // +1 front .. -1 back
        const double rr = std::max (0.05, pos.radius);

        const float xL = inL[i], xR = inR[i];
        const float mono = 0.5f * (xL + xR);
        delay.push (mono);

        // Angle between the source and each ear's axis (0 = facing it)
        const double phiR = std::abs (wrapPi (th - 0.5 * pi)), phiL = std::abs (wrapPi (th + 0.5 * pi));

        // 1. Delays: Doppler (the source's distance, 0 at the path's nearest
        //    point, scaled by DISTANCE) + Woodworth interaural delay (only the
        //    difference: the earlier ear is not delayed)
        const double tL = earDelaySeconds (phiL), tR = earDelaySeconds (phiR), tMin = std::min (tL, tR);
        // (smoothed: the nearest point and the scale move with the shape, once per block)
        const double doppler = std::clamp (sDoppler.next (dist * dopplerScale * (rr - nearestRadius) / speedOfSound), 0.0, maxDopplerSeconds);
        double l = delay.read (std::min (maxDelaySamples, (doppler + tL - tMin) * sr));
        double r = delay.read (std::min (maxDelaySamples, (doppler + tR - tMin) * sr));

        // 2. Head shadow per ear (Brown-Duda), normalised so a source straight
        //    ahead is flat, its depth scaled for loudspeakers
        // (the high-frequency power of the two ears together is kept constant:
        // the difference between the ears, the cue, is the model's; the
        // brightness of the whole does not change as the source goes round)
        double alphaL = std::pow (shadowAlpha (phiL), shadowAmount), alphaR = std::pow (shadowAlpha (phiR), shadowAmount);
        const double hfNorm = std::sqrt (2.0 / (alphaL * alphaL + alphaR * alphaR));
        alphaL *= hfNorm;
        alphaR *= hfNorm;
        auto shadow = [this] (double x, double alpha, double& xPrev, double& yPrev)
        {
            const double y = ((1.0 + alpha * hsG) * x + (1.0 - alpha * hsG) * xPrev - (1.0 - hsG) * yPrev) / (1.0 + hsG);
            xPrev = x;
            yPrev = y;
            return y;
        };
        l = shadow (l, alphaL, hsXL, hsYL);
        r = shadow (r, alphaR, hsXR, hsYR);

        // 3. Pinna front / back cue (parametric): a small peak around 4 kHz in
        //    front, a dip there behind, and less air above 8 kHz behind
        const double behind = std::max (0.0, -front), ahead = std::max (0.0, front);
        const double bellGain = std::pow (10.0, (frontPinnaDb * ahead + backPinnaDb * behind) / 20.0);
        auto pinna = [this, bellGain] (double x, Svf& s)
        {
            const double v3 = x - s.ic2;
            const double v1 = svfA1 * s.ic1 + svfA2 * v3;
            const double v2 = s.ic2 + svfA2 * s.ic1 + svfA3 * v3;
            s.ic1 = 2.0 * v1 - s.ic1;
            s.ic2 = 2.0 * v2 - s.ic2;
            return x + svfK * (bellGain - 1.0) * v1; // bell: x + k (A^2 - 1) band-pass, with A^2 = bellGain
        };
        l = pinna (l, pinnaL);
        r = pinna (r, pinnaR);
        airL += airCoeff * (l - airL);
        airR += airCoeff * (r - airR);
        l += 0.6 * behind * (airL - l);
        r += 0.6 * behind * (airR - r);

        // 4. Constant-power panning from sin θ; close to the head the level
        //    difference grows (near field)
        const double nearField = std::clamp ((0.6 - rr) / 0.4, 0.0, 1.0);
        const double pan = 0.25 * pi * (1.0 + panDepth * side);
        const double nfL = 1.0 - 0.3 * nearField * side, nfR = 1.0 + 0.3 * nearField * side;
        const double gL = std::cos (pan) * nfL, gR = std::sin (pan) * nfR;

        // 5. Distance: direct level 1/r (DISTANCE sets how strongly, +-6 dB at
        //    most); the room is fed at a near-constant level, so the
        //    direct-to-reverberant ratio falls with distance, more behind
        const double gDist = std::clamp (std::pow (1.0 / rr, dist), 0.5, 2.0);
        const double send = dist * (0.18 + 0.12 * std::min (1.0, rr) + 0.15 * behind);
        float roomL = 0.0f, roomR = 0.0f;
        room.process ((float) (mono * send), roomL, roomR);

        // (+2.5 dB: a constant-power pan of the mono source sits that much
        // under the dry, the input on both channels; measured on music, so
        // turning MIX keeps the loudness)
        constexpr double wetMakeup = 1.3335;
        const double wetL = wetMakeup * (gDist * gL * l + roomL), wetR = wetMakeup * (gDist * gR * r + roomR);

        // MIX: constant-power dry / wet (MIX 0 = the input, exactly)
        const double dryG = std::cos (0.5 * pi * mix), wetG = std::sin (0.5 * pi * mix);
        outL[i] = (float) (level * (dryG * xL + wetG * wetL));
        outR[i] = (float) (level * (dryG * xR + wetG * wetR));
    }
}
} // namespace headspin
