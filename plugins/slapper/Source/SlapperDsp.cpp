#include "SlapperDsp.h"

namespace slapper
{
namespace
{
double noteHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }
} // namespace

void SlapperDsp::prepare (double sampleRate, int, int)
{
    sr = sampleRate;
    for (int i = 0; i < numVoices; ++i)
    {
        voices[(size_t) i].prepare (sr);
        voiceSeed[(size_t) i] = 0x9e3779b9u * (std::uint32_t) (i + 1);
    }
    amp.prepare (sr);
    driveSmooth.prepare (sr, 0.020);
    toneSmooth.prepare (sr, 0.020);
    volume.prepare (sr, 0.020);
    reset();
}

void SlapperDsp::reset()
{
    for (auto& v : voices)
        v.reset();
    voiceNote.fill (-1);
    for (auto& e : pending)
        e.used = false;
    numHeld = 0;
    bendRatio = 1.0;
    amp.reset();
    driveSmooth.snap (std::clamp (knobs[driveKnob] + funk::driveAdd (funk), 0.0f, 1.0f));
    toneSmooth.snap (std::clamp (knobs[toneKnob] + funk::toneAdd (funk), -1.0f, 1.0f));
    volume.snap (gk::dspmath::dbToGain (knobs[levelKnob]));
    envelope = 0.0f;
}

void SlapperDsp::setMidi (const juce::MidiBuffer& m, int start, int count)
{
    midi = &m;
    midiStart = start;
    midiCount = count;
}

void SlapperDsp::setKnobs (const float* values, int n)
{
    for (int i = 0; i < std::min (n, (int) numKnobs); ++i)
        knobs[i] = values[i];
}

float SlapperDsp::humanRandom (int voice) noexcept
{
    auto& s = voiceSeed[(size_t) voice];
    s = s * 1664525u + 1013904223u;
    return (float) (s >> 8) / 8388608.0f - 1.0f;
}

NoteParams SlapperDsp::paramsFor (int voice, float velocity) noexcept
{
    juce::ignoreUnused (velocity);
    const float human = std::clamp (knobs[humanKnob], 0.0f, 1.0f);
    NoteParams p;
    p.attack = knobs[attackKnob];
    p.pick = knobs[pickKnob] * (1.0f + 0.03f * human * humanRandom (voice));
    p.string = knobs[stringKnob];
    p.body = knobs[bodyKnob];
    p.mute = knobs[muteKnob];
    p.hardness = funk::hardness (funk);
    p.clank = funk::clank (funk);
    p.rebound = funk::rebound (funk);
    p.sub = funk::sub (funk);
    return p;
}

void SlapperDsp::schedule (int note, float velocity, double delaySeconds)
{
    for (auto& e : pending)
        if (! e.used)
        {
            e = { clock + (std::int64_t) std::llround (delaySeconds * sr), note, velocity, true };
            return;
        }
    // (64 events in 8 ms: play it now rather than lose it)
    velocity > 0.0f ? play (note, velocity) : release (note);
}

void SlapperDsp::handle (const juce::MidiMessage& m, float)
{
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        const float human = std::clamp (knobs[humanKnob], 0.0f, 1.0f);
        // HUMAIN: a little late, never early (no latency)
        timingSeed = timingSeed * 1664525u + 1013904223u;
        const double delay = human * maxHumanSeconds * (double) (timingSeed >> 8) / 16777216.0;
        noteDelay[(size_t) note] = delay;
        if (delay > 0.0)
            schedule (note, m.getFloatVelocity(), delay);
        else
            play (note, m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        if (noteDelay[(size_t) note] > 0.0)
            schedule (note, 0.0f, noteDelay[(size_t) note]);
        else
            release (note);
    }
    else if (m.isPitchWheel())
    {
        // ±2 semitones
        bendRatio = std::pow (2.0, (m.getPitchWheelValue() - 8192) / 8192.0 * 2.0 / 12.0);
        for (auto& v : voices)
            v.setBend (bendRatio);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices)
            v.noteOff();
        numHeld = 0;
        for (auto& e : pending)
            e.used = false;
    }
}

void SlapperDsp::play (int note, float velocity)
{
    const double hz = noteHz (note);
    ++order;
    if (! poly)
    {
        auto& v = voices[0];
        // HUMAIN: the velocity ±6 %
        const float vel = std::clamp (velocity * (1.0f + 0.06f * knobs[humanKnob] * humanRandom (0)), 0.01f, 1.0f);
        const bool legato = numHeld > 0 && v.isHeld() && velocity >= Voice::ghostVelocity;
        // the held keys: this one on top (moved there if already held)
        for (int i = 0; i < numHeld; ++i)
            if (heldKeys[(size_t) i] == note)
            {
                for (int j = i; j + 1 < numHeld; ++j)
                    heldKeys[(size_t) j] = heldKeys[(size_t) j + 1];
                --numHeld;
                break;
            }
        if (numHeld < (int) heldKeys.size())
            heldKeys[(size_t) numHeld++] = note;
        if (legato)
            v.glideTo (hz, glideSeconds (12.0 * std::log2 (hz / v.currentHz())));
        else
            v.noteOn (hz, vel, paramsFor (0, vel));
        voiceNote[0] = note;
    }
    else
    {
        // poly: the voice already on this note, else a silent one, else a
        // released one, else the oldest
        int best = -1;
        for (int i = 0; i < numVoices && best < 0; ++i)
            if (voiceNote[(size_t) i] == note && voices[(size_t) i].isActive())
                best = i;
        for (int i = 0; i < numVoices && best < 0; ++i)
            if (! voices[(size_t) i].isActive())
                best = i;
        if (best < 0)
        {
            std::int64_t oldest = std::numeric_limits<std::int64_t>::max();
            for (int pass = 0; pass < 2 && best < 0; ++pass)
                for (int i = 0; i < numVoices; ++i)
                    if ((pass == 1 || ! voices[(size_t) i].isHeld()) && voiceOrder[(size_t) i] < oldest)
                    {
                        oldest = voiceOrder[(size_t) i];
                        best = i;
                    }
        }
        const float vel = std::clamp (velocity * (1.0f + 0.06f * knobs[humanKnob] * humanRandom (best)), 0.01f, 1.0f);
        voices[(size_t) best].noteOn (hz, vel, paramsFor (best, vel));
        voiceNote[(size_t) best] = note;
        voiceOrder[(size_t) best] = order;
    }
    // the UI: which attack it was
    const float a = knobs[attackKnob];
    telemetry.lastKind.store (velocity < Voice::ghostVelocity ? 3 : a < 0.25f ? 0 : a < 0.75f ? 1 : 2);
    telemetry.lastVelocity.store (velocity);
    telemetry.hitCounter.fetch_add (1);
}

void SlapperDsp::release (int note)
{
    if (! poly)
    {
        int at = -1;
        for (int i = 0; i < numHeld; ++i)
            if (heldKeys[(size_t) i] == note)
                at = i;
        if (at < 0)
            return;
        const bool wasTop = at == numHeld - 1;
        for (int j = at; j + 1 < numHeld; ++j)
            heldKeys[(size_t) j] = heldKeys[(size_t) j + 1];
        --numHeld;
        if (! wasTop)
            return;
        auto& v = voices[0];
        if (numHeld > 0 && v.isHeld())
        {
            // back to the key still held (a trill, a hammer-on / pull-off)
            const int back = heldKeys[(size_t) numHeld - 1];
            const double hz = noteHz (back);
            v.glideTo (hz, glideSeconds (12.0 * std::log2 (hz / v.currentHz())));
            voiceNote[0] = back;
        }
        else
            v.noteOff();
    }
    else
    {
        for (int i = 0; i < numVoices; ++i)
            if (voiceNote[(size_t) i] == note && voices[(size_t) i].isHeld())
                voices[(size_t) i].noteOff();
    }
}

void SlapperDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming&)
{
    juce::ScopedNoDenormals noDenormals;
    funk = amount != nullptr ? std::clamp (amount[0], 0.0f, 1.0f) : funk;
    driveSmooth.setTarget (std::clamp (knobs[driveKnob] + funk::driveAdd (funk), 0.0f, 1.0f));
    toneSmooth.setTarget (std::clamp (knobs[toneKnob] + funk::toneAdd (funk), -1.0f, 1.0f));
    volume.setTarget (gk::dspmath::dbToGain (knobs[levelKnob]));

    // MIDI of this chunk, in time order, interleaved with the samples
    auto it = midi != nullptr ? midi->findNextSamplePosition (midiStart) : juce::MidiBufferIterator();
    const auto end = midi != nullptr ? midi->cend() : juce::MidiBufferIterator();

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    int active = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        if (amount != nullptr)
            funk = amount[i];
        while (midi != nullptr && it != end)
        {
            const auto meta = *it;
            const int at = meta.samplePosition - midiStart;
            if (at > i || at >= midiCount)
                break;
            handle (meta.getMessage(), funk);
            ++it;
        }
        for (auto& e : pending)
            if (e.used && e.due <= clock)
            {
                e.used = false;
                e.velocity > 0.0f ? play (e.note, e.velocity) : release (e.note);
            }
        float y = 0.0f;
        for (auto& v : voices)
            if (v.isActive())
                y += v.process();
        // (the amp's filters redesigned every 32 samples while TIMBRE moves, not every sample)
        const float d = driveSmooth.next(), t = toneSmooth.next();
        if ((clock & 31) == 0)
        {
            amp.drive = d;
            amp.tone = t;
        }
        const float out = amp.process (1.5f * y) * volume.next();
        left[i] = out;
        if (right != nullptr)
            right[i] = out;
        envelope = std::max (std::fabs (out), envelope * 0.9995f);
        ++clock;
    }
    for (auto& v : voices)
        active += v.isActive() ? 1 : 0;
    telemetry.activeVoices.store (active);
    telemetry.level.store (envelope);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
}
} // namespace slapper
