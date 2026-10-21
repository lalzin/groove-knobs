#include "SingItBackDsp.h"

namespace sib
{
using namespace gk::dspmath;

//==============================================================================
const std::vector<int>& Harmony::degrees (int s)
{
    static const std::vector<int> tables[numScales] = {
        { 0, 2, 4, 5, 7, 9, 11 }, // major
        { 0, 2, 3, 5, 7, 8, 10 }, // natural minor
        { 0, 2, 3, 5, 7, 9, 10 }, // dorian
        { 0, 2, 4, 5, 7, 9, 10 }, // mixolydian
        { 0, 2, 4, 7, 9 },        // major pentatonic
        { 0, 3, 5, 7, 10 },       // minor pentatonic
    };
    return tables[juce::jlimit (0, numScales - 1, s)];
}

namespace
{
    /** Absolute MIDI note of scale degree `d` (any integer, octaves wrap). */
    int noteOfDegree (int d, int key, const std::vector<int>& deg)
    {
        const int n = (int) deg.size();
        const int octave = (int) std::floor ((double) d / n);
        return key + 12 * octave + deg[(size_t) (d - octave * n)];
    }

    /** Scale degree (absolute index) of the scale note nearest to `midi`. */
    int nearestDegree (double midi, int key, const std::vector<int>& deg)
    {
        const int n = (int) deg.size();
        const int base = (int) std::floor ((midi - key) / 12.0) * n;
        int best = base;
        double bestDist = 1.0e9;
        for (int d = base - n; d <= base + 2 * n; ++d)
            if (const double dist = std::abs (noteOfDegree (d, key, deg) - midi); dist < bestDist)
            {
                bestDist = dist;
                best = d;
            }
        return best;
    }
} // namespace

double Harmony::snapToScale (double midi, int key, int scale)
{
    const auto& deg = degrees (scale);
    return noteOfDegree (nearestDegree (midi, key, deg), key, deg);
}

bool Harmony::inScale (double midi, int key, int scale)
{
    const int pc = ((int) std::lround (midi) - key) % 12;
    const int p = pc < 0 ? pc + 12 : pc;
    const auto& deg = degrees (scale);
    return std::find (deg.begin(), deg.end(), p) != deg.end();
}

double Harmony::target (double detected, int key, int scale, bool snapOn, int interval, int role)
{
    const auto& deg = degrees (scale);
    const int n = (int) deg.size();
    const bool heptatonic = n == 7;
    // The interval of the harmony voices, up (harmonyUp) or down (harmonyDown)
    auto semitonesOf = [&] (int iv) // fixed sizes (snap off, pentatonic)
    {
        switch (iv)
        {
            case octaves: return 12;
            case fifths: return 7;
            case fourths: return 5;
            default: return 4; // thirds (Auto handled by the caller)
        }
    };
    auto degreesOf = [&] (int iv)
    {
        switch (iv)
        {
            case octaves: return n;
            case fifths: return 4;
            case fourths: return 3;
            default: return 2; // thirds, Auto
        }
    };

    if (role == unison)
        return snapOn ? snapToScale (detected, key, scale) : detected;
    if (role == octaveDown)
        return (snapOn ? snapToScale (detected, key, scale) : detected) - 12.0;

    const double sign = role == harmonyDown ? -1.0 : 1.0;
    if (! snapOn)
    {
        int st = semitonesOf (interval);
        if (interval == autoThirds)
        {
            // the diatonic third of the nearest degree (3 or 4), the pitch untouched
            const int d = nearestDegree (detected, key, deg);
            st = heptatonic ? std::abs (noteOfDegree (d + (role == harmonyDown ? -2 : 2), key, deg) - noteOfDegree (d, key, deg)) : 4;
        }
        return detected + sign * st;
    }

    const int d = nearestDegree (detected, key, deg);
    if (heptatonic)
        return noteOfDegree (d + (int) sign * degreesOf (interval), key, deg);
    // pentatonic: the interval in semitones, then the nearest scale note
    const int from = noteOfDegree (d, key, deg);
    const int st = interval == autoThirds ? 4 : semitonesOf (interval);
    return snapToScale (from + sign * st, key, scale);
}

juce::String Harmony::noteName (int pc)
{
    static const char* names[] = { "Do", "Do#", "R\xc3\xa9", "Mi\xe2\x99\xad", "Mi", "Fa", "Fa#", "Sol", "La\xe2\x99\xad", "La", "Si\xe2\x99\xad", "Si" };
    return juce::isPositiveAndBelow (pc, 12) ? juce::String::fromUTF8 (names[pc]) : juce::String();
}

//==============================================================================
void PitchTracker::prepare (double sr)
{
    decimation = juce::jmax (1, (int) std::round (sr / 12000.0));
    rate = sr / decimation;
    c1 = gk::TptSvf::Coeffs::make (0.42 * rate, sr, 0.5412);
    c2 = gk::TptSvf::Coeffs::make (0.42 * rate, sr, 1.3066);
    window = (int) std::round (0.032 * rate);
    tauMin = juce::jmax (2, (int) std::floor (rate / 1100.0));
    tauMax = (int) std::ceil (rate / 70.0);
    hop = juce::jmax (8, (int) std::round (0.005 * rate));
    ring.assign (4096, 0.0f);
    frame.assign ((size_t) (window + tauMax + 2), 0.0f);
    diff.assign ((size_t) (tauMax + 2), 0.0f);
    reset();
}

void PitchTracker::reset()
{
    aa1.reset();
    aa2.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    counter = write = sinceHop = 0;
    f0 = 220.0f;
    aperiodic = 1.0f;
    isVoiced = false;
    recent = { 220.0f, 220.0f, 220.0f };
}

bool PitchTracker::push (float x) noexcept
{
    const float y = (float) aa2.processLowpass (aa1.processLowpass (x, c1), c2);
    if (++counter < decimation)
        return false;
    counter = 0;
    ring[(size_t) write] = y;
    write = (write + 1) & 4095;
    if (++sinceHop < hop)
        return false;
    sinceHop = 0;
    analyse();
    return true;
}

void PitchTracker::analyse() noexcept
{
    const int len = window + tauMax;
    double energy = 0.0;
    for (int i = 0; i < len; ++i)
    {
        frame[(size_t) i] = ring[(size_t) ((write - len + i) & 4095)];
        if (i < window)
            energy += (double) frame[(size_t) i] * frame[(size_t) i];
    }
    const double frameRms = std::sqrt (energy / window);
    // YIN: difference function, cumulative mean normalised
    double running = 0.0;
    int best = -1;
    float bestValue = 1.0e9f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double e = frame[(size_t) j] - frame[(size_t) (j + tau)];
            d += e * e;
        }
        running += d;
        diff[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
    }
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (diff[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau])
                ++tau;
            best = tau;
            bestValue = diff[(size_t) tau];
            break;
        }
        if (diff[(size_t) tau] < bestValue)
        {
            bestValue = diff[(size_t) tau];
            best = tau;
        }
    }
    aperiodic = bestValue;
    // voiced: a clear period (real voices with breath / reverb reach 0.3),
    // with hysteresis so a held note does not flicker
    isVoiced = best > 0 && bestValue < (isVoiced ? 0.4f : 0.3f) && frameRms > 0.004;
    if (! isVoiced)
        return;
    // parabolic interpolation
    double tauFine = best;
    if (best > 1 && best < tauMax)
    {
        const double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const double den = a - 2.0 * b + c;
        if (std::abs (den) > 1.0e-12)
            tauFine += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den);
    }
    recent[(size_t) recentIndex] = (float) (rate / tauFine);
    recentIndex = (recentIndex + 1) % 3;
    // median of the last three: no octave jumps from a single bad frame
    auto r = recent;
    std::sort (r.begin(), r.end());
    f0 = r[1];
}

//==============================================================================
SingItBackDsp::SingItBackDsp()
{
    for (int k = 0; k < maxVoices; ++k)
    {
        partGain[(size_t) k] = 1.0f;
        partPan[(size_t) k] = defaultPan (k);
    }
}

int SingItBackDsp::latencyFor (double sr) noexcept
{
    // ~21 ms: 1024 at 44.1 / 48 kHz, 2048 at 88.2 / 96, 4096 at 176.4 / 192.
    // Enough for two-period grains down to ~94 Hz (below, the grains shorten)
    int b = 512;
    while (b < 8192 && b < 0.019 * sr)
        b <<= 1;
    return b;
}

const SingItBackDsp::StyleSpec& SingItBackDsp::styleSpec (int st) noexcept
{
    using H = Harmony;
    static const StyleSpec specs[numStyles] = {
        // Club: the reference crowd (unison, harmony, an octave below)
        { { H::unison, H::harmonyUp, H::unison, H::harmonyUp, H::octaveDown, H::unison, H::harmonyUp, H::harmonyDown, H::unison, H::octaveDown,
            H::harmonyUp, H::unison },
          1.0f, 1.0f, 1.0f, 1.0f, 0.08f },
        // Gospel: stacked triads, wide vibrato
        { { H::unison, H::harmonyUp, H::fifthUp, H::octaveDown, H::harmonyUp, H::unison, H::fifthUp, H::harmonyDown, H::unison, H::harmonyUp,
            H::octaveDown, H::fifthUp },
          1.4f, 0.8f, 1.0f, 1.0f, 0.04f },
        // Stade: a chanting mass, mostly unison, loose, all kinds of voices
        { { H::unison, H::unison, H::octaveDown, H::unison, H::harmonyUp, H::unison, H::octaveDown, H::unison, H::unison, H::harmonyUp, H::unison,
            H::octaveDown },
          1.0f, 1.5f, 1.5f, 1.0f, 0.1f },
        // Chorale: a trained choir, tight and in tune, four parts
        { { H::unison, H::harmonyUp, H::octaveDown, H::fifthUp, H::unison, H::harmonyUp, H::harmonyDown, H::fifthUp, H::unison, H::octaveDown,
            H::harmonyUp, H::unison },
          0.6f, 0.5f, 0.6f, 1.0f, 0.03f, 0.5f },
        // Enfants: a children's choir, an octave up, small voices
        { { H::octaveUp, H::unison, H::octaveUp, H::harmonyUp, H::octaveUp, H::unison, H::octaveUp, H::harmonyUp, H::unison, H::octaveUp,
            H::harmonyUp, H::octaveUp },
          1.2f, 1.0f, 1.0f, 1.25f, 0.05f },
        // Géants: monks / giants, an octave down, big voices
        { { H::octaveDown, H::octaveDown, H::unison, H::octaveDown, H::harmonyDown, H::octaveDown, H::unison, H::octaveDown, H::harmonyDown,
            H::octaveDown, H::unison, H::octaveDown },
          0.7f, 1.2f, 1.0f, 0.8f, 0.04f },
        // Robots: dead-straight chords (no vibrato, no detune, lockstep)
        { { H::unison, H::fifthUp, H::octaveDown, H::harmonyUp, H::unison, H::fifthUp, H::octaveDown, H::harmonyUp, H::unison, H::octaveUp,
            H::fifthUp, H::octaveDown },
          0.0f, 0.15f, 0.0f, 1.0f, 0.0f, 1.0f },
    };
    return specs[juce::jlimit (0, numStyles - 1, st)];
}

double SingItBackDsp::responseBeats (int r, double barBeats) noexcept
{
    return r == 1 ? 0.5 * barBeats : r == 2 ? barBeats : 0.0;
}

juce::StringArray SingItBackDsp::partNames()
{
    return { "Style", "Unisson", "Tierce +", "Quarte +", "Quinte +", "Octave +", juce::String::fromUTF8 ("Tierce \xe2\x88\x92"),
             juce::String::fromUTF8 ("Quinte \xe2\x88\x92"), juce::String::fromUTF8 ("Octave \xe2\x88\x92"), "Muette" };
}

float SingItBackDsp::defaultPan (int k) noexcept
{
    // left (even) / right (odd), from near the centre to the sides, the
    // sides alternating so a small crowd is already wide
    static const float pans[maxVoices] { -0.35f, 0.4f, -0.8f, 0.75f, -0.15f, 0.2f, -0.95f, 0.9f, -0.55f, 0.6f, -0.05f, 0.1f };
    return pans[juce::jlimit (0, maxVoices - 1, k)];
}

Harmony::Role SingItBackDsp::effectiveRole (int k, Harmony::Role styleRole) const noexcept
{
    switch (part[(size_t) k])
    {
        case partUnison: return Harmony::unison;
        case partThirdUp: case partFourthUp: case partFifthUp: return Harmony::harmonyUp;
        case partOctaveUp: return Harmony::octaveUp;
        case partThirdDown: case partFifthDown: return Harmony::harmonyDown;
        case partOctaveDown: return Harmony::octaveDown;
        default: return styleRole;
    }
}

double SingItBackDsp::partTarget (double midi, int k, Harmony::Role styleRole) const noexcept
{
    const auto at = [&] (int interval, int role) { return Harmony::target (midi, key, scale, snap, interval, role); };
    switch (part[(size_t) k])
    {
        case partUnison: return at (harmony, Harmony::unison);
        case partThirdUp: return at (Harmony::thirds, Harmony::harmonyUp);
        case partFourthUp: return at (Harmony::fourths, Harmony::harmonyUp);
        case partFifthUp: return at (Harmony::fifths, Harmony::harmonyUp);
        case partOctaveUp: return at (harmony, Harmony::unison) + 12.0;
        case partThirdDown: return at (Harmony::thirds, Harmony::harmonyDown);
        case partFifthDown: return at (Harmony::fifths, Harmony::harmonyDown);
        case partOctaveDown: return at (harmony, Harmony::octaveDown);
        default: return roleTarget (midi, styleRole);
    }
}

double SingItBackDsp::roleTarget (double midi, Harmony::Role role) const noexcept
{
    if (role == Harmony::fifthUp)
        return Harmony::target (midi, key, scale, snap, Harmony::fifths, Harmony::harmonyUp);
    if (role == Harmony::octaveUp)
        return Harmony::target (midi, key, scale, snap, harmony, Harmony::unison) + 12.0;
    return Harmony::target (midi, key, scale, snap, harmony, (int) role);
}

float SingItBackDsp::crowdDb (float crowd) noexcept
{
    // +1.5 dB at 100 %: with the lead stepping back, the crowd takes over
    // (Alan: "à 100 % on entend presque que l'effet", then "plus fort");
    // -9 dB at 0 %; the output ceiling keeps the peaks in check
    return 1.5f - 10.5f * (1.0f - clamp01 (crowd));
}

float SingItBackDsp::leadDb (float crowd) noexcept
{
    // untouched up to 30 %, then steps back to -18 dB at 100 %: nearly all
    // crowd, the voice a faint guide (Alan: "le dry / wet doit être plus fort")
    return -18.0f * smoothstep (0.3f, 1.0f, crowd);
}

float SingItBackDsp::audibleVoices (float crowd, int maxCount) noexcept
{
    // crowd^0.8 (not ^1.5): 25 % gives two or three voices, as heard
    // (DECISIONS.md); fractional: the last one fades in
    return (float) maxCount * std::pow (juce::jlimit (0.0f, 1.0f, crowd), 0.8f);
}

void SingItBackDsp::designVoices()
{
    // Each voice its own person (fixed at instantiation; humanize scales the
    // spread at run time). Offsets are stratified then shuffled, so they are
    // never bunched; vibrato rates follow the golden-ratio sequence over
    // 4.8-6.2 Hz, so the first 4, 8 or 12 voices are always spread apart.
    juce::Random rng (77);
    std::array<int, maxVoices> order {};
    for (int i = 0; i < maxVoices; ++i)
        order[(size_t) i] = i;
    for (int i = maxVoices - 1; i > 0; --i)
        std::swap (order[(size_t) i], order[(size_t) rng.nextInt (i + 1)]);
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        v.role = styleSpec (club).roles[(size_t) i];
        v.offsetMs = ((double) order[(size_t) i] + 0.25 + 0.5 * rng.nextDouble()) / maxVoices; // 0..1, stratified
        v.driftPhase = rng.nextDouble() * juce::MathConstants<double>::twoPi;
        v.driftRate = 0.12 + 0.2 * rng.nextDouble(); // a few seconds
        v.detune = (rng.nextDouble() * 2.0 - 1.0);   // x 8 cents x humanize
        const double golden = 0.6180339887498949 * i;
        v.vibHz = 0.03 + 0.94 * (golden - std::floor (golden)); // 0..1 -> 4.8..6.2
        v.vibCents = rng.nextDouble();                          // -> 8..20 cents
        v.vibPhase = rng.nextDouble() * juce::MathConstants<double>::twoPi;
        v.baseGain = (float) rng.nextDouble();                  // -> 0.9 .. 0.55
        v.pan = (float) (rng.nextDouble() * 2.0 - 1.0);
        v.send = (float) (0.55 + 0.45 * rng.nextDouble());
        v.formantJitter = (float) (rng.nextDouble() * 2.0 - 1.0);
        v.wanderRate[0] = 0.11 + 0.17 * rng.nextDouble();
        v.wanderRate[1] = 0.37 + 0.41 * rng.nextDouble();
        v.wanderPhase[0] = rng.nextDouble() * juce::MathConstants<double>::twoPi;
        v.wanderPhase[1] = rng.nextDouble() * juce::MathConstants<double>::twoPi;
        v.ampRate = 0.15 + 0.3 * rng.nextDouble();
        v.ampPhase = rng.nextDouble() * juce::MathConstants<double>::twoPi;
    }
}

SingItBackDsp::VoiceInfo SingItBackDsp::voiceInfo (int i) const
{
    const auto& v = voices[(size_t) juce::jlimit (0, maxVoices - 1, i)];
    const double h = juce::jmax (0.2, (double) humanize);
    const auto& st = styleSpec (style);
    VoiceInfo info;
    info.offsetMs = 3.0 + st.offsets * 37.0 * (0.6 + 0.4 * h) * v.offsetMs;
    info.detuneCents = st.detune * 11.0 * h * v.detune;
    info.vibratoHz = 4.8 + 1.4 * v.vibHz;
    info.vibratoCents = st.vibrato * (8.0 + 12.0 * v.vibCents) * (0.5 + 0.5 * h);
    info.gain = 0.9 - 0.35 * v.baseGain * h;
    info.pan = partPan[(size_t) juce::jlimit (0, maxVoices - 1, i)];
    info.send = v.send;
    info.role = (int) effectiveRole (juce::jlimit (0, maxVoices - 1, i), st.roles[(size_t) juce::jlimit (0, maxVoices - 1, i)]);
    return info;
}

void SingItBackDsp::prepare (double sr, int maxBlock, int nch)
{
    sampleRate = sr;
    numChannels = juce::jlimit (1, 2, nch);
    latency = latencyFor (sr);
    halfMax = latency / 2;
    gateLag = (int) (0.02 * sr); // the tracker's decision comes ~20 ms after the sound
    markLag = (int) (0.016 * sr); // the YIN window's centre (32 ms window)
    designVoices();
    // history: the oldest read is ~ L + 65 ms (Stade offsets) + the response
    // delay (up to 4 s) + a grain before the newest input
    maxResponse = (int) (4.0 * sr);
    std::int64_t size = 1;
    while (size < 2 * latency + (int) (0.1 * sr) + maxResponse + 4 * chunk)
        size <<= 1;
    mask = size - 1;
    history.assign ((size_t) size, 0.0f);
    gateHistory.assign ((size_t) size, 0.0f);
    marks.assign ((size_t) markRing, Mark {});
    // overlap-add ring: grains reach at most 2 x halfMax x 1.3 past the chunk
    std::int64_t accSize = 1;
    while (accSize < 4 * latency + 4 * chunk)
        accSize <<= 1;
    accMask = accSize - 1;
    for (auto& v : voices)
        v.acc.assign ((size_t) accSize, 0.0f);
    leadDelay.prepare (numChannels, latency);
    lead.setSize (numChannels, juce::jmax (1, maxBlock), false, true, false);
    tracker.prepare (sr);
    reverb.setSampleRate (sr);
    appliedRoomSize = -1;
    rmsCoeff = (float) (1.0 - std::exp (-1.0 / (0.04 * sr)));
    gateCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sr / 3.0))); // ~20 ms to settle
    leadCoeff = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    limiterRelease = (float) (1.0 - std::exp (-1.0 / (0.15 * sr)));
    limGain.fill (1.0f);
    for (auto* s : { &crowdLevel, &width, &roomMix })
        s->prepare (sr, 0.030);
    reset();
}

void SingItBackDsp::reset()
{
    for (auto& v : voices)
    {
        std::fill (v.acc.begin(), v.acc.end(), 0.0f);
        v.running = false;
        v.nextGrain = 0.0;
        v.semis = 0.0f;
        v.audible = 0.0f;
        v.drop = v.dropTarget = 1.0f;
        v.dropLeft = 0;
        v.level = 0.0f;
        v.interval = v.scoop = v.noteAge = 0.0;
        v.lastNote = 0;
        v.rng.setSeed (1000 + (juce::int64) (&v - voices.data()));
    }
    heldNote = -1.0f;
    smoothValid = false;
    noteId = 0;
    lastVoicedMark = -1000000;
    limGain.fill (1.0f);
    limLevel = 1.0f;
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (gateHistory.begin(), gateHistory.end(), 0.0f);
    markCount = 0;
    nextMark = 0.0;
    for (auto* a : { &outL, &outR, &revL, &revR })
        a->fill (0.0f);
    leadDelay.reset();
    tracker.reset();
    reverb.reset();
    gate = rms = 0.0f;
    clock = 0;
    for (auto* s : { &crowdLevel, &width, &roomMix })
        s->invalidate();
    crowdSmoothed = -1.0f;
    leadGain = -1.0f;
}

void SingItBackDsp::setKnobs (const float* v, int n)
{
    spread = n > spreadKnob ? clamp01 (v[spreadKnob]) : 0.6f;
    humanize = n > humanizeKnob ? clamp01 (v[humanizeKnob]) : 0.5f;
    dropouts = n > dropoutsKnob ? clamp01 (v[dropoutsKnob]) : 0.3f;
    room = n > roomKnob ? clamp01 (v[roomKnob]) : 0.4f;
    for (int k = 0; k < maxVoices; ++k)
    {
        const float db = n > levelKnob0 + k ? v[levelKnob0 + k] : 0.0f;
        partGain[(size_t) k] = db <= -29.9f ? 0.0f : dbToGain (db);
        partPan[(size_t) k] = n > panKnob0 + k ? juce::jlimit (-1.0f, 1.0f, v[panKnob0 + k]) : defaultPan (k);
        partSize[(size_t) k] = n > sizeKnob0 + k ? juce::jlimit (-0.3f, 0.3f, v[sizeKnob0 + k]) : 0.0f;
    }
}

void SingItBackDsp::setChoices (const int* idx, int n)
{
    key = n > keyChoice ? juce::jlimit (0, 11, idx[keyChoice]) : 0;
    scale = n > scaleChoice ? juce::jlimit (0, Harmony::numScales - 1, idx[scaleChoice]) : 0;
    harmony = n > harmonyChoice ? juce::jlimit (0, 4, idx[harmonyChoice]) : 0;
    voicesMax = n > voicesChoice ? (idx[voicesChoice] == 0 ? 4 : idx[voicesChoice] == 2 ? 12 : 8) : 8;
    roomSize = n > roomSizeChoice ? juce::jlimit (0, 2, idx[roomSizeChoice]) : 1;
    style = n > styleChoice ? juce::jlimit (0, numStyles - 1, idx[styleChoice]) : club;
    response = n > responseChoice ? juce::jlimit (0, 2, idx[responseChoice]) : 0;
    for (int k = 0; k < maxVoices; ++k)
        part[(size_t) k] = n > partChoice0 + k ? juce::jlimit (0, numParts - 1, idx[partChoice0 + k]) : partStyle;
}

void SingItBackDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());

    // Lead: the input, delayed by the latency, untouched
    for (int c = 0; c < nch; ++c)
        leadDelay.process (c, buffer.getReadPointer (c), lead.getWritePointer (c), numSamples);
    leadDelay.advance (numSamples);

    if (appliedRoomSize != roomSize)
    {
        juce::Reverb::Parameters rp;
        const float sizes[] { 0.45f, 0.72f, 0.93f }, damp[] { 0.55f, 0.45f, 0.3f };
        rp.roomSize = sizes[roomSize];
        rp.damping = damp[roomSize];
        rp.wetLevel = 1.0f;
        rp.dryLevel = 0.0f;
        rp.width = 1.0f;
        reverb.setParameters (rp);
        appliedRoomSize = roomSize;
    }
    // RÉPONSE: the crowd answers half a bar / a bar later (host tempo, else 120)
    bpm = timing.bpm > 1.0 ? timing.bpm : 120.0;
    beatsPerBar = timing.beatsPerBar > 0.0 ? timing.beatsPerBar : 4.0;

    const float roomMax[] { 0.4f, 0.6f, 0.85f };
    for (int i = 0; i < numSamples; ++i)
    {
        const int j = (int) (clock % (2 * chunk));
        float x = 0.0f;
        for (int c = 0; c < nch; ++c)
            x += buffer.getSample (c, i);
        x /= (float) nch;

        // Output: lead + the crowd computed at the end of the previous chunk
        roomMix.setTarget (room * roomMax[roomSize]);
        const float rm = roomMix.next();
        const float l = crowdMuted ? 0.0f : outL[(size_t) j] + rm * revL[(size_t) j];
        const float r = crowdMuted ? 0.0f : outR[(size_t) j] + rm * revR[(size_t) j];
        // lead level, from the crowd value of the last chunk (global grid), 10 ms
        const float leadTarget = dbToGain (leadDb (crowdSmoothed < 0.0f ? clamp01 (amount[i]) : crowdSmoothed));
        if (leadGain < 0.0f || std::abs (leadTarget - leadGain) < 1.0e-6f)
            leadGain = leadTarget; // lands exactly (1 = the input, bit for bit)
        else
            leadGain += (leadTarget - leadGain) * leadCoeff;
        float outA = leadGain * lead.getSample (0, i) + (nch == 2 ? l : 0.5f * (l + r));
        float outB = nch == 2 ? leadGain * lead.getSample (1, i) + r : outA;
        // ceiling at -1.5 dBFS (a big crowd peaks with the voice), planned a
        // chunk ahead; 1 (untouched) below it
        if (const float lg = limGain[(size_t) j]; ! juce::exactlyEqual (lg, 1.0f))
        {
            outA *= lg;
            outB *= lg;
        }
        buffer.getWritePointer (0)[i] = outA;
        if (nch == 2)
            buffer.getWritePointer (1)[i] = outB;

        // Input: level, pitch, voicing (consonants, breath, silence: the crowd
        // fades out in 20 ms), history
        rms += (std::abs (x) - rms) * rmsCoeff;
        if (tracker.push (x) && tracker.voiced())
            detectedMidi = (float) (69.0 + 12.0 * std::log2 (tracker.frequency() / 440.0));
        gate += ((tracker.voiced() ? 1.0f : 0.0f) - gate) * gateCoeff;
        history[(size_t) (clock & mask)] = x;
        gateHistory[(size_t) (clock & mask)] = gate;
        if (j % chunk == chunk - 1)
            processChunk (clock - (chunk - 1), clamp01 (amount[i]));
        ++clock;
    }

    // UI
    telemetry.pitchHz.store (tracker.voiced() ? tracker.frequency() : 0.0f, std::memory_order_relaxed);
    telemetry.inputRms.store (rms, std::memory_order_relaxed);
    telemetry.voicedFlag.store (gate > 0.5f, std::memory_order_relaxed);
    int heard = 0, dropping = 0;
    for (int k = 0; k < maxVoices; ++k)
    {
        const auto& v = voices[(size_t) k];
        heard += v.audible > 0.5f ? 1 : 0;
        dropping += v.audible > 0.5f && v.dropTarget < 0.99f ? 1 : 0;
        telemetry.voiceGain[(size_t) k].store (v.audible * v.drop, std::memory_order_relaxed);
        telemetry.voiceOffsetMs[(size_t) k].store ((float) voiceInfo (k).offsetMs, std::memory_order_relaxed);
    }
    for (int k = 0; k < maxVoices; ++k)
    {
        telemetry.voiceLevel[(size_t) k].store (voices[(size_t) k].level, std::memory_order_relaxed);
        voices[(size_t) k].level = 0.0f;
    }
    telemetry.responseSeconds.store ((float) (responseDelay / sampleRate), std::memory_order_relaxed);
    telemetry.activeChorus.store (heard, std::memory_order_relaxed);
    telemetry.dropoutCount.store (dropping, std::memory_order_relaxed);
    telemetry.degreePitchClass.store (tracker.voiced() ? (((int) std::lround (Harmony::snapToScale (detectedMidi, key, scale)) % 12) + 12) % 12 : -1,
                                      std::memory_order_relaxed);
}

void SingItBackDsp::makeMarks (std::int64_t available)
{
    // Analysis marks one period apart, each moved onto the waveform's peak
    // within ±1/4 period (the grains are then centred on the glottal pulses);
    // unvoiced: every 8 ms; those grains are laid untransposed at the same
    // spacing, half-overlapping Hann windows: the consonant comes out intact
    for (;;)
    {
        const bool voiced = tracker.voiced();
        double period = 0.008 * sampleRate;
        if (voiced)
        {
            // the detector gives a new pitch every 5 ms: as is, every grain of
            // those 5 ms gets the same period and the pitch moves in stairs
            // (exactly the auto-tune sound). Its contour is smoothed instead
            // (~8 ms, in log); a jump of more than a semitone (a new note, an
            // octave) is taken at once
            const double target = std::log (juce::jlimit (sampleRate / 1100.0, sampleRate / 60.0, sampleRate / (double) tracker.frequency()));
            if (! smoothValid || std::abs (target - smoothLogPeriod) > 0.058)
                smoothLogPeriod = target;
            else
                smoothLogPeriod += (target - smoothLogPeriod) * (1.0 - std::exp (-std::exp (smoothLogPeriod) / (0.008 * sampleRate)));
            smoothValid = true;
            period = std::exp (smoothLogPeriod);
        }
        else
            smoothValid = false;
        const int radius = voiced ? (int) (period * 0.25) : 0;
        const auto predicted = (std::int64_t) std::llround (nextMark);
        if (predicted + radius + markLag > available)
            return;
        auto pos = predicted;
        float midi = -1.0f;
        if (voiced)
        {
            const std::int64_t floorPos = markCount > 0 ? marks[(size_t) ((markCount - 1) % markRing)].pos + (std::int64_t) (period * 0.5) : 0;
            float best = -1.0f;
            for (auto t = juce::jmax (floorPos, predicted - radius); t <= predicted + radius; ++t)
                if (const float a = std::abs (history[(size_t) (t & mask)]); a > best)
                {
                    best = a;
                    pos = t;
                }
            midi = (float) (69.0 + 12.0 * std::log2 (sampleRate / period / 440.0));
            // the note being sung, held with hysteresis: a vibrato or a slide
            // across the boundary between two notes does not flip the harmony
            const float nearest = snap ? (float) Harmony::snapToScale (midi, key, scale) : std::round (midi);
            const bool newPhrase = pos - lastVoicedMark > (std::int64_t) (0.08 * sampleRate);
            if (heldNote < 0.0f || newPhrase || (! juce::exactlyEqual (nearest, heldNote) && std::abs (midi - heldNote) - std::abs (midi - nearest) > 0.3f))
            {
                heldNote = nearest;
                ++noteId;
            }
            lastVoicedMark = pos;
        }
        marks[(size_t) (markCount % markRing)] = { pos, (float) period, midi, voiced ? heldNote : -1.0f, noteId };
        ++markCount;
        nextMark = (double) pos + period;
    }
}

const SingItBackDsp::Mark* SingItBackDsp::findMark (double u, std::int64_t available, double span) const noexcept
{
    // the mark nearest to `u` whose grain (±span x period, capped) is already
    // in the history; marks are in time order: binary search
    const std::int64_t first = juce::jmax ((std::int64_t) 0, markCount - markRing);
    if (markCount == 0)
        return nullptr;
    std::int64_t lo = first, hi = markCount; // first mark at or after u
    while (lo < hi)
    {
        const auto mid = (lo + hi) / 2;
        if ((double) marks[(size_t) (mid % markRing)].pos < u)
            lo = mid + 1;
        else
            hi = mid;
    }
    auto fits = [&] (const Mark& m) { return (double) m.pos + span * juce::jmin ((double) m.period, (double) halfMax) + 1.0 <= (double) available; };
    const Mark* best = nullptr;
    double bestDist = 1.0e18;
    for (auto k = juce::jmin (lo, markCount - 1); k >= first && k >= lo - 2; --k)
    {
        const auto& m = marks[(size_t) (k % markRing)];
        if (! fits (m))
            continue;
        if (const double d = std::abs ((double) m.pos - u); d < bestDist)
        {
            bestDist = d;
            best = &m;
        }
    }
    if (best == nullptr) // u is in the not-yet-available future: the latest that fits
        for (auto k = markCount - 1; k >= first && k >= markCount - 64; --k)
            if (fits (marks[(size_t) (k % markRing)]))
                return &marks[(size_t) (k % markRing)];
    return best;
}

float SingItBackDsp::historyAt (double t) const noexcept
{
    const double f = std::floor (t);
    const auto i = (std::int64_t) f;
    const float a = history[(size_t) (i & mask)], b = history[(size_t) ((i + 1) & mask)];
    return a + (float) (t - f) * (b - a);
}

void SingItBackDsp::processChunk (std::int64_t chunkStart, float crowdNow)
{
    // Input is known up to `available`; this computes the crowd of the chunk
    // after next, [outStart, outStart + chunk): two chunks later, inside the
    // latency (one for the computation grid, one of lookahead for the ceiling)
    const std::int64_t available = chunkStart + chunk - 1, outStart = chunkStart + 2 * chunk, outEnd = outStart + chunk;
    const int hb = (int) (outStart % (2 * chunk)); // its half of the rings
    makeMarks (available);

    // Crowd knob, smoothed 30 ms
    if (crowdSmoothed < 0.0f)
        crowdSmoothed = crowdNow;
    crowdSmoothed += (crowdNow - crowdSmoothed) * (1.0f - std::exp (-(float) chunk / (0.03f * (float) sampleRate)));
    const float crowd = crowdSmoothed;
    const float count = audibleVoices (crowd, voicesMax);
    const float level = dbToGain (crowdDb (crowd)) / std::sqrt (juce::jmax (1.0f, count));
    const float w = spread * smoothstep (0.2f, 1.0f, crowd);
    const double h = juce::jmax (0.2, (double) humanize); // never fully quantised
    const double dt = (double) chunk / sampleRate;
    const float fadeStep = (float) chunk / (0.06f * (float) sampleRate); // a voice comes / goes in 60 ms
    const float semiCoeff = 1.0f - std::exp (-(float) chunk / (0.02f * (float) sampleRate));
    const auto& st = styleSpec (style);
    responseDelay = juce::jlimit (0.0, (double) maxResponse, responseBeats (response, beatsPerBar) * 60.0 / bpm * sampleRate);
    const float dropStep = (float) (1.0 / (0.06 * sampleRate));

    std::array<float, chunk> sendL {}, sendR {};
    std::fill (outL.begin() + hb, outL.begin() + hb + chunk, 0.0f);
    std::fill (outR.begin() + hb, outR.begin() + hb + chunk, 0.0f);

    for (int k = 0; k < maxVoices; ++k)
    {
        auto& v = voices[(size_t) k];
        float want = k < voicesMax ? juce::jlimit (0.0f, 1.0f, count - (float) k) : 0.0f;
        if (solo >= 0)
            want = k == solo && crowd > 0.0f ? 1.0f : 0.0f;
        if (part[(size_t) k] == partMuted)
            want = 0.0f; // its place in the crowd stays (the next voices do not move up)
        v.audible = want > v.audible ? juce::jmin (want, v.audible + fadeStep) : juce::jmax (want, v.audible - fadeStep);
        const bool active = k < voicesMax && crowd > 0.0f;
        if (! active && v.audible <= 0.0f)
        {
            v.running = false; // a voice that is not used costs nothing
            continue;
        }

        const auto info = voiceInfo (k);
        const auto styleRole = st.roles[(size_t) k];
        v.role = effectiveRole (k, styleRole);
        // its own tuning, slow wander and vibrato (which only comes once the
        // note is held, as singers do); the harmony interval comes with each grain
        // a sung vibrato is not a sine: its rate and depth drift a little
        v.vibPhase += juce::MathConstants<double>::twoPi * info.vibratoHz * (1.0 + 0.12 * h * std::sin (0.7 * v.ampPhase + (double) k)) * dt;
        for (int w2 = 0; w2 < 2; ++w2)
            v.wanderPhase[w2] += juce::MathConstants<double>::twoPi * v.wanderRate[w2] * dt;
        v.ampPhase += juce::MathConstants<double>::twoPi * v.ampRate * dt;
        const double wander = 9.0 * h * (double) st.detune * (0.6 * std::sin (v.wanderPhase[0]) + 0.4 * std::sin (v.wanderPhase[1]));
        const double vibOnset = smoothstep (0.12f, 0.5f, (float) v.noteAge);
        const double vibDepth = info.vibratoCents * (0.75 + 0.25 * std::sin (0.5 * v.wanderPhase[1] + (double) k));
        const float semis = (float) ((info.detuneCents + wander + vibOnset * vibDepth * std::sin (v.vibPhase)) * 0.01);
        if (! v.running)
        {
            std::fill (v.acc.begin(), v.acc.end(), 0.0f);
            v.nextGrain = (double) (outStart + halfMax); // the first grain starts whole (no step)
            v.hasPending = false;
            v.semis = semis;
            v.running = true;
        }
        v.semis += (semis - v.semis) * semiCoeff;
        // its voice size (formants): the style's, a little different for each
        const double alpha = (double) st.formant * (1.0 + (double) st.formantSpread * v.formantJitter) * (1.0 + (double) partSize[(size_t) k]);

        // its own delay, drifting slowly (±2 ms x humanize)
        v.driftPhase += juce::MathConstants<double>::twoPi * v.driftRate * dt;
        const double offset = (info.offsetMs + 2.0 * h * std::sin (v.driftPhase)) * 0.001 * sampleRate + responseDelay;

        // PSOLA: grains (two input periods, Hann) laid out one target period apart
        for (;;)
        {
            const double ts = v.nextGrain;
            // transposing down, grains longer than two periods so they still
            // overlap (two periods at an octave down only touch: the breath
            // in the voice would be chopped at half the pitch, a rasp)
            const double stretch = juce::jlimit (1.0, 2.0, 1.0 / v.lastRatio);
            const auto* m = v.hasPending ? &v.pending : findMark (ts - latency - offset, available, alpha * stretch);
            if (m == nullptr)
            {
                if (ts >= (double) outEnd)
                    break;
                v.nextGrain += 0.01 * sampleRate; // nothing sung yet
                continue;
            }
            const double period = m->period;
            const int half = juce::jmin ((int) (period * stretch), halfMax);
            if (ts - half >= (double) outEnd)
            {
                // not yet: keep this mark for it, so its start (decided now)
                // can never fall in the part already played
                v.pending = *m;
                v.hasPending = true;
                break;
            }
            v.hasPending = false;
            double ratio = 1.0; // unvoiced (consonants, breath): not transposed, just doubled
            if (m->midi >= 0.0f)
            {
                // the interval of its part for the note being sung, gliding
                // (~30 ms) from one note to the next, applied to the singer's
                // own pitch: the crowd keeps the singer's slides and vibrato
                // (no auto-tune), unless the style pulls it in tune
                const double base = snap ? (double) m->note : (double) m->midi;
                const double target = partTarget (base, k, styleRole) - base;
                const double pull = snap ? (double) st.tune * ((double) m->note - (double) m->midi) : 0.0; // not smoothed: it follows the vibrato
                if (m->noteId != v.lastNote)
                {
                    // a new note: some voices scoop into it from below
                    const bool firstNote = v.lastNote == 0;
                    v.lastNote = m->noteId;
                    v.noteAge = 0.0;
                    v.scoop = v.rng.nextDouble() < 0.6 ? -(12.0 + 38.0 * v.rng.nextDouble()) * h * (double) st.detune : 0.0;
                    v.scoopTau = 0.05 + 0.1 * v.rng.nextDouble();
                    if (firstNote)
                        v.interval = target;
                }
                v.interval += (target - v.interval) * (1.0 - std::exp (-period / (0.09 * sampleRate))); // a singer takes ~100 ms to move to the next harmony note
                ratio = std::exp2 ((v.interval + pull + (double) v.semis + 0.01 * v.scoop) / 12.0);
                v.scoop *= std::exp (-period / ratio / sampleRate / v.scoopTau);
            }
            v.noteAge += period / ratio / sampleRate;
            v.lastRatio = ratio;
            // level: one grain per output period (the harmonics keep the
            // input's envelope; power follows the pitch by sqrt)
            // a real voice is never exactly periodic: each voice's periods
            // vary a little from one to the next (jitter) and so does their
            // level (shimmer) — without it, the copies sound synthetic
            const double human = h * (double) st.detune;
            const double jitter = m->midi >= 0.0f ? (v.rng.nextDouble() * 2.0 - 1.0) * 0.008 * human : 0.0;
            const float shimmer = (float) (1.0 + (m->midi >= 0.0f ? (v.rng.nextDouble() * 2.0 - 1.0) * 0.1 * human : 0.0));
            const float norm = shimmer * (float) (period / half / std::sqrt (ratio));
            const auto p0 = juce::jmax (outStart, (std::int64_t) std::ceil (ts - half));
            const auto p1 = (std::int64_t) std::floor (ts + half);
            // Hann window by rotation (no cos per sample); the input read
            // walks by alpha (1: a fixed fraction, no floor per sample)
            const double step = juce::MathConstants<double>::pi / half, d0 = (double) p0 - ts;
            double wc = std::cos (d0 * step), ws = std::sin (d0 * step);
            const double rc = std::cos (step), rs = std::sin (step);
            double src = (double) m->pos + d0 * alpha;
            float* acc = v.acc.data();
            if (juce::exactlyEqual (alpha, 1.0))
            {
                auto i0 = (std::int64_t) std::floor (src);
                const float frac = (float) (src - (double) i0);
                for (auto p = p0; p <= p1; ++p, ++i0)
                {
                    const float a = history[(size_t) (i0 & mask)], b = history[(size_t) ((i0 + 1) & mask)];
                    acc[(size_t) (p & accMask)] += (float) (0.5 + 0.5 * wc) * norm * (a + frac * (b - a));
                    const double nc = wc * rc - ws * rs;
                    ws = ws * rc + wc * rs;
                    wc = nc;
                }
            }
            else
                for (auto p = p0; p <= p1; ++p, src += alpha)
                {
                    acc[(size_t) (p & accMask)] += (float) (0.5 + 0.5 * wc) * norm * historyAt (src);
                    const double nc = wc * rc - ws * rs;
                    ws = ws * rc + wc * rs;
                    wc = nc;
                }
            v.nextGrain += period / ratio * (1.0 + jitter);
        }

        // dropouts: now and then a voice dips or stops for 0.5-1.5 s
        if (solo < 0 && v.dropLeft <= 0 && dropouts > 0.0f && random.nextDouble() < (double) dropouts * 0.001 * dt * 1000.0)
        {
            v.dropLeft = (int) ((0.5 + random.nextDouble()) * sampleRate);
            v.dropTarget = random.nextBool() ? 0.0f : 0.3f;
        }
        if (v.dropLeft > 0 && (v.dropLeft -= chunk) <= 0)
            v.dropTarget = 1.0f;
        if (solo >= 0)
            v.dropTarget = 1.0f;

        // the voices an octave down are the most "machine" of all after the
        // transposition: they stay in the background (-4 dB)
        const float octaveDownGain = v.role == Harmony::octaveDown ? 0.63f : 1.0f;
        const float g = level * (float) info.gain * v.audible * octaveDownGain * partGain[(size_t) k] * (float) (1.0 + 0.12 * h * (double) st.detune * std::sin (v.ampPhase));
        const float panPos = (float) info.pan * w;
        const float send = (float) info.send;
        const float gl = std::cos ((panPos + 1.0f) * 0.25f * juce::MathConstants<float>::pi) * 1.41421356f;
        const float gr = std::sin ((panPos + 1.0f) * 0.25f * juce::MathConstants<float>::pi) * 1.41421356f;
        const auto gateDelay = (std::int64_t) std::llround (latency + offset) - gateLag;
        float sumSq = 0.0f;
        for (int j = 0; j < chunk; ++j)
        {
            const auto t = outStart + j;
            float& a = v.acc[(size_t) (t & accMask)];
            const float y = a;
            a = 0.0f;
            // its voicing, read where its own (late) copy of the input is
            // consonants and breaths are doubled at half level, not cut
            const float gt = 0.5f + 0.5f * gateHistory[(size_t) (juce::jmin (available, t - gateDelay) & mask)];
            v.drop += juce::jlimit (-dropStep, dropStep, v.dropTarget - v.drop);
            const float s = y * g * v.drop * gt;
            sumSq += (y * v.drop * gt) * (y * v.drop * gt);
            outL[(size_t) (hb + j)] += s * gl;
            outR[(size_t) (hb + j)] += s * gr;
            sendL[(size_t) j] += s * gl * send;
            sendR[(size_t) j] += s * gr * send;
        }
        v.level = juce::jmax (v.level, v.audible * std::sqrt (sumSq / (float) chunk));
    }

    // Crowd reverb (on the crowd only), per chunk as well
    reverb.processStereo (sendL.data(), sendR.data(), chunk);
    std::copy (sendL.begin(), sendL.end(), revL.begin() + hb);
    std::copy (sendR.begin(), sendR.end(), revR.begin() + hb);

    // Output ceiling (-1.5 dBFS): the peak of that chunk (the lead is the
    // input L samples earlier, already known), ramped down over the chunk
    // before it (not played yet), released over 150 ms
    constexpr float ceiling = 0.84f;
    const float rm = roomMix.getCurrent();
    float peak = 0.0f;
    for (int j = 0; j < chunk; ++j)
    {
        const float ld = leadGain < 0.0f ? 1.0f : leadGain;
        const float x = ld * history[(size_t) ((outStart + j - latency) & mask)];
        const float cm = crowdMuted ? 0.0f : 1.0f;
        const float a = std::abs (x + cm * (outL[(size_t) (hb + j)] + rm * revL[(size_t) (hb + j)]));
        const float b = std::abs (x + cm * (outR[(size_t) (hb + j)] + rm * revR[(size_t) (hb + j)]));
        peak = juce::jmax (peak, a, b);
    }
    const float target = peak > ceiling ? ceiling / peak : 1.0f;
    if (target < limLevel)
    {
        const int pb = (hb + chunk) % (2 * chunk); // the chunk before (next to be played)
        const float g0 = limGain[(size_t) pb];
        for (int j = 0; j < chunk; ++j)
            limGain[(size_t) (pb + j)] = juce::jmin (limGain[(size_t) (pb + j)], g0 + (target - g0) * (float) (j + 1) / (float) chunk);
        limLevel = target;
    }
    for (int j = 0; j < chunk; ++j)
    {
        limLevel = juce::jmin (target, limLevel + (1.0f - limLevel) * limiterRelease);
        if (limLevel > 0.99999f)
            limLevel = 1.0f;
        limGain[(size_t) (hb + j)] = limLevel;
    }
}
} // namespace sib
