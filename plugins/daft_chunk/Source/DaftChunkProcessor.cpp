#include "DaftChunkProcessor.h"
#include "DaftChunkEditor.h"
#include <DaftChunkPresets.h>

namespace daftchunk
{
namespace
{
    gk::OneKnobConfig makeConfig (gk::PresetManager presets)
    {
        gk::OneKnobConfig cfg;
        cfg.productName = DC_PRODUCT_NAME;
        cfg.version = DC_VERSION_STRING;
        cfg.updateSlug = "daft-chunk"; // key in lalzin.com/plugins/versions.json
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.isInstrument = true;
        cfg.amountParamId = "chop";
        cfg.amountParamName = "Chop";
        cfg.defaultAmount = 0.35f;
        cfg.knobLowLabel = "ORDER";
        cfg.knobHighLabel = "WILD";
        cfg.presetSetsParameters = true; // a preset sets CHOP, the style, the effects...
        cfg.editorBuildsControls = false;
        cfg.editorWidth = 860;
        cfg.editorHeight = 660;
        // the sound (the SON page): host parameters, in DaftChunkEngine::Knob order
        using E = DaftChunkEngine;
        cfg.knobs = { { "attack", "Attack", "ATTACK", 0.0005f, 0.0005f, 2.0f, 0.25f, "s" },
                      { "decay", "Decay", "DECAY", 0.3f, 0.005f, 5.0f, 0.3f, "s" },
                      { "sustain", "Sustain", "SUSTAIN", 1.0f },
                      { "release", "Release", "RELEASE", 0.05f, 0.003f, 5.0f, 0.3f, "s" },
                      { "pitch", "Pitch", "PITCH", 0.0f, -24.0f, 24.0f, 1.0f, "st" },
                      { "filter", "Filter", "FILTER", 0.0f, -1.0f, 1.0f, 1.0f, "" }, // left a low-pass, right a high-pass
                      { "reso", "Resonance", "RESO", 0.0f },
                      { "velocity", "Velocity", "VEL", 0.7f },
                      { "volume", "Volume", "VOLUME", 0.0f, -60.0f, 6.0f, 3.02f, "dB" },
                      { "swing", "Swing", "SWING", 0.0f, 0.0f, 0.75f, 1.0f, "%" },
                      // the output effects (step 5)
                      { "pump", "Pump", "PUMP", 0.0f },
                      { "crush", "Crush", "CRUSH", 0.0f },
                      { "mix", "Mix", "MIX", 1.0f },
                      { "phaser", "Phaser", "PHASER", 0.0f },
                      { "flanger", "Flanger", "FLANGER", 0.0f } };
        const auto time = [] (float v) { return v < 1.0f ? juce::String (v * 1000.0f, v < 0.01f ? 1 : 0) + " ms" : juce::String (v, 2) + " s"; };
        cfg.knobs[E::attackKnob].textFromValue = time;
        cfg.knobs[E::decayKnob].textFromValue = time;
        cfg.knobs[E::releaseKnob].textFromValue = time;
        cfg.knobs[E::pitchKnob].textFromValue = [] (float v) { const int st = juce::roundToInt (v); return (st > 0 ? "+" : "") + juce::String (st) + " st"; };
        cfg.knobs[E::filterKnob].textFromValue = [] (float v)
        {
            const auto [lp, hp] = DaftChunkEngine::filterCutoffs (v);
            const double hz = v < 0.0f ? lp : hp;
            if (std::abs (v) < 0.01f)
                return juce::String ("OPEN");
            return juce::String (v < 0.0f ? "LP " : "HP ") + (hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz");
        };
        cfg.knobs[E::volumeKnob].textFromValue = [] (float v) { return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
        cfg.outputGainKnob = E::volumeKnob;
        cfg.choices = { { "mode", "Mode", "MODE", { "Choke", "Poly" }, E::cut, true },
                        { "play", "Play", "PLAY", { "One shot", "Gate" }, E::gate, true },
                        { "sync", "Sync", "SYNC", { "Free", "Tempo", "Vinyl" }, E::tempo, true },
                        { "style", "Style", "STYLE", { "Filter Touch", "Disco Loop", "Vocal Chop", "Stutter House", "Reverse Party", "Pitch Party" }, 0, true },
                        { "capture", "MIDI Capture", "MIDI CAPTURE", { "Off", "On" }, 0, true },
                        { "length", "Loop length", "LENGTH", { "Auto", "1 bar", "2 bars", "4 bars" }, 0, true },
                        { "sweep", "Sweep", "SWEEP", { "Idea", "Off", "4 bars", "8 bars", "16 bars" }, 0, true },
                        { "modrate", "Mod rate", "RATE", { "1 bar", "2 bars", "4 bars", "8 bars" }, E::mod2, true } };
        cfg.tailSeconds = 0.1;
        cfg.featuresFromOutput = true; // (an instrument: Chunky hears what it plays)
        return cfg;
    }

    double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    juce::String joinInts (const std::vector<int64_t>& v)
    {
        juce::StringArray s;
        for (auto x : v)
            s.add (juce::String (x));
        return s.joinIntoString (",");
    }
} // namespace

gk::PresetManager DaftChunkProcessor::loadBuiltInPresets()
{
    struct Entry { const char* data; int size; };
    const Entry entries[] = {
        { DCPresets::one_more_chop_json, DCPresets::one_more_chop_jsonSize }, // the default
        { DCPresets::disco_loop_json, DCPresets::disco_loop_jsonSize },
        { DCPresets::vocal_chop_json, DCPresets::vocal_chop_jsonSize },
        { DCPresets::stutter_house_json, DCPresets::stutter_house_jsonSize },
        { DCPresets::reverse_party_json, DCPresets::reverse_party_jsonSize },
        { DCPresets::pitch_party_json, DCPresets::pitch_party_jsonSize },
        { DCPresets::mpc_mode_json, DCPresets::mpc_mode_jsonSize },
        { DCPresets::chunky_funk_json, DCPresets::chunky_funk_jsonSize },
        { DCPresets::robot_glitch_json, DCPresets::robot_glitch_jsonSize },
        { DCPresets::filter_sunrise_json, DCPresets::filter_sunrise_jsonSize },
    };
    gk::PresetManager pm;
    for (const auto& e : entries)
    {
        const auto r = pm.add (juce::String::fromUTF8 (e.data, e.size));
        jassertquiet (r.wasOk());
    }
    return pm;
}

/** Applies a preset when the preset changes (the parameter may move on the audio thread: deferred). */
class DaftChunkProcessor::PresetWatcher final : private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    explicit PresetWatcher (DaftChunkProcessor& p) : proc (p) { proc.getValueTreeState().addParameterListener ("preset", this); }
    ~PresetWatcher() override
    {
        proc.getValueTreeState().removeParameterListener ("preset", this);
        cancelPendingUpdate();
    }

private:
    void parameterChanged (const juce::String&, float) override
    {
        if (juce::MessageManager::existsAndIsCurrentThread())
            handleAsyncUpdate();
        else
            triggerAsyncUpdate();
    }
    void handleAsyncUpdate() override { proc.applyPreset (proc.getPresetParameter().getIndex()); }
    DaftChunkProcessor& proc;
};

void DaftChunkProcessor::applyPreset (int index)
{
    if (loadingState || index == appliedPreset || ! juce::isPositiveAndBelow (index, getPresets().size()))
        return;
    appliedPreset = index;
    const auto params = getPresets()[index].json.getProperty ("params", {});
    auto set = [] (juce::RangedAudioParameter& p, float plain)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plain));
        p.endChangeGesture();
    };
    if (params.hasProperty ("chop"))
        set (getAmountParameter(), (float) params["chop"]);
    // the knobs and choices it names (by id; choices by their option's name)
    const auto& knobs = getConfig().knobs;
    for (int k = 0; k < (int) knobs.size(); ++k)
        if (params.hasProperty (knobs[(size_t) k].id))
            set (getKnobParameter (k), (float) params[juce::Identifier (knobs[(size_t) k].id)]);
    const auto& choices = getConfig().choices;
    for (int c = 0; c < (int) choices.size(); ++c)
        if (const auto& spec = choices[(size_t) c]; params.hasProperty (spec.id))
            if (const int i = spec.options.indexOf (params[juce::Identifier (spec.id)].toString(), true); i >= 0)
                set (getChoiceParameter (c), (float) i);
    // the slicing (MPC MODE: the 1/16 grid)
    if (params.hasProperty ("slicing"))
    {
        const auto m = params["slicing"].toString();
        settings.mode = m == "Grid" ? SliceMode::grid : m == "Mixed" ? SliceMode::mixed : SliceMode::transients;
        if (params.hasProperty ("division"))
            settings.division = (int) params["division"];
        if (sample != nullptr)
            reslice();
    }
    lastChop = -1.0f; // (the preset's CHOP and LENGTH are not moves of the knob)
    lastLength = -1;
    // heard at once: a new loop in the preset's style
    if (sample != nullptr && kit != nullptr && ! kit->slices.empty())
        rollDice();
    sendChangeMessage();
}

DaftChunkProcessor::DaftChunkProcessor()
    : OneKnobProcessor (makeConfig (loadBuiltInPresets()), std::make_unique<DaftChunkEngine>())
{
    applyPreset (0);
    watcher = std::make_unique<PresetWatcher> (*this);
    startTimerHz (30);
}

DaftChunkProcessor::~DaftChunkProcessor()
{
    watcher = nullptr;
    stopTimer();
    getDsp().setKit (nullptr);
    getDsp().setSequence (nullptr);
}

juce::AudioProcessorEditor* DaftChunkProcessor::createEditor()
{
    return new DaftChunkEditor (*this);
}

//==============================================================================
void DaftChunkProcessor::loadFile (const juce::File& file)
{
    settings.bpm = -1.0;
    settings.firstBeat = -1.0;
    pendingBoundaries.clear();
    sliceParams.clear();
    locked.clear();
    selected = 0;
    samplePath = file.getFullPathName();
    status = {};
    loader.load (file);
    sendChangeMessage();
}

void DaftChunkProcessor::loadAudio (juce::AudioBuffer<float> audio, double rate, const juce::String& name)
{
    settings.bpm = -1.0;
    settings.firstBeat = -1.0;
    pendingBoundaries.clear();
    sliceParams.clear();
    selected = 0;
    samplePath = {};
    status = {};
    loader.load (std::move (audio), rate, name);
}

void DaftChunkProcessor::waitForAnalysis()
{
    loader.waitUntilDone();
    takeAnalysis();
}

void DaftChunkProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // the saved settings win over the preset's (no loop made): the restored preset is marked applied
    loadingState = true;
    OneKnobProcessor::setStateInformation (data, sizeInBytes);
    loadingState = false;
    appliedPreset = getPresetParameter().getIndex();
}

void DaftChunkProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    bool known = false;
    if (wrapperType != wrapperType_Standalone)
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition())
                known = pos->getBpm().hasValue() && *pos->getBpm() > 0.0;
    getDsp().setHostTempoKnown (known);
    getDsp().clearOutEvents();
    OneKnobProcessor::processBlock (buffer, midi);
    // the MIDI out: only MIDI CAPTURE's notes (the keys played in are not echoed back)
    midi.clear();
    auto& e = getDsp();
    for (int i = 0; i < e.numOutEvents(); ++i)
    {
        const auto& ev = e.outEvent (i);
        const int at = juce::jlimit (0, std::max (0, buffer.getNumSamples() - 1), ev.sample);
        midi.addEvent (ev.on ? juce::MidiMessage::noteOn (1, ev.note, juce::jlimit (0.01f, 1.0f, ev.velocity)) : juce::MidiMessage::noteOff (1, ev.note), at);
    }
}

void DaftChunkProcessor::timerCallback()
{
    takeAnalysis();
    updateStretch (false);
    freeRetired (false);
    // CHOP live: once the knob rests, the idea follows it
    const float chop = getAmountParameter().convertFrom0to1 (getAmountParameter().getValue());
    if (lastChop < 0.0f)
        lastChop = chop;
    if (std::abs (chop - lastChop) > 0.002f)
    {
        lastChop = chop;
        chopMoved = now();
    }
    if (chopMoved > 0.0 && now() - chopMoved > 0.12)
    {
        chopMoved = -1.0;
        applyChop();
    }
    // LONGUEUR changed: the idea too (same seed, style and CHOP)
    const int length = getChoiceParameter (DaftChunkEngine::lengthChoice).getIndex();
    if (lastLength < 0)
        lastLength = length;
    if (length != lastLength)
    {
        lastLength = length;
        applyLength();
    }
    checkTransport();
}

void DaftChunkProcessor::checkTransport()
{
    // Ableton's PLAY: the loop heard in the plug-in stops (the track plays; its own play button starts it again, in time)
    const bool hostPlaying = getDsp().getTelemetry().hostPlaying.load (std::memory_order_relaxed);
    if (hostPlaying && ! wasHostPlaying && playingIdea >= 0)
        stopIdea();
    wasHostPlaying = hostPlaying;
}

void DaftChunkProcessor::applyLength()
{
    const int i = playingIdea >= 0 ? playingIdea : selectedIdea;
    if (! juce::isPositiveAndBelow (i, numIdeas) || ! ideas[(size_t) i].used)
        return;
    const auto src = sourceInfo();
    if (src.types.empty())
        return;
    const int lengths[] { 0, 1, 2, 4 };
    const int bars = lengths[juce::jlimit (0, 3, getChoiceParameter (DaftChunkEngine::lengthChoice).getIndex())];
    auto& idea = ideas[(size_t) i];
    const auto old = idea.pattern;
    remember();
    idea.pattern = generate (src, old.style, old.chop, old.seed, bars);
    ideasChanged();
}

void DaftChunkProcessor::applyChop()
{
    const int i = playingIdea >= 0 ? playingIdea : selectedIdea;
    if (! juce::isPositiveAndBelow (i, numIdeas) || ! ideas[(size_t) i].used)
        return;
    const float chop = getAmountParameter().convertFrom0to1 (getAmountParameter().getValue());
    auto& idea = ideas[(size_t) i];
    if (std::abs (idea.pattern.chop - chop) < 0.005f)
        return;
    const auto src = sourceInfo();
    if (src.types.empty())
        return;
    // one ANNULER for a whole turn of the knob
    if (now() - chopRemembered > 1.5)
        remember();
    chopRemembered = now();
    const auto old = idea.pattern;
    idea.pattern = generate (src, old.style, chop, old.seed, old.bars);
    ideasChanged();
}

int DaftChunkProcessor::getSync() const noexcept
{
    return const_cast<DaftChunkProcessor*> (this)->getChoiceParameter (DaftChunkEngine::syncChoice).getIndex();
}

void DaftChunkProcessor::setSync (int sync)
{
    auto& c = getChoiceParameter (DaftChunkEngine::syncChoice);
    c.beginChangeGesture();
    c.setValueNotifyingHost (c.convertTo0to1 ((float) juce::jlimit (0, 2, sync)));
    c.endChangeGesture();
    updateStretch (false);
}

void DaftChunkProcessor::stretchNow()
{
    updateStretch (true);
    stretcher.waitUntilDone();
    updateStretch (true);
}

void DaftChunkProcessor::updateStretch (bool now)
{
    // a finished stretch: played from the next block (if it is still the one wanted)
    if (auto r = stretcher.takeResult(); r.audio != nullptr && r.id == stretchId && sample != nullptr)
    {
        stretched = std::move (r.audio);
        stretchedFactor = r.factor;
        stretchWanted = false;
        republish();
    }
    if (sample == nullptr)
        return;
    const double host = getHostBpm();
    const double t = now ? 1.0e9 : juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (std::abs (host - lastHostBpm) > 1.0e-6)
    {
        lastHostBpm = host;
        hostStableSince = t;
    }
    // what TEMPO wants: the sample stretched by its tempo / the host's (none: LIBRE, VINYLE, no host tempo, already right)
    double want = 1.0;
    if (getSync() == DaftChunkEngine::tempo && host > 0.0)
        want = juce::jlimit (0.25, 4.0, getBpm() / host);
    if (std::abs (want - 1.0) < 0.001)
    {
        stretchWanted = false;
        requestedFactor = 0.0;
        if (stretched != nullptr)
        {
            ++stretchId; // (a stretch still running is no longer wanted)
            stretched = nullptr;
            stretchedFactor = 1.0;
            republish();
        }
        return;
    }
    if (stretched != nullptr && std::abs (want / stretchedFactor - 1.0) < 0.0005)
    {
        stretchWanted = false;
        return; // already there
    }
    stretchWanted = true;
    // the host's tempo must hold 250 ms first (a tempo ramp would restart it at every block)
    if (std::abs (want / std::max (requestedFactor, 1.0e-9) - 1.0) >= 0.0005 && (now || t - hostStableSince > 0.25))
    {
        requestedFactor = want;
        StretchWorker::Job job;
        job.source = std::shared_ptr<const juce::AudioBuffer<float>> (sample, &sample->audio);
        job.sampleRate = sample->sampleRate;
        job.factor = want;
        job.id = ++stretchId;
        stretcher.request (std::move (job));
    }
}

void DaftChunkProcessor::takeAnalysis()
{
    juce::String error;
    auto result = loader.takeResult (error);
    if (result == nullptr)
    {
        if (error.isNotEmpty() && error != "Cancelled")
        {
            status = error;
            sendChangeMessage();
        }
        return;
    }
    status = result->truncated ? juce::String ("Trimmed to 60 s") : juce::String();
    sample = std::shared_ptr<const LoadedSample> (std::move (result));
    taps.clear();
    stretched = nullptr; // (the new sample is stretched once its tempo is known: the timer)
    stretchedFactor = 1.0;
    requestedFactor = 0.0;
    ++stretchId;
    const bool sameFile = pendingLength < 0 || pendingLength == sample->analysis.numSamples;
    if (! sameFile)
        sliceParams.clear(); // the file changed since the session: its cuts and slice settings no longer fit
    pendingLength = -1;
    if (! pendingBoundaries.empty() && sameFile)
    {
        userCuts = true;
        publish (slicesFromCuts (sample->analysis, pendingBoundaries));
    }
    else
        reslice();
    pendingBoundaries.clear();
}

//==============================================================================
void DaftChunkProcessor::reslice()
{
    userCuts = false;
    if (sample == nullptr)
    {
        publish ({});
        return;
    }
    publish (slice (sample->analysis, settings));
}

void DaftChunkProcessor::publish (std::vector<Slice> slices)
{
    auto k = std::make_shared<Kit>();
    k->sample = slices.empty() ? nullptr : sample;
    if (k->sample == nullptr && sample != nullptr)
        k->sample = sample; // (no slices: still the sample, for the display)
    k->slices = std::move (slices);
    sliceParams.resize (k->slices.size()); // (kept by index when the cuts move)
    k->params = sliceParams;
    if (stretched != nullptr && getSync() == DaftChunkEngine::tempo)
    {
        k->stretched = stretched;
        k->timeScale = stretchedFactor;
    }
    k->playBpm = getBpm() / k->timeScale;
    selected = juce::jlimit (0, std::max (0, (int) k->slices.size() - 1), selected);
    k->version = ++kitVersion;
    boundaries.clear();
    for (const auto& s : k->slices)
        boundaries.push_back (s.start);
    if (! k->slices.empty())
        boundaries.push_back (k->slices.back().end);
    if (kit != nullptr)
        retired.push_back ({ kit, now() });
    kit = std::move (k);
    getDsp().setKit (kit.get());
    sendChangeMessage();
}

void DaftChunkProcessor::freeRetired (bool force)
{
    const auto t = now();
    const auto* inUse = getDsp().kitInUse();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [&] (const Retired& r) { return force || (t - r.since > 1.0 && r.kit.get() != inUse); }),
                   retired.end());
    const auto* seqInUse = getDsp().sequenceInUse();
    retiredSeqs.erase (std::remove_if (retiredSeqs.begin(), retiredSeqs.end(),
                                       [&] (const RetiredSeq& r) { return force || (t - r.since > 1.0 && r.pattern.get() != seqInUse); }),
                       retiredSeqs.end());
}

//==============================================================================
SourceInfo DaftChunkProcessor::sourceInfo() const
{
    if (kit == nullptr || sample == nullptr || kit->slices.empty())
        return {};
    const double sixteenth = sample->analysis.beatSamples (getBpm()) / 4.0;
    return describe (kit->slices, sample->analysis.key, getFirstBeat(), sixteenth, sample->analysis.numSamples, locked);
}

void DaftChunkProcessor::remember()
{
    undoStack.push_back ({ ideas, locked });
    if (undoStack.size() > 50)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void DaftChunkProcessor::ideasChanged()
{
    ++ideasVersion;
    // what plays follows its edits (a new pattern object: the old one retires)
    if (playingIdea >= 0 && ideas[(size_t) playingIdea].used)
    {
        if (sequence != nullptr)
            retiredSeqs.push_back ({ sequence, now() });
        sequence = std::make_shared<const Pattern> (ideas[(size_t) playingIdea].pattern);
        getDsp().setSequence (sequence.get());
    }
    else if (playingIdea >= 0)
        setPlaying (-1);
    sendChangeMessage();
}

void DaftChunkProcessor::setPlaying (int i)
{
    playingIdea = juce::isPositiveAndBelow (i, numIdeas) && ideas[(size_t) i].used ? i : -1;
    if (sequence != nullptr)
        retiredSeqs.push_back ({ sequence, now() });
    sequence = playingIdea >= 0 ? std::make_shared<const Pattern> (ideas[(size_t) playingIdea].pattern) : nullptr;
    getDsp().setSequence (sequence.get());
    ++ideasVersion;
    sendChangeMessage();
}

int DaftChunkProcessor::freeSlot (int except)
{
    for (int i = 0; i < numIdeas; ++i)
        if (! ideas[(size_t) i].used)
            return i;
    int oldest = -1;
    for (int i = 0; i < numIdeas; ++i)
        if (i != except && ! ideas[(size_t) i].starred && (oldest < 0 || ideas[(size_t) i].age < ideas[(size_t) oldest].age))
            oldest = i;
    return oldest;
}

int DaftChunkProcessor::rollDice()
{
    const auto src = sourceInfo();
    if (src.types.empty())
        return -1;
    const int slot = freeSlot();
    if (slot < 0)
    {
        status = "All 8 loops are starred: unstar one";
        sendChangeMessage();
        return -1;
    }
    remember();
    const auto style = (Style) getChoiceParameter (DaftChunkEngine::styleChoice).getIndex();
    const float chop = getAmountParameter().convertFrom0to1 (getAmountParameter().getValue());
    const int lengths[] { 0, 1, 2, 4 };
    const int bars = lengths[juce::jlimit (0, 3, getChoiceParameter (DaftChunkEngine::lengthChoice).getIndex())];
    // never the same loop twice: a few seeds tried, the one least like the loops already
    // there (and the last ones made) kept
    std::vector<const Pattern*> others;
    for (const auto& i : ideas)
        if (i.used)
            others.push_back (&i.pattern);
    for (const auto& r : recentLoops)
        others.push_back (&r);
    Pattern best;
    float bestSim = 2.0f;
    for (int attempt = 0; attempt < 24 && bestSim > 0.6f; ++attempt)
    {
        auto candidate = generate (src, style, chop, (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff), bars);
        float sim = 0.0f;
        for (const auto* o : others)
            sim = std::max (sim, similarity (candidate, *o));
        if (sim < bestSim)
        {
            bestSim = sim;
            best = std::move (candidate);
        }
    }
    recentLoops.push_back (best);
    if (recentLoops.size() > 16)
        recentLoops.erase (recentLoops.begin());
    auto& idea = ideas[(size_t) slot];
    idea.pattern = std::move (best);
    idea.used = true;
    idea.starred = false;
    idea.age = ++ideaAge;
    selectedIdea = slot;
    ++diceVersion;
    setPlaying (slot); // heard at once, on the host's bars
    ideasChanged();
    return slot;
}

int DaftChunkProcessor::encore()
{
    const auto src = sourceInfo();
    if (src.types.empty() || ! juce::isPositiveAndBelow (selectedIdea, numIdeas) || ! ideas[(size_t) selectedIdea].used)
        return 0;
    remember();
    const auto base = ideas[(size_t) selectedIdea].pattern;
    int made = 0;
    for (int v = 0; v < 4; ++v)
    {
        const int slot = freeSlot (selectedIdea);
        if (slot < 0 || slot == selectedIdea)
            break;
        auto& idea = ideas[(size_t) slot];
        idea.pattern = mutate (base, src, (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff));
        idea.used = true;
        idea.starred = false;
        idea.age = ++ideaAge;
        ++made;
    }
    ++diceVersion;
    ideasChanged();
    return made;
}

void DaftChunkProcessor::toggleStar (int i)
{
    if (! juce::isPositiveAndBelow (i, numIdeas) || ! ideas[(size_t) i].used)
        return;
    remember();
    ideas[(size_t) i].starred = ! ideas[(size_t) i].starred;
    ideasChanged();
}

void DaftChunkProcessor::deleteIdea (int i)
{
    if (! juce::isPositiveAndBelow (i, numIdeas) || ! ideas[(size_t) i].used)
        return;
    remember();
    ideas[(size_t) i] = {};
    if (selectedIdea == i)
        selectedIdea = -1;
    ideasChanged();
}

void DaftChunkProcessor::togglePlay (int i)
{
    if (! juce::isPositiveAndBelow (i, numIdeas) || ! ideas[(size_t) i].used)
        return;
    selectedIdea = i;
    setPlaying (playingIdea == i ? -1 : i);
}

void DaftChunkProcessor::selectIdea (int i)
{
    selectedIdea = juce::isPositiveAndBelow (i, numIdeas) && ideas[(size_t) i].used ? i : -1;
    ++ideasVersion;
    sendChangeMessage();
}

void DaftChunkProcessor::setStep (int i, int k, const Step& st)
{
    if (! juce::isPositiveAndBelow (i, numIdeas) || ! ideas[(size_t) i].used || ! juce::isPositiveAndBelow (k, ideas[(size_t) i].pattern.numSteps()))
        return;
    if (ideas[(size_t) i].pattern.steps[(size_t) k] == st)
        return;
    remember();
    ideas[(size_t) i].pattern.steps[(size_t) k] = st;
    ideasChanged();
}

void DaftChunkProcessor::toggleLock (int slice)
{
    const int n = kit != nullptr ? (int) kit->slices.size() : 0;
    if (! juce::isPositiveAndBelow (slice, n))
        return;
    remember();
    locked.resize ((size_t) n, false);
    locked[(size_t) slice] = ! locked[(size_t) slice];
    ideasChanged();
}

void DaftChunkProcessor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back ({ ideas, locked });
    ideas = undoStack.back().ideas;
    locked = undoStack.back().locked;
    undoStack.pop_back();
    ideasChanged();
}

void DaftChunkProcessor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back ({ ideas, locked });
    ideas = redoStack.back().ideas;
    locked = redoStack.back().locked;
    redoStack.pop_back();
    ideasChanged();
}

void DaftChunkProcessor::setMode (SliceMode m)
{
    settings.mode = m;
    reslice();
}

void DaftChunkProcessor::setDivision (int d)
{
    settings.division = d == 4 || d == 8 ? d : 16;
    reslice();
}

void DaftChunkProcessor::setSensitivity (float s)
{
    settings.sensitivity = juce::jlimit (0.0f, 1.0f, s);
    reslice();
}

double DaftChunkProcessor::getBpm() const noexcept
{
    if (settings.bpm > 0.0)
        return settings.bpm;
    return sample != nullptr ? sample->analysis.bpm : 120.0;
}

void DaftChunkProcessor::setBpm (double bpm)
{
    settings.bpm = bpm > 0.0 ? juce::jlimit (30.0, 300.0, bpm) : -1.0;
    reslice();
}

void DaftChunkProcessor::tap (double seconds)
{
    if (! taps.empty() && seconds - taps.back() > 2.0)
        taps.clear(); // a pause: a new series
    taps.push_back (seconds);
    if (taps.size() > 8)
        taps.erase (taps.begin());
    if (const double b = tapTempo (taps); b > 0.0)
        setBpm (std::round (b * 10.0) / 10.0);
}

double DaftChunkProcessor::getFirstBeat() const noexcept
{
    if (settings.firstBeat >= 0.0)
        return settings.firstBeat;
    return sample != nullptr ? sample->analysis.firstBeat : 0.0;
}

void DaftChunkProcessor::setFirstBeat (double samples)
{
    settings.firstBeat = samples >= 0.0 && sample != nullptr ? std::min (samples, (double) sample->analysis.numSamples - 1.0) : -1.0;
    reslice();
}

void DaftChunkProcessor::moveCut (int index, int64_t samples)
{
    if (sample == nullptr || index <= 0 || index >= (int) boundaries.size() - 1)
        return;
    const auto gap = (int64_t) (0.02 * sample->sampleRate);
    auto b = boundaries;
    b[(size_t) index] = juce::jlimit (b[(size_t) index - 1] + gap, b[(size_t) index + 1] - gap, samples);
    userCuts = true;
    publish (slicesFromCuts (sample->analysis, b));
}

void DaftChunkProcessor::addCut (int64_t samples)
{
    if (sample == nullptr || (int) boundaries.size() - 1 >= Analysis::maxSlices)
        return;
    auto b = boundaries;
    b.insert (std::upper_bound (b.begin(), b.end() - 1, samples), samples);
    userCuts = true;
    publish (slicesFromCuts (sample->analysis, b));
}

void DaftChunkProcessor::removeCut (int index)
{
    if (sample == nullptr || index <= 0 || index >= (int) boundaries.size() - 1)
        return;
    auto b = boundaries;
    b.erase (b.begin() + index);
    userCuts = true;
    publish (slicesFromCuts (sample->analysis, b));
}

void DaftChunkProcessor::resetCuts()
{
    reslice();
}

//==============================================================================
SliceParams DaftChunkProcessor::getSliceParams (int index) const
{
    return juce::isPositiveAndBelow (index, (int) sliceParams.size()) ? sliceParams[(size_t) index] : SliceParams {};
}

void DaftChunkProcessor::setSliceParams (int index, const SliceParams& p)
{
    if (kit == nullptr || ! juce::isPositiveAndBelow (index, (int) sliceParams.size()))
        return;
    sliceParams[(size_t) index] = p;
    republish();
}

void DaftChunkProcessor::applyToAllSlices (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) sliceParams.size()))
        return;
    const auto p = sliceParams[(size_t) index];
    std::fill (sliceParams.begin(), sliceParams.end(), p);
    republish();
}

void DaftChunkProcessor::selectSlice (int index)
{
    const int n = kit != nullptr ? (int) kit->slices.size() : 0;
    if (n > 0)
        selected = juce::jlimit (0, n - 1, index);
    sendChangeMessage();
}

void DaftChunkProcessor::republish()
{
    if (kit != nullptr)
        publish (kit->slices);
}

//==============================================================================
void DaftChunkProcessor::saveExtraState (juce::ValueTree& state)
{
    state.setProperty ("samplePath", samplePath, nullptr);
    state.setProperty ("sliceMode", (int) settings.mode, nullptr);
    state.setProperty ("division", settings.division, nullptr);
    state.setProperty ("sensitivity", settings.sensitivity, nullptr);
    state.setProperty ("bpm", settings.bpm, nullptr);
    state.setProperty ("firstBeat", settings.firstBeat, nullptr);
    state.setProperty ("userCuts", userCuts ? joinInts (boundaries) : juce::String(), nullptr);
    {
        juce::StringArray ps; // "pitch gain filter start end reverse" per slice, ";" between
        for (const auto& p : sliceParams)
            ps.add (juce::String (p.pitch, 3) + " " + juce::String (p.gainDb, 3) + " " + juce::String (p.filter, 3) + " " + juce::String (p.start, 4) + " "
                    + juce::String (p.end, 4) + " " + (p.reverse ? "1" : "0"));
        state.setProperty ("sliceParams", ps.joinIntoString (";"), nullptr);
    }
    {
        // ideas: "used starred bars seed style chop filterBars pump|step;step;..." per slot, "/" between slots
        juce::StringArray slots;
        for (const auto& idea : ideas)
        {
            if (! idea.used)
            {
                slots.add ("-");
                continue;
            }
            const auto& pt = idea.pattern;
            juce::StringArray st;
            for (const auto& x : pt.steps)
                st.add (juce::String (x.slice) + ":" + juce::String (x.pitch) + ":" + juce::String (x.repeat) + ":" + (x.reverse ? "1" : "0") + ":" + (x.cut ? "1" : "0") + ":"
                        + juce::String (x.velocity, 3));
            slots.add ((idea.starred ? juce::String ("1 ") : juce::String ("0 ")) + juce::String (pt.bars) + " " + juce::String ((juce::int64) pt.seed) + " " + juce::String ((int) pt.style) + " "
                       + juce::String (pt.chop, 3) + " " + juce::String (pt.filterBars) + " " + (pt.pump ? "1" : "0") + "|" + st.joinIntoString (";"));
        }
        state.setProperty ("ideas", slots.joinIntoString ("/"), nullptr);
        juce::StringArray lk;
        for (bool b : locked)
            lk.add (b ? "1" : "0");
        state.setProperty ("locked", lk.joinIntoString (","), nullptr);
        state.setProperty ("selectedIdea", selectedIdea, nullptr);
    }
    state.setProperty ("sampleLength", sample != nullptr ? (juce::int64) sample->analysis.numSamples : (juce::int64) -1, nullptr);
}

void DaftChunkProcessor::loadExtraState (const juce::ValueTree& state)
{
    settings.mode = (SliceMode) juce::jlimit (0, 2, (int) state.getProperty ("sliceMode", 0));
    settings.division = (int) state.getProperty ("division", 16);
    settings.sensitivity = (float) state.getProperty ("sensitivity", 0.5f);
    settings.bpm = (double) state.getProperty ("bpm", -1.0);
    settings.firstBeat = (double) state.getProperty ("firstBeat", -1.0);
    pendingBoundaries.clear();
    for (const auto& s : juce::StringArray::fromTokens (state.getProperty ("userCuts").toString(), ",", ""))
        pendingBoundaries.push_back (s.getLargeIntValue());
    pendingLength = (int64_t) (juce::int64) state.getProperty ("sampleLength", -1);
    sliceParams.clear();
    for (const auto& item : juce::StringArray::fromTokens (state.getProperty ("sliceParams").toString(), ";", ""))
    {
        const auto v = juce::StringArray::fromTokens (item, " ", "");
        SliceParams p;
        if (v.size() >= 6)
        {
            p.pitch = juce::jlimit (-24.0f, 24.0f, v[0].getFloatValue());
            p.gainDb = juce::jlimit (-24.0f, 12.0f, v[1].getFloatValue());
            p.filter = juce::jlimit (-1.0f, 1.0f, v[2].getFloatValue());
            p.start = juce::jlimit (0.0f, 0.9f, v[3].getFloatValue());
            p.end = juce::jlimit (0.0f, 0.9f, v[4].getFloatValue());
            p.reverse = v[5] == "1";
        }
        sliceParams.push_back (p);
    }
    {
        ideas = {};
        const auto slots = juce::StringArray::fromTokens (state.getProperty ("ideas").toString(), "/", "");
        for (int i = 0; i < std::min (slots.size(), numIdeas); ++i)
        {
            const auto head = slots[i].upToFirstOccurrenceOf ("|", false, false);
            const auto body = slots[i].fromFirstOccurrenceOf ("|", false, false);
            const auto h = juce::StringArray::fromTokens (head, " ", "");
            if (slots[i] == "-" || h.size() < 7)
                continue;
            auto& idea = ideas[(size_t) i];
            idea.used = true;
            idea.starred = h[0] == "1";
            idea.age = ++ideaAge;
            auto& pt = idea.pattern;
            pt.bars = juce::jlimit (1, 4, h[1].getIntValue());
            pt.seed = (uint32_t) h[2].getLargeIntValue();
            pt.style = (Style) juce::jlimit (0, numStyles - 1, h[3].getIntValue());
            pt.chop = h[4].getFloatValue();
            pt.filterBars = h[5].getIntValue();
            pt.pump = h[6] == "1";
            for (const auto& st : juce::StringArray::fromTokens (body, ";", ""))
            {
                const auto f = juce::StringArray::fromTokens (st, ":", "");
                Step x;
                if (f.size() >= 6)
                {
                    x.slice = f[0].getIntValue();
                    x.pitch = juce::jlimit (-24, 24, f[1].getIntValue());
                    x.repeat = juce::jlimit (1, 4, f[2].getIntValue());
                    x.reverse = f[3] == "1";
                    x.cut = f[4] == "1";
                    x.velocity = juce::jlimit (0.0f, 1.0f, f[5].getFloatValue());
                }
                pt.steps.push_back (x);
            }
            pt.steps.resize ((size_t) pt.bars * Pattern::stepsPerBar);
        }
        locked.clear();
        for (const auto& b : juce::StringArray::fromTokens (state.getProperty ("locked").toString(), ",", ""))
            locked.push_back (b == "1");
        selectedIdea = (int) state.getProperty ("selectedIdea", -1);
        if (! juce::isPositiveAndBelow (selectedIdea, numIdeas) || ! ideas[(size_t) selectedIdea].used)
            selectedIdea = -1;
        undoStack.clear();
        redoStack.clear();
        setPlaying (-1);
        lastChop = -1.0f; // (the restored CHOP and LONGUEUR are not moves)
        lastLength = -1;
        chopMoved = -1.0;
    }
    samplePath = state.getProperty ("samplePath").toString();
    status = {};
    if (samplePath.isNotEmpty())
    {
        const juce::File f (samplePath);
        if (f.existsAsFile())
            loader.load (f);
        else
            status = "File not found: " + f.getFileName();
    }
    sendChangeMessage();
}
} // namespace daftchunk
