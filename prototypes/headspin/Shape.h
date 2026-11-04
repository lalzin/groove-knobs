#pragma once
// HEAD SPIN — a closed path around the head, seen from above.
//
// Coordinates: x to the right, y to the front (the nose points up), the unit
// circle passes through both ears. A path is stored as 1024 points spaced by
// equal arc length (built once, when the shape changes, never in the audio
// callback): reading it at a steady phase moves the sound at a constant speed
// along the path, without speeding up in the corners.
//
// Phase 0 is the front, increasing phase goes clockwise seen from above
// (front -> right -> back -> left): that is the "left to right" direction
// (the sound crosses from left to right in front of the listener).

#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace headspin
{
struct Point
{
    double x = 0.0, y = 0.0;
};

class ShapeTable
{
public:
    static constexpr int size = 1024;

    /** Builds the table from a closed curve c(u), u in [0, 1): sampled
        finely, then resampled at equal arc length. */
    /** `rounding`: corners rounded over this fraction of the curve's
        parameter (a moving average, twice): the sound turns a corner instead
        of changing direction at once, and the arc-length table holds a
        constant speed everywhere (a sharp corner would be cut by one chord). */
    void build (const std::function<Point (double)>& curve, double rounding = 0.0)
    {
        constexpr int fine = 16384;
        std::vector<Point> p (fine + 1);
        std::vector<double> len (fine + 1, 0.0);
        for (int i = 0; i < fine; ++i)
            p[(size_t) i] = curve ((double) i / fine);
        if (const int half = (int) (0.5 * rounding * fine); half > 0)
            for (int pass = 0; pass < 2; ++pass)
            {
                std::vector<Point> q (fine);
                double sx = 0.0, sy = 0.0;
                for (int k = -half; k <= half; ++k)
                {
                    sx += p[(size_t) ((k + fine) % fine)].x;
                    sy += p[(size_t) ((k + fine) % fine)].y;
                }
                const double w = 1.0 / (2 * half + 1);
                for (int i = 0; i < fine; ++i)
                {
                    q[(size_t) i] = { sx * w, sy * w };
                    const auto& add = p[(size_t) ((i + half + 1) % fine)];
                    const auto& sub = p[(size_t) ((i - half + fine) % fine)];
                    sx += add.x - sub.x;
                    sy += add.y - sub.y;
                }
                std::copy (q.begin(), q.end(), p.begin());
            }
        p[fine] = p[0];
        for (int i = 1; i <= fine; ++i)
            len[(size_t) i] = len[(size_t) i - 1] + std::hypot (p[(size_t) i].x - p[(size_t) i - 1].x, p[(size_t) i].y - p[(size_t) i - 1].y);
        perimeter = len[fine];
        int j = 0;
        for (int k = 0; k < size; ++k)
        {
            const double target = perimeter * k / size;
            while (j < fine - 1 && len[(size_t) j + 1] < target)
                ++j;
            const double seg = len[(size_t) j + 1] - len[(size_t) j];
            const double f = seg > 0.0 ? (target - len[(size_t) j]) / seg : 0.0;
            points[(size_t) k] = { p[(size_t) j].x + f * (p[(size_t) j + 1].x - p[(size_t) j].x),
                                   p[(size_t) j].y + f * (p[(size_t) j + 1].y - p[(size_t) j].y) };
        }
    }

    /** The point at phase u (any real: wraps), linear between table points. */
    Point at (double u) const noexcept
    {
        u -= std::floor (u);
        const double pos = u * size;
        const int i = (int) pos % size, i1 = (i + 1) % size;
        const double f = pos - std::floor (pos);
        return { points[(size_t) i].x + f * (points[(size_t) i1].x - points[(size_t) i].x),
                 points[(size_t) i].y + f * (points[(size_t) i1].y - points[(size_t) i].y) };
    }

    double getPerimeter() const noexcept { return perimeter; }

    /** The circle through both ears, starting in front, clockwise. */
    static Point circle (double u)
    {
        const double a = 2.0 * M_PI * u;
        return { std::sin (a), std::cos (a) };
    }

private:
    std::array<Point, size> points {};
    double perimeter = 0.0;
};
} // namespace headspin
