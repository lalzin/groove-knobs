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
        cfg.updateSlug = ""; // not on the website yet: no update check
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.isInstrument = true;
        cfg.amountParamId = "chop";
        cfg.amountParamName = "Chop";
        cfg.defaultAmount = 0.2f;
        cfg.knobLowLabel = "ORDER";
        cfg.knobHighLabel = "WILD";
        cfg.hasPresetParameter = false; // the presets come with step 5
        cfg.editorBuildsControls = false;
        cfg.editorWidth = 860;
        cfg.editorHeight = 580;
        // the sound (the SON page): host parameters, in DaftChunkEngine::Knob order
        using E = DaftChunkEngine;
        cfg.knobs = { { "attack", "Attack", "ATTACK", 0.0005f, 0.0005f, 2.0f, 0.25f, "s" },
                      { "decay", "Decay", "DECAY", 0.3f, 0.005f, 5.0f, 0.3f, "s" },
                      { "sustain", "Sustain", "SUSTAIN", 1.0f },
                      { "release", "Release", "RELEASE", 0.05f, 0.003f, 5.0f, 0.3f, "s" },
                      { "pitch", "Pitch", "PITCH", 0.0f, -24.0f, 24.0f, 1.0f, "st" },
                      { "cutoff", "Filter", "FILTER", 20000.0f, 20.0f, 20000.0f, 0.25f, "Hz" },
                      { "reso", "Resonance", "RESO", 0.0f },
                      { "velocity", "Velocity", "VEL", 0.7f },
                      { "volume", "Volume", "VOLUME", 0.0f, -60.0f, 6.0f, 3.02f, "dB" } };
        const auto time = [] (float v) { return v < 1.0f ? juce::String (v * 1000.0f, v < 0.01f ? 1 : 0) + " ms" : juce::String (v, 2) + " s"; };
        cfg.knobs[E::attackKnob].textFromValue = time;
        cfg.knobs[E::decayKnob].textFromValue = time;
        cfg.knobs[E::releaseKnob].textFromValue = time;
        cfg.knobs[E::pitchKnob].textFromValue = [] (float v) { const int st = juce::roundToInt (v); return (st > 0 ? "+" : "") + juce::String (st) + " st"; };
        cfg.knobs[E::cutoffKnob].textFromValue = [] (float v) { return v >= 19900.0f ? juce::String ("OPEN") : v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
        cfg.knobs[E::volumeKnob].textFromValue = [] (float v) { return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
        cfg.outputGainKnob = E::volumeKnob;
        cfg.choices = { { "mode", "Mode", "MODE", { "Coupe", "Poly" }, E::cut, true },
                        { "play", "Play", "LECTURE", { "One shot", "Gate" }, E::gate, true } };
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
    gk::PresetManager pm;
    const auto r = pm.add (juce::String::fromUTF8 (DCPresets::default_json, DCPresets::default_jsonSize));
    jassertquiet (r.wasOk());
    return pm;
}

DaftChunkProcessor::DaftChunkProcessor()
    : OneKnobProcessor (makeConfig (loadBuiltInPresets()), std::make_unique<DaftChunkEngine>())
{
    startTimerHz (30);
}

DaftChunkProcessor::~DaftChunkProcessor()
{
    stopTimer();
    getDsp().setKit (nullptr);
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

void DaftChunkProcessor::timerCallback()
{
    takeAnalysis();
    freeRetired (false);
}

void DaftChunkProcessor::takeAnalysis()
{
    juce::String error;
    auto result = loader.takeResult (error);
    if (result == nullptr)
    {
        if (error.isNotEmpty() && error != juce::String::fromUTF8 ("Annul\xc3\xa9"))
        {
            status = error;
            sendChangeMessage();
        }
        return;
    }
    status = result->truncated ? juce::String (juce::String::fromUTF8 ("Coup\xc3\xa9 \xc3\xa0 60 s")) : juce::String();
    sample = std::shared_ptr<const LoadedSample> (std::move (result));
    taps.clear();
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
            p.filter = juce::jlimit (-8.0f, 0.0f, v[2].getFloatValue());
            p.start = juce::jlimit (0.0f, 0.9f, v[3].getFloatValue());
            p.end = juce::jlimit (0.0f, 0.9f, v[4].getFloatValue());
            p.reverse = v[5] == "1";
        }
        sliceParams.push_back (p);
    }
    samplePath = state.getProperty ("samplePath").toString();
    status = {};
    if (samplePath.isNotEmpty())
    {
        const juce::File f (samplePath);
        if (f.existsAsFile())
            loader.load (f);
        else
            status = "Fichier introuvable : " + f.getFileName();
    }
    sendChangeMessage();
}
} // namespace daftchunk
