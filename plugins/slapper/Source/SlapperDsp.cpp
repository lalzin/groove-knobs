#include "SlapperDsp.h"

namespace slapper
{
namespace
{
double noteHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }
} // namespace

int SlapperDsp::engineIndexOf (const juce::var& json)
{
    const auto e = json.getProperty ("engine", "string").toString();
    return (int) (e == "analog" ? Engine::analog : e == "fm" ? Engine::fm : e == "pulse" ? Engine::pulse : Engine::string);
}

void SlapperDsp::setPresets (const gk::PresetManager& presets)
{
    trims.clear();
    for (int i = 0; i < presets.size(); ++i)
        trims.push_back ((float) presets[i].json.getProperty ("trimDb", 0.0));
}

void SlapperDsp::prepare (double sampleRate, int, int)
{
    sr = sampleRate;
    for (int i = 0; i < numVoices; ++i)
    {
        voices[(size_t) i].prepare (sr);
        synths[(size_t) i].prepare (sr);
        voiceSeed[(size_t) i] = 0x9e3779b9u * (std::uint32_t) (i + 1);
    }
    amp.prepare (sr);
    // (short: FUNK turned fast must be heard at once — Alan: "the effect stays a moment")
    driveSmooth.prepare (sr, 0.010);
    toneSmooth.prepare (sr, 0.010);
    volume.prepare (sr, 0.020);
    reset();
}

void SlapperDsp::reset()
{
    for (auto& v : voices)
        v.reset();
    for (auto& v : synths)
        v.reset();
    voiceNote.fill (-1);
    pressed.fill (0);
    for (auto& e : pending)
        e.used = false;
    numHeld = 0;
    bendRatio = 1.0;
    amp.reset();
    driveSmooth.snap (std::clamp (knobs[driveKnob] + funk::driveAdd (funk), 0.0f, 1.0f));
    toneSmooth.snap (std::clamp (knobs[toneKnob] + funk::toneAdd (funk), -1.0f, 1.0f));
    volume.snap (knobs[levelKnob] <= -59.9f ? 0.0f : gk::dspmath::dbToGain (knobs[levelKnob] + trimDb));
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

SynthParams SlapperDsp::synthParamsFor (int voice) noexcept
{
    const float human = std::clamp (knobs[humanKnob], 0.0f, 1.0f);
    SynthParams p;
    p.engine = engine;
    p.attack = knobs[attackKnob];
    p.pick = knobs[pickKnob] * (1.0f + 0.03f * human * humanRandom (voice));
    p.string = knobs[stringKnob];
    p.body = knobs[bodyKnob];
    p.mute = knobs[muteKnob];
    p.funk = funk;
    return p;
}

void SlapperDsp::start (int slot, double hz, float velocity)
{
    // the other kind of voice in this slot, if it still sounds, is let go
    if (engine == Engine::string)
    {
        if (synths[(size_t) slot].isActive())
            synths[(size_t) slot].noteOff();
        voices[(size_t) slot].noteOn (hz, velocity, paramsFor (slot, velocity));
    }
    else
    {
        if (voices[(size_t) slot].isActive())
            voices[(size_t) slot].noteOff();
        synths[(size_t) slot].noteOn (hz, velocity, synthParamsFor (slot));
    }
    slotEngine[(size_t) slot] = engine;
}

void SlapperDsp::slide (int slot, double hz)
{
    const double semis = 12.0 * std::log2 (hz / slotHz (slot));
    // SLIDE: from quick (a hammer-on) to a slow portamento
    const double seconds = glideSeconds (semis) * (0.4 + 2.0 * std::clamp ((double) knobs[slideKnob], 0.0, 1.0));
    if (slotEngine[(size_t) slot] == Engine::string)
        voices[(size_t) slot].glideTo (hz, seconds);
    else
        synths[(size_t) slot].glideTo (hz, seconds);
}

void SlapperDsp::letGo (int slot)
{
    voices[(size_t) slot].noteOff();
    synths[(size_t) slot].noteOff();
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
        for (auto& v : synths)
            v.setBend (bendRatio);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (int i = 0; i < numVoices; ++i)
            letGo (i);
        numHeld = 0;
        pressed.fill (0);
        for (auto& e : pending)
            e.used = false;
    }
}

void SlapperDsp::play (int note, float velocity)
{
    const double hz = noteHz (note);
    ++order;
    const bool again = pressed[(size_t) note] > 0; // (the same key, still held from before)
    ++pressed[(size_t) note];
    // GHOST: under its velocity (0 .. 0.3, 0.15 by default) a note is a ghost; the
    // voices take ghosts as "under 0.15", so the velocity is brought to their scale
    const float ghostUnder = 0.3f * std::clamp (knobs[ghostKnob], 0.0f, 1.0f);
    const bool ghostNote = velocity < ghostUnder;
    velocity = ghostNote ? 0.01f : std::max (velocity, Voice::ghostVelocity);
    if (! poly)
    {
        // HUMAIN: the velocity ±6 %
        const float vel = std::clamp (velocity * (1.0f + 0.06f * knobs[humanKnob] * humanRandom (0)), 0.01f, 1.0f);
        // (a slide stays on the engine that is sounding; a new preset's engine starts with the next pluck)
        const bool legato = knobs[slideKnob] > 0.001f && numHeld > 0 && slotHeld (0) && ! ghostNote && ! again && slotEngine[0] == engine;
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
            slide (0, hz);
        else
            start (0, hz, vel);
        voiceNote[0] = note;
    }
    else
    {
        // poly: the voice already on this note, else a silent one, else a
        // released one, else the oldest
        int best = -1;
        for (int i = 0; i < numVoices && best < 0; ++i)
            if (voiceNote[(size_t) i] == note && slotActive (i))
                best = i;
        for (int i = 0; i < numVoices && best < 0; ++i)
            if (! slotActive (i))
                best = i;
        if (best < 0)
        {
            std::int64_t oldest = std::numeric_limits<std::int64_t>::max();
            for (int pass = 0; pass < 2 && best < 0; ++pass)
                for (int i = 0; i < numVoices; ++i)
                    if ((pass == 1 || ! slotHeld (i)) && voiceOrder[(size_t) i] < oldest)
                    {
                        oldest = voiceOrder[(size_t) i];
                        best = i;
                    }
        }
        const float vel = std::clamp (velocity * (1.0f + 0.06f * knobs[humanKnob] * humanRandom (best)), 0.01f, 1.0f);
        start (best, hz, vel);
        voiceNote[(size_t) best] = note;
        voiceOrder[(size_t) best] = order;
    }
    // the UI: which attack it was
    const float a = knobs[attackKnob];
    telemetry.lastKind.store (ghostNote ? 3 : a < 0.25f ? 0 : a < 0.75f ? 1 : 2);
    telemetry.lastVelocity.store (velocity);
    telemetry.hitCounter.fetch_add (1);
}

void SlapperDsp::release (int note)
{
    // a key pressed twice (MIDI overlaps): only its last release lets it go
    if (pressed[(size_t) note] > 0 && --pressed[(size_t) note] > 0)
        return;
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
        if (numHeld > 0 && slotHeld (0))
        {
            // back to the key still held (a trill, a hammer-on / pull-off)
            const int back = heldKeys[(size_t) numHeld - 1];
            slide (0, noteHz (back));
            voiceNote[0] = back;
        }
        else
            letGo (0);
    }
    else
    {
        for (int i = 0; i < numVoices; ++i)
            if (voiceNote[(size_t) i] == note && slotHeld (i))
                letGo (i);
    }
}

void SlapperDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming&)
{
    juce::ScopedNoDenormals noDenormals;
    funk = amount != nullptr ? std::clamp (amount[0], 0.0f, 1.0f) : funk;
    driveSmooth.setTarget (std::clamp (knobs[driveKnob] + funk::driveAdd (funk), 0.0f, 1.0f));
    toneSmooth.setTarget (std::clamp (knobs[toneKnob] + funk::toneAdd (funk), -1.0f, 1.0f));
    volume.setTarget (knobs[levelKnob] <= -59.9f ? 0.0f : gk::dspmath::dbToGain (knobs[levelKnob] + trimDb)); // (VOL: -inf at the bottom)

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
        // FUNK on the notes already ringing, and on the amp, every 32 samples
        if ((clock & 31) == 0)
        {
            driveSmooth.setTarget (std::clamp (knobs[driveKnob] + funk::driveAdd (funk), 0.0f, 1.0f));
            toneSmooth.setTarget (std::clamp (knobs[toneKnob] + funk::toneAdd (funk), -1.0f, 1.0f));
            for (int v = 0; v < numVoices; ++v)
            {
                voices[(size_t) v].setFunkLive (funk::sub (funk), funk::clank (funk));
                synths[(size_t) v].setFunk (funk);
            }
        }
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
        for (int v = 0; v < numVoices; ++v)
        {
            if (voices[(size_t) v].isActive())
                y += voices[(size_t) v].process();
            if (synths[(size_t) v].isActive())
                y += synths[(size_t) v].process();
        }
        // (both move every sample: the amp is built for it)
        amp.drive = driveSmooth.next();
        amp.tone = toneSmooth.next();
        // (the amp's soft limiter tops out near 0.95: -6 dB after it, VOL +6 dB stays under the safety clip)
        const float out = 0.5f * amp.process (1.5f * y) * volume.next();
        left[i] = out;
        if (right != nullptr)
            right[i] = out;
        envelope = std::max (std::fabs (out), envelope * 0.9995f);
        ++clock;
    }
    for (int v = 0; v < numVoices; ++v)
        active += slotActive (v) ? 1 : 0;
    telemetry.activeVoices.store (active);
    telemetry.level.store (envelope);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
}
} // namespace slapper
