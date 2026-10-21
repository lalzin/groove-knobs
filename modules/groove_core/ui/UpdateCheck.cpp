namespace gk
{
std::unique_ptr<juce::PropertiesFile> UpdateCheck::settings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Groove Knobs";
    o.folderName = "LALZIN";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.millisecondsBeforeSaving = 0;
    return std::make_unique<juce::PropertiesFile> (o);
}

bool UpdateCheck::isEnabled()
{
    if (juce::SystemStats::getEnvironmentVariable ("GK_NO_UPDATE_CHECK", {}).isNotEmpty())
        return false;
    return settings()->getBoolValue ("updateCheck", true);
}

void UpdateCheck::setEnabled (bool enabled)
{
    auto s = settings();
    s->setValue ("updateCheck", enabled);
    s->saveIfNeeded();
}

int UpdateCheck::compareVersions (const juce::String& a, const juce::String& b)
{
    auto pa = juce::StringArray::fromTokens (a.trim(), ".", ""), pb = juce::StringArray::fromTokens (b.trim(), ".", "");
    for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
    {
        const int x = i < pa.size() ? pa[i].getIntValue() : 0, y = i < pb.size() ? pb[i].getIntValue() : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

std::optional<UpdateCheck::Result> UpdateCheck::newerIn (const juce::String& json, const juce::String& slug, const juce::String& currentVersion)
{
    const auto root = juce::JSON::parse (json);
    const auto entry = root.getProperty ("plugins", {}).getProperty (slug, {});
    const auto latest = entry.getProperty ("version", {}).toString();
    if (latest.isEmpty() || compareVersions (latest, currentVersion) <= 0)
        return std::nullopt;
    auto url = entry.getProperty ("url", {}).toString();
    if (! url.startsWithIgnoreCase ("https://"))
        url = "https://www.lalzin.com/plugins/" + slug; // only ever open an https page
    return Result { latest, url };
}

UpdateCheck::UpdateCheck (juce::String s, juce::String v, juce::String u, std::function<void (const Result&)> cb)
    : juce::Thread ("Groove Knobs update check"), slug (std::move (s)), currentVersion (std::move (v)), jsonUrl (std::move (u)), onNewer (std::move (cb))
{
    if (slug.isEmpty() || currentVersion.isEmpty() || ! jsonUrl.startsWithIgnoreCase ("https://") || ! isEnabled())
        return;
    // the last answer at once (offline too), then a fresh one if it is old
    auto st = settings();
    deliver (st->getValue ("versionsJson"));
    const auto last = st->getValue ("lastCheckMs").getLargeIntValue();
    if (juce::Time::currentTimeMillis() - last > checkEveryMs)
        startThread (juce::Thread::Priority::low);
}

UpdateCheck::~UpdateCheck()
{
    alive->store (false);
    stopThread (3000);
}

void UpdateCheck::checkNow()
{
    if (! jsonUrl.startsWithIgnoreCase ("https://") || isThreadRunning())
        return;
    startThread (juce::Thread::Priority::low);
}

void UpdateCheck::run()
{
    // only the plug-in's name and version go out (User-Agent), nothing else
    int status = 0;
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs (2500)
                             .withNumRedirectsToFollow (3)
                             .withStatusCode (&status)
                             .withExtraHeaders ("User-Agent: GrooveKnobs/" + slug + "/" + currentVersion);
    auto stream = juce::URL (jsonUrl).createInputStream (options);
    if (stream == nullptr || threadShouldExit())
        return; // offline: try again next time
    const auto json = status == 200 ? stream->readEntireStreamAsString() : juce::String();
    if (threadShouldExit())
        return;
    // the server answered (even "not found"): that was today's check
    auto st = settings();
    st->setValue ("lastCheckMs", juce::Time::currentTimeMillis());
    if (juce::JSON::parse (json).isObject())
        st->setValue ("versionsJson", json);
    st->saveIfNeeded();
    if (juce::JSON::parse (json).isObject())
        deliver (json);
}

void UpdateCheck::deliver (const juce::String& json)
{
    if (json.isEmpty())
        return;
    if (const auto r = newerIn (json, slug, currentVersion))
    {
        auto flag = alive;
        auto cb = onNewer;
        juce::MessageManager::callAsync ([flag, cb, res = *r]
                                         {
                                             if (flag->load() && cb)
                                                 cb (res);
                                         });
    }
}
} // namespace gk
