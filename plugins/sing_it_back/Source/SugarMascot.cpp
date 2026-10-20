#include "SugarMascot.h"
#include <SingItBackArt.h>

namespace sib
{
using gk::dspmath::smoothstep;

namespace
{
    const juce::Colour orange (0xffff8a3d), teal (0xff1fb5a8), plum (0xff5b2a86), cream (0xfff3e9d2);
}

SugarMascot::SugarMascot (SingItBackProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal), proc (p)
{
    setFrames (loadFrames());
    showSleepZ = false; // the sleeping poses have their own "Zzz"
    motion = 0.3f;      // a diva, not a bouncing ball (Alan: "trop d'animation")
    // a quaver: head + stem + flag
    note.addEllipse (-4.0f, -2.5f, 6.5f, 5.0f);
    note.addRectangle (1.6f, -13.0f, 1.4f, 11.5f);
    note.startNewSubPath (3.0f, -13.0f);
    note.quadraticTo (7.0f, -10.0f, 6.0f, -5.5f);
    note.quadraticTo (5.5f, -8.5f, 3.0f, -9.5f);
    note.closeSubPath();
}

std::vector<juce::Image> SugarMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "sugar_" + juce::String (i) + "_png";
        const char* data = SIBArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

int SugarMascot::chooseFrame (float crowd, float bypass, bool musicOn)
{
    juce::ignoreUnused (musicOn);
    const auto& s = getState();
    const double dt = juce::jlimit (0.0, 0.1, s.time - lastTime);
    lastTime = s.time;
    auto& tel = proc.getDsp().getTelemetry();
    const bool snapshot = dt <= 0.0;

    // The crowd: each voice's gain (in / dropout) and what it sings now
    const int voicesMax = proc.getVoicesParameter().getIndex() == 0 ? 4 : proc.getVoicesParameter().getIndex() == 2 ? 12 : 8;
    float answer = 0.0f;
    for (int k = 0; k < SingItBackDsp::maxVoices; ++k)
    {
        auto& sg = singers[(size_t) k];
        float gain = tel.voiceGain[(size_t) k].load (std::memory_order_relaxed);
        float level = juce::jlimit (0.0f, 1.0f, 6.0f * tel.voiceLevel[(size_t) k].load (std::memory_order_relaxed));
        if (preview)
        {
            gain = juce::jlimit (0.0f, 1.0f, SingItBackDsp::audibleVoices (crowd, voicesMax) - (float) k) * (k < voicesMax ? 1.0f : 0.0f);
            // with RÉPONSE the crowd sings while she is silent
            const bool crowdSings = previewSinging != (proc.getResponseParameter().getIndex() > 0);
            level = crowdSings ? gain * (0.55f + 0.4f * (float) ((k * 7) % 5) / 4.0f) : 0.0f;
        }
        const float a = snapshot ? 1.0f : 1.0f - std::exp (-(float) dt / 0.06f);
        sg.gain += (gain - sg.gain) * a;
        sg.level += (level - sg.level) * (level > sg.level ? a * 2.0f : a * 0.5f);
        sg.bob += (float) dt * (1.6f + 0.15f * (float) k); // a gentle sway, not a jig
        answer = juce::jmax (answer, sg.level * sg.gain);
    }
    crowdAnswer = answer;

    // Her voice: syllables (voiced onsets) drive the lip-sync
    const bool voiced = preview ? previewSinging : tel.voicedFlag.load (std::memory_order_relaxed) && bypass < 0.5f;
    const int beat = (int) std::floor (s.time * juce::jmax (40.0, (double) getFeatures().bpm.load()) / 60.0);
    if (! preview && voiced && ! wasVoiced)
    {
        ++syllables;
        beatAtSyllable = beat;
        if (s.time - lastVoiced > 0.6)
            phraseStart = s.time;
    }
    if (! preview && ! voiced && wasVoiced && phraseStart >= 0.0 && s.time - phraseStart > 1.5 && crowd > 0.7f)
        heartUntil = s.time + 0.9; // after a long phrase at the top: thank you
    wasVoiced = voiced;
    if (voiced)
        lastVoiced = s.time;

    if (numFrames() <= asleepLying)
        return 0;

    // Bypass: asleep (sitting, then lying down)
    if (bypass >= 0.5f)
    {
        if (bypassSince < 0.0)
            bypassSince = s.time;
        if (snapshot)
            return asleepLying;
        return s.time - bypassSince < 1.2 ? asleepSitting : asleepLying;
    }
    bypassSince = -1.0;

    // The pose: a "mood" (singing at an energy, answered, waiting, ...) and,
    // within it, a pose held at least ~1.5 s (singing) or ~3 s (waiting):
    // she moves with the phrases, not with every syllable
    const bool singing = voiced || (! preview && s.time - lastVoiced < 0.6); // word gaps keep her singing
    int mood = 0;
    if (singing)
        mood = 1 + tierFor (crowd);
    else if (s.time < heartUntil)
        mood = 10;
    else if (crowdAnswer > 0.15f)
        mood = 11;
    else
        mood = crowd >= 0.5f ? 12 : 13;
    const double hold = singing ? 1.5 : 3.0;
    if (mood != currentMood)
    {
        currentMood = mood;
        poseStep = preview ? syllables : 0;
        poseSince = s.time;
    }
    else if (! snapshot && s.time - poseSince > hold)
    {
        ++poseStep;
        poseSince = s.time;
    }
    const int step = poseStep;
    switch (mood)
    {
        case 1: return armOut + step % 4;
        case 2: { static const int poses[] { pointingA, pointingB + 1, pointingA + 2, pointingB, pointingA + 1, pointingB + 2, pointingA + 3, pointingB + 3 };
                  return poses[step % 8]; }
        case 3: { // the top: arm up, and a jump now and then
                  static const int poses[] { pointingA, jumpA, pointingB + 1, pointingA + 2, jumpWink, pointingB };
                  return poses[step % 6]; }
        case 10: return heart;
        case 11: return inviting + (step & 1);
        case 12: return inviting + 2 + (step & 1);
        default: break;
    }
    const double blink = std::fmod (s.time, 4.3);
    if (! snapshot && blink < 0.16)
        return calmB + 1; // eyes closed
    static const int calm[] { calmA, calmA + 2, calmB, calmA + 1 };
    return calm[step % 4];
}

void SugarMascot::paintBehind (juce::Graphics& g, const State& s, juce::Rectangle<float> fig)
{
    // A stage spotlight on her: warmer and wider with the crowd
    const float awake = 1.0f - s.bypass;
    const float spot = (0.25f + 0.55f * s.amount) * awake;
    if (spot <= 0.01f)
        return;
    const float top = (float) getLocalBounds().getY();
    juce::Path cone;
    const float cx = fig.getCentreX(), baseW = fig.getWidth() * (0.55f + 0.35f * s.amount);
    cone.startNewSubPath (cx - 10.0f, top);
    cone.lineTo (cx + 10.0f, top);
    cone.lineTo (cx + baseW * 0.5f, fig.getBottom());
    cone.lineTo (cx - baseW * 0.5f, fig.getBottom());
    cone.closeSubPath();
    g.setGradientFill (juce::ColourGradient (palette (cream).withAlpha (0.0f), cx, top, palette (orange).withAlpha (0.22f * spot), cx, fig.getBottom(), false));
    g.fillPath (cone);
    g.setColour (palette (orange).withAlpha (0.25f * spot));
    g.fillEllipse (juce::Rectangle<float> (baseW, 14.0f).withCentre ({ cx, fig.getBottom() - 4.0f }));
}

juce::Rectangle<float> SugarMascot::singerArea (int voice, int count, juce::Rectangle<float> fig) const noexcept
{
    // Left / right of her, alternately; front row (bigger, lower) first
    const auto b = getLocalBounds().toFloat();
    const int perSide = (count + 1) / 2, slot = voice / 2;
    const bool left = (voice & 1) == 0;
    const int perRow = juce::jmax (1, (perSide + 1) / 2);
    const int row = slot / perRow, col = slot % perRow;
    const float sideW = left ? fig.getX() - b.getX() : b.getRight() - fig.getRight();
    const float size = (row == 0 ? 34.0f : 27.0f) * juce::jlimit (0.75f, 1.0f, sideW / (float) (perRow * 34));
    const float colW = juce::jmax (size, (sideW - 6.0f) / (float) perRow);
    // the front row stands nearest to her; the back row is staggered
    const float fromEdge = 4.0f + colW * ((float) col + (row == 0 ? 0.0f : 0.5f)) + colW * 0.5f;
    const float x = juce::jlimit (b.getX() + size * 0.6f, b.getRight() - size * 0.6f, left ? fig.getX() - fromEdge : fig.getRight() + fromEdge);
    const float y = b.getBottom() - (row == 0 ? 2.0f : 26.0f);
    return juce::Rectangle<float> (size, size * 1.15f).withCentre ({ x, y - size * 0.575f });
}

void SugarMascot::paintOverlay (juce::Graphics& g, const State& s, juce::Rectangle<float> fig)
{
    const int choice = proc.getVoicesParameter().getIndex();
    const int count = choice == 0 ? 4 : choice == 2 ? 12 : 8;
    const float awake = 1.0f - s.bypass;
    // the style shows in the crowd: children small, giants tall, robots with
    // square heads and antennas, gospel in orange robes, the choir with collars,
    // the stadium with arms up
    const int st = proc.getStyleParameter().getIndex();
    const float sizeMul = st == SingItBackDsp::kids ? 0.75f : st == SingItBackDsp::giants ? 1.18f : 1.0f;
    const juce::Colour robe = st == SingItBackDsp::gospel ? orange.darker (0.35f) : st == SingItBackDsp::robots ? juce::Colour (0xff6d7480) : plum;
    const float armsUpAt = st == SingItBackDsp::stadium ? 0.15f : 0.35f;
    // back row first (drawn behind the front one)
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < count; ++k)
        {
            const int perSide = (count + 1) / 2, perRow = juce::jmax (1, (perSide + 1) / 2);
            const int row = (k / 2) / perRow;
            if ((pass == 0) != (row == 1))
                continue;
            const auto& sg = singers[(size_t) k];
            const float present = sg.gain;
            if (present < 0.02f && s.bypass < 0.5f)
                continue;
            auto r = singerArea (k, count, fig);
            r = r.withSizeKeepingCentre (r.getWidth() * sizeMul, r.getHeight() * sizeMul).withBottomY (r.getBottom());
            const float sing = sg.level * awake;
            // bob with what it sings, sink on a dropout
            const float bob = (0.8f + 1.4f * sing) * std::sin (sg.bob) * awake;
            r = r.translated (0.0f, -bob + (1.0f - juce::jmin (1.0f, present * 1.4f)) * r.getHeight() * 0.35f);
            const float alpha = s.bypass > 0.5f ? 0.25f : juce::jlimit (0.0f, 1.0f, 0.35f + 0.65f * present);

            const float head = r.getWidth() * 0.42f;
            const auto headR = juce::Rectangle<float> (head, head).withCentre ({ r.getCentreX(), r.getY() + head * 0.55f });
            juce::Path body;
            body.addRoundedRectangle (r.getX() + r.getWidth() * 0.08f, headR.getBottom() + 1.0f, r.getWidth() * 0.84f, r.getBottom() - headR.getBottom(),
                                      r.getWidth() * 0.3f, r.getWidth() * 0.3f, true, true, false, false);
            if (st == SingItBackDsp::robots)
            {
                body.addRoundedRectangle (headR.reduced (head * 0.04f), head * 0.15f);
                body.addRectangle (headR.getCentreX() - 0.7f, headR.getY() - head * 0.35f, 1.4f, head * 0.35f);
                body.addEllipse (juce::Rectangle<float> (head * 0.22f, head * 0.22f).withCentre ({ headR.getCentreX(), headR.getY() - head * 0.38f }));
            }
            else
                body.addEllipse (headR);
            // a raised arm when the crowd is big and singing loud
            if (s.amount > 0.6f && sing > armsUpAt)
            {
                const bool leftSide = (k & 1) == 0;
                const float ax = leftSide ? r.getRight() - r.getWidth() * 0.2f : r.getX() + r.getWidth() * 0.2f;
                juce::Path arm;
                arm.startNewSubPath (ax, headR.getBottom() + 4.0f);
                arm.lineTo (ax + (leftSide ? 1.0f : -1.0f) * r.getWidth() * 0.18f, r.getY() - r.getHeight() * 0.15f - 3.0f * sing);
                g.setColour (palette (robe).darker (0.3f).withAlpha (alpha));
                g.strokePath (arm, juce::PathStrokeType (r.getWidth() * 0.13f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (palette (robe).darker (row == 0 ? 0.35f : 0.6f).withAlpha (alpha));
            g.fillPath (body);
            if (st == SingItBackDsp::choir) // white collar
            {
                juce::Path collar;
                collar.addTriangle (headR.getCentreX() - head * 0.45f, headR.getBottom() + 1.0f, headR.getCentreX() + head * 0.45f, headR.getBottom() + 1.0f,
                                    headR.getCentreX(), headR.getBottom() + head * 0.45f);
                g.setColour (palette (cream).withAlpha (alpha * 0.9f));
                g.fillPath (collar);
            }
            // rim light from the stage, brighter as it sings
            g.setColour (palette (teal).withAlpha (alpha * (0.25f + 0.75f * sing)));
            g.drawEllipse (headR.reduced (0.5f), 1.4f);
            g.setColour (palette (juce::Colour (0xff141414)).withAlpha (alpha * 0.8f));
            g.strokePath (body, juce::PathStrokeType (1.2f));
            // singing: a little open mouth and a note floating up
            if (sing > 0.12f)
            {
                g.setColour (palette (orange).withAlpha (alpha * smoothstep (0.12f, 0.4f, sing)));
                g.fillEllipse (juce::Rectangle<float> (head * 0.28f, head * 0.12f + head * 0.18f * sing).withCentre (headR.getCentre().translated (0.0f, head * 0.18f)));
                const float rise = (float) std::fmod (s.time * 0.8 + k * 0.37, 1.0);
                g.setColour (palette (k % 3 == 0 ? orange : k % 3 == 1 ? teal : cream).withAlpha (alpha * sing * (1.0f - rise)));
                g.fillPath (note, juce::AffineTransform::scale (0.6f + 0.3f * sing)
                                      .translated (headR.getCentreX() + ((k & 1) ? -1.0f : 1.0f) * head * 0.6f, headR.getY() - 4.0f - 22.0f * rise));
            }
        }
}
} // namespace sib
