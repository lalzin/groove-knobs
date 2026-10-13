// asset_prep — slices green-screen sprite sheets into keyed, aligned frames.
//
//   asset_prep <out-dir> [--cols 4] [--rows 3] [--height 560] <sheet>:<i,j,...> [<sheet>:<...> ...]
//
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
void keepMainBand (juce::Image& img)
{
    const int hgt = img.getHeight(), wid = img.getWidth();
    std::vector<int> rowCount ((size_t) hgt, 0);
    {
        const juce::Image::BitmapData d (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < hgt; ++y)
            for (int x = 0; x < wid; ++x)
                if (d.getPixelColour (x, y).getAlpha() > 24)
                    ++rowCount[(size_t) y];
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
                d.setPixelColour (x, y, juce::Colours::transparentBlack);
}

/** Cuts cell `index` (row by row) of a cols × rows sheet and keys it. */
Frame cutFrame (const juce::Image& sheet, int cols, int rows, int index)
{
    const int c = index % cols, r = index / cols;
    const int x0 = sheet.getWidth() * c / cols, x1 = sheet.getWidth() * (c + 1) / cols;
    const int y0 = sheet.getHeight() * r / rows, y1 = sheet.getHeight() * (r + 1) / rows;
    Frame f;
    f.image = keyGreen (sheet.getClippedImage ({ x0, y0, x1 - x0, y1 - y0 }));
    keepMainBand (f.image);
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
        const auto path = spec.upToLastOccurrenceOf (":", false, false);
        const auto sheet = juce::ImageFileFormat::loadFrom (cwd.getChildFile (path));
        if (! sheet.isValid())
        {
            std::cerr << "cannot load " << path << "\n";
            return 1;
        }
        std::vector<int> heights;
        std::vector<Frame> sheetFrames;
        for (const auto& t : juce::StringArray::fromTokens (spec.fromLastOccurrenceOf (":", false, false), ",", ""))
        {
            sheetFrames.push_back (cutFrame (sheet, cols, rows, t.getIntValue()));
            heights.push_back (sheetFrames.back().box.getHeight());
        }
        // Reference height: median over all the sheet's cells (not only the selected ones)
        std::vector<int> all;
        for (int i = 0; i < cols * rows; ++i)
            all.push_back (cutFrame (sheet, cols, rows, i).box.getHeight());
        std::sort (all.begin(), all.end());
        const float median = (float) all[all.size() / 2];
        sheetMedians.push_back (median);
        for (auto& f : sheetFrames)
        {
            f.scale = 1.0f / median; // normalised later
            frames.push_back (std::move (f));
        }
    }

    // Normalise all sheets to the largest median height (no upscaling of the
    // biggest drawing), then build one canvas around the anchors.
    const float refHeight = *std::max_element (sheetMedians.begin(), sheetMedians.end());
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
        juce::FileOutputStream os (cwd.getChildFile (args[ci + 1]));
        png.writeImageToStream (sheet, os);
    }
    return 0;
}
