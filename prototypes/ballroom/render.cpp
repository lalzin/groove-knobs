// BALLROOM renders and measurements: the late FDN's RT60 (step 1), the
// early / depth / send (step 2), the six modes (step 3), clicks, FREEZE, CPU.
//   render <in-dir with guitar.wav (mono 48 kHz 24-bit)> <out-dir>
// Build: c++ -std=c++20 -O2 -I../../plugins/ballroom/Source -o render render.cpp ../../plugins/ballroom/Source/{LateFdn,EarlyNet,Ballroom}.cpp
//
// RT60: the impulse response, band-filtered (eight one-pole high-passes and
// eight low-passes), Schroeder's backward integration, a straight line fitted
// to the decay curve between -5 and -35 dB (T30), extrapolated to -60 dB.
#include "Presets.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <random>
#include <string>

using namespace ballroom;

struct Stereo { std::vector<float> l, r; };
static const int sr = 48000;

static std::vector<float> readMono24 (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<char> b ((std::istreambuf_iterator<char> (f)), {});
    std::vector<float> out;
    size_t i = 12;
    int ch = 1;
    while (i + 8 <= b.size())
    {
        std::uint32_t n;
        std::memcpy (&n, &b[i + 4], 4);
        if (! std::memcmp (&b[i], "fmt ", 4))
            ch = *(std::uint16_t*) &b[i + 10];
        if (! std::memcmp (&b[i], "data", 4))
            for (size_t k = i + 8; k + (size_t) (3 * ch) <= i + 8 + n && k + 3 <= b.size(); k += (size_t) (3 * ch))
                out.push_back ((float) (std::int32_t) ((std::uint8_t) b[k] << 8 | (std::uint8_t) b[k + 1] << 16 | (std::uint8_t) b[k + 2] << 24) / 2147483648.0f);
        i += 8 + n + (n & 1);
    }
    return out;
}

static void writeWav (const std::string& path, const Stereo& s)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (std::uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (std::uint16_t v) { f.write ((const char*) &v, 2); };
    const std::uint32_t data = (std::uint32_t) s.l.size() * 6;
    f.write ("RIFF", 4); u32 (36 + data); f.write ("WAVEfmt ", 8);
    u32 (16); u16 (1); u16 (2); u32 ((std::uint32_t) sr); u32 ((std::uint32_t) sr * 6); u16 (6); u16 (24);
    f.write ("data", 4); u32 (data);
    for (size_t i = 0; i < s.l.size(); ++i)
        for (float v : { s.l[i], s.r[i] })
        {
            const auto q = (std::int32_t) std::lround (std::fmax (-1.0, std::fmin (1.0, v)) * 8388607.0);
            f.put ((char) (q & 0xff)); f.put ((char) ((q >> 8) & 0xff)); f.put ((char) ((q >> 16) & 0xff));
        }
}

static Stereo run (const std::vector<float>& l, const std::vector<float>& r, const LateParams& p, size_t length, double size = 0.6,
                   Mode mode = Mode::ballroom)
{
    LateFdn fdn;
    fdn.prepare (sr);
    fdn.configure (mode, size);
    fdn.setParams (p);
    Stereo out { std::vector<float> (length), std::vector<float> (length) };
    std::vector<float> il (length, 0.0f), ir (length, 0.0f);
    std::copy_n (l.begin(), std::min (length, l.size()), il.begin());
    std::copy_n (r.begin(), std::min (length, r.size()), ir.begin());
    for (size_t i = 0; i < length; i += 256)
    {
        const int n = (int) std::min ((size_t) 256, length - i);
        fdn.process (il.data() + i, ir.data() + i, out.l.data() + i, out.r.data() + i, n);
    }
    return out;
}

/** RT60 of a band of the response (lo..hi Hz), from its T30. */
static double rt60 (const Stereo& ir, double lo, double hi)
{
    std::vector<double> e (ir.l.size());
    const double aH = std::exp (-2.0 * M_PI * lo / sr), aL = 1.0 - std::exp (-2.0 * M_PI * hi / sr);
    for (int c = 0; c < 2; ++c)
    {
        const auto& x = c == 0 ? ir.l : ir.r;
        // 8 high-passes and 8 low-passes (48 dB / octave): a band decaying
        // fast is not hidden by the slower neighbours leaking in
        double h[8] {}, p[8] {}, lp[8] {};
        for (size_t i = 0; i < x.size(); ++i)
        {
            double v = x[i];
            for (int k = 0; k < 8; ++k)
            {
                h[k] = aH * (h[k] + v - p[k]);
                p[k] = v;
                v = h[k];
            }
            for (int k = 0; k < 8; ++k)
            {
                lp[k] += aL * (v - lp[k]);
                v = lp[k];
            }
            e[i] += v * v;
        }
    }
    // Schroeder: energy remaining after each sample
    std::vector<double> edc (e.size());
    double acc = 0.0;
    for (size_t i = e.size(); i-- > 0;)
        edc[i] = (acc += e[i]);
    const double total = edc[0];
    // straight line through the -5 .. -35 dB part (least squares)
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / total + 1e-30);
        if (db <= -5.0 && db >= -35.0)
        {
            const double t = (double) i / sr;
            sx += t; sy += db; sxx += t * t; sxy += t * db;
            ++count;
        }
    }
    if (count < 10)
        return 0.0;
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx); // dB per second
    return -60.0 / slope;
}

int main (int argc, char** argv)
{
    const std::string in = argc > 1 ? argv[1] : ".", out = argc > 2 ? argv[2] : ".";
    std::vector<float> impulse (1, 1.0f), none;

    // 1. RT60: DECAY 0.5 / 2 / 8 s, mids (500 Hz - 4 kHz) within 15 %
    std::printf ("RT60, 500 Hz - 4 kHz (spec: within 15 %% of DECAY):\n");
    for (double decay : { 0.5, 2.0, 8.0 })
        for (double size : { 0.2, 0.6, 1.0 })
        {
            LateParams p;
            p.decaySeconds = decay;
            p.bassMult = 1.0;
            p.highMult = 1.0;
            const auto ir = run (impulse, impulse, p, (size_t) ((decay * 0.9 + 0.6) * sr), size);
            const double m = rt60 (ir, 500, 4000);
            std::printf ("  DECAY %4.1f s, SIZE %3.0f %%: %6.2f s (%+5.1f %%)\n", decay, size * 100, m, 100.0 * (m / decay - 1.0));
        }
    // 2. BASS MULT / HIGH MULT: the low (80 - 200 Hz) and high (9 - 14 kHz) bands follow within 20 %
    std::printf ("bands (DECAY 2 s, XOVERs 500 Hz / 5 kHz; spec: within 20 %%; the low band without the mode's own warmth):\n");
    for (auto [bm, hm] : { std::pair { 1.2, 0.5 }, { 2.0, 0.3 }, { 0.5, 1.0 }, { 4.0, 0.1 } })
    {
        LateParams p;
        p.decaySeconds = 2.0;
        p.bassMult = bm;
        p.highMult = hm;
        const auto ir = run (impulse, impulse, p, (size_t) ((2.0 * std::max (1.0, bm) * 0.9 + 0.6) * sr));
        const double lo = rt60 (ir, 80, 200) / modeShape (Mode::ballroom).lowWarmth, mi = rt60 (ir, 700, 2500), hi = rt60 (ir, 9000, 14000);
        std::printf ("  BASS x%.2f HIGH x%.2f: low %.2f s (want %.2f, %+.0f %%), mid %.2f s, high %.2f s (want %.2f, %+.0f %%)\n", bm, hm, lo, 2.0 * bm,
                     100.0 * (lo / (2.0 * bm) - 1.0), mi, hi, 2.0 * hm, 100.0 * (hi / (2.0 * hm) - 1.0));
    }

    // --- the whole reverb ------------------------------------------------------
    const auto guitarRaw = readMono24 (in + "/guitar.wav");
    std::vector<float> guitar ((size_t) (8 * sr), 0.0f);
    if (! guitarRaw.empty())
        for (size_t i = 0; i < guitar.size(); ++i)
            guitar[i] = 0.7f * guitarRaw[i % guitarRaw.size()];
    // snare-ish hits on 2 and 4 at 115 BPM
    std::vector<float> snare ((size_t) (8 * sr), 0.0f);
    {
        std::mt19937 rng (2);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        for (int beat = 1; beat < 8; beat += 2)
        {
            const size_t at = (size_t) (beat * 60.0 / 115.0 * sr);
            for (size_t i = 0; i < (size_t) (0.12 * sr) && at + i < snare.size(); ++i)
                snare[at + i] = 0.6f * u (rng) * (float) std::exp (-(double) i / (0.03 * sr));
        }
    }
    auto render = [] (const std::vector<float>& src, const Params& p, double tail, const std::function<void (Params&, size_t)>& change = {})
    {
        Ballroom b;
        b.prepare (sr, 256);
        Params q = p;
        b.setParams (q);
        const size_t len = src.size() + (size_t) (tail * sr);
        std::vector<float> x (len, 0.0f);
        std::copy (src.begin(), src.end(), x.begin());
        Stereo o { std::vector<float> (len), std::vector<float> (len) };
        for (size_t i = 0; i < len; i += 256)
        {
            if (change)
            {
                change (q, i);
                b.setParams (q);
            }
            const int n = (int) std::min ((size_t) 256, len - i);
            b.process (x.data() + i, x.data() + i, o.l.data() + i, o.r.data() + i, n);
        }
        return o;
    };
    auto rms = [] (const std::vector<float>& x, size_t a, size_t b)
    {
        double e = 0;
        for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i];
        return 10.0 * std::log10 (e / (double) (b - a) + 1e-30);
    };

    // DEPTH's level match: noise, the early only (DEPTH 0) against the late only (DEPTH 1)
    {
        std::vector<float> noise ((size_t) (6 * sr));
        std::mt19937 rng (5);
        std::normal_distribution<float> g (0.0f, 0.1f);
        for (auto& v : noise) v = g (rng);
        std::printf ("DEPTH levels (noise, wet only; the late against the early):");
        for (double decay : { 0.5, 2.0, 8.0 })
        {
            Params p;
            p.mix = 1.0; p.decaySeconds = decay; p.highCutHz = 20000; p.mode = Mode::ballroom;
            p.depth = 0.0;
            const auto e = render (noise, p, 0.0);
            p.depth = 1.0;
            const auto l = render (noise, p, 0.0);
            std::printf ("  %.1f s: %+.1f dB", decay, rms (l.l, (size_t) (3 * sr), noise.size()) - rms (e.l, (size_t) (3 * sr), noise.size()));
        }
        std::printf ("\n");
    }

    // Step 2: early / depth / send / size
    {
        Params p;
        p.mode = Mode::ballroom; p.mix = 0.4; p.decaySeconds = 2.2;
        for (double d : { 0.0, 0.5, 1.0 })
        {
            p.depth = d;
            char name[96];
            std::snprintf (name, sizeof name, "/ballroom_step2_snare_depth%03d.wav", (int) (d * 100));
            writeWav (out + name, render (snare, p, 3.0));
        }
        p.depth = 0.6;
        for (double s : { 0.0, 1.0 })
        {
            p.earlySend = s;
            writeWav (out + (s == 0.0 ? "/ballroom_step2_snare_send0_parallel.wav" : "/ballroom_step2_snare_send1_series.wav"), render (snare, p, 3.0));
        }
        p.earlySend = 0.5;
        for (double ms : { 15.0, 120.0 })
        {
            p.earlySizeMs = ms;
            p.depth = 0.3;
            char name[96];
            std::snprintf (name, sizeof name, "/ballroom_step2_snare_earlysize%03dms_depth30.wav", (int) ms);
            writeWav (out + name, render (snare, p, 3.0));
        }
    }

    // Step 3: the six modes (each at a decay that suits it)
    const double modeDecay[] { 0.8, 1.5, 3.0, 2.2, 2.5, 8.0 };
    for (int m = 0; m < (int) Mode::count; ++m)
    {
        Params p;
        p.mode = (Mode) m;
        p.decaySeconds = modeDecay[m];
        p.mix = 0.38;
        if (p.mode == Mode::mirror) { p.lateModDepth = 0.7; p.earlyModDepth = 0.6; }
        if (p.mode == Mode::space) { p.lateModDepth = 0.5; }
        std::string nm = modeName ((Mode) m);
        for (auto& ch : nm) ch = (char) std::tolower (ch);
        writeWav (out + "/ballroom_step3_mode_" + nm + "_snare.wav", render (snare, p, p.decaySeconds));
        if (! guitarRaw.empty())
            writeWav (out + "/ballroom_step3_mode_" + nm + "_guitar.wav", render (guitar, p, p.decaySeconds));
        std::vector<float> imp (1, 0.8f);
        Params q = p;
        q.mix = 1.0;
        writeWav (out + "/ballroom_step3_mode_" + nm + "_ir.wav", render (imp, q, p.decaySeconds + 0.5));
    }

    // Step 4: the 10 presets on a guitar, a snare, a voice (Frozen Floor: frozen after 2 s)
    {
        const auto vocalRaw = readMono24 (in + "/vocal.wav");
        std::vector<float> vocal ((size_t) (6 * sr), 0.0f);
        for (int k = 0; k < 3 && ! vocalRaw.empty(); ++k)
            for (size_t i = 0; i < vocalRaw.size() && (size_t) (k * 2 * sr) + i < vocal.size(); ++i)
                vocal[(size_t) (k * 2 * sr) + i] = 0.8f * vocalRaw[i];
        std::printf ("presets (SPACE -> mix / decay / size / depth / predelay, then the tail of the snare: RT60 500-4k, brightness = 4-10k against 0.5-2k):\n");
        for (const auto& pr : presets())
        {
            const Params p = presetParams (pr);
            std::string nm = pr.name;
            for (auto& ch : nm) ch = ch == ' ' ? '_' : (char) std::tolower (ch);
            const bool frozen = p.freeze;
            auto freezeLater = [frozen] (Params& q, size_t i) { q.freeze = frozen && i >= (size_t) (2 * sr); };
            const double tail = std::min (8.0, p.decaySeconds);
            writeWav (out + "/ballroom_step4_preset_" + nm + "_guitar.wav", render (guitar, p, tail, freezeLater));
            writeWav (out + "/ballroom_step4_preset_" + nm + "_snare.wav", render (snare, p, tail, freezeLater));
            if (! vocalRaw.empty())
                writeWav (out + "/ballroom_step4_preset_" + nm + "_vocal.wav", render (vocal, p, tail, freezeLater));
            // the tail of one hit, wet only
            Params q = p;
            q.mix = 1.0;
            q.freeze = false;
            std::vector<float> hit (snare.begin() + (size_t) (60.0 / 115.0 * sr), snare.begin() + (size_t) (60.0 / 115.0 * sr + 0.12 * sr));
            const auto ir = render (hit, q, std::min (12.0, q.decaySeconds * 1.2 + 0.5));
            auto band = [&] (double lo, double hi)
            {
                Stereo f = ir;
                const double aH = std::exp (-2.0 * M_PI * lo / sr), aL = 1.0 - std::exp (-2.0 * M_PI * hi / sr);
                double e = 0, h = 0, pv = 0, l1 = 0, l2 = 0;
                for (size_t i = (size_t) (0.2 * sr); i < f.l.size(); ++i)
                {
                    h = aH * (h + f.l[i] - pv); pv = f.l[i];
                    l1 += aL * (h - l1); l2 += aL * (l1 - l2);
                    e += l2 * l2;
                }
                return 10 * std::log10 (e + 1e-30);
            };
            std::printf ("  %-15s %-8s SPACE %.2f: mix %2.0f %%, decay %5.2f s, size %3.0f %%, depth %3.0f %%, predelay %4.1f ms%s | RT60 %5.2f s, brightness %+5.1f dB\n",
                         pr.name, modeName (p.mode), pr.space, p.mix * 100, p.decaySeconds, p.lateSize * 100, p.depth * 100, p.predelayMs, p.freeze ? ", FREEZE" : "",
                         rt60 (ir, 500, 4000), band (4000, 10000) - band (500, 2000));
        }
    }

    // No click: a 1 kHz sine, the mode / sizes changed, FREEZE on and off; the
    // largest sample step against the steady render
    {
        std::vector<float> sine ((size_t) (6 * sr));
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.4f * (float) std::sin (2.0 * M_PI * 1000.0 * i / sr);
        Params p;
        p.mix = 0.5;
        auto maxStep = [] (const Stereo& o)
        {
            float m = 0;
            for (size_t i = 1; i < o.l.size(); ++i) m = std::fmax (m, std::fmax (std::fabs (o.l[i] - o.l[i - 1]), std::fabs (o.r[i] - o.r[i - 1])));
            return m;
        };
        const auto steady = render (sine, p, 0.0);
        const auto modes = render (sine, p, 0.0, [] (Params& q, size_t i) { q.mode = (Mode) ((i / (sr / 2)) % (int) Mode::count); });
        const auto sizes = render (sine, p, 0.0, [] (Params& q, size_t i) { q.lateSize = ((i / (sr / 2)) % 2) ? 0.9 : 0.2; q.earlySizeMs = ((i / (sr / 3)) % 2) ? 150.0 : 10.0; });
        const auto drag = render (sine, p, 0.0, [] (Params& q, size_t i) { q.lateSize = 0.5 + 0.4 * std::sin (2.0 * M_PI * 0.3 * i / sr); });
        const auto freeze = render (sine, p, 0.0, [] (Params& q, size_t i) { q.freeze = ((i / sr) % 2) == 1; });
        std::printf ("largest step: steady %.3f, mode every 0.5 s %.3f, sizes every 0.33-0.5 s %.3f, size dragged %.3f, FREEZE on / off every 1 s %.3f\n",
                     maxStep (steady), maxStep (modes), maxStep (sizes), maxStep (drag), maxStep (freeze));
    }

    // FREEZE for 10 minutes: the level must not grow
    {
        Ballroom b;
        b.prepare (sr, 512);
        Params p;
        p.mix = 1.0;
        p.mode = Mode::space;
        b.setParams (p);
        std::vector<float> x (512), l (512), r (512);
        std::mt19937 rng (9);
        std::normal_distribution<float> g (0.0f, 0.2f);
        double first = 0, last = 0, peak = 0;
        const int blocks = (int) (601.0 * sr / 512);
        for (int k = 0; k < blocks; ++k)
        {
            const bool feeding = k < (int) (1.0 * sr / 512);
            for (auto& v : x) v = feeding ? g (rng) : 0.0f;
            p.freeze = ! feeding;
            b.setParams (p);
            b.process (x.data(), x.data(), l.data(), r.data(), 512);
            double e = 0;
            for (int i = 0; i < 512; ++i) { e += (double) l[(size_t) i] * l[(size_t) i]; peak = std::fmax (peak, std::fabs (l[(size_t) i])); }
            if (k >= (int) (2.0 * sr / 512) && k < (int) (62.0 * sr / 512)) first += e;
            if (k >= blocks - (int) (60.0 * sr / 512)) last += e;
        }
        std::printf ("FREEZE 10 min (Space): the last minute %+.2f dB against the first, peak %.2f\n", 10 * std::log10 (last / first), peak);
    }

    // CPU: 20 s of stereo noise, 48 kHz, blocks of 512
    {
        Ballroom b;
        b.prepare (sr, 512);
        Params p;
        p.mode = Mode::ballroom;
        b.setParams (p);
        std::vector<float> x (512), l (512), r (512);
        std::mt19937 rng (1);
        std::normal_distribution<float> g (0.0f, 0.1f);
        for (auto& v : x) v = g (rng);
        const int blocks = (int) (20.0 * sr / 512);
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < blocks; ++k)
            b.process (x.data(), x.data(), l.data(), r.data(), 512);
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("CPU: %.2f %% of one core (20 s of audio in %.3f s)\n", 100.0 * s / 20.0, s);
    }
    return 0;
}
