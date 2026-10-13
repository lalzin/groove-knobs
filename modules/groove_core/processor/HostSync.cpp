namespace gk
{
BlockTiming HostSync::beginBlock (juce::AudioPlayHead* playHead, int numSamples) noexcept
{
    BlockTiming t;
    double bpm = lastBpm;
    bool playing = false;
    std::optional<double> ppq;

    if (playHead != nullptr)
    {
        if (const auto pos = playHead->getPosition())
        {
            if (const auto hostBpm = pos->getBpm(); hostBpm.hasValue() && *hostBpm > 1.0 && *hostBpm < 1000.0)
                bpm = *hostBpm;
            playing = pos->getIsPlaying();
            if (const auto p = pos->getPpqPosition(); p.hasValue() && std::isfinite (*p))
                ppq = *p;
            if (const auto sig = pos->getTimeSignature(); sig.hasValue() && sig->numerator > 0 && sig->denominator > 0)
                lastBeatsPerBar = juce::jlimit (0.25, 64.0, 4.0 * sig->numerator / sig->denominator);
        }
    }

    lastBpm = bpm;
    t.bpm = bpm;
    t.beatsPerBar = lastBeatsPerBar;
    t.isPlaying = playing;
    t.beatsPerSample = bpm / (60.0 * sr);
    t.beatsAtStart = (playing && ppq.has_value()) ? *ppq : freeBeats;

    freeBeats = t.beatsAtStart + numSamples * t.beatsPerSample;
    return t;
}
} // namespace gk
