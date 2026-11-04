#pragma once
// HEAD SPIN — the six paths and the morph between neighbours.
//
// SHAPE 0..5 (continuous): circle, oval, triangle, square, figure eight, star.
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

    constexpr int count = 6;
    inline Point base (int index, double u)
    {
        switch (index)
        {
            case 0: return circle (u);
            case 1: return oval (u);
            case 2: return triangle (u);
            case 3: return square (u);
            case 4: return eight (u);
            default: return star (u);
        }
    }

    inline const char* name (int index)
    {
        static const char* names[count] { "circle", "oval", "triangle", "square", "eight", "star" };
        return names[index < 0 ? 0 : index >= count ? count - 1 : index];
    }
} // namespace shapes

/** The six base shapes, each already at constant speed (built once). */
class ShapeBank
{
public:
    static constexpr double cornerRounding = 0.02; // 2 % of the way round, on either side of a corner

    ShapeBank()
    {
        for (int i = 0; i < shapes::count; ++i)
            tables[i].build ([i] (double u) { return shapes::base (i, u); }, cornerRounding);
    }

    /** The path for SHAPE value s (0..5): neighbours mixed at the same
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
