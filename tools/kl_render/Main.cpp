// kl_render — listening renders of Kick Lock, from the repository root:
//   kl_render [--out-dir renders/kick_lock]
// A funky bass loop (sub + mids, 4 notes, 124 BPM) with a 4-on-the-floor kick
// on the sidechain; LOCK off / on at MIX 60 % for every SHAPE, plus the dry
// bass, the kick, and bass + kick mixes (what you hear in the track).
// Also prints the phase at each kick and the convergence after each note change.

#include "KickLockProcessor.h"
#include "KickBass.h"
#include <iostream>

namespace
{
void setParam (juce::RangedAudioParameter& p, float plain)
{
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (plain));
    p.endChangeGesture();
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sr)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (sr).withNumChannels (buffer.getNumChannels()).withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}
} // namespace

int main (int argc, char** argv)
{
#if JUCE_WINDOWS
    _putenv_s ("GK_NO_UPDATE_CHECK", "1");
#else
    setenv ("GK_NO_UPDATE_CHECK", "1", 1);
#endif
    juce::ScopedJuceInitialiser_GUI init;
    juce::String outDir = "renders/kick_lock";
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--out-dir")
            outDir = argv[i + 1];
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (outDir);
    const double sr = 48000.0, bpm = 124.0, bar = 4.0 * 60.0 / bpm;
    // A1, C2, D2, G1, one bar each, twice (16 s), with a little syncopation
    std::vector<kickbass::Note> notes;
    const double hz[] { 55.0, 65.41, 73.42, 49.0 };
    for (int rep = 0; rep < 2; ++rep)
        for (int n = 0; n < 4; ++n)
        {
            const double t0 = (rep * 4 + n) * bar;
            notes.push_back ({ t0, bar * 0.62, hz[n] });
            notes.push_back ({ t0 + bar * 0.75, bar * 0.22, hz[n] * (n % 2 == 0 ? 1.0 : 1.5) });
        }
    const double seconds = 8.0 * bar + 0.5;
    const auto bassLoop = kickbass::bass (notes, seconds, sr);
    const auto kick = kickbass::kicks (seconds, sr, bpm, 0.0, 0.8);
    writeWav (dir.getChildFile ("bass_dry.wav"), bassLoop, sr);
    writeWav (dir.getChildFile ("kick.wav"), kick, sr);

    const char* shapes[] { "exponential", "linear", "s_curve", "bounce" };
    for (bool lock : { false, true })
        for (int shape = 0; shape < 4; ++shape)
        {
            kl::KickLockProcessor proc;
            proc.setNonRealtime (true);
            setParam (proc.getMixParameter(), 0.6f);
            setParam (proc.getLockParameter(), lock ? 1.0f : 0.0f);
            setParam (proc.getShapeParameter(), (float) shape);
            proc.prepareToPlay (sr, 512);
            const auto out = kickbass::run (proc, bassLoop, &kick, { 512 }, bpm);
            const int L = proc.getLatencySamples();
            // the bass, and the bass + the kick as heard in the track (the kick aligned by the host's latency compensation)
            juce::AudioBuffer<float> bassOut (2, out.getNumSamples() - L), mix (2, out.getNumSamples() - L);
            for (int c = 0; c < 2; ++c)
            {
                bassOut.copyFrom (c, 0, out, c, L, bassOut.getNumSamples());
                mix.copyFrom (c, 0, bassOut, c, 0, mix.getNumSamples());
                mix.addFrom (c, 0, kick, c, 0, juce::jmin (mix.getNumSamples(), kick.getNumSamples()));
            }
            const juce::String name = juce::String (lock ? "lock_on_" : "lock_off_") + shapes[shape];
            writeWav (dir.getChildFile (name + "_bass.wav"), bassOut, sr);
            writeWav (dir.getChildFile (name + "_with_kick.wav"), mix, sr);
            std::cout << "wrote " << name << " (latency " << L << " samples = " << 1000.0 * L / sr << " ms)\n";

        }

    // The phase table: the processed bass alone (MIX 100 %, no ducking: the
    // sub that comes back after each kick), LOCK off / on
    for (bool lock : { false, true })
    {
        kl::KickLockProcessor proc;
        proc.setNonRealtime (true);
        setParam (proc.getMixParameter(), 1.0f);
        setParam (proc.getDepthParameter(), 0.0f);
        setParam (proc.getLockParameter(), lock ? 1.0f : 0.0f);
        proc.prepareToPlay (sr, 512);
        const auto out = kickbass::run (proc, bassLoop, &kick, { 512 }, bpm);
        const int L = proc.getLatencySamples();
        std::cout << "\n" << (lock ? "LOCK on" : "LOCK off") << " — phase of the fundamental at each kick of each note (deg, target 0):\n";
        for (const auto& n : notes)
        {
            juce::String line;
            int k = 0;
            for (double t : kickbass::kickTimes (seconds, bpm))
                if (t >= n.start && t < n.start + n.length - 0.05)
                    line << "kick " << ++k << ": " << juce::String (kl::KickLockDsp::wrap180 (kickbass::phaseAt (out.getReadPointer (0), out.getNumSamples(),
                                                                                                                  (int) std::round (t * sr) + L, n.hz, sr)),
                                                                     1)
                         << "   ";
            if (line.isNotEmpty())
                std::cout << "  " << juce::String (n.hz, 2) << " Hz (note at " << juce::String (n.start, 2) << " s)  " << line << "\n";
        }
    }
    return 0;
}
