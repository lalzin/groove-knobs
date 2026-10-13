#pragma once

namespace gk
{
/** Animated mascot built from a strip of bitmap poses.

    Mood model (the knob is a filter: moving it away from rest builds
    tension, coming back quickly is the release — the "drop"):
      - at rest + music      → dancing (the groove poses, one per beat)
      - at rest, silence     → calm
      - low side (bipolar)   → walks down the low ladder (e.g. muffled → sad)
      - high side            → walks up the high ladder (e.g. hyped → ears covered)
      - quick return to rest → the drop: the drop poses in sequence, one per beat
      - bypass               → asleep
    Continuous animation on top, all smoothed:
      - pop         small squash & stretch each time the pose changes;
      - breath      body scale ±3 % following the input RMS (slow idle breath
                    when nothing plays);
      - bounce      transient → damped spring squash, back in ~150 ms;
      - nod         dip on every beat while the host plays (PPQ + BPM);
      - sway        rotation locked to the engine LFO above 80 %;
      - sleep       bypass: first pose, tilted, desaturated, floating "Zzz".
    Audio data comes from AudioFeatures (lock-free atomics). The editor calls
    advance() at frame rate; only this component is repainted. */
class MascotComponent : public juce::Component
{
public:
    /** Which frames play which role (indices into the frame strip). */
    struct Poses
    {
        std::vector<int> groove { 0 };   // at rest with music, cycled on the beat
        int calm = 0;                    // at rest, no music
        std::vector<int> lowLadder;      // low side (bipolar), from just off-centre to the end
        std::vector<int> highLadder;     // high side (or the whole travel when unipolar)
        std::vector<int> drop;           // the drop, in order; the last one holds
        int sleep = 0;                   // bypass
    };

    struct State
    {
        float amount = 0.0f, bypass = 0.0f, joy = 0.0f;
        float breath = 0.0f, squash = 0.0f, nod = 0.0f, sway = 0.0f, pop = 0.0f;
        float beatPhase = 0.0f, modPhase = 0.0f;
        double time = 0.0;
        int frame = 0;
        bool playing = false;
    };

    MascotComponent (AudioFeatures& features, const Palette& palette);

    /** Frames in pose order. Desaturated copies are prepared here. */
    void setFrames (std::vector<juce::Image> frames);
    void setPoses (Poses p) { poses = p; }

    /** Starts the "drop" euphoria (normally detected from the knob). */
    void triggerDrop() noexcept;

    /** Advances the animation by dt seconds (message thread). `macro` is the
        signed knob position: -1..1 (bipolar) or 0..1. */
    void advance (double dt, float macro, float bypass);

    /** Jumps every smoothed value to its target (screenshots, first frame). */
    void snap (float macro, float bypass);

    const State& getState() const noexcept { return state; }

    void paint (juce::Graphics&) override;

protected:
    /** Which frame to show (called at frame rate). Default: the mood model. */
    virtual int chooseFrame (float macro, float bypass, bool musicOn);

    /** Draws the character(s). Default: the current frame with `animation`
        (pivot at the feet). Override to draw more than one figure. */
    virtual void paintCharacter (juce::Graphics& g, juce::Rectangle<float> figure, const juce::AffineTransform& animation);

    /** Draws one frame into `figure` with an animation transform, honouring
        the UI desaturation. `tinted`: optional alternative full-size frames
        (e.g. sepia), drawn instead of the colour ones. */
    void drawFrame (juce::Graphics& g, int frame, juce::Rectangle<float> figure, const juce::AffineTransform& animation, float opacity,
                    const std::vector<juce::Image>* tinted = nullptr);

    int numFrames() const noexcept { return (int) colourFrames.size(); }
    const juce::Image& getColourFrame (int i) const { return colourFrames[(size_t) i]; }
    float musicLevel() const noexcept { return level; }
    AudioFeatures& getFeatures() noexcept { return features; }
    State& mutableState() noexcept { return state; }
    juce::Rectangle<float> getFigureArea() const { return figureArea(); }

    bool showSleepZ = true; // generic floating "Zzz" in bypass

    /** Plugin-specific decorations, in component coordinates. `figure` is the
        area the character is drawn into (before the animation transform). */
    virtual void paintBehind (juce::Graphics&, const State&, juce::Rectangle<float> figure) { juce::ignoreUnused (figure); }
    virtual void paintOverlay (juce::Graphics&, const State&, juce::Rectangle<float> figure) { juce::ignoreUnused (figure); }

    const Palette& palette;

private:
    int moodFrame (float macro, bool musicOn) noexcept;
    juce::Rectangle<float> figureArea() const;
    const juce::Image& scaledFrame (int index, bool grey, int pixelHeight);
    const juce::Image& scaledTintedFrame (const std::vector<juce::Image>& tinted, int index, int pixelHeight);

    AudioFeatures& features;
    std::vector<juce::Image> colourFrames, greyFrames;
    // Frames pre-scaled to the on-screen pixel size (rebuilt lazily when the
    // editor scale or display density changes), so each animation frame only
    // does a cheap bilinear draw instead of resampling the full-size bitmap.
    std::vector<juce::Image> scaledColour, scaledGrey;
    int scaledHeight = 0;
    std::vector<juce::Image> scaledTinted;
    const std::vector<juce::Image>* tintedSource = nullptr;
    int tintedHeight = 0;
    State state;

    Poses poses;
    static constexpr double joySeconds = 3.5, joyBeats = 8.0;
    double joyRemaining = 0.0, joyStartTime = 0.0, joyStartBeats = 0.0;
    float recentPeak = 0.0f; // furthest knob distance from rest over the last ~2 s
    int side = 0;            // -1 low, 0 rest, 1 high (with hysteresis)
    int rung = 0;            // position on the current ladder (with hysteresis)

    // dynamics
    float level = 0.0f, springX = 0.0f, springV = 0.0f;
    std::uint32_t lastTransients = 0;
    double beatsUi = 0.0;
    bool initialised = false;
};
} // namespace gk
