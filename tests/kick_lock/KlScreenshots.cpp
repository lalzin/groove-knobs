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
#include <iostream>

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
};

juce::Image shoot (const Case& c, int& statusOut)
{
    using namespace kickbass;
    const double sr = 48000.0, bpm = 124.0, seconds = 3.2;
    kl::KickLockProcessor proc;
    proc.setNonRealtime (true);
    setParam (proc.getLockParameter(), c.lock ? 1.0f : 0.0f);
    setParam (proc.getLockModeParameter(), c.transition ? 1.0f : 0.0f);
    if (c.customCurve)
    {
        // a pumping curve drawn by hand: a fast dip, a hold, a late swell
        proc.setCustomCurve ({ { 0.0f, 0.0f, 0.0f }, { 0.3f, 0.15f, -4.0f }, { 0.75f, 0.85f, 3.0f }, { 1.0f, 1.0f, 0.0f } });
        setParam (proc.getShapeParameter(), (float) kl::KickLockDsp::custom);
    }
    proc.prepareToPlay (sr, 512);
    auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
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
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, juce::jmax (1.0f, c.scale / 2.0f));
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
