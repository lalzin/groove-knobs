// asset_prep — slices green-screen sprite sheets into keyed, aligned frames.
//
//   asset_prep <out-dir> [--cols 4] [--rows 3] [--height 560] [--key-flat] <sheet>:<i,j,...> [<sheet>:<...> ...]
//
// --key-flat: the background is a flat colour (sampled from the sheet's
// corners) instead of a green screen.
// --auto-cells: characters are found automatically (irregular grids); cell
// numbers then follow the detected characters, row by row.
// A sheet may give a region, its own grid and a size factor:
//   <sheet>#<x>,<y>,<w>,<h>@<cols>x<rows>*<factor>^<inset>:<cells>   (inset: see --inset, this sheet only)
// (e.g. a single big drawing: big.png@1x1*1.1:0). --scale-to-first: every sheet
// is brought to the first sheet's character size (default: to the largest).
// Cells are numbered row by row. Frames are written in the order given
// (mascot_0.png, mascot_1.png, ...); a sheet may appear several times.
// Different sheets are scaled so the character has the same size.
//
// Frames are read row by row; only the first --frames cells are kept. All
// frames are aligned on the character (head centre horizontally, feet
// vertically) and share one canvas size, so it stays anchored when the plugin
// switches frames. Output: <out-dir>/mascot_<n>.png (RGBA).

#include <juce_graphics/juce_graphics.h>
#include <iostream>

namespace
{
int argInt (const juce::StringArray& args, const juce::String& key, int fallback)
{
    const int i = args.indexOf (key);
    return i >= 0 && i + 1 < args.size() ? args[i + 1].getIntValue() : fallback;
}

/** Chroma key on green dominance with spill suppression. */
juce::Image keyGreen (const juce::Image& src)
{
    juce::Image out (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
    const juce::Image::BitmapData in (src, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < src.getHeight(); ++y)
        for (int x = 0; x < src.getWidth(); ++x)
        {
            const auto c = in.getPixelColour (x, y);
            const int r = c.getRed(), g = c.getGreen(), b = c.getBlue();
            const int dominance = g - juce::jmax (r, b);

            // dominance >= 70: background; <= 20: subject; linear in between.
            const float alpha = juce::jlimit (0.0f, 1.0f, (70.0f - (float) dominance) / 50.0f);
            if (alpha <= 0.0f)
                continue;

            // Despill: never let green exceed the other channels on the subject.
            const int gg = dominance > 0 ? juce::jmax (r, b) : g;
            dst.setPixelColour (x, y, juce::Colour ((juce::uint8) r, (juce::uint8) gg, (juce::uint8) b, alpha));
        }
    return out;
}

/** Keys out a flat background colour (any hue): distance in RGB from `bg`;
    below `lo` transparent, above `hi` opaque, soft in between (keeps soft
    ground shadows as translucency). */
juce::Image keyColour (const juce::Image& src, juce::Colour bg, float lo = 34.0f, float hi = 78.0f)
{
    juce::Image out (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
    const juce::Image::BitmapData in (src, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < src.getHeight(); ++y)
        for (int x = 0; x < src.getWidth(); ++x)
        {
            const auto c = in.getPixelColour (x, y);
            const float dr = (float) c.getRed() - (float) bg.getRed(), dg = (float) c.getGreen() - (float) bg.getGreen(),
                        db = (float) c.getBlue() - (float) bg.getBlue();
            const float d = std::sqrt (dr * dr + dg * dg + db * db);
            const float alpha = juce::jlimit (0.0f, 1.0f, (d - lo) / (hi - lo));
            if (alpha <= 0.0f)
                continue;
            // Ground shadows / painted strokes (background-hued, darker,
            // desaturated): neutral translucent shadow instead of tinted paint
            {
                const float lumC = 0.3f * c.getRed() + 0.59f * c.getGreen() + 0.11f * c.getBlue();
                const float lumB = 0.3f * bg.getRed() + 0.59f * bg.getGreen() + 0.11f * bg.getBlue();
                float hueDiff = std::abs (c.getHue() - bg.getHue());
                hueDiff = juce::jmin (hueDiff, 1.0f - hueDiff);
                if (lumC < lumB && hueDiff < 0.14f && c.getSaturation() < 0.45f)
                {
                    const float k = lumC / juce::jmax (1.0f, lumB);
                    dst.setPixelColour (x, y, juce::Colours::black.withAlpha (juce::jlimit (0.0f, 0.55f, (1.0f - k) * 1.3f)));
                    continue;
                }
            }
            if (alpha >= 1.0f)
            {
                dst.setPixelColour (x, y, c);
                continue;
            }
            // Un-mix the background from semi-transparent edge pixels
            auto unmix = [alpha] (int v, int b) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt ((float) b + ((float) v - (float) b) / alpha)); };
            dst.setPixelColour (x, y, juce::Colour (unmix (c.getRed(), bg.getRed()), unmix (c.getGreen(), bg.getGreen()), unmix (c.getBlue(), bg.getBlue()), alpha));
        }
    return out;
}

/** Background colour estimate: median of small patches in the four corners. */
juce::Colour sampleBackground (const juce::Image& sheet)
{
    std::vector<int> r, g, b;
    const int w = sheet.getWidth(), h = sheet.getHeight(), p = 12;
    for (auto [cx, cy] : { std::pair { 2, 2 }, std::pair { w - p - 2, 2 }, std::pair { 2, h - p - 2 }, std::pair { w - p - 2, h - p - 2 } })
        for (int y = cy; y < cy + p; ++y)
            for (int x = cx; x < cx + p; ++x)
            {
                const auto c = sheet.getPixelAt (x, y);
                r.push_back (c.getRed());
                g.push_back (c.getGreen());
                b.push_back (c.getBlue());
            }
    auto med = [] (std::vector<int>& v) { std::sort (v.begin(), v.end()); return (juce::uint8) v[v.size() / 2]; };
    return juce::Colour (med (r), med (g), med (b));
}

juce::Rectangle<int> opaqueBounds (const juce::Image& img)
{
    const juce::Image::BitmapData d (img, juce::Image::BitmapData::readOnly);
    int x0 = img.getWidth(), y0 = img.getHeight(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (d.getPixelColour (x, y).getAlpha() > 24)
            {
                x0 = juce::jmin (x0, x);
                y0 = juce::jmin (y0, y);
                x1 = juce::jmax (x1, x);
                y1 = juce::jmax (y1, y);
            }
    return x1 < 0 ? juce::Rectangle<int>() : juce::Rectangle<int> (x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

/** Horizontal centre of the opaque pixels in the top 40 % of the character
    (beret + head): arms and legs move between poses, the head does not. */
int headCentreX (const juce::Image& img, juce::Rectangle<int> box)
{
    const juce::Image::BitmapData d (img, juce::Image::BitmapData::readOnly);
    double sum = 0.0, count = 0.0;
    for (int y = box.getY(); y < box.getY() + box.getHeight() * 2 / 5; ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            if (d.getPixelColour (x, y).getAlpha() > 128)
            {
                sum += x;
                count += 1.0;
            }
    return count > 0.0 ? juce::roundToInt (sum / count) : box.getCentreX();
}
struct Frame
{
    juce::Image image;
    juce::Rectangle<int> box;
    int anchorX = 0, anchorY = 0;
    float scale = 1.0f;
};

/** Keeps only the main subject of a keyed cell: the band of rows (allowing
    small gaps) holding the most opaque pixels. Removes bits of the
    neighbouring cells' characters that bleed over the cell border. */
void keepMainBand (juce::Image& img, bool columns);

/** Removes fragments cut by the cell border: every connected opaque region
    that touches the border, except the largest one (the character). Small
    elements fully inside the cell (sparkles, drops) are kept. */
void dropEdgeFragments (juce::Image& img, juce::Rectangle<int> core = {})
{
    const int w = img.getWidth(), h = img.getHeight();
    std::vector<char> opaque ((size_t) (w * h), 0);
    {
        const juce::Image::BitmapData d (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                opaque[(size_t) (y * w + x)] = d.getPixelColour (x, y).getAlpha() > 24 ? 1 : 0;
    }
    std::vector<int> label ((size_t) (w * h), 0), size (1, 0);
    std::vector<char> touches (1, 0);
    std::vector<double> sumX (1, 0.0), sumY (1, 0.0);
    std::vector<int> stack;
    int next = 0;
    for (int start = 0; start < w * h; ++start)
    {
        if (! opaque[(size_t) start] || label[(size_t) start])
            continue;
        ++next;
        size.push_back (0);
        touches.push_back (0);
        sumX.push_back (0.0);
        sumY.push_back (0.0);
        stack.push_back (start);
        label[(size_t) start] = next;
        while (! stack.empty())
        {
            const int i = stack.back();
            stack.pop_back();
            const int x = i % w, y = i / w;
            ++size[(size_t) next];
            sumX[(size_t) next] += x;
            sumY[(size_t) next] += y;
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
                touches[(size_t) next] = 1;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= w || yy >= h)
                        continue;
                    const int n = yy * w + xx;
                    if (opaque[(size_t) n] && ! label[(size_t) n])
                    {
                        label[(size_t) n] = next;
                        stack.push_back (n);
                    }
                }
        }
    }
    int biggest = 0;
    for (int l = 1; l <= next; ++l)
        if (size[(size_t) l] > size[(size_t) biggest])
            biggest = l;
    juce::Image::BitmapData d (img, juce::Image::BitmapData::writeOnly);
    for (int i = 0; i < w * h; ++i)
    {
        const int l = label[(size_t) i];
        if (l == 0 || l == biggest)
            continue;
        // Neighbours' fragments: cut by the border, or centred outside the core cell
        const juce::Point<int> centre ((int) (sumX[(size_t) l] / size[(size_t) l]), (int) (sumY[(size_t) l] / size[(size_t) l]));
        if (touches[(size_t) l] || (! core.isEmpty() && ! core.contains (centre)))
            d.setPixelColour (i % w, i / w, juce::Colours::transparentBlack);
    }
}

/** Rows, then columns: removes neighbours' bits bleeding in from any side. */
void keepMainBand (juce::Image& img)
{
    keepMainBand (img, false);
    keepMainBand (img, true);
}

void keepMainBand (juce::Image& img, bool columns)
{
    const int hgt = columns ? img.getWidth() : img.getHeight(), wid = columns ? img.getHeight() : img.getWidth();
    auto at = [columns] (int line, int k) { return columns ? juce::Point<int> (line, k) : juce::Point<int> (k, line); };
    std::vector<int> rowCount ((size_t) hgt, 0);
    {
        const juce::Image::BitmapData d (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < hgt; ++y)
            for (int x = 0; x < wid; ++x)
            {
                const auto pt = at (y, x);
                if (d.getPixelColour (pt.x, pt.y).getAlpha() > 24)
                    ++rowCount[(size_t) y];
            }
    }
    constexpr int maxGap = 14;
    int bestStart = 0, bestEnd = hgt, start = -1, lastFilled = -1;
    long bestSum = -1, sum = 0;
    for (int y = 0; y <= hgt; ++y)
    {
        const bool filled = y < hgt && rowCount[(size_t) y] > 0;
        if (filled)
        {
            if (start < 0 || y - lastFilled > maxGap)
            {
                start = y;
                sum = 0;
            }
            sum += rowCount[(size_t) y];
            lastFilled = y;
            if (sum > bestSum)
            {
                bestSum = sum;
                bestStart = start;
                bestEnd = y + 1;
            }
        }
    }
    juce::Image::BitmapData d (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < hgt; ++y)
        if (y < bestStart || y >= bestEnd)
            for (int x = 0; x < wid; ++x)
            {
                const auto pt = at (y, x);
                d.setPixelColour (pt.x, pt.y, juce::Colours::transparentBlack);
            }
}

/** Keying mode: green screen (default) or a flat colour sampled from the corners. */
bool keyFlatBackground = false;

/** Grid of a sheet whose rows / columns are not evenly spaced: splits at the
    least dense row / column (character pixels only) near each expected
    boundary (±30 % of a cell). Cells are returned row by row. */
std::vector<juce::Rectangle<int>> findCells (const juce::Image& keyed, int cols, int rows)
{
    const int w = keyed.getWidth(), h = keyed.getHeight();
    std::vector<char> mask ((size_t) (w * h), 0);
    {
        const juce::Image::BitmapData d (keyed, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                // Character pixels only: saturated colours or dark outlines
                // (painted ground strokes are greyish)
                const auto c = d.getPixelColour (x, y);
                if (c.getAlpha() > 200 && (c.getSaturation() > 0.35f || c.getPerceivedBrightness() < 0.25f))
                    mask[(size_t) (y * w + x)] = 1;
            }
    }

    auto splits = [] (const std::vector<double>& profile, int parts)
    {
        const int n = (int) profile.size();
        std::vector<double> smooth ((size_t) n, 0.0);
        for (int i = 0; i < n; ++i)
            for (int k = -6; k <= 6; ++k)
                smooth[(size_t) i] += profile[(size_t) juce::jlimit (0, n - 1, i + k)];
        std::vector<int> cuts { 0 };
        for (int p = 1; p < parts; ++p)
        {
            const int expected = n * p / parts, span = n * 3 / (10 * parts);
            int best = expected;
            for (int i = juce::jmax (1, expected - span); i < juce::jmin (n - 1, expected + span); ++i)
                if (smooth[(size_t) i] < smooth[(size_t) best])
                    best = i;
            cuts.push_back (best);
        }
        cuts.push_back (n);
        return cuts;
    };

    std::vector<double> rowProfile ((size_t) h, 0.0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            rowProfile[(size_t) y] += mask[(size_t) (y * w + x)];
    const auto rowCuts = splits (rowProfile, rows);

    std::vector<juce::Rectangle<int>> cells;
    for (int r = 0; r < rows; ++r)
    {
        const int y0 = rowCuts[(size_t) r], y1 = rowCuts[(size_t) r + 1];
        std::vector<double> colProfile ((size_t) w, 0.0);
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x)
                colProfile[(size_t) x] += mask[(size_t) (y * w + x)];
        const auto colCuts = splits (colProfile, cols);
        for (int c = 0; c < cols; ++c)
            cells.push_back ({ colCuts[(size_t) c], y0, colCuts[(size_t) c + 1] - colCuts[(size_t) c], y1 - y0 });
    }
    if (std::getenv ("ASSET_PREP_DEBUG") != nullptr)
        for (const auto& b : cells)
            std::cerr << "cell " << b.toString() << "\n";
    return cells;
}

bool autoCells = false;
int cellInset = 0; // --inset N: pixels trimmed from every cell edge (grid lines drawn on the sheet)
bool eraseGrid = false; // --erase-grid: dark lines across (almost) the whole sheet become background

/** Repaints grid lines (rows / columns dark over > 85 % of their length) in
    the background colour, so the cells can be cut without trimming. */
juce::Image eraseGridLines (const juce::Image& src, juce::Colour bg)
{
    auto img = src.createCopy();
    juce::Image::BitmapData d (img, juce::Image::BitmapData::readWrite);
    auto dark = [&d] (int x, int y)
    {
        const auto c = d.getPixelColour (x, y);
        return (int) c.getRed() + c.getGreen() + c.getBlue() < 250;
    };
    const int w = img.getWidth(), h = img.getHeight();
    // find the lines first (a dark row / column over > 85 % of its length),
    // then paint them and their anti-aliased neighbours
    std::vector<int> rowsToErase, colsToErase;
    for (int y = 0; y < h; ++y)
    {
        int n = 0;
        for (int x = 0; x < w; ++x)
            n += dark (x, y) ? 1 : 0;
        if (n > w * 85 / 100)
            rowsToErase.push_back (y);
    }
    for (int x = 0; x < w; ++x)
    {
        int n = 0;
        for (int y = 0; y < h; ++y)
            n += dark (x, y) ? 1 : 0;
        if (n > h * 85 / 100)
            colsToErase.push_back (x);
    }
    for (int y0 : rowsToErase)
        for (int y = juce::jmax (0, y0 - 2); y <= juce::jmin (h - 1, y0 + 2); ++y)
            for (int x = 0; x < w; ++x)
                d.setPixelColour (x, y, bg);
    for (int x0 : colsToErase)
        for (int x = juce::jmax (0, x0 - 2); x <= juce::jmin (w - 1, x0 + 2); ++x)
            for (int y = 0; y < h; ++y)
                d.setPixelColour (x, y, bg);
    return img;
}

/** Cuts cell `index` (row by row) of a cols × rows sheet and keys it. */
Frame cutFrame (const juce::Image& sheet, int cols, int rows, int index)
{
    const int c = index % cols, r = index / cols;
    const int x0 = sheet.getWidth() * c / cols, x1 = sheet.getWidth() * (c + 1) / cols;
    const int y0 = sheet.getHeight() * r / rows, y1 = sheet.getHeight() * (r + 1) / rows;
    Frame f;
    if (autoCells)
    {
        const auto keyedSheet = keyFlatBackground ? keyColour (sheet, sampleBackground (sheet)) : keyGreen (sheet);
        const auto cells = findCells (keyedSheet, cols, rows);
        if (! juce::isPositiveAndBelow (index, (int) cells.size()))
        {
            std::cerr << "cell " << index << " not found (" << cells.size() << " characters detected)\n";
            std::exit (1);
        }
        // Cut with a margin (hands, boards reaching over the grid line stay
        // whole), then keep the character and what belongs to its cell.
        const auto cell = cells[(size_t) index];
        const auto wide = cell.expanded (48, 24).getIntersection (keyedSheet.getBounds());
        f.image = keyedSheet.getClippedImage (wide).createCopy();
        dropEdgeFragments (f.image, cell.translated (-wide.getX(), -wide.getY()));
    }
    else
    {
        const auto cell = sheet.getClippedImage (juce::Rectangle<int> { x0, y0, x1 - x0, y1 - y0 }.reduced (cellInset));
        f.image = keyFlatBackground ? keyColour (cell, sampleBackground (sheet)) : keyGreen (cell);
        keepMainBand (f.image);
    }
    f.box = opaqueBounds (f.image);
    f.anchorX = headCentreX (f.image, f.box);
    f.anchorY = f.box.getBottom();
    return f;
}
} // namespace

int main (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    // Usage:
    //   asset_prep <out-dir> [--cols 4] [--rows 3] [--height 560] <sheet>:<i,j,...> [<sheet>:<...> ...]
    // Frames are written in the order given: mascot_0.png, mascot_1.png, ...
    // Legacy form: asset_prep <sheet.png> <out-dir> [--frames 8]
    if (args.size() < 2)
    {
        std::cerr << "usage: asset_prep <out-dir> [--cols 4] [--rows 3] [--height 560] <sheet>:<i,j,...> ...\n";
        return 1;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const int cols = argInt (args, "--cols", 4), rows = argInt (args, "--rows", 3);
    const int targetHeight = argInt (args, "--height", 560);
    keyFlatBackground = args.contains ("--key-flat");
    autoCells = args.contains ("--auto-cells");
    cellInset = argInt (args, "--inset", 0);
    eraseGrid = args.contains ("--erase-grid");

    juce::StringArray specs;
    juce::File outDir;
    if (args[0].endsWithIgnoreCase (".png") || args[0].endsWithIgnoreCase (".jpg"))
    {
        outDir = cwd.getChildFile (args[1]);
        juce::StringArray idx;
        for (int i = 0; i < juce::jmin (cols * rows, argInt (args, "--frames", 8)); ++i)
            idx.add (juce::String (i));
        specs.add (args[0] + ":" + idx.joinIntoString (","));
    }
    else
    {
        outDir = cwd.getChildFile (args[0]);
        for (int i = 1; i < args.size(); ++i)
        {
            if (args[i] == "--key-flat" || args[i] == "--auto-cells" || args[i] == "--scale-to-first" || args[i] == "--erase-grid")
                continue;
            if (args[i].startsWith ("--"))
            {
                ++i;
                continue;
            }
            specs.add (args[i]);
        }
    }
    outDir.createDirectory();

    // Cut every requested frame; each sheet gets a scale factor so that the
    // character has the same size whatever sheet it comes from (median height
    // of the sheet's frames).
    std::vector<Frame> frames;
    std::vector<float> sheetMedians;
    for (const auto& spec : specs)
    {
        auto path = spec.upToLastOccurrenceOf (":", false, false);
        int sheetCols = cols, sheetRows = rows;
        float factor = 1.0f;
        cellInset = argInt (args, "--inset", 0);
        if (path.contains ("^"))
        {
            cellInset = path.fromLastOccurrenceOf ("^", false, false).getIntValue();
            path = path.upToLastOccurrenceOf ("^", false, false);
        }
        if (path.contains ("@"))
        {
            auto grid = path.fromLastOccurrenceOf ("@", false, false);
            path = path.upToLastOccurrenceOf ("@", false, false);
            if (grid.contains ("*"))
            {
                factor = grid.fromFirstOccurrenceOf ("*", false, false).getFloatValue();
                grid = grid.upToFirstOccurrenceOf ("*", false, false);
            }
            sheetCols = juce::jmax (1, grid.upToFirstOccurrenceOf ("x", false, false).getIntValue());
            sheetRows = juce::jmax (1, grid.fromFirstOccurrenceOf ("x", false, false).getIntValue());
        }
        juce::Rectangle<int> region;
        if (path.contains ("#"))
        {
            const auto r = juce::StringArray::fromTokens (path.fromLastOccurrenceOf ("#", false, false), ",", "");
            path = path.upToLastOccurrenceOf ("#", false, false);
            if (r.size() == 4)
                region = { r[0].getIntValue(), r[1].getIntValue(), r[2].getIntValue(), r[3].getIntValue() };
        }
        auto sheet = juce::ImageFileFormat::loadFrom (cwd.getChildFile (path));
        if (! sheet.isValid())
        {
            std::cerr << "cannot load " << path << "\n";
            return 1;
        }
        if (! region.isEmpty())
            sheet = sheet.getClippedImage (region.getIntersection (sheet.getBounds())).createCopy();
        if (eraseGrid)
            sheet = eraseGridLines (sheet, sampleBackground (sheet));
        std::vector<int> heights;
        std::vector<Frame> sheetFrames;
        for (const auto& t : juce::StringArray::fromTokens (spec.fromLastOccurrenceOf (":", false, false), ",", ""))
        {
            sheetFrames.push_back (cutFrame (sheet, sheetCols, sheetRows, t.getIntValue()));
            heights.push_back (sheetFrames.back().box.getHeight());
        }
        // Reference height: median over all the sheet's cells (not only the selected ones)
        std::vector<int> all;
        for (int i = 0; i < sheetCols * sheetRows; ++i)
            all.push_back (cutFrame (sheet, sheetCols, sheetRows, i).box.getHeight());
        std::sort (all.begin(), all.end());
        const float median = (float) all[all.size() / 2];
        sheetMedians.push_back (median);
        for (auto& f : sheetFrames)
        {
            f.scale = factor / median; // normalised later
            frames.push_back (std::move (f));
        }
    }

    // Normalise all sheets to the largest median height (no upscaling of the
    // biggest drawing), then build one canvas around the anchors.
    const float refHeight = args.contains ("--scale-to-first") ? sheetMedians.front()
                                                              : *std::max_element (sheetMedians.begin(), sheetMedians.end());
    int left = 0, right = 0, up = 0, down = 0;
    for (auto& f : frames)
    {
        f.scale *= refHeight;
        left = juce::jmax (left, juce::roundToInt ((float) (f.anchorX - f.box.getX()) * f.scale));
        right = juce::jmax (right, juce::roundToInt ((float) (f.box.getRight() - f.anchorX) * f.scale));
        up = juce::jmax (up, juce::roundToInt ((float) (f.anchorY - f.box.getY()) * f.scale));
        down = juce::jmax (down, juce::roundToInt ((float) (f.box.getBottom() - f.anchorY) * f.scale));
    }
    const int half = juce::jmax (left, right) + 6;
    const int canvasW = 2 * half, canvasH = up + down + 12;
    const float outScale = juce::jmin (1.0f, (float) targetHeight / (float) canvasH);
    const int w = juce::roundToInt ((float) canvasW * outScale), h = juce::roundToInt ((float) canvasH * outScale);

    for (const auto& old : outDir.findChildFiles (juce::File::findFiles, false, "mascot_*.png"))
        old.deleteFile();

    juce::PNGImageFormat png;
    std::vector<juce::Image> written;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        const auto& f = frames[i];
        juce::Image frame (juce::Image::ARGB, w, h, true);
        {
            juce::Graphics g (frame);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            const float s = f.scale * outScale;
            g.drawImageTransformed (f.image, juce::AffineTransform::translation ((float) -f.anchorX, (float) -f.anchorY)
                                                 .scaled (s)
                                                 .translated ((float) half * outScale, (float) (up + 6) * outScale));
        }
        const auto file = outDir.getChildFile ("mascot_" + juce::String ((int) i) + ".png");
        juce::FileOutputStream os (file);
        if (! png.writeImageToStream (frame, os))
        {
            std::cerr << "cannot write " << file.getFullPathName() << "\n";
            return 1;
        }
        std::cout << "wrote " << file.getFileName() << " " << w << "x" << h << "\n";
        written.push_back (frame);
    }

    // Optional contact sheet for review: --contact <file.png>
    if (const int ci = args.indexOf ("--contact"); ci >= 0 && ci + 1 < args.size())
    {
        const int perRow = 6, cw = w / 2, ch = h / 2;
        const int nRows = ((int) written.size() + perRow - 1) / perRow;
        juce::Image sheet (juce::Image::RGB, perRow * cw, nRows * (ch + 18), true);
        juce::Graphics g (sheet);
        g.fillAll (juce::Colour (0xff36363c));
        for (size_t i = 0; i < written.size(); ++i)
        {
            const int x = (int) (i % perRow) * cw, y = (int) (i / perRow) * (ch + 18);
            g.drawImage (written[i], x, y + 18, cw, ch, 0, 0, w, h);
            g.setColour (juce::Colours::white);
            g.drawText (juce::String ((int) i), x, y, cw, 18, juce::Justification::centred);
        }
        const auto contactFile = cwd.getChildFile (args[ci + 1]);
        contactFile.deleteFile(); // FileOutputStream appends to existing files
        juce::FileOutputStream os (contactFile);
        png.writeImageToStream (sheet, os);
    }
    return 0;
}
