namespace gk
{
juce::Result PresetManager::parse (const juce::String& jsonText, PresetData& out)
{
    juce::var root;
    const auto result = juce::JSON::parse (jsonText, root);
    if (result.failed())
        return result;
    if (! root.isObject())
        return juce::Result::fail ("preset root is not an object");

    out.name = root.getProperty ("name", {}).toString();
    if (out.name.isEmpty())
        return juce::Result::fail ("preset has no name");

    out.json = root;
    out.makeup = {};
    out.makeupLow = {};
    out.hasMakeup = false;
    out.bipolar = root.hasProperty ("low") || root.hasProperty ("high") || root.hasProperty ("makeupLowDb");

    auto readTable = [&] (const char* key, MakeupTable& table) -> juce::Result
    {
        if (const auto* arr = root.getProperty (key, {}).getArray())
        {
            if (arr->size() != MakeupTable::size)
                return juce::Result::fail (juce::String (key) + " must have " + juce::String (MakeupTable::size) + " values");
            for (int i = 0; i < MakeupTable::size; ++i)
                table.values[(size_t) i] = (float) (double) arr->getReference (i);
            out.hasMakeup = true;
        }
        return juce::Result::ok();
    };

    out.hasVariant = {};
    if (out.bipolar)
    {
        if (auto r = readTable ("makeupLowDb", out.makeupLow); r.failed())
            return r;
        return readTable ("makeupHighDb", out.makeup);
    }
    for (int mask = 1; mask < (1 << PresetData::maxToggles); ++mask)
    {
        const auto key = "makeupDb@" + juce::String (mask);
        if (root.hasProperty (key))
        {
            if (auto r = readTable (key.toRawUTF8(), out.variants[(size_t) mask - 1]); r.failed())
                return r;
            out.hasVariant[(size_t) mask - 1] = true;
        }
    }
    return readTable ("makeupDb", out.makeup);
}

juce::String PresetManager::toJson (const PresetData& preset)
{
    // Hand-written layout: the nested sections stay compact and readable and
    // the 101 makeup values are wrapped 10 per line.
    auto* obj = preset.json.getDynamicObject();
    juce::String s ("{\n");
    s << "  \"name\": " << juce::JSON::toString (preset.name);

    if (obj != nullptr)
    {
        for (const auto& prop : obj->getProperties())
        {
            const auto key = prop.name.toString();
            if (key == "name" || key.startsWith ("makeup"))
                continue;
            const auto fmt = juce::JSON::FormatOptions {}.withSpacing (juce::JSON::Spacing::singleLine).withMaxDecimalPlaces (4);
            s << ",\n  " << juce::JSON::toString (key) << ": ";
            // Sides ("low" / "high"): one section per line
            if (auto* side = prop.value.getDynamicObject(); side != nullptr && key != "tags")
            {
                s << "{";
                bool first = true;
                for (const auto& sub : side->getProperties())
                {
                    s << (first ? "\n    " : ",\n    ") << juce::JSON::toString (sub.name.toString()) << ": " << juce::JSON::toString (sub.value, fmt);
                    first = false;
                }
                s << "\n  }";
            }
            else
                s << juce::JSON::toString (prop.value, fmt);
        }
    }

    auto writeTable = [&s] (const char* key, const MakeupTable& table)
    {
        s << ",\n  \"" << key << "\": [";
        for (int i = 0; i < MakeupTable::size; ++i)
        {
            if (i % 10 == 0)
                s << "\n    ";
            s << juce::String (table.values[(size_t) i], 2);
            if (i < MakeupTable::size - 1)
                s << (i % 10 == 9 ? "," : ", ");
        }
        s << "\n  ]";
    };

    if (preset.bipolar)
    {
        writeTable ("makeupLowDb", preset.makeupLow);
        writeTable ("makeupHighDb", preset.makeup);
    }
    else
    {
        writeTable ("makeupDb", preset.makeup);
        for (int mask = 1; mask < (1 << PresetData::maxToggles); ++mask)
            if (preset.hasVariant[(size_t) mask - 1])
                writeTable (("makeupDb@" + juce::String (mask)).toRawUTF8(), preset.variants[(size_t) mask - 1]);
    }
    s << "\n}\n";
    return s;
}

juce::Result PresetManager::add (const juce::String& jsonText)
{
    PresetData p;
    const auto r = parse (jsonText, p);
    if (r.wasOk())
        presets.push_back (std::move (p));
    return r;
}

int PresetManager::indexOf (const juce::String& name) const
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == name)
            return (int) i;
    return -1;
}

juce::StringArray PresetManager::getNames() const
{
    juce::StringArray names;
    for (const auto& p : presets)
        names.add (p.name);
    return names;
}
} // namespace gk
