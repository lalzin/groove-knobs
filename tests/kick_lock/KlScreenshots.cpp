// kl_screenshots — renders the Kick Lock editor offscreen, one capture per
// status, each after real audio through the plugin (the engine's own
// telemetry, nothing faked): OK (LOCK on), Décalé (LOCK off, the kick off
// the sub's phase), Instable (a noise sub), Sub faible, Somme en perte (the
// right channel inverted), Pas de kick (no sidechain), bypass; MODE
// Transition; 200 %.
//   kl_screenshots [out-dir]   (default: screenshots/kick_lock)
// Writes contact_sheet.png for review.

#include "KickLockEditor.h"
#include "KickBass.h"
#include "KickSendProcessor.h"
#include "KickSendEditor.h"
#include <iostream>
#include "WebAssets.h"

namespace
{
void setParam (juce::RangedAudioParameter& p, float plain) { p.setValueNotifyingHost (p.convertTo0to1 (plain)); }

bool writePng (const juce::Image& img, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream os (file);
    return os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os);
}

struct Case
{
    juce::String slug, label;
    bool lock = true, sidechain = true, noise = false, invertRight = false, bypass = false, transition = false;
    float level = 1.0f, scale = 1.0f;
    double kickOffset = 0.0;
    int kicks = 1; // the mascot's step (its pose in the dance)
    bool customCurve = false, stopped = false;
    int explain = -1; // the scope explaining TARGET / OFFSET / SUB CUT / WAVE START
    bool precise = true, phase = true, pad = false; // pad: a chord synth, no bass (a plain sidechain)
    int monoBelow = 0;
};

juce::Image shoot (const Case& c, int& statusOut)
{
    using namespace kickbass;
    const double sr = 48000.0, bpm = 124.0, seconds = 3.2;
    kl::KickLockProcessor proc;
    proc.setNonRealtime (true);
    setParam (proc.getLockParameter(), c.lock ? 1.0f : 0.0f);
    setParam (proc.getLockModeParameter(), c.transition ? 1.0f : 0.0f);
    setParam (proc.getPreciseParameter(), c.precise ? 1.0f : 0.0f);
    setParam (proc.getPhaseParameter(), c.phase ? 1.0f : 0.0f);
    setParam (proc.getMonoBelowParameter(), (float) c.monoBelow);
    if (c.sidechain) // the kick fed through the sidechain bus here (in a song: Kick Lock Send)
        setParam (proc.getTriggerParameter(), (float) kl::KickLockDsp::triggerSidechain);
    if (c.customCurve)
    {
        // a pumping curve drawn by hand: a fast dip, a hold, a late swell
        proc.setCustomCurve ({ { 0.0f, 0.0f, 0.0f }, { 0.3f, 0.15f, -4.0f }, { 0.75f, 0.85f, 3.0f }, { 1.0f, 1.0f, 0.0f } });
        setParam (proc.getShapeParameter(), (float) kl::KickLockDsp::custom);
    }
    proc.prepareToPlay (sr, 512);
    auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
    if (c.pad)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            // a chord (A minor, saw-ish), nothing under 200 Hz
            const double t = i / sr;
            double v = 0.0;
            for (double f : { 220.0, 261.6, 329.6, 440.0 })
                for (int h = 1; h <= 4; ++h)
                    v += 0.06 / h * std::sin (juce::MathConstants<double>::twoPi * f * h * t + h);
            b.setSample (0, i, (float) v);
            b.setSample (1, i, (float) v);
        }
    if (c.noise)
    {
        gk::testsignals::Rng rng (3);
        gk::TptSvf lp;
        const auto co = gk::TptSvf::Coeffs::make (80.0, sr, 0.7);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = (float) (2.0 * lp.processLowpass (rng.bipolar(), co));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
    }
    b.applyGain (c.level);
    if (c.invertRight)
        b.applyGain (1, 0, b.getNumSamples(), -1.0f);
    const auto k = kicks (seconds, sr, bpm, 0.1 + c.kickOffset);
    run (proc, b, c.sidechain ? &k : nullptr, { 512 }, bpm, {}, ! c.stopped);
    statusOut = proc.getDsp().getTelemetry().status.load();
    if (c.bypass)
        proc.getBypassParameterObject().setValueNotifyingHost (1.0f);
    proc.editorScale.store (juce::jmin (2.0f, c.scale));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<kl::KickLockEditor*> (ed.get());
    editor->setPreviewStatus (statusOut, c.kicks);
    if (c.explain >= 0)
    {
        setParam (proc.getTargetPhaseParameter(), 90.0f);
        setParam (proc.getOffsetParameter(), 4.0f);
        setParam (proc.getWaveStartParameter(), 60.0f);
        editor->setPreviewExplanation (c.explain);
    }
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, juce::jmax (1.0f, c.scale / 2.0f));
}

// Kick Lock Send, as on the kick's track: kicks coming in, a Kick Lock
// listening on its LINK (the status says LINKED), PUNCH a little up
juce::Image shootSend (float scale)
{
    using namespace kickbass;
    const double sr = 48000.0, bpm = 124.0, seconds = 2.0;
    kl::KickLink::removeBlock();
    kls::KickSendProcessor send;
    send.setNonRealtime (true);
    setParam (send.getAmountParameter(), 0.68f); // PUNCH a little up (0.5 = centre)
    send.prepareToPlay (sr, 512);
    kl::KickLockProcessor lock;
    lock.setNonRealtime (true);
    lock.prepareToPlay (sr, 512);
    gk::OfflinePlayHead ph;
    ph.bpm = bpm;
    ph.sampleRate = sr;
    ph.playing = true;
    send.setPlayHead (&ph);
    lock.setPlayHead (&ph);
    auto k = kicks (seconds, sr, bpm, 0.1);
    auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
    juce::MidiBuffer midi;
    for (int pos = 0; pos + 512 <= k.getNumSamples(); pos += 512)
    {
        ph.samplePosition = pos;
        juce::AudioBuffer<float> kv (k.getArrayOfWritePointers(), 2, pos, 512), bv (b.getArrayOfWritePointers(), 2, pos, 512);
        send.processBlock (kv, midi);
        lock.processBlock (bv, midi);
    }
    send.setPlayHead (nullptr);
    lock.setPlayHead (nullptr);
    send.editorScale.store (juce::jmin (2.0f, scale));
    std::unique_ptr<juce::AudioProcessorEditor> ed (send.createEditor());
    if (auto* e = dynamic_cast<kls::KickSendEditor*> (ed.get()))
        e->refreshNow(); // LINKED: the Kick Lock above has just read it
    return ed->createComponentSnapshot (ed->getLocalBounds(), true, juce::jmax (1.0f, scale / 2.0f));
}

juce::File repoRoot()
{
    for (auto d = juce::File::getCurrentWorkingDirectory(); d.exists() && ! d.isRoot(); d = d.getParentDirectory())
        if (d.getChildFile ("plugins").isDirectory() && d.getChildFile ("modules/groove_core").isDirectory())
            return d;
    return juce::File::getCurrentWorkingDirectory();
}

// --web: visuals for lalzin.com (web-assets/kick-lock)
int runWeb (const juce::File& out)
{
    std::vector<gkweb::Shot> shots;
    auto shot = [&] (const char* name, Case c) { int st = -1; shots.push_back ({ name, shoot (c, st) }); };
    { Case c; c.slug = "hero"; c.scale = 3.0f; c.kicks = 1; shot ("hero", c); }
    { Case c; c.slug = "curve"; c.scale = 2.0f; c.customCurve = true; shot ("curve", c); }
    { Case c; c.slug = "explain"; c.scale = 2.0f; c.explain = 0; shot ("explain", c); }
    // 0.1.5: LIVE (the phase at the kick), the plain sidechain (PHASE off, a
    // chord: the whole signal), MONO BELOW set
    { Case c; c.slug = "live"; c.scale = 2.0f; c.precise = false; shot ("live", c); }
    { Case c; c.slug = "sidechain"; c.scale = 2.0f; c.phase = false; c.pad = true; c.lock = false; shot ("sidechain", c); }
    { Case c; c.slug = "mono"; c.scale = 2.0f; c.monoBelow = 4; c.kicks = 3; shot ("mono", c); }
    gkweb::PluginInfo info;
    info.slug = "kick-lock";
    info.name = "Kick Lock";
    info.taglineFr = juce::String::fromUTF8 ("Le sidechain qui cale le sub sur chaque kick");
    info.taglineEn = "The sidechain that locks the sub on every kick";
    info.palette = kl::KickLockEditor::makePalette();
    info.assetsDir = repoRoot().getChildFile ("plugins/kick_lock/Assets");
    info.framePrefix = "phaze_";
    info.heroFrame = 13;
    for (int i = 0; i < 21; ++i)
        info.poses.push_back (i);
    gkweb::writePlugin (info, shots, out);
    gkweb::writePng (shootSend (3.0f), out.getChildFile ("kick-lock/ui-send@3x.png"));
    // (the suite visuals, Kick Lock included with its Send on the banner:
    // sib_screenshots --web, after this)
    return 0;
}

const char* statusName (int s)
{
    static const char* names[] { "OK", "Decale", "Instable", "Sub faible", "Pas de kick", "Somme en perte" };
    return juce::isPositiveAndBelow (s, 6) ? names[s] : "?";
}
} // namespace

int main (int argc, char** argv)
{
#if JUCE_WINDOWS
    _putenv_s ("GK_NO_UPDATE_CHECK", "1");
#else
    setenv ("GK_NO_UPDATE_CHECK", "1", 1);
#endif
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc > 1 && juce::String (argv[1]) == "--web")
        return runWeb (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "web-assets"));
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/kick_lock");
    outDir.createDirectory();

    std::vector<Case> cases;
    { Case c; c.slug = "ok"; c.label = "LOCK on"; c.kicks = 1; cases.push_back (c); }
    { Case c; c.slug = "ok_cheer"; c.label = "LOCK on (4th kick)"; c.kicks = 3; cases.push_back (c); }
    { Case c; c.slug = "shifted"; c.label = "LOCK off, kick 6 ms off"; c.lock = false; c.kickOffset = 0.006; cases.push_back (c); }
    { Case c; c.slug = "unstable"; c.label = "noise sub"; c.noise = true; cases.push_back (c); }
    { Case c; c.slug = "weak_sub"; c.label = "quiet bass"; c.level = 0.0008f; cases.push_back (c); }
    { Case c; c.slug = "mono_loss"; c.label = "right channel inverted"; c.invertRight = true; cases.push_back (c); }
    { Case c; c.slug = "no_kick"; c.label = "no sidechain: the tempo"; c.sidechain = false; cases.push_back (c); }
    { Case c; c.slug = "stopped"; c.label = "no sidechain, host stopped"; c.sidechain = false; c.stopped = true; cases.push_back (c); }
    { Case c; c.slug = "bypass"; c.label = "bypass"; c.bypass = true; cases.push_back (c); }
    { Case c; c.slug = "transition"; c.label = "MODE Transition"; c.transition = true; c.kicks = 2; cases.push_back (c); }
    { Case c; c.slug = "explain_target"; c.label = "TARGET 90 deg explained"; c.explain = 0; cases.push_back (c); }
    { Case c; c.slug = "explain_offset"; c.label = "OFFSET +4 ms explained"; c.explain = 1; cases.push_back (c); }
    { Case c; c.slug = "explain_subcut"; c.label = "SUB CUT explained"; c.explain = 2; cases.push_back (c); }
    { Case c; c.slug = "explain_wavestart"; c.label = "WAVE START 60 deg explained"; c.explain = 3; cases.push_back (c); }
    { Case c; c.slug = "custom_curve"; c.label = "custom ducking curve"; c.customCurve = true; cases.push_back (c); }

    constexpr int w = 760, h = 480, pad = 12, label = 28, cols = 3;
    juce::Image sheet (juce::Image::RGB, cols * (w + pad) + pad, (int) ((cases.size() + cols - 1) / cols) * (h + pad + label) + pad, true);
    juce::Graphics g (sheet);
    g.fillAll (juce::Colour (0xff101012));
    for (size_t i = 0; i < cases.size(); ++i)
    {
        int st = -1;
        const auto img = shoot (cases[i], st);
        writePng (img, outDir.getChildFile (cases[i].slug + ".png"));
        const int x = pad + (int) (i % cols) * (w + pad), y = pad + (int) (i / cols) * (h + pad + label);
        g.setColour (juce::Colours::white);
        g.setFont (gk::GrooveFonts::font (18.0f));
        g.drawText (cases[i].label + "  ->  status " + statusName (st), x, y, w, label, juce::Justification::centredLeft);
        g.drawImageAt (img, x, y + label);
        std::cout << cases[i].slug << ": status " << statusName (st) << "\n";
    }
    writePng (sheet, outDir.getChildFile ("contact_sheet.png"));
    { Case c; c.slug = "scale_200"; c.scale = 2.0f; int st = -1; writePng (shoot (c, st), outDir.getChildFile ("scale_200.png")); }
    {
        // Kick Lock Send, on a kick
        kls::KickSendProcessor send;
        send.prepareToPlay (48000.0, 512);
        const auto k = kickbass::kicks (2.0, 48000.0, 124.0, 0.1);
        kickbass::run (send, k, nullptr, { 512 }, 124.0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (send.createEditor());
        auto* editor = dynamic_cast<gk::OneKnobEditor*> (ed.get());
        editor->snapForSnapshot();
        writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), outDir.getChildFile ("send.png"));
    }
    std::cout << "wrote " << outDir.getFullPathName() << "\n";
    return 0;
}
