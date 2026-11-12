#include "DaftChunkEngine.h"

namespace daftchunk
{
void DaftChunkEngine::prepare (double sr, int maxBlockSize, int)
{
    sampleRate = sr;
    dryCopy.setSize (2, std::max (1, maxBlockSize));
    cutFade = std::max (1, (int) std::lround (0.003 * sr));
    reset();
}

void DaftChunkEngine::reset()
{
    for (auto& v : voices)
        v = {};
    lead = -1;
    filterSmoothed = knobs[filterKnob];
    for (auto& f : sweepFilter)
        f.reset();
    crushHold[0] = crushHold[1] = 0.0f;
    crushCount = 0;
    pumpSmoothed = knobs[pumpKnob];
    crushSmoothed = knobs[crushKnob];
    mixSmoothed = knobs[mixKnob];
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
    if (n > captureChoice)
        captureOn = indices[captureChoice] == 1;
    if (n > sweepChoice)
        sweepMode = indices[sweepChoice];
}

void DaftChunkEngine::noteOn (int note, float velocity, int previewSamples)
{
    // a key (slice = note - C1), or a preview (`note` is the slice)
    if (previewSamples >= 0)
        startSlice (note, velocity, -1, previewSamples, 0.0f, false);
    else
        startSlice (note - baseNote, velocity, note, -1, 0.0f, false);
}

void DaftChunkEngine::cutAll() noexcept
{
    for (auto& v : voices)
        if (v.active() && v.cutLeft < 0)
            v.cutLeft = cutFade;
}

void DaftChunkEngine::startSlice (int index, float velocity, int note, int gateSamples, float extraPitch, bool flipReverse)
{
    if (current == nullptr || index < 0 || index >= (int) current->slices.size())
        return;
    // COUPE: everything playing is cut; POLY: only the same key again
    for (auto& v : voices)
        if (v.active() && v.cutLeft < 0 && (mode == cut || (note >= 0 && v.note == note)))
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
    v.note = note;
    v.first = s0 + juce::jlimit (0.0, 0.9, (double) p.start) * len;
    v.last = s1 - juce::jlimit (0.0, 0.9, (double) p.end) * len;
    if (v.last - v.first < 64.0)
        v.last = std::min (s1, v.first + 64.0);
    v.reverse = p.reverse != flipReverse;
    v.pos = v.reverse ? v.last - 2.0 : v.first;
    const float sens = juce::jlimit (0.0f, 1.0f, knobs[velocityKnob]);
    v.gain = (1.0f - sens * (1.0f - juce::jlimit (0.0f, 1.0f, velocity))) * juce::Decibels::decibelsToGain (p.gainDb, -60.0f);
    v.env = 0.0f;
    v.stage = Stage::attack;
    v.held = note >= 0;
    v.previewLeft = gateSamples;
    v.extraPitch = extraPitch;
    v.age = ++ageCounter;
    lead = (int) (slot - voices.data());
    telemetry.triggers.fetch_add (1, std::memory_order_relaxed);
}

void DaftChunkEngine::collectSequence (int numSamples, const gk::BlockTiming& timing)
{
    numSeqEvents = 0;
    const auto* seq = sequence.load (std::memory_order_acquire);
    seqInUse.store (seq, std::memory_order_release);
    if (seq == nullptr || seq->steps.empty() || timing.beatsPerSample <= 0.0)
    {
        telemetry.step.store (-1, std::memory_order_relaxed);
        return;
    }
    // 16ths locked to the host's beats; the odd ones swung (SWING 75 %: 3/8 of a 16th late)
    const int n = seq->numSteps();
    const double b0 = timing.beatsAt (0), b1 = timing.beatsAt (numSamples);
    const double swingDelay = juce::jlimit (0.0f, 0.75f, knobs[swingKnob]) * 0.125;
    const auto first = (long long) std::floor (b0 * 4.0) - 1, last = (long long) std::floor (b1 * 4.0);
    for (long long k = first; k <= last; ++k)
    {
        const auto& st = seq->steps[(size_t) (((k % n) + n) % n)];
        const int reps = std::max (1, st.repeat);
        const double stepStart = (double) k * 0.25 + ((k & 1) != 0 ? swingDelay : 0.0);
        for (int j = 0; j < reps && numSeqEvents < (int) seqEvents.size(); ++j)
        {
            const double t = stepStart + j * 0.25 / reps;
            if (t < b0 || t >= b1 || (st.slice < 0 && ! st.cut))
                continue;
            // a slice holds until the idea's next event (the empty steps after it), like the sample itself
            int run = 1;
            if (j == reps - 1)
                while (run < n)
                {
                    const auto& nx = seq->steps[(size_t) (((k + run) % n + n) % n)];
                    if (nx.slice >= 0 || nx.cut)
                        break;
                    ++run;
                }
            const double length = j < reps - 1 ? 0.25 / reps : 0.25 / reps + (run - 1) * 0.25;
            seqEvents[(size_t) numSeqEvents++] = { juce::jlimit (0, numSamples - 1, (int) ((t - b0) / timing.beatsPerSample)), &st,
                                                   std::max (1, (int) (length / timing.beatsPerSample)) };
        }
    }
    // (k, then j: already in time order, but swing can put an odd step after the next even one's start: sort)
    std::sort (seqEvents.begin(), seqEvents.begin() + numSeqEvents, [] (const SeqEvent& x, const SeqEvent& y) { return x.at < y.at; });
    telemetry.step.store ((int) ((((long long) std::floor (b1 * 4.0)) % n + n) % n), std::memory_order_relaxed);
}

void DaftChunkEngine::emit (int sample, int note, float velocity, bool on) noexcept
{
    if (numOut < (int) outEvents.size())
        outEvents[(size_t) numOut++] = { sample, note, velocity, on };
}

void DaftChunkEngine::captureNote (int at, int slice, float velocity, int gate) noexcept
{
    const int note = juce::jlimit (0, 127, baseNote + slice);
    // the same key still held: off first
    for (auto& o : pendingOffs)
        if (o.note == note)
        {
            emit (midiStart + at, note, 0.0f, false);
            o.note = -1;
        }
    emit (midiStart + at, note, velocity, true);
    for (auto& o : pendingOffs)
        if (o.note < 0)
        {
            o.note = note;
            o.left = at + std::max (1, gate - 1); // from the chunk's start
            return;
        }
    emit (midiStart + at, note, 0.0f, false); // (no room: a very short note)
}

void DaftChunkEngine::advanceCapture (int numSamples) noexcept
{
    for (auto& o : pendingOffs)
        if (o.note >= 0)
        {
            if (o.left < numSamples)
            {
                emit (midiStart + o.left, o.note, 0.0f, false);
                o.note = -1;
            }
            else
                o.left -= numSamples;
        }
}

void DaftChunkEngine::processEffects (juce::AudioBuffer<float>& buffer, int numSamples, const gk::BlockTiming& timing)
{
    const int chans = std::min (buffer.getNumChannels(), 2);
    const double a = std::min (1.0, numSamples / (0.03 * sampleRate));
    pumpSmoothed += a * (juce::jlimit (0.0, 1.0, (double) knobs[pumpKnob]) - pumpSmoothed);
    crushSmoothed += a * (juce::jlimit (0.0, 1.0, (double) knobs[crushKnob]) - crushSmoothed);
    mixSmoothed += a * (juce::jlimit (0.0, 1.0, (double) knobs[mixKnob]) - mixSmoothed);

    // SWEEP: how many bars (IDEA: the playing loop's own, its style's)
    int sweepBars = 0;
    if (sweepMode == sweepIdea)
    {
        const auto* seq = sequence.load (std::memory_order_relaxed);
        sweepBars = seq != nullptr ? seq->filterBars : 0;
    }
    else if (sweepMode == sweep4 || sweepMode == sweep8 || sweepMode == sweep16)
        sweepBars = sweepMode == sweep4 ? 4 : sweepMode == sweep8 ? 8 : 16;
    const bool sweeping = sweepBars > 0 && timing.beatsPerSample > 0.0;
    const bool pumping = pumpSmoothed > 0.002 && timing.beatsPerSample > 0.0;
    const bool crushing = crushSmoothed > 0.005;
    telemetry.sweep.store (sweeping ? (float) std::fmod (timing.beatsAt (numSamples) / (sweepBars * timing.beatsPerBar), 1.0) : -1.0f, std::memory_order_relaxed);
    if (! sweeping && ! pumping && ! crushing)
    {
        telemetry.pump.store (1.0f, std::memory_order_relaxed);
        return;
    }
    const bool mixing = mixSmoothed < 0.999 && numSamples <= dryCopy.getNumSamples();
    if (mixing)
        for (int c = 0; c < chans; ++c)
            dryCopy.copyFrom (c, 0, buffer, c, 0, std::min (numSamples, dryCopy.getNumSamples()));

    auto* const* x = buffer.getArrayOfWritePointers();
    gk::TptSvf::Coeffs sc;
    const double bitsScale = std::exp2 (16.0 - 10.0 * crushSmoothed); // 16 bits -> 6 bits
    const int hold = 1 + (int) (crushSmoothed * 7.0);                   // up to 8x lower rate
    float pumpGain = 1.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const double beats = timing.beatsAt (i);
        if (sweeping && (i & 31) == 0)
        {
            // closed (300 Hz) at the start of the N bars, open (18 kHz) at their end: the French touch filter
            const double phase = std::fmod (beats / (sweepBars * timing.beatsPerBar), 1.0);
            sc = gk::TptSvf::Coeffs::make (std::min (0.45 * sampleRate, 300.0 * std::pow (60.0, std::pow (phase, 1.3))), sampleRate, 0.9);
        }
        if (pumping)
        {
            // ducked on every beat, back by the next one (a sidechain to an imaginary kick)
            const double ph = beats - std::floor (beats);
            pumpGain = (float) (1.0 - 0.85 * pumpSmoothed * std::pow (1.0 - std::min (1.0, ph / 0.75), 2.2) * std::min (1.0, ph / 0.01));
        }
        const bool takeCrush = crushing && ++crushCount >= hold;
        if (takeCrush)
            crushCount = 0;
        for (int c = 0; c < chans; ++c)
        {
            float y = x[c][i];
            if (sweeping)
                y = (float) sweepFilter[c].processLowpass (y, sc);
            if (crushing)
            {
                if (takeCrush)
                    crushHold[c] = (float) (std::round (y * bitsScale) / bitsScale);
                y = crushHold[c];
            }
            y *= pumpGain;
            x[c][i] = mixing ? (float) (mixSmoothed * y + (1.0 - mixSmoothed) * dryCopy.getSample (c, i)) : y;
        }
    }
    telemetry.pump.store (pumpGain, std::memory_order_relaxed);
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
    const double step = k.sample->sampleRate / sampleRate * rate * std::exp2 ((knobs[pitchKnob] + p.pitch + v.extraPitch) / 12.0);
    const double fade = 0.002 * k.sample->sampleRate; // the 2 ms fades, in source samples
    const double length = v.last - v.first;
    const float attackInc = (float) (1.0 / (std::max (0.0005, (double) knobs[attackKnob]) * sampleRate));
    const float decayCoef = (float) std::exp (-4.6 / (std::max (0.005, (double) knobs[decayKnob]) * sampleRate));
    const float releaseCoef = (float) std::exp (-4.6 / (std::max (0.003, (double) knobs[releaseKnob]) * sampleRate));
    const float sustain = juce::jlimit (0.0f, 1.0f, knobs[sustainKnob]);

    // FILTER and the slice's own, in series: the lower low-pass, the higher high-pass
    const auto [lpG, hpG] = filterCutoffs (filterSmoothed);
    const auto [lpS, hpS] = filterCutoffs ((double) p.filter);
    const double lpHz = juce::jlimit (20.0, 0.45 * sampleRate, std::min (lpG, lpS)), hpHz = juce::jlimit (10.0, 0.45 * sampleRate, std::max (hpG, hpS));
    const double q = 0.707 + 8.0 * juce::jlimit (0.0f, 1.0f, knobs[resoKnob]);
    const bool lowPassed = lpHz < 19000.0, highPassed = hpHz > 21.0;
    const auto lpCoeffs = gk::TptSvf::Coeffs::make (lpHz, sampleRate, q), hpCoeffs = gk::TptSvf::Coeffs::make (hpHz, sampleRate, q);
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
            if (lowPassed)
                y = v.lowPass[c].processLowpass (y, lpCoeffs);
            if (highPassed)
                y = v.highPass[c].processHighpass (y, hpCoeffs);
            out[c][i] += g * (float) y;
        }
        v.pos += v.reverse ? -step : step;
    }
}

void DaftChunkEngine::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float*, const gk::BlockTiming& timing)
{
    buffer.clear (0, numSamples);
    telemetry.hostBpm.store (hostTempoKnown ? timing.bpm : 0.0, std::memory_order_relaxed);
    telemetry.hostPlaying.store (hostTempoKnown && timing.isPlaying, std::memory_order_relaxed);
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

    // FILTER glides, over ~20 ms
    {
        const double target = juce::jlimit (-1.0, 1.0, (double) knobs[filterKnob]);
        const double a = std::min (1.0, numSamples / (0.02 * sampleRate));
        filterSmoothed += a * (target - filterSmoothed);
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
    // the idea's steps and the keys, merged in time order
    collectSequence (numSamples, timing);
    int e = 0;
    auto playSteps = [&] (int until)
    {
        for (; e < numSeqEvents && seqEvents[(size_t) e].at <= until; ++e)
        {
            const auto& ev = seqEvents[(size_t) e];
            renderTo (ev.at);
            if (ev.step->cut)
                cutAll();
            else
            {
                startSlice (ev.step->slice, ev.step->velocity, -1, ev.gate, (float) ev.step->pitch, ev.step->reverse);
                if (captureOn)
                    captureNote (ev.at, ev.step->slice, ev.step->velocity, ev.gate);
            }
        }
    };
    if (midi != nullptr)
        for (const auto meta : *midi)
        {
            const int t = meta.samplePosition - midiStart;
            if (t < 0 || t >= midiCount)
                continue;
            playSteps (std::min (t, numSamples) - 1);
            const auto msg = meta.getMessage();
            renderTo (std::min (t, numSamples));
            if (msg.isNoteOn())
                noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
                noteOff (msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                cutAll();
        }
    playSteps (numSamples);
    renderTo (numSamples);
    // MIDI CAPTURE: the notes held end in time; capture off or the idea stopped: all off now
    if (! captureOn || sequence.load (std::memory_order_relaxed) == nullptr)
        for (auto& o : pendingOffs)
            if (o.note >= 0)
                o.left = 0;
    advanceCapture (numSamples);
    processEffects (buffer, numSamples, timing);

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
