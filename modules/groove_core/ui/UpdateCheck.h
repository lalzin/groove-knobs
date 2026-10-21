#pragma once

namespace gk
{
/** "Is there a newer version?" — the small publishers' way: the plug-in
    reads a versions file on the website (OneKnobConfig::updateUrl) at most
    once a day, in the background (never on the audio thread, 2.5 s timeout,
    nothing happens offline), keeps the answer in a small settings file, and
    the editor shows a badge with a link to the download page. Nothing is
    downloaded or installed by the plug-in.

    versions.json: { "plugins": { "<slug>": { "version": "0.2.0", "url": "https://..." }, ... } }

    Can be turned off (right-click on "made by LALZIN"; stored for every
    Groove Knobs plug-in) or with the environment variable GK_NO_UPDATE_CHECK
    (screenshots, CI). */
class UpdateCheck final : private juce::Thread
{
public:
    struct Result
    {
        juce::String latest, url;
    };

    UpdateCheck (juce::String slug, juce::String currentVersion, juce::String jsonUrl, std::function<void (const Result&)> onNewer);
    ~UpdateCheck() override;

    /** Fetches now (the menu's "check now"), whatever the last check. */
    void checkNow();

    static bool isEnabled();
    static void setEnabled (bool enabled);
    /** < 0 when a is older than b, 0 when equal, > 0 when newer ("0.10.1" > "0.9"). */
    static int compareVersions (const juce::String& a, const juce::String& b);
    /** The newer version for `slug` in a versions file, if any. */
    static std::optional<Result> newerIn (const juce::String& json, const juce::String& slug, const juce::String& currentVersion);

    static constexpr juce::int64 checkEveryMs = 24 * 60 * 60 * 1000;

private:
    void run() override;
    void deliver (const juce::String& json);
    static std::unique_ptr<juce::PropertiesFile> settings();

    juce::String slug, currentVersion, jsonUrl;
    std::function<void (const Result&)> onNewer;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace gk
