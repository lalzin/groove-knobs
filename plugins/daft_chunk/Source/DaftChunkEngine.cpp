#include "DaftChunkEngine.h"

namespace daftchunk
{
void DaftChunkEngine::prepare (double sr, int, int)
{
    sampleRate = sr;
    cutFade = std::max (1, (int) std::lround (0.003 * sr));
    reset();
}

void DaftChunkEngine::reset()
{
    for (auto& v : voices)
        v = {};
    lead = -1;
    cutoffSmoothed = knobs[cutoffKnob];
    telemetry.slice.store (-1, std::memory_order_relaxed);
}

void DaftChunkEngine::setMidi (const juce::MidiBuffer& m, int start, int count)
{
    midi = &m;
    midiStart = start;
    midiCount = count;
}

void DaftChunkEngine::setKnobs (const float* values, int n)
{
    for (int i = 0; i < std::min (n, (int) numKnobs); ++i)
        knobs[(size_t) i] = values[i];
}

void DaftChunkEngine::setChoices (const int* indices, int n)
{
    if (n > modeChoice)
        mode = indices[modeChoice];
    if (n > playChoice)
        play = indices[playChoice];
    if (n > syncChoice)
        sync = indices[syncChoice];
}

void DaftChunkEngine::noteOn (int note, float velocity, int previewSamples)
{
    const int index = (previewSamples >= 0 ? note : note - baseNote);
    if (current == nullptr || index < 0 || index >= (int) current->slices.size())
        return;
    // COUPE: everything playing is cut; POLY: only the same key again
    for (auto& v : voices)
        if (v.active() && v.cutLeft < 0 && (mode == cut || (previewSamples < 0 && v.note == note)))
            v.cutLeft = cutFade;

    // a free voice, else the oldest
    Voice* slot = nullptr;
    for (auto& v : voices)
        if (! v.active())
        {
            slot = &v;
            break;
        }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& v : voices)
            if (v.age < slot->age)
                slot = &v;
    }
    auto& v = *slot;
    const auto& s = current->slices[(size_t) index];
    const SliceParams p = index < (int) current->params.size() ? current->params[(size_t) index] : SliceParams {};
    // in the audio played (the stretched one: positions x timeScale)
    const double scale = current->timeScale, audioLen = (double) current->audio().getNumSamples();
    const double s0 = (double) s.start * scale, s1 = std::min (audioLen, (double) s.end * scale), len = s1 - s0;
    v = {};
    v.slice = index;
    v.note = previewSamples >= 0 ? -1 : note;
    v.first = s0 + juce::jlimit (0.0, 0.9, (double) p.start) * len;
    v.last = s1 - juce::jlimit (0.0, 0.9, (double) p.end) * len;
    if (v.last - v.first < 64.0)
        v.last = std::min (s1, v.first + 64.0);
    v.reverse = p.reverse;
    v.pos = v.reverse ? v.last - 2.0 : v.first;
    const float sens = juce::jlimit (0.0f, 1.0f, knobs[velocityKnob]);
    v.gain = (1.0f - sens * (1.0f - juce::jlimit (0.0f, 1.0f, velocity))) * juce::Decibels::decibelsToGain (p.gainDb, -60.0f);
    v.env = 0.0f;
    v.stage = Stage::attack;
    v.held = true;
    v.previewLeft = previewSamples;
    v.age = ++ageCounter;
    lead = (int) (slot - voices.data());
}

void DaftChunkEngine::releaseVoice (Voice& v) noexcept
{
    if (v.active() && v.stage != Stage::release)
        v.stage = Stage::release;
}

void DaftChunkEngine::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active() && v.held && v.note == note)
        {
            v.held = false;
            if (play == gate)
                releaseVoice (v);
        }
}

void DaftChunkEngine::render (Voice& v, float* const* out, int numChannels, int from, int to)
{
    const auto& k = *current;
    const auto& audio = k.audio();
    const int srcChans = audio.getNumChannels();
    const SliceParams p = v.slice < (int) k.params.size() ? k.params[(size_t) v.slice] : SliceParams {};
    const double step = k.sample->sampleRate / sampleRate * rate * std::exp2 ((knobs[pitchKnob] + p.pitch) / 12.0);
    const double fade = 0.002 * k.sample->sampleRate; // the 2 ms fades, in source samples
    const double length = v.last - v.first;
    const float attackInc = (float) (1.0 / (std::max (0.0005, (double) knobs[attackKnob]) * sampleRate));
    const float decayCoef = (float) std::exp (-4.6 / (std::max (0.005, (double) knobs[decayKnob]) * sampleRate));
    const float releaseCoef = (float) std::exp (-4.6 / (std::max (0.003, (double) knobs[releaseKnob]) * sampleRate));
    const float sustain = juce::jlimit (0.0f, 1.0f, knobs[sustainKnob]);

    const double cutoff = juce::jlimit (20.0, 0.45 * sampleRate, cutoffSmoothed * std::exp2 ((double) p.filter));
    const bool filtered = cutoff < 19000.0 || knobs[resoKnob] > 0.01f;
    const auto coeffs = gk::TptSvf::Coeffs::make (cutoff, sampleRate, 0.707 + 8.0 * juce::jlimit (0.0f, 1.0f, knobs[resoKnob]));
    const float* src[2] { audio.getReadPointer (0), audio.getReadPointer (std::min (1, srcChans - 1)) };

    for (int i = from; i < to; ++i)
    {
        const double rel = v.reverse ? (v.last - 1.0 - v.pos) : (v.pos - v.first);
        if (rel >= length - 2.0 || v.pos < v.first)
        {
            v = {};
            return;
        }
        // the envelope
        switch (v.stage)
        {
            case Stage::attack:
                v.env += attackInc;
                if (v.env >= 1.0f)
                {
                    v.env = 1.0f;
                    v.stage = Stage::decay;
                }
                break;
            case Stage::decay:
                v.env = sustain + (v.env - sustain) * decayCoef;
                if (std::abs (v.env - sustain) < 1.0e-4f)
                {
                    v.env = sustain;
                    v.stage = Stage::sustain;
                }
                break;
            case Stage::sustain:
                v.env = sustain;
                break;
            case Stage::release:
                v.env *= releaseCoef;
                if (v.env < 1.0e-4f)
                {
                    v = {};
                    return;
                }
                break;
            case Stage::off:
                return;
        }
        if (v.previewLeft > 0 && --v.previewLeft == 0 && play == gate)
            releaseVoice (v);

        float g = v.gain * v.env;
        if (rel < fade)
            g *= (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * rel / fade));
        if (length - rel < fade)
            g *= (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (length - rel) / fade));
        if (v.cutLeft >= 0)
        {
            if (v.cutLeft == 0)
            {
                v = {};
                return;
            }
            g *= (float) v.cutLeft-- / (float) cutFade;
        }
        const auto k0 = (int64_t) v.pos;
        const float f = (float) (v.pos - (double) k0);
        for (int c = 0; c < numChannels; ++c)
        {
            const float* x = src[std::min (c, 1)];
            double y = x[k0] + f * (x[k0 + 1] - x[k0]);
            if (filtered)
                y = v.filter[c].processLowpass (y, coeffs);
            out[c][i] += g * (float) y;
        }
        v.pos += v.reverse ? -step : step;
    }
}

void DaftChunkEngine::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float*, const gk::BlockTiming& timing)
{
    buffer.clear (0, numSamples);
    telemetry.hostBpm.store (hostTempoKnown ? timing.bpm : 0.0, std::memory_order_relaxed);
    const auto* k = kit.load (std::memory_order_acquire);
    if (k != current)
    {
        // new settings for the same slices: what plays goes on; another sample or other cuts: it stops
        const bool sameSlices = k != nullptr && current != nullptr && k->sample == current->sample && k->slices.size() == current->slices.size()
                                && k->stretched == current->stretched;
        if (! sameSlices)
        {
            for (auto& v : voices)
                v = {};
            lead = -1;
        }
        current = k;
    }
    inUse.store (k, std::memory_order_release);
    if (current == nullptr || current->sample == nullptr || current->slices.empty())
    {
        telemetry.slice.store (-1, std::memory_order_relaxed);
        return;
    }
    // TEMPO / VINYLE: the speed that puts the kit's tempo on the host's (TEMPO: ~1 once stretched)
    rate = hostTempoKnown && sync != free && timing.bpm > 0.0 && current->playBpm > 0.0 ? juce::jlimit (0.25, 4.0, timing.bpm / current->playBpm) : 1.0;

    // FILTER glides (log), over ~20 ms
    {
        const double target = juce::jlimit (20.0, 20000.0, (double) knobs[cutoffKnob]);
        const double a = std::min (1.0, numSamples / (0.02 * sampleRate));
        cutoffSmoothed = std::exp (std::log (cutoffSmoothed) + a * (std::log (target) - std::log (cutoffSmoothed)));
    }
    auto* const* out = buffer.getArrayOfWritePointers();
    const int chans = std::min (buffer.getNumChannels(), 2);

    if (const int p = previewRequest.exchange (-1, std::memory_order_relaxed); p >= 0)
        noteOn (p, 0.85f, (int) (0.35 * sampleRate));

    int pos = 0;
    auto renderTo = [&] (int to)
    {
        if (to > pos)
            for (auto& v : voices)
                if (v.active())
                    render (v, out, chans, pos, to);
        pos = std::max (pos, to);
    };
    if (midi != nullptr)
        for (const auto meta : *midi)
        {
            const int t = meta.samplePosition - midiStart;
            if (t < 0 || t >= midiCount)
                continue;
            const auto msg = meta.getMessage();
            renderTo (std::min (t, numSamples));
            if (msg.isNoteOn())
                noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
                noteOff (msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                for (auto& v : voices)
                    if (v.active() && v.cutLeft < 0)
                        v.cutLeft = cutFade;
        }
    renderTo (numSamples);

    if (lead >= 0 && voices[(size_t) lead].active())
    {
        const auto& v = voices[(size_t) lead];
        telemetry.slice.store (v.slice, std::memory_order_relaxed);
        const double rel = v.reverse ? (v.last - 1.0 - v.pos) : (v.pos - v.first);
        // the playhead in the slice's own coordinates (fine start / end and reverse included)
        const auto& s = current->slices[(size_t) v.slice];
        const double at = (v.reverse ? v.last - rel : v.first + rel) / current->timeScale;
        telemetry.position.store ((float) ((at - (double) s.start) / (double) std::max<int64_t> (1, s.end - s.start)), std::memory_order_relaxed);
    }
    else
        telemetry.slice.store (-1, std::memory_order_relaxed);
}
} // namespace daftchunk
