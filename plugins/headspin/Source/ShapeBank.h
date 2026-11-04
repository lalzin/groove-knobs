#pragma once
// HEAD SPIN — the six paths and the morph between neighbours.
//
// SHAPE 0..6 (continuous): circle, oval, triangle, square, figure eight, star,
// custom (drawn by hand in the view from above; a circle until drawn).
// Every path starts in front of the head and goes clockwise seen from above
// (so "left to right" crosses in front for all of them). A value between two
// shapes mixes their points at the same arc-length position, then the mix is
// re-parametrised by arc length (constant speed again).
//
// Building a table takes ~1 ms: never on the audio thread. ShapeStore keeps
// four slots: the message thread writes the next table into a free one and
// publishes it with one atomic; the audio thread picks it up at the start of
// a block and crossfades from the table it was using (both stay reserved
// until the crossfade ends). No lock, no allocation on the audio side.

#include "Shape.h"
#include <algorithm>
#include <atomic>
#include <memory>

namespace headspin
{
namespace shapes
{
    constexpr double twoPi = 2.0 * headspin::pi;

    inline Point polygon (const Point* v, int count, double u)
    {
        // closed polyline through v[0..count-1], parametrised by vertex index
        // (the arc-length table evens the speed out afterwards)
        const double pos = (u - std::floor (u)) * count;
        const int i = (int) pos % count, j = (i + 1) % count;
        const double f = pos - std::floor (pos);
        return { v[i].x + f * (v[j].x - v[i].x), v[i].y + f * (v[j].y - v[i].y) };
    }

    inline Point circle (double u) { return { std::sin (twoPi * u), std::cos (twoPi * u) }; }

    /** Wider than deep (the ping-pong path). */
    inline Point oval (double u) { return { std::sin (twoPi * u), 0.55 * std::cos (twoPi * u) }; }

    /** Equilateral, a corner straight ahead. */
    inline Point triangle (double u)
    {
        static const Point v[3] { { 0.0, 1.0 }, { std::sin (twoPi / 3.0), std::cos (twoPi / 3.0) }, { -std::sin (twoPi / 3.0), std::cos (twoPi / 3.0) } };
        return polygon (v, 3, u);
    }

    /** Sides facing front / back / ears, starting in the middle of the front side. */
    inline Point square (double u)
    {
        constexpr double h = 0.8;
        static const Point v[8] { { 0.0, h }, { h, h }, { h, 0.0 }, { h, -h }, { 0.0, -h }, { -h, -h }, { -h, 0.0 }, { -h, h } };
        return polygon (v, 8, u);
    }

    /** A figure eight round the two ears, crossing in front of the face:
        the right loop clockwise, the left one anticlockwise, both reaching
        past the ears. Crossing 0.8 ahead, the path stays at least 0.54 from
        the head's centre (an eight crossing nearer brushes the face: up to
        +6 dB and an ear-to-ear flip in a few milliseconds). */
    inline Point eight (double u)
    {
        return { 1.2 * std::sin (twoPi * u), 0.8 + 0.7 * std::sin (2.0 * twoPi * u) };
    }

    /** Five points, a tip straight ahead. */
    inline Point star (double u)
    {
        static const Point* v = []
        {
            static Point pts[10];
            for (int k = 0; k < 10; ++k)
            {
                const double a = twoPi * k / 10.0, rad = (k % 2 == 0) ? 1.05 : 0.45;
                pts[k] = { rad * std::sin (a), rad * std::cos (a) };
            }
            return pts;
        }();
        return polygon (v, 10, u);
    }

    constexpr int count = 7, custom = 6;
    inline Point base (int index, double u)
    {
        switch (index)
        {
            case 0: return circle (u);
            case 1: return oval (u);
            case 2: return triangle (u);
            case 3: return square (u);
            case 4: return eight (u);
            case 5: return star (u);
            default: return circle (u); // custom, until drawn
        }
    }

    inline const char* name (int index)
    {
        static const char* names[count] { "circle", "oval", "triangle", "square", "eight", "star", "custom" };
        return names[index < 0 ? 0 : index >= count ? count - 1 : index];
    }
} // namespace shapes

/** The six base shapes, each already at constant speed (built once). */
class ShapeBank
{
public:
    static constexpr double cornerRounding = 0.02; // 2 % of the way round, on either side of a corner
    static constexpr double customRounding = 0.03; // a hand drawing: its jitter smoothed a little more

    ShapeBank()
    {
        for (int i = 0; i < shapes::count; ++i)
            tables[i].build ([i] (double u) { return shapes::base (i, u); }, cornerRounding);
    }

    /** The custom shape (6): a closed path drawn by hand, already put in
        order by normaliseDrawing (front first, clockwise). Message thread. */
    void setCustom (const std::vector<Point>& points)
    {
        if (points.size() < 8)
        {
            tables[shapes::custom].build (shapes::circle, cornerRounding);
            return;
        }
        tables[shapes::custom].build ([&points] (double u) { return polyline (points, u); }, customRounding);
    }

    /** A hand drawing (in units: x right, y front) made usable as a path:
        resampled evenly, kept outside the head (r >= 0.3) and inside the
        view (r <= 1.55), turned clockwise seen from above (the direction
        buttons then mean the same as for the other shapes), and started at
        the point most in front. Fewer than 8 points: empty. */
    static std::vector<Point> normaliseDrawing (const std::vector<Point>& raw)
    {
        if (raw.size() < 8)
            return {};
        // evenly spaced along the stroke (closed)
        constexpr int n = 256;
        std::vector<Point> p (n);
        for (int k = 0; k < n; ++k)
            p[(size_t) k] = polyline (raw, (double) k / n);
        ShapeTable::evenOut (p, n);
        auto clampRadius = [&p]
        {
            for (auto& q : p)
            {
                const double r = std::hypot (q.x, q.y);
                const double clamped = std::clamp (r, 0.3, 1.55);
                if (r > 1.0e-9)
                {
                    q.x *= clamped / r;
                    q.y *= clamped / r;
                }
                else
                    q = { 0.0, 0.3 };
            }
        };
        clampRadius();
        // the hand's jitter and its hairpins smoothed (5-point average, 4 passes)
        for (int pass = 0; pass < 4; ++pass)
        {
            std::vector<Point> q (p);
            for (int k = 0; k < n; ++k)
            {
                double sx = 0.0, sy = 0.0;
                for (int j = -2; j <= 2; ++j)
                {
                    sx += p[(size_t) ((k + j + n) % n)].x;
                    sy += p[(size_t) ((k + j + n) % n)].y;
                }
                q[(size_t) k] = { sx / 5.0, sy / 5.0 };
            }
            p.swap (q);
        }
        clampRadius();
        // clockwise seen from above (x right, y front): a negative signed area
        double area = 0.0;
        for (int k = 0; k < n; ++k)
        {
            const auto& a = p[(size_t) k];
            const auto& b = p[(size_t) ((k + 1) % n)];
            area += a.x * b.y - b.x * a.y;
        }
        if (area > 0.0)
            std::reverse (p.begin(), p.end());
        ShapeTable::evenOut (p, n); // (the clamp moved some points: even again)
        // start where the path is most in front of the face
        int start = 0;
        double best = -1.0e9;
        for (int k = 0; k < n; ++k)
        {
            const auto& q = p[(size_t) k];
            const double score = q.y / std::max (1.0e-6, std::hypot (q.x, q.y)); // cos of the azimuth
            if (score > best)
            {
                best = score;
                start = k;
            }
        }
        std::rotate (p.begin(), p.begin() + start, p.end());
        return p;
    }

    /** The path for SHAPE value s (0..6): neighbours mixed at the same
        arc-length position, then evened out again. Message thread. */
    void buildMorph (double s, ShapeTable& out) const
    {
        s = std::fmax (0.0, std::fmin ((double) shapes::count - 1, s));
        const int a = std::min ((int) s, shapes::count - 1), b = std::min (a + 1, shapes::count - 1);
        const double f = s - a;
        if (f < 1.0e-9)
        {
            out = tables[a];
            return;
        }
        const ShapeTable& ta = tables[a];
        const ShapeTable& tb = tables[b];
        out.build ([&ta, &tb, f] (double u)
                   {
                       const Point p = ta.at (u), q = tb.at (u);
                       return Point { p.x + f * (q.x - p.x), p.y + f * (q.y - p.y) };
                   },
                   cornerRounding); // (a mix can fold into a cusp: rounded like the corners)
    }

private:
    /** The closed polyline through `pts` at u (by index; evened out after). */
    static Point polyline (const std::vector<Point>& pts, double u)
    {
        const int count = (int) pts.size();
        const double pos = (u - std::floor (u)) * count;
        const int i = (int) pos % count, j = (i + 1) % count;
        const double f = pos - std::floor (pos);
        return { pts[(size_t) i].x + f * (pts[(size_t) j].x - pts[(size_t) i].x), pts[(size_t) i].y + f * (pts[(size_t) j].y - pts[(size_t) i].y) };
    }

    ShapeTable tables[shapes::count];
};

/** Lock-free hand-over of shape tables (see the header comment). */
class ShapeStore
{
public:
    static constexpr int slots = 4;

    ShapeStore()
    {
        bank.buildMorph (0.0, table[0]);
        published.store (0);
    }

    /** Message thread: builds the path for SHAPE s and publishes it (skipped
        when s did not change). */
    void request (double s)
    {
        if (std::abs (s - lastBuilt) < 1.0e-6)
            return;
        // one table in flight at a time: the audio thread must have adopted
        // (and reserved) the last one before the next is written
        if (adopted.load (std::memory_order_acquire) != published.load (std::memory_order_acquire))
            return;
        const int busy = inUse.load (std::memory_order_acquire), pub = published.load (std::memory_order_acquire);
        int free = -1;
        for (int k = 0; k < slots && free < 0; ++k)
            if (k != pub && ! (busy & (1 << k)))
                free = k;
        if (free < 0)
            return; // all reserved (a crossfade is running): try again on the next call
        bank.buildMorph (s, table[free]);
        lastBuilt = s;
        published.store (free, std::memory_order_release);
    }

    /** Message thread: a new custom shape (normalised); rebuilds the path
        on the next request. */
    void setCustom (const std::vector<Point>& points)
    {
        bank.setCustom (points);
        lastBuilt = -1.0; // (the same SHAPE value, a new path)
    }

    /** Audio thread: the latest published slot. */
    int latest() const noexcept { return published.load (std::memory_order_acquire); }
    /** Audio thread: the slots it reads (current and, while crossfading, the
        previous one); `current` adopted. */
    void reserve (int current, int previous) noexcept
    {
        inUse.store ((1 << current) | (previous >= 0 ? (1 << previous) : 0), std::memory_order_release);
        adopted.store (current, std::memory_order_release);
    }
    const ShapeTable& get (int slot) const noexcept { return table[slot]; }

private:
    ShapeBank bank;
    ShapeTable table[slots];
    std::atomic<int> published { 0 }, inUse { 1 }, adopted { 0 };
    double lastBuilt = 0.0;
};
} // namespace headspin
