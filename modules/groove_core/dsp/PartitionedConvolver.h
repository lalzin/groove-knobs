#pragma once

namespace gk
{
/** Uniformly partitioned FFT convolution (overlap-save, frequency-domain
    delay line), one mono input → two outputs (stereo IR).

    - Latency: exactly one partition (P samples), independent of the host's
      block sizes (samples go through a FIFO; the output is bit-identical
      whatever the block partition).
    - Kernels are built off the audio thread (Kernel::build) and only their
      non-zero partitions are stored and processed: an IR that is silent at
      the start costs nothing for that part.
    - fadeTo() crossfades sample-accurately from the current kernel to a new
      one (both are convolved during the fade).
    Real-time safe after prepare(): no allocation, no lock. */
class PartitionedConvolver
{
public:
    using Complex = std::complex<float>;

    struct Kernel
    {
        int partitionSize = 0, firstPartition = 0, numPartitions = 0;
        std::array<std::vector<Complex>, 2> spectra; // numPartitions × (P + 1) per output

        /** Builds a kernel from two IRs of `length` samples (allocates). */
        static std::unique_ptr<Kernel> build (const float* irL, const float* irR, int length, int partitionSize)
        {
            auto k = std::make_unique<Kernel>();
            k->partitionSize = partitionSize;
            const int P = partitionSize, bins = P + 1;
            const int total = (length + P - 1) / P;

            auto isSilent = [&] (int part)
            {
                for (int i = part * P; i < juce::jmin (length, (part + 1) * P); ++i)
                    if (std::abs (irL[i]) > 1.0e-12f || std::abs (irR[i]) > 1.0e-12f)
                        return false;
                return true;
            };
            int first = 0;
            while (first < total && isSilent (first))
                ++first;
            int last = total - 1;
            while (last >= first && isSilent (last))
                --last;
            k->firstPartition = first;
            k->numPartitions = juce::jmax (0, last - first + 1);

            juce::dsp::FFT fft (juce::roundToInt (std::log2 (2 * P)));
            std::vector<float> buf ((size_t) (4 * P));
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* ir = ch == 0 ? irL : irR;
                auto& spec = k->spectra[(size_t) ch];
                spec.assign ((size_t) (k->numPartitions * bins), Complex());
                for (int j = 0; j < k->numPartitions; ++j)
                {
                    std::fill (buf.begin(), buf.end(), 0.0f);
                    const int start = (first + j) * P;
                    for (int i = 0; i < P && start + i < length; ++i)
                        buf[(size_t) i] = ir[start + i];
                    fft.performRealOnlyForwardTransform (buf.data(), true);
                    const auto* c = reinterpret_cast<const Complex*> (buf.data());
                    // (JUCE's inverse real FFT is already normalised by 1 / N)
                    for (int b = 0; b < bins; ++b)
                        spec[(size_t) (j * bins + b)] = c[b];
                }
            }
            return k;
        }

        int endPartition() const noexcept { return firstPartition + numPartitions; }
    };

    /** @param maxPartitions  longest IR, in partitions (sizes the delay line). */
    void prepare (int partitionSize, int maxPartitions)
    {
        P = partitionSize;
        bins = P + 1;
        numSlots = juce::jmax (1, maxPartitions);
        fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (2 * P)));
        fdl.assign ((size_t) (numSlots * bins), Complex());
        inFifo.assign ((size_t) P, 0.0f);
        prevInput.assign ((size_t) P, 0.0f);
        for (auto& o : outFifo)
            o.assign ((size_t) P, 0.0f);
        fftBuf.assign ((size_t) (4 * P), 0.0f);
        accA.assign ((size_t) (2 * bins), Complex());
        accB.assign ((size_t) (2 * bins), Complex());
        tmpA.assign ((size_t) (4 * P), 0.0f);
        tmpB.assign ((size_t) (4 * P), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (fdl.begin(), fdl.end(), Complex());
        std::fill (inFifo.begin(), inFifo.end(), 0.0f);
        std::fill (prevInput.begin(), prevInput.end(), 0.0f);
        for (auto& o : outFifo)
            std::fill (o.begin(), o.end(), 0.0f);
        pos = 0;
        head = 0;
        previous = nullptr;
        fadePos = fadeLen = 0;
    }

    int getLatency() const noexcept { return P; }
    int getPartitionSize() const noexcept { return P; }

    /** Uses `k` immediately (no fade). The kernel must outlive its use. */
    void setKernel (const Kernel* k) noexcept
    {
        current = k;
        previous = nullptr;
        fadePos = fadeLen = 0;
    }

    /** Crossfades from the current kernel to `k` over `fadeSamples`. */
    void fadeTo (const Kernel* k, int fadeSamples) noexcept
    {
        previous = current;
        current = k;
        fadeLen = juce::jmax (1, fadeSamples);
        fadePos = 0;
    }

    bool isFading() const noexcept { return previous != nullptr; }
    const Kernel* getCurrent() const noexcept { return current; }
    /** The kernel being faded out (or nullptr once the fade is over). */
    const Kernel* getPrevious() const noexcept { return previous; }

    /** One sample in, two out (latency P). */
    inline void process (float in, float& outL, float& outR) noexcept
    {
        inFifo[(size_t) pos] = in;
        outL = outFifo[0][(size_t) pos];
        outR = outFifo[1][(size_t) pos];
        if (++pos == P)
        {
            computeFrame();
            pos = 0;
        }
    }

private:
    void computeFrame() noexcept
    {
        // Input spectrum of the last 2P samples into the delay line
        std::copy (prevInput.begin(), prevInput.end(), fftBuf.begin());
        std::copy (inFifo.begin(), inFifo.end(), fftBuf.begin() + P);
        std::fill (fftBuf.begin() + 2 * P, fftBuf.end(), 0.0f);
        std::copy (inFifo.begin(), inFifo.end(), prevInput.begin());
        fft->performRealOnlyForwardTransform (fftBuf.data(), true);
        const auto* x = reinterpret_cast<const Complex*> (fftBuf.data());
        std::copy (x, x + bins, fdl.begin() + head * bins);

        const bool fading = previous != nullptr;
        convolve (current, accA);
        renderTime (accA, tmpA);
        if (fading)
        {
            convolve (previous, accB);
            renderTime (accB, tmpB);
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& o = outFifo[(size_t) ch];
            const float* a = tmpA.data() + ch * 2 * P + P; // second half (overlap-save)
            const float* b = tmpB.data() + ch * 2 * P + P;
            for (int i = 0; i < P; ++i)
            {
                if (fading)
                {
                    const float w = juce::jmin (1.0f, (float) (fadePos + i) / (float) fadeLen);
                    o[(size_t) i] = b[i] + w * (a[i] - b[i]);
                }
                else
                    o[(size_t) i] = a[i];
            }
        }
        if (fading)
        {
            fadePos += P;
            if (fadePos >= fadeLen)
                previous = nullptr;
        }
        head = (head + 1) % numSlots;
    }

    /** acc[ch * bins + b] = Σ_j X(t - j) · H_j */
    void convolve (const Kernel* k, std::vector<Complex>& acc) noexcept
    {
        std::fill (acc.begin(), acc.end(), Complex());
        if (k == nullptr)
            return;
        const int n = juce::jmin (k->numPartitions, numSlots - k->firstPartition);
        for (int j = 0; j < n; ++j)
        {
            const int age = k->firstPartition + j;
            const int slot = (head - age + numSlots * 4) % numSlots;
            const Complex* xs = fdl.data() + slot * bins;
            for (int ch = 0; ch < 2; ++ch)
            {
                const Complex* h = k->spectra[(size_t) ch].data() + j * bins;
                Complex* a = acc.data() + ch * bins;
                for (int b = 0; b < bins; ++b)
                    a[b] += xs[b] * h[b];
            }
        }
    }

    /** Inverse FFT of both channels into `out` (2 × 2P floats). */
    void renderTime (const std::vector<Complex>& acc, std::vector<float>& out) noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* buf = out.data() + ch * 2 * P;
            // the inverse needs a 4P-float workspace: use fftBuf (input spectrum already stored)
            auto* c = reinterpret_cast<Complex*> (fftBuf.data());
            std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
            std::copy (acc.begin() + ch * bins, acc.begin() + (ch + 1) * bins, c);
            fft->performRealOnlyInverseTransform (fftBuf.data());
            std::copy (fftBuf.begin(), fftBuf.begin() + 2 * P, buf);
        }
    }

    int P = 512, bins = 513, numSlots = 1, pos = 0, head = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<Complex> fdl, accA, accB;
    std::vector<float> inFifo, prevInput, fftBuf, tmpA, tmpB;
    std::array<std::vector<float>, 2> outFifo;
    const Kernel* current = nullptr;
    const Kernel* previous = nullptr;
    int fadePos = 0, fadeLen = 0;
};
} // namespace gk
