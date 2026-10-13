namespace gk
{
MascotComponent::MascotComponent (AudioFeatures& f, const Palette& p) : palette (p), features (f)
{
    setInterceptsMouseClicks (false, false);
    setAccessible (false);
}

void MascotComponent::setFrames (std::vector<juce::Image> frames)
{
    colourFrames = std::move (frames);
    greyFrames.clear();
    for (const auto& img : colourFrames)
    {
        // 40 % saturation: grey copy with the colour frame on top at 40 %.
        juce::Image grey (juce::Image::ARGB, img.getWidth(), img.getHeight(), true);
        juce::Graphics g (grey);
        auto desat = img.createCopy();
        desat.desaturate();
        g.drawImageAt (desat, 0, 0);
        g.setOpacity (0.4f);
        g.drawImageAt (img, 0, 0);
        greyFrames.push_back (grey);
    }
    scaledHeight = 0;
    repaint();
}

const juce::Image& MascotComponent::scaledFrame (int index, bool grey, int pixelHeight)
{
    if (pixelHeight != scaledHeight)
    {
        scaledHeight = pixelHeight;
        scaledColour.assign (colourFrames.size(), {});
        scaledGrey.assign (colourFrames.size(), {});
    }
    auto& cache = grey ? scaledGrey : scaledColour;
    auto& img = cache[(size_t) index];
    if (! img.isValid())
    {
        const auto& src = (grey ? greyFrames : colourFrames)[(size_t) index];
        const int h = juce::jlimit (16, src.getHeight(), pixelHeight);
        const int w = juce::roundToInt ((float) h * (float) src.getWidth() / (float) src.getHeight());
        img = h >= src.getHeight() ? src : src.rescaled (w, h, juce::Graphics::highResamplingQuality);
    }
    return img;
}

void MascotComponent::triggerDrop() noexcept
{
    const double bpm = juce::jmax (40.0, (double) features.bpm.load (std::memory_order_relaxed));
    joyRemaining = state.playing ? joyBeats * 60.0 / bpm : joySeconds;
    joyStartTime = state.time;
    joyStartBeats = beatsUi;
}

const juce::Image& MascotComponent::scaledTintedFrame (const std::vector<juce::Image>& tinted, int index, int pixelHeight)
{
    if (pixelHeight != tintedHeight || tintedSource != &tinted)
    {
        tintedHeight = pixelHeight;
        tintedSource = &tinted;
        scaledTinted.assign (tinted.size(), {});
    }
    auto& img = scaledTinted[(size_t) index];
    if (! img.isValid())
    {
        const auto& src = tinted[(size_t) index];
        const int h = juce::jlimit (16, src.getHeight(), pixelHeight);
        const int w = juce::roundToInt ((float) h * (float) src.getWidth() / (float) src.getHeight());
        img = h >= src.getHeight() ? src : src.rescaled (w, h, juce::Graphics::highResamplingQuality);
    }
    return img;
}

int MascotComponent::chooseFrame (float macro, float bypass, bool musicOn)
{
    return bypass >= 0.5f ? poses.sleep : moodFrame (macro, musicOn);
}

void MascotComponent::paintCharacter (juce::Graphics& g, juce::Rectangle<float> fig, const juce::AffineTransform& animation)
{
    drawFrame (g, state.frame, fig, animation, 1.0f);
}

void MascotComponent::drawFrame (juce::Graphics& g, int frame, juce::Rectangle<float> fig, const juce::AffineTransform& animation,
                                 float opacity, const std::vector<juce::Image>* tinted)
{
    // Physical pixels per component unit (editor scale × display density)
    const float physical = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int pixelHeight = juce::roundToInt (fig.getHeight() * physical * 1.1f / 8.0f) * 8;
    const auto& colour = tinted != nullptr ? scaledTintedFrame (*tinted, frame, pixelHeight) : scaledFrame (frame, false, pixelHeight);
    const auto& grey = scaledFrame (frame, true, pixelHeight);
    const auto place = juce::AffineTransform::scale (fig.getWidth() / (float) colour.getWidth(), fig.getHeight() / (float) colour.getHeight())
                           .translated (fig.getX(), fig.getY())
                           .followedBy (animation);
    const auto greyPlace = juce::AffineTransform::scale (fig.getWidth() / (float) grey.getWidth(), fig.getHeight() / (float) grey.getHeight())
                               .translated (fig.getX(), fig.getY())
                               .followedBy (animation);

    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    const float desat = 1.0f - palette.saturation; // 0 = colour, 0.6 = 40 % saturation
    if (desat < 0.6f)
    {
        g.setOpacity (opacity);
        g.drawImageTransformed (colour, place);
    }
    if (desat > 0.0f)
    {
        g.setOpacity (opacity * juce::jlimit (0.0f, 1.0f, desat / 0.6f));
        g.drawImageTransformed (grey, greyPlace);
    }
    g.setOpacity (1.0f);
}

int MascotComponent::moodFrame (float macro, bool musicOn) noexcept
{
    const int n = (int) colourFrames.size();
    auto frameAt = [n] (const std::vector<int>& set, int i)
    {
        if (set.empty() || n == 0)
            return 0;
        return juce::jlimit (0, n - 1, set[(size_t) juce::jlimit (0, (int) set.size() - 1, i)]);
    };
    auto cycle = [&] (const std::vector<int>& set, double step)
    {
        return set.empty() ? 0 : frameAt (set, (int) std::fmod (std::floor (step), (double) set.size()));
    };

    // The drop: its poses in order, one per beat (or every 0.45 s), last one holds
    if (joyRemaining > 0.0)
    {
        const double step = state.playing ? beatsUi - joyStartBeats : (state.time - joyStartTime) / 0.45;
        return frameAt (poses.drop, (int) std::floor (juce::jmax (0.0, step)));
    }

    // Which side of rest, with hysteresis around the centre zone
    constexpr float restZone = 0.08f, h = 0.03f;
    const float mag = std::abs (macro);
    if (side == 0 && mag > restZone + h)
        side = macro < 0.0f ? -1 : 1;
    else if (side != 0 && (mag < restZone - h || (macro < 0.0f) != (side < 0)))
        side = mag > restZone + h ? (macro < 0.0f ? -1 : 1) : 0;

    if (side == 0)
    {
        rung = 0;
        if (! musicOn)
            return juce::jlimit (0, juce::jmax (0, n - 1), poses.calm);
        // Dancing: next groove pose on every beat (every 0.5 s without transport)
        return cycle (poses.groove, state.playing ? beatsUi : state.time / 0.5);
    }

    const auto& ladder = side < 0 && ! poses.lowLadder.empty() ? poses.lowLadder : poses.highLadder;
    const int rungs = juce::jmax (1, (int) ladder.size());
    const float pos = juce::jlimit (0.0f, 0.999f, (mag - restZone) / (1.0f - restZone)) * (float) rungs;
    if (pos > (float) (rung + 1) + 0.15f || pos < (float) rung - 0.15f)
        rung = juce::jlimit (0, rungs - 1, (int) pos);
    return frameAt (ladder, rung);
}

void MascotComponent::snap (float amount, float bypass)
{
    state.amount = amount;
    state.bypass = bypass;
    side = 0;
    rung = 0;
    recentPeak = std::abs (amount);
    state.joy = joyRemaining > 0.0 ? 1.0f : 0.0f;
    state.frame = chooseFrame (amount, bypass, false);
    state.pop = state.squash = state.nod = state.sway = 0.0f;
    springX = springV = 0.0f;
    level = 0.0f;
    lastTransients = features.transientCount.load (std::memory_order_relaxed);
    beatsUi = features.beats.load (std::memory_order_relaxed);
    initialised = true;
    repaint();
}

void MascotComponent::advance (double dt, float amount, float bypass)
{
    if (! initialised)
        snap (amount, bypass);

    dt = juce::jlimit (0.0, 0.1, dt);
    const auto fdt = (float) dt;
    state.time += dt;
    state.amount = amount;
    state.bypass = bypass;

    // Drop detection: the knob was far from rest and comes back quickly
    const float mag = std::abs (amount);
    recentPeak = juce::jmax (mag, recentPeak * std::exp (-fdt / 1.2f));
    if (bypass < 0.5f && joyRemaining <= 0.0 && mag < 0.2f && recentPeak > 0.6f)
    {
        triggerDrop();
        recentPeak = mag;
    }
    if (joyRemaining > 0.0)
        joyRemaining = juce::jmax (0.0, joyRemaining - dt);
    state.joy += ((joyRemaining > 0.0 ? 1.0f : 0.0f) - state.joy) * (1.0f - std::exp (-fdt / 0.15f));

    // Pose (+ pop when it changes)
    const bool musicOn = level > 0.05f;
    const int target = chooseFrame (amount, bypass, musicOn);
    if (target != state.frame)
    {
        state.frame = target;
        state.pop = 1.0f;
        springV -= 2.5f; // a little stretch on pose change
    }
    state.pop *= std::exp (-fdt / 0.12f);

    // Breath from RMS: fast attack, slow release, mapped from -42..-6 dBFS
    const float rms = features.rms.load (std::memory_order_relaxed);
    const float target01 = dspmath::clamp01 ((dspmath::gainToDb (rms) + 42.0f) / 36.0f);
    const float coeff = std::exp (-fdt / (target01 > level ? 0.03f : 0.25f));
    level = target01 + (level - target01) * coeff;
    const float idle = 0.5f + 0.5f * (float) std::sin (state.time * 2.0 * dspmath::pi * (bypass > 0.5f ? 0.18 : 0.25));
    state.breath = level > 0.02f ? level : 0.35f * idle;

    // Transients: damped spring (≈ 150 ms back to rest)
    const auto count = features.transientCount.load (std::memory_order_relaxed);
    if (count != lastTransients && bypass < 0.5f)
        springV += 9.0f * (0.6f + 0.4f * amount);
    lastTransients = count;
    {
        const float w = 2.0f * juce::MathConstants<float>::pi * 5.0f, zeta = 0.45f;
        const int sub = 4;
        for (int i = 0; i < sub; ++i)
        {
            const float h = fdt / (float) sub;
            springV += (-w * w * springX - 2.0f * zeta * w * springV) * h;
            springX += springV * h;
        }
        state.squash = juce::jlimit (-0.18f, 0.22f, springX);
    }

    // Beat position: follow the audio thread, interpolate between blocks
    state.playing = features.playing.load (std::memory_order_relaxed);
    const double hostBeats = features.beats.load (std::memory_order_relaxed);
    const double bpm = features.bpm.load (std::memory_order_relaxed);
    beatsUi += dt * bpm / 60.0;
    if (std::abs (beatsUi - hostBeats) > 0.25)
        beatsUi = hostBeats;
    state.beatPhase = (float) dspmath::frac (beatsUi);
    const float openness = 1.0f - dspmath::smoothstep (0.2f, 0.8f, mag);
    const float nodTarget = state.playing && bypass < 0.5f ? (0.35f + 0.65f * juce::jmax (openness, state.joy)) * std::exp (-6.0f * state.beatPhase) : 0.0f;
    state.nod += (nodTarget - state.nod) * (1.0f - std::exp (-fdt / 0.02f));

    // Sway with the engine LFO above 80 %
    state.modPhase = features.modulationPhase.load (std::memory_order_relaxed);
    const float swayAmount = macro::lateOnset (mag, 0.8f) * (1.0f - bypass);
    state.sway = swayAmount * (float) std::sin (2.0 * dspmath::pi * state.modPhase);

    repaint();
}

juce::Rectangle<float> MascotComponent::figureArea() const
{
    if (colourFrames.empty())
        return getLocalBounds().toFloat();
    const auto& img = colourFrames.front();
    const auto b = getLocalBounds().toFloat().reduced (0.0f, 4.0f);
    const float h = b.getHeight() - 6.0f;
    const float w = h * (float) img.getWidth() / (float) img.getHeight();
    return juce::Rectangle<float> (w, h).withCentre ({ b.getCentreX(), b.getY() + h * 0.5f });
}

void MascotComponent::paint (juce::Graphics& g)
{
    const auto fig = figureArea();
    const auto& s = state;
    paintBehind (g, s, fig);

    // Ground shadow
    const float squashX = 1.0f + 0.5f * s.squash;
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (juce::Rectangle<float> (fig.getWidth() * 0.62f * squashX, 9.0f).withCentre ({ fig.getCentreX(), fig.getBottom() - 2.0f }));

    if (colourFrames.empty())
        return;

    // Animation transform, pivot at the feet
    const float breathe = 0.03f * s.breath; // up to +3 % with the music
    const float amp = 2.0f + 4.0f * s.joy;
    const float sy = 1.0f - s.squash + breathe + 0.05f * s.pop;
    const float sx = 1.0f + 0.5f * s.squash + 0.5f * breathe + 0.02f * s.pop;
    const float tilt = 0.06f * s.sway - 0.08f * s.bypass;
    const juce::Point<float> feet (fig.getCentreX(), fig.getBottom());
    const auto t = juce::AffineTransform::translation (-feet.x, -feet.y)
                       .scaled (sx, sy)
                       .rotated (tilt)
                       .translated (feet.x, feet.y + amp * s.nod);

    paintCharacter (g, fig, t);

    // Sleep: floating "Zzz"
    if (showSleepZ && s.bypass > 0.01f)
    {
        for (int i = 0; i < 3; ++i)
        {
            const float ph = (float) dspmath::frac (s.time * 0.35 + i / 3.0);
            const float x = fig.getRight() + 4.0f + 22.0f * ph + 4.0f * std::sin (ph * 6.0f);
            const float y = fig.getY() + fig.getHeight() * 0.30f - 44.0f * ph;
            const float size = 16.0f + 12.0f * ph;
            const auto area = juce::Rectangle<float> (size * 1.5f, size * 1.4f).withCentre ({ x, y });
            g.setFont (GrooveFonts::font (size));
            g.setColour (palette (palette.outline).withAlpha (s.bypass * (1.0f - ph) * 0.8f));
            g.drawText ("Z", area.translated (1.5f, 2.0f), juce::Justification::centred);
            g.setColour (palette (palette.text).withAlpha (s.bypass * (1.0f - 0.8f * ph)));
            g.drawText ("Z", area, juce::Justification::centred);
        }
    }

    paintOverlay (g, s, fig);
}
} // namespace gk
