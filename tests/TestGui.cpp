#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include <JuceHeader.h>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/gui/GateCircleComponent.h"
#include "../Source/gui/ManualWindow.h"
#include "../Source/gui/MeterComponent.h"
#include "../Source/gui/PresetMenuItem.h"
#include "../Source/gui/ScopeComponent.h"
#include "../Source/gui/SequenceEditorComponent.h"

using namespace juce;

namespace
{
    File previewFolder()
    {
        auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("spiral303_gui");
        dir.createDirectory();
        return dir;
    }

    /** Paints a component into an image with no window, no peer and no audio device.
        paintEntireComponent walks the whole child tree, so on composite editors (the
        plugin UI) children like combo boxes are painted too. */
    Image paintOffscreen (Component& comp, int w, int h)
    {
        comp.setSize (w, h);
        Image image (Image::ARGB, w, h, true);
        Graphics g (image);
        comp.paintEntireComponent (g, true);
        return image;
    }

    int pixelsLike (const Image& image, std::function<bool (Colour)> match)
    {
        int count = 0;
        for (int y = 0; y < image.getHeight(); y += 2)
            for (int x = 0; x < image.getWidth(); x += 2)
                if (match (image.getPixelAt (x, y)))
                    ++count;
        return count;
    }

    // Colours land on the panel background after alpha compositing, so every
    // channel is pulled toward the panel grey; these are the composited values.
    bool isAmber (Colour c)
    {
        return c.getFloatRed() > 0.58f && c.getFloatGreen() > 0.32f
               && c.getFloatRed() - c.getFloatBlue() > 0.35f;
    }

    bool isGateRed (Colour c)
    {
        return c.getFloatRed() > 0.52f && c.getFloatGreen() < 0.42f && c.getFloatBlue() < 0.42f;
    }

    File save (const Image& image, const String& name)
    {
        auto file = previewFolder().getChildFile (name);
        // FileOutputStream appends to whatever is on disk; without deleting the
        // old bytes every new PNG would be a second stream hiding behind the first.
        file.deleteFile();
        FileOutputStream out (file);
        PNGImageFormat png;
        const bool ok = out.openedOk() && png.writeImageToStream (image, out);
        if (! ok)
            std::cout << "save FAILED " << name << std::endl;
        return file;
    }

    std::unique_ptr<PluginProcessor> makeGuiProcessor()
    {
        // The constructor already decodes the built-in starting pattern.
        return std::make_unique<PluginProcessor>();
    }

    void setFloat (PluginProcessor& p, const String& id, float raw)
    {
        if (auto* f = dynamic_cast<AudioParameterFloat*> (p.state().getParameter (id)))
            f->setValueNotifyingHost (f->getNormalisableRange().convertTo0to1 (raw));
    }

    /** The editor's constructor reads UiSkin and installs the palette, so the
        parameter is the only way a test should ask for a skin. */
    void setSkin (PluginProcessor& p, int index)
    {
        if (auto* c = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (ParamIDs::uiSkin)))
            c->setValueNotifyingHost (static_cast<float> (index)
                                      / static_cast<float> (kFaceCount - 1));
    }

    Image crop (const Image& src, Rectangle<int> area)
    {
        Image out (Image::ARGB, area.getWidth(), area.getHeight(), true);
        Graphics g (out);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (src, 0, 0, area.getWidth(), area.getHeight(),
                     area.getX(), area.getY(), area.getWidth(), area.getHeight(), false);
        return out;
    }

    double srgbLuminance (double c)
    {
        return c <= 0.03928 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
    }

    /** WCAG relative luminance, straight from the rendered pixel. */
    double luminance (Colour c)
    {
        return 0.2126 * srgbLuminance (c.getFloatRed())
             + 0.7152 * srgbLuminance (c.getFloatGreen())
             + 0.0722 * srgbLuminance (c.getFloatBlue());
    }

    double contrastRatio (Colour a, Colour b)
    {
        const double la = luminance (a), lb = luminance (b);
        return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
    }

    struct BandStats
    {
        double meanL = 0.0, meanR = 0.0, meanG = 0.0, meanB = 0.0;
        double darkest = 1.0, lightest = 0.0;
        double spread = 0.0;       // standard deviation of luminance: the texture itself
        double textureShare = 0.0; // fraction of pixels off the mean by more than 0.01
        int count = 0;
    };

    inline BandStats bandStats (const Image& img, Rectangle<int> area)
    {
        std::vector<double> lums;
        BandStats s;
        double r = 0.0, g = 0.0, b = 0.0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const Colour c = img.getPixelAt (x, y);
                const double l = luminance (c);
                lums.push_back (l);
                r += c.getFloatRed();
                g += c.getFloatGreen();
                b += c.getFloatBlue();
                s.darkest = std::min (s.darkest, l);
                s.lightest = std::max (s.lightest, l);
            }
        s.count = static_cast<int> (lums.size());
        if (s.count == 0)
            return s;
        for (const double l : lums)
            s.meanL += l;
        s.meanL /= static_cast<double> (s.count);
        s.meanR = r / s.count;
        s.meanG = g / s.count;
        s.meanB = b / s.count;
        int off = 0;
        for (const double l : lums)
        {
            const double d = l - s.meanL;
            s.spread += d * d;
            if (std::abs (d) > 0.01)
                ++off;
        }
        s.spread = std::sqrt (s.spread / static_cast<double> (s.count));
        s.textureShare = static_cast<double> (off) / static_cast<double> (s.count);
        return s;
    }

    /** Walks the knurled rim of a knob and counts how often the brightness
        turns around: that is the rib count the eye actually reads. */
    inline int ribSignChanges (const Image& img, float cx, float cy, float radius)
    {
        auto sample = [&] (float a)
        {
            const int x = static_cast<int> (std::lround (cx + std::cos (a) * radius));
            const int y = static_cast<int> (std::lround (cy + std::sin (a) * radius));
            return luminance (img.getPixelAt (jlimit (0, img.getWidth() - 1, x),
                                              jlimit (0, img.getHeight() - 1, y)));
        };
        const int steps = 720;
        double previous = sample (0.0f) - sample (2.0f * static_cast<float> (juce::MathConstants<float>::pi)
                                                 / static_cast<float> (steps));
        int flips = 0;
        for (int i = 0; i < steps; ++i)
        {
            const float a = static_cast<float> (i) * 2.0f * juce::MathConstants<float>::pi
                            / static_cast<float> (steps);
            const float next = a + 2.0f * juce::MathConstants<float>::pi / static_cast<float> (steps);
            const double delta = sample (next) - sample (a);
            if (std::abs (delta) > 0.004 && ((delta > 0.0) != (previous > 0.0)) && previous != 0.0)
                ++flips;
            if (std::abs (delta) > 0.004)
                previous = delta;
        }
        return flips;
    }
}

void runGuiTests (const std::function <void (bool, const String&)>& check)
{
    std::cout << "--- components painted offscreen ---" << std::endl;

    // UTF-8 handling sanity check: a Cyrillic literal must decode to its
    // characters (juce::String(const char*) reads raw bytes, so any non-ASCII
    // text has to go through String::fromUTF8, as the manual does).
    {
        check (U8 ("Се").length() == 2, "String::fromUTF8 decodes Cyrillic literals");
        check (juce::String ("Се").length() == 4, "juce::String(const char*) read bytes, hence U8()");
    }

    {
        auto p = makeGuiProcessor();
        SequenceEditorComponent editor (*p);
        auto image = paintOffscreen (editor, 1150, 270);

        const auto amber = pixelsLike (image, [] (Colour c) { return isAmber (c); });
        check (amber > 200, "the step grid paints amber notes (" + String (amber) + " cells)");
        check (pixelsLike (image, [] (Colour c)
               { return c.getFloatRed() < 0.20f && c.getFloatGreen() < 0.20f; }) > 0,
               "the grid has a dark background");
        save (image, "sequence-editor.png");
    }

    {
        auto p = makeGuiProcessor();
        GatePattern gate;
        gate.closedMask = 0xf0f0;   // two quarter-wheels of muted sectors
        p->setGate (gate);

        GateCircleComponent wheel (*p);
        auto image = paintOffscreen (wheel, 280, 300);

        const auto red = pixelsLike (image, [] (Colour c) { return isGateRed (c); });
        const auto total = image.getWidth() / 2 * (image.getHeight() / 2);
        check (red > total / 12 && red < total / 2,
               "closed sectors are coloured, open ones are not (" + String (red) + "/" + String (total) + ")");

        // An all-open wheel must paint no red at all, so the colour really is bound to
        // the pattern rather than being a fixed decoration.
        GatePattern allOpen;
        allOpen.closedMask = 0;
        p->setGate (allOpen);
        wheel.refresh();
        auto openImage = paintOffscreen (wheel, 280, 300);
        const auto redOpen = pixelsLike (openImage, [] (Colour c) { return isGateRed (c); });
        check (redOpen == 0,
               "an all-open wheel paints no muted sectors (" + String (redOpen) + ")");
        save (openImage, "gate-wheel-open.png");
        save (image, "gate-wheel.png");
    }

    {
        auto p = makeGuiProcessor();
        setFloat (*p, ParamIDs::wave, 0.5f);
        ScopeComponent waveScope (*p, ScopeComponent::WaveOnly);
        auto waveImage = paintOffscreen (waveScope, 210, 130);
        const auto waveBright = pixelsLike (waveImage, [] (Colour c)
            { return c.getFloatRed() > 0.55f || c.getFloatBlue() > 0.55f; });
        check (waveBright > 50, "the wave scope draws its wave (" + String (waveBright) + ")");
        save (waveImage, "wave-scope.png");

        ScopeComponent filterScope (*p, ScopeComponent::FilterOnly);
        auto filterImage = paintOffscreen (filterScope, 210, 130);
        const auto filterBright = pixelsLike (filterImage, [] (Colour c)
            { return c.getFloatRed() > 0.55f || c.getFloatBlue() > 0.55f; });
        check (filterBright > 50, "the filter scope draws its response (" + String (filterBright) + ")");
        save (filterImage, "filter-scope.png");
    }

    {
        // The grid has to stand on frequencies, not on equal pixel gaps: decades
        // span equal distances, and every painted line lands where the axis says.
        auto p = makeGuiProcessor();
        ScopeComponent scope (*p, ScopeComponent::FilterOnly);
        scope.setSize (400, 160);
        const ScopeAxes axes { scope.responsePlot() };

        check (std::abs ((axes.freqToX (200.0f) - axes.freqToX (100.0f))
                       - (axes.freqToX (400.0f) - axes.freqToX (200.0f))) < 0.01f,
               "an octave spans the same pixels everywhere on the axis");
        check (std::abs ((axes.freqToX (500.0f) - axes.freqToX (100.0f))
                       - (axes.freqToX (100.0f) - axes.freqToX (20.0f))) < 0.01f,
               "two decades span the same pixels as one");
        check (std::abs (axes.xToFreq (axes.freqToX (853.0f)) - 853.0f) < 0.5f,
               "the frequency axis round-trips");
        check (std::abs (axes.yToDb (axes.dbToY (-7.5f)) + 7.5f) < 0.01f,
               "the decibel axis round-trips");

        // The resonance is counted exactly once in the display model, so the top of
        // the window is the analytic peak of the cascade at full resonance: the
        // tallest hump touches kDbTop and is never cut by it.
        const float cut = 1000.0f;
        auto peakOf = [&] (float res, float slope)
        {
            float best = -1000.0f;
            for (int i = 0; i <= 2000; ++i)
            {
                const float f = 20.0f * std::pow (1000.0f, static_cast<float> (i) / 2000.0f);
                best = std::max (best, ScopeAxes::responseDb (f, cut, res, slope));
            }
            return best;
        };
        check (std::abs (peakOf (1.0f, 1.0f) - ScopeAxes::kDbTop) < 0.05f,
               "full resonance tops out exactly on the window edge (" + String (peakOf (1.0f, 1.0f), 2) + " dB)");
        check (std::abs (peakOf (1.0f, 0.0f) - 12.73f) < 0.05f,
               "the 12 dB section peaks at 10log10 of the split Q (" + String (peakOf (1.0f, 0.0f), 2) + " dB)");
        check (std::abs (ScopeAxes::responseDb (0.4f * cut, cut, 0.0f, 1.0f)) < 0.5f,
               "with no resonance the passband is flat well below the knee");
        check (std::abs (ScopeAxes::responseDb (cut, cut, 0.0f, 1.0f) + 6.02f) < 0.1f,
               "the Q=0.5 cascade sits -6 dB on its cutoff");
        {
            float prevRes = -1000.0f, prevSlope = -1000.0f, global = -1000.0f;
            bool grows = true;
            for (const float r : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const float p = peakOf (r, 1.0f);
                if (p <= prevRes) grows = false;
                prevRes = p;
                global = std::max (global, p);
            }
            for (const float s : { 0.0f, 0.5f, 1.0f })
            {
                const float p = peakOf (1.0f, s);
                if (p <= prevSlope) grows = false;
                prevSlope = p;
                global = std::max (global, p);
            }
            check (grows, "the peak grows with both resonance and slope");
            check (global <= ScopeAxes::kDbTop + 0.05f,
                   "no parameter set paints a hump over the window (" + String (global, 2) + " dB)");
        }

        const auto image = paintOffscreen (scope, 400, 160);
        const auto plot = axes.plot.toNearestInt();
        const Colour beat (0xff3d4451), minor (0xff2c313a);
        save (image, "filter-scope-large.png");

        auto onRowLine = [&] (int y)
        {
            for (const float db : ScopeAxes::gridDecibels())
                if (std::abs (y - roundToInt (axes.dbToY (db))) <= 1)
                    return true;
            return false;
        };
        auto linePixels = [&] (int x, Colour want)
        {
            int n = 0;
            for (int y = plot.getY(); y < plot.getBottom(); ++y)
                if (! onRowLine (y) && image.getPixelAt (x, y) == want)
                    ++n;
            return n;
        };

        int missing = 0, stray = 0;
        // The last decade sits exactly on the right edge; the component draws it in
        // the last column the clip still covers, so the probe follows the same clamp.
        auto gridCol = [&] (float f)
        {
            return juce::jlimit (plot.getX(), plot.getRight() - 1, roundToInt (axes.freqToX (f)));
        };
        for (const float f : ScopeAxes::gridFrequencies())
        {
            const bool major = ScopeAxes::isMajorFrequency (f);
            if (linePixels (gridCol (f), major ? beat : minor) < plot.getHeight() / 2)
                ++missing;
        }
        check (missing == 0, "every grid frequency paints its column (" + String (missing) + " missing)");        for (int x = plot.getX() + 3; x < plot.getRight() - 3; ++x)
        {
            bool nearGrid = false;
            for (const float f : ScopeAxes::gridFrequencies())
                if (std::abs (x - gridCol (f)) <= 2)
                    nearGrid = true;
            if (! nearGrid && linePixels (x, beat) + linePixels (x, minor) > 0)
                ++stray;
        }
        check (stray == 0, "no grid line stands between the frequencies (" + String (stray) + " stray)");

        for (const float db : { 0.0f, -24.0f })
        {
            const int y = roundToInt (axes.dbToY (db));
            int across = 0;
            for (int x = plot.getX(); x < plot.getRight(); ++x)
                if (image.getPixelAt (x, y) == (db == 0.0f ? beat : minor))
                    ++across;
            check (across > plot.getWidth() / 2,
                   "the dB grid draws its " + String (db, 0) + " dB row across the plot ("
                   + String (across) + " px)");
        }

        // The wave box counts phase, so its rulers sit at the quarters.
        ScopeComponent wave (*p, ScopeComponent::WaveOnly);
        const auto waveImage = paintOffscreen (wave, 210, 130);
        const Rectangle<float> wr = Rectangle<float> (210, 130).reduced (1.0f).reduced (2.0f);
        const int half = roundToInt (wr.getX() + wr.getWidth() * 0.5f);
        const int quarter = roundToInt (wr.getX() + wr.getWidth() * 0.25f);
        int halfBeat = 0, quarterMinor = 0;
        for (int y = roundToInt (wr.getY()); y < wr.getBottom(); ++y)
        {
            if (waveImage.getPixelAt (half, y) == beat)  ++halfBeat;
            if (waveImage.getPixelAt (quarter, y) == minor) ++quarterMinor;
        }
        check (halfBeat > 40 && quarterMinor > 40,
               "the wave scope rules its cycle in quarters (" + String (halfBeat) + "/"
               + String (quarterMinor) + ")");

        // The editor repaints both boxes 30 times a second; the maths must run once
        // per parameter set, not once per frame.
        ScopeComponent cached (*p, ScopeComponent::FilterOnly);
        paintOffscreen (cached, 400, 160);
        const int built = cached.curveBuildsForTests();
        check (built == 1, "the first paint builds the response once (" + String (built) + ")");
        paintOffscreen (cached, 400, 160);
        paintOffscreen (cached, 400, 160);
        check (cached.curveBuildsForTests() == built,
               "repainting the same response rebuilds nothing");
        setFloat (*p, ParamIDs::cutoff, 1200.0f);
        paintOffscreen (cached, 400, 160);
        check (cached.curveBuildsForTests() == built + 1,
               "moving the cutoff rebuilds the response exactly once");
        paintOffscreen (cached, 300, 160);
        check (cached.curveBuildsForTests() == built + 2,
               "a narrower plot rebuilds for its own geometry");

        // And the hump is actually visible: at full resonance the lit stroke turns
        // over just under the window top instead of being shorn by it.
        setFloat (*p, ParamIDs::res, 1.0f);
        setFloat (*p, ParamIDs::tone, 1.0f);
        paintOffscreen (cached, 400, 160);
        const auto hotImage = paintOffscreen (scope, 400, 160);
        save (hotImage, "filter-scope-hot.png");
        auto isCyan = [&] (int x, int y)
        {
            const Colour c = hotImage.getPixelAt (x, y);
            return c.getFloatBlue() > 0.6f && c.getFloatGreen() > 0.5f && c.getFloatRed() < 0.55f;
        };
        int topRow = plot.getBottom(), crestCol = -1;
        for (int y = plot.getY(); y < plot.getBottom() && topRow == plot.getBottom(); ++y)
            for (int x = plot.getX(); x < plot.getRight(); ++x)
                if (isCyan (x, y)) { topRow = y; crestCol = x; break; }
        check (topRow - plot.getY() <= 2,
               "the full-resonance peak touches the top of the window, it is not clipped ("
               + String (topRow - plot.getY()) + " px below)");
        // A hump, not a shelf: the stroke runs along the top edge for a handful of
        // pixels and turns down on both sides - a clipped flat top would stay lit.
        int along = 0;
        for (int x = plot.getX(); x < plot.getRight(); ++x)
            if (isCyan (x, plot.getY()) || isCyan (x, plot.getY() + 1))
                ++along;
        check (along <= 12, "the crest grazes the edge, it does not run across it ("
               + String (along) + " px)");
        auto fallsBelow = [&] (int x)
        {
            if (x < plot.getX() + 1 || x >= plot.getRight() - 1)
                return false;
            for (int y = plot.getY(); y < plot.getBottom(); ++y)
                if (isCyan (x, y))
                    return y - topRow >= 8;
            return false;
        };
        check (fallsBelow (crestCol - 25) && fallsBelow (crestCol + 25),
               "the curve turns down on both sides of the crest");

        // The plot is an input as well as a display. Half a plot width is one and a
        // half decades, so the mapping is the axis the eye is looking at; the wheel
        // is one percent of the normalised span, five with Shift.
        {
            auto pg = makeGuiProcessor();
            ScopeComponent live (*pg, ScopeComponent::FilterOnly);
            live.setSize (210, 130);
            const auto lp = live.responsePlot();
            setFloat (*pg, ParamIDs::cutoff, 900.0f);
            setFloat (*pg, ParamIDs::res, 0.5f);

            live.dragForTests (lp.getWidth() / 6.0f, 0.0f);
            check (std::abs (pg->getCutoffHz() - 900.0f * std::sqrt (10.0f)) < 5.0f,
                   "a sixth of the plot is half a decade of cutoff (" + String (pg->getCutoffHz(), 0) + " Hz)");
            live.dragForTests (lp.getWidth(), 0.0f);
            check (std::abs (pg->getCutoffHz() - 8000.0f) < 0.5f,
                   "a long throw parks the cutoff on the top of its range");
            live.dragForTests (0.0f, -lp.getHeight());
            check (std::abs (pg->getResonance() - 1.0f) < 0.001f,
                   "dragging to the top of the plot opens the resonance");
            live.dragForTests (0.0f, lp.getHeight());
            check (pg->getResonance() <= 0.001f, "and dragging to the bottom closes it");

            setFloat (*pg, ParamIDs::cutoff, 900.0f);
            const float n0 = pg->state().getParameter (ParamIDs::cutoff)->getValue();
            live.wheelForTests (1.0f, false);
            check (std::abs (pg->state().getParameter (ParamIDs::cutoff)->getValue() - (n0 + 0.01f)) < 0.001f,
                   "one wheel notch is one percent of the span");
            live.wheelForTests (1.0f, true);
            check (std::abs (pg->state().getParameter (ParamIDs::cutoff)->getValue() - (n0 + 0.06f)) < 0.001f,
                   "Shift makes it five");

            // Only the response box answers: the wave half has nothing to grab.
            ScopeComponent quiet (*pg, ScopeComponent::WaveOnly);
            quiet.setSize (210, 130);
            const float before = pg->getCutoffHz();
            quiet.dragForTests (100.0f, 100.0f);
            quiet.wheelForTests (5.0f, true);
            check (std::abs (pg->getCutoffHz() - before) < 0.001f,
                   "the wave scope ignores the same gestures");
        }

        // And the knob really does follow: the attachment is the only route, so
        // the slider's own value has to land on the parameter the plot wrote.
        {
            auto pe = makeGuiProcessor();
            PluginEditor edg (*pe);
            edg.setSize (1180, 780);
            auto& scopeInEditor = edg.filterScopeForTests();
            const auto ep = scopeInEditor.responsePlot();
            check (ep.getWidth() > 100.0f, "the editor's response box has a plot to drag");
            scopeInEditor.dragForTests (ep.getWidth() / 6.0f, 0.0f);
            scopeInEditor.wheelForTests (2.0f, false);
            auto* knob = edg.knobForTests (ParamIDs::cutoff);
            check (knob != nullptr
                     && std::abs (static_cast<float> (knob->getValue()) - pe->getCutoffHz()) < 0.5f,
                   "the Cutoff knob reads the value the plot wrote");
            auto* resKnob = edg.knobForTests (ParamIDs::res);
            scopeInEditor.dragForTests (0.0f, -ep.getHeight() * 0.5f);
            check (resKnob != nullptr
                     && std::abs (static_cast<float> (resKnob->getValue()) - pe->getResonance()) < 0.001f,
                   "the Res knob follows the vertical half of the same drag");
        }
    }

    {
        // The ballistics are pure numbers, so the release can be checked without a
        // window: the point of the port is that the fall is dB per *second*.
        MeterBallistics m;
        m.update (0.5f, 0.5f, 1.0f, 1.0 / 30.0);
        check (std::abs (m.holdDb() + 6.02f) < 0.1f && std::abs (m.barDb() + 6.02f) < 0.1f,
               "half scale reads -6 dB (" + String (m.holdDb(), 2) + ")");
        check (! m.clipped(), "half scale is not a clip");

        for (int i = 0; i < 40; ++i)
            m.update (0.0f, 0.0f, 1.0f, 1.0 / 30.0);
        check (std::abs (m.holdDb() + 6.02f) < 0.1f,
               "the peak holds through the first second and a half (" + String (m.holdDb(), 2) + ")");
        m.update (0.0f, 0.0f, 1.0f, 0.2);
        check (std::abs (m.holdDb() + 10.02f) < 0.1f,
               "then it falls 20 dB per second (" + String (m.holdDb(), 2) + ")");
        m.update (0.0f, 0.0f, 1.0f, 0.1);
        check (std::abs (m.holdDb() + 12.02f) < 0.1f,
               "one tenth of a second is two decibels (" + String (m.holdDb(), 2) + ")");
        check (m.holdDb() >= m.barDb(), "the held peak never sinks under the bar");

        MeterBallistics fast;
        for (int i = 0; i < 30; ++i)
            fast.update (0.0f, 0.0f, 1.0f, 1.0 / 30.0);
        check (std::abs (fast.barDb() + 60.0f) < 0.5f,
               "the bar falls 40 dB per second (" + String (fast.barDb(), 1) + ")");
        fast.update (0.0f, 0.0f, 1.0f, 10.0);
        check (fast.barDb() >= MeterBallistics::kFloorDb,
               "a stalled paint thread does not run the decay past the floor");

        MeterBallistics clip;
        clip.update (1.2f, 0.9f, 1.0f, 1.0 / 30.0);
        check (clip.clipped(), "over full scale latches the clip lamp");
        for (int i = 0; i < 28; ++i)
            clip.update (0.0f, 0.0f, 1.0f, 1.0 / 30.0);
        check (clip.clipped(), "the clip lamp stays up for a second");
        for (int i = 0; i < 8; ++i)
            clip.update (0.0f, 0.0f, 1.0f, 1.0 / 30.0);
        check (! clip.clipped(), "and goes after that");

        MeterBallistics gr;
        gr.update (0.5f, 0.5f, 0.5f, 1.0 / 30.0);
        check (std::abs (gr.grDb() - 6.02f) < 0.1f, "half gain reduction is 6 dB");
        for (int i = 0; i < 3; ++i)
            gr.update (0.5f, 0.5f, 1.0f, 1.0 / 30.0);
        check (gr.grDb() > 1.0f, "a short grab of the limiter is still visible ("
               + String (gr.grDb(), 1) + " dB)");

        check (MeterBallistics::fraction (MeterBallistics::kFloorDb) == 0.0f
                   && MeterBallistics::fraction (MeterBallistics::kTopDb) == 1.0f,
               "the dB to pixel mapping spans the whole bar");
        check (MeterBallistics::fraction (-120.0f) == 0.0f
                   && MeterBallistics::fraction (12.0f) == 1.0f,
               "and clamps outside the window");

        // The component itself: nothing at rest, a bar up to its dB mark once driven.
        auto p = makeGuiProcessor();
        MeterComponent meter (*p, true);
        const auto quiet = paintOffscreen (meter, 72, 30);
        meter.setLevelsForTests (0.5f, 0.4f, 0.7f);
        const auto loud = paintOffscreen (meter, 72, 30);

        int changed = 0, grChanged = 0;
        for (int x = 4; x < 68; ++x)
        {
            for (int y = 11; y < 26; ++y)
                if (loud.getPixelAt (x, y) != quiet.getPixelAt (x, y))
                    ++changed;
            if (loud.getPixelAt (x, 27) != quiet.getPixelAt (x, 27))
                ++grChanged;
        }
        check (changed > 100, "a driven meter paints its bar (" + String (changed) + " px)");
        check (grChanged > 40, "and the gain-reduction strip (" + String (grChanged) + " px)");

        // The fill has to stop where the decibel axis says, not at a tuned fraction.
        const float barW = 72.0f - 8.0f;
        const int expectEnd = 4 + roundToInt (barW * MeterBallistics::fraction (meter.barDbForTests()) - 2.0f);
        int fillEnd = 4;
        for (int x = 5; x < 68; ++x)
            if (loud.getPixelAt (x, 18) != quiet.getPixelAt (x, 18))
                fillEnd = x;
            else
                break;
        check (std::abs (fillEnd - expectEnd) <= 2,
               "the bar ends on its dB mark (" + String (fillEnd) + " vs " + String (expectEnd) + ")");

        // The audio thread pushes one peak per block and the GUI reads one number
        // per 33 ms: the pushes in between must survive, not overwrite each other.
        auto pm = makeGuiProcessor();
        pm->releaseResources();
        pm->pushMeterPeak (0.25f);
        pm->pushMeterPeak (0.75f);
        pm->pushMeterPeak (0.1f);
        check (std::abs (pm->takeMeterPeak() - 0.75f) < 1.0e-6f,
               "the meter keeps the loudest block of the poll window");
        check (pm->takeMeterPeak() == 0.0f, "reading the peak opens the next window");
        pm->pushMeterPeak (0.5f);
        pm->releaseResources();
        check (pm->takeMeterPeak() == 0.0f, "a stopped engine drops the meter state");
    }

    {
        // The editor must survive a resize without losing its children's bounds.
        auto p = makeGuiProcessor();
        SequenceEditorComponent editor (*p);
        for (auto size : { 300, 700, 1150 })
        {
            editor.setScale (static_cast<float> (size) / 1150.0f);
            auto image = paintOffscreen (editor, size, 270);
            check (image.isValid(), "the grid paints at " + String (size) + " px");
        }
    }

    {
        // Per-step velocity: soft bars paint dimmer than full ones and the
        // percentage caption appears on the drag.
        auto p = makeGuiProcessor();

        auto velImage = [&] (double vel)
        {
            Sequence s;
            s.setNumSteps (16);
            for (int i = 0; i < 16; ++i)
                s.steps[static_cast<size_t> (i)] = static_cast<juce::uint16> (Sequence::kNoteBase + 0);
            s.setVelocity (0, static_cast<float> (vel));
            p->setSequence (s);
            SequenceEditorComponent editor (*p);
            auto image = paintOffscreen (editor, 1150, 270);
            // The bottom-most pitch row is degree 0, which every step here fills:
            // sample it and average the red channel (the bar covers the whole cell).
            const double rowH = (270.0 - 18.0 - 2.0) / 18.0;
            const double pitchBottom = 18.0 + rowH * 14.0;
            double sum = 0.0;
            int count = 0;
            const int x0 = 30, x1 = 66;
            const int y0 = static_cast<int> (pitchBottom - rowH + 4);
            const int y1 = static_cast<int> (pitchBottom - 4);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                {
                    sum += image.getPixelAt (x, y).getFloatRed();
                    ++count;
                }
            return sum / count;
        };

        const double full = velImage (1.0);
        const double soft = velImage (0.25);
        check (soft < full * 0.85, "a low-velocity bar paints dimmer ("
               + String (soft, 3) + " vs " + String (full, 3) + ")");

        // The VEL row under the grid: full bars rise higher than soft ones.
        Sequence sv;
        sv.setNumSteps (16);
        for (int i = 0; i < 16; ++i)
            sv.steps[static_cast<size_t> (i)] = static_cast<juce::uint16> (Sequence::kNoteBase + 0);
        sv.setVelocity (0, 1.0f);    // one full, one soft in the same pattern
        sv.setVelocity (1, 0.25f);
        p->setSequence (sv);
        SequenceEditorComponent velEditor (*p);
        auto velRowImage = paintOffscreen (velEditor, 1150, 270);
        const double rowHv = (270.0 - 18.0 - 2.0) / 18.0;
        const double velRowTop = 18.0 + rowHv * 17.0;
        auto amberIn = [&velRowImage, velRowTop, rowHv] (int x0, int x1)
        {
            int full = 0;
            for (int y = static_cast<int> (velRowTop); y < static_cast<int> (velRowTop + rowHv); ++y)
                for (int x = x0; x < x1; ++x)
                {
                    const auto c = velRowImage.getPixelAt (x, y);
                    if (c.getFloatRed() > 0.5f && c.getFloatRed() - c.getFloatBlue() > 0.25f)
                        ++full;
                }
            return full;
        };
        // column widths: grid starts at 29 px, step = (1150 - 30)/16
        const int stepPx = static_cast<int> ((1150.0 - 30.0) / 16.0);
        const int fullCol = amberIn (29 + 1, 29 + stepPx - 2);
        const int softCol = amberIn (29 + stepPx + 1, 29 + 2 * stepPx - 2);
        check (fullCol > softCol + 2, "the VEL row bar is taller at full velocity ("
               + String (fullCol) + " vs " + String (softCol) + ")");
        save (velRowImage, "vel-row.png");
        SequenceEditorComponent velShot (*p);
        save (paintOffscreen (velShot, 1150, 270), "velocity-grid.png");
    }

    // Cyrillic rendering probe: one page, every theme font style over one string —
    // catches byte-split literals and bold-face fallback at a glance.
    {
        const auto& th = Skins::current();
        Image img (Image::ARGB, 460, 140, true);
        Graphics pg (img);
        pg.fillAll (th.background);

        const juce::String s = U8 ("Руководство Проверка Эмуляция");
        pg.setColour (th.text);
        pg.setFont (Theme::font (15.0f));
        pg.drawText (s + "  [plain]", 10, 4, 440, 20, Justification::centredLeft);
        pg.setFont (Theme::font (15.0f, juce::Font::bold));
        pg.setColour (th.accent);
        pg.drawText (s + "  [bold]", 10, 26, 440, 20, Justification::centredLeft);
        pg.setFont (Theme::font (13.0f));
        pg.setColour (th.textDim);
        pg.drawText (s + "  [dim]", 10, 48, 440, 20, Justification::centredLeft);
        save (img, "utf8-probe.png");
    }

    {
        // The manual paints in both languages and switches with the toggle button.
        ManualContent manual;
        manual.setRussian (false);
        manual.relayout (660);
        auto enImage = paintOffscreen (manual, 660, manual.getHeight());
        check (pixelsLike (enImage, [] (Colour c) { return isAmber (c); }) > 5,
               "the manual paints its amber headings");
        save (enImage, "manual-EN.png");

        manual.setRussian (true);
        auto ruImage = paintOffscreen (manual, 660, manual.getHeight());
        // RU and EN must differ and both keep a dark page.
        auto diffAt = [&] (const Image& a, const Image& b)
        {
            int diff = 0;
            for (int y = 0; y < a.getHeight() && y < b.getHeight(); y += 4)
                for (int x = 0; x < a.getWidth() && x < b.getWidth(); x += 4)
                    if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                        ++diff;
            return diff;
        };
        const int changed = diffAt (enImage, ruImage);
        check (changed > 20, "switching to Russian rewrites the page ("
               + String (changed) + " pixels)");
        save (ruImage, "manual-RU.png");

        // The real window path: the language button's handler runs synchronously
        // through the same code a real click invokes.
        {
            ManualView view;
            view.setSize (660, 560);
            auto before = paintOffscreen (view, 660, 560);
            if (auto* manual = dynamic_cast<ManualContent*> (view.getViewedComponent()))
            {
                manual->toggleLanguage();
                if (auto* langButton = dynamic_cast<juce::TextButton*> (manual->getChildComponent (0)))
                    std::cout << "MBTN   button text after toggle: '" << langButton->getButtonText()
                              << "', content " << manual->getWidth() << "x" << manual->getHeight()
                              << std::endl;
            }
            auto after = paintOffscreen (view, 660, 560);
            check (diffAt (before, after) > 20,
                   "toggling the language reflows the manual through the viewport");
        }

        // The editor paints offscreen with the filter selector under Cutoff
        // (nothing crashy, real layout, real children bounds).
        auto p = makeGuiProcessor();
        PluginEditor ed (*p);
        ed.setSize (1180, 780);
        auto editorImage = paintOffscreen (ed, 1180, 780);
        check (editorImage.isValid(), "the editor paints offscreen");
        int comboText = 0;
        for (int y = 198; y < 220; ++y)
            for (int x = 190; x < 296; ++x)
            {
                const auto c = editorImage.getPixelAt (x, y);
                if (c.getFloatRed() > 0.55f && c.getFloatGreen() > 0.55f)
                    ++comboText;
            }
        check (comboText > 3, "the filter model combo paints under Cutoff ("
               + String (comboText) + " text pixels)");

        // The UiSkin parameter reaches the paint, and each skin's whole window
        // goes to disk for looking at. The palette is process-global, so the
        // loop hands it back to Dark when it is done.
        const StringArray kSkinTags { "dark", "console", "wood", "metal" };
        const Rectangle<int> panelBand { 20, 60, 410, 40 };    // pocket above the knobs
        const Rectangle<int> faceBand  { 890, 410, 270, 80 };  // bare plate, right of the sequencer
        // The Drive knob: 44 px wide, centred at (56, 322), dome radius 11.5.
        // JUCE's default rotary sweep leaves the gap at the bottom, so the
        // probe 15 px below the centre is bare plate on Dark, which draws its
        // knobs flat and casts nothing.
        const float knobCx = 56.0f, knobCy = 322.0f, knobBodyR = 11.5f;

        for (int skin = 0; skin < kFaceCount; ++skin)
        {
            auto ps = makeGuiProcessor();
            setSkin (*ps, skin);
            PluginEditor eds (*ps);
            eds.setSize (1180, 780);
            auto skinImage = paintOffscreen (eds, 1180, 780);
            const String tag = "skin-" + String (skin) + "-" + kSkinTags[skin];
            save (skinImage, tag + ".png");
            save (crop (skinImage, { 24, 288, 96, 76 }), tag + "-knob44.png");
            save (crop (skinImage, { 40, 306, 32, 32 }), tag + "-knob16.png");
            save (crop (skinImage, { 884, 52, 284, 344 }), tag + "-gate.png");
            save (crop (skinImage, { 12, 504, 1156, 268 }), tag + "-seq.png");

            const auto& th = Skins::all()[static_cast<size_t> (skin)];
            check (contrastRatio (th.text, th.panel) >= 4.5,
                   tag + ": body text clears 4.5:1 on the pocket ("
                   + String (contrastRatio (th.text, th.panel), 2) + ":1)");
            check (contrastRatio (th.textDim, th.panel) >= 3.0,
                   tag + ": captions clear 3:1 on the pocket ("
                   + String (contrastRatio (th.textDim, th.panel), 2) + ":1)");
            check (contrastRatio (th.chipText, th.chip) >= 3.0,
                   tag + ": scope captions clear 3:1 on their chip ("
                   + String (contrastRatio (th.chipText, th.chip), 2) + ":1)");

            const auto panel = bandStats (skinImage, panelBand);
            const auto face = bandStats (skinImage, faceBand);

            if (skin == 0)
            {
                // Dark is the baseline: nothing here may have moved.
                check (panel.meanL < 0.16 && face.meanL < 0.16,
                       "dark stays a flat, untextured plate (panel L " + String (panel.meanL, 3) + ")");
                check (pixelsLike (skinImage, [] (Colour c) { return c == Colour (0xff22262d); }) > 3000,
                       "dark keeps the original pocket literal");
                check (pixelsLike (skinImage, [] (Colour c) { return c == Colour (0xff181b20); }) > 3000,
                       "dark keeps the original face literal");
                const float below = static_cast<float> (luminance (
                    skinImage.getPixelAt (roundToInt (knobCx), roundToInt (knobCy + knobBodyR + 1.5f))));
                check (std::abs (below - static_cast<float> (panel.meanL)) < 0.012f,
                       "dark draws no contact shadow under the knob ("
                       + String (below, 3) + " vs pocket " + String (panel.meanL, 3) + ")");
                continue;
            }

            int changed = 0;
            for (int y = 60; y < 100; ++y)
                for (int x = 20; x < 430; ++x)
                    if (skinImage.getPixelAt (x, y) != editorImage.getPixelAt (x, y))
                        ++changed;
            check (changed > 4000, tag + " repaints the face band (" + String (changed) + " px)");

            // A material skin must never fall back on the old flat dome colour.
            check (pixelsLike (skinImage, [] (Colour c) { return c == Colour (0xff464e5c); }) == 0,
                   tag + " has no leftover flat-plastic dome");

            check (face.spread > 0.0035 || panel.spread > 0.0035,
                   tag + " shows a real material (face " + String (face.spread, 4)
                   + ", pocket " + String (panel.spread, 4) + ")");

            if (skin == 1)
            {
                check (face.meanB > face.meanR, "console plate reads cold (b "
                       + String (face.meanB, 2) + " > r " + String (face.meanR, 2) + ")");
                const int cyan = pixelsLike (crop (skinImage, { 12, 52, 424, 200 }), [] (Colour c)
                {
                    return c.getFloatBlue() > 0.55f && c.getFloatGreen() > 0.50f && c.getFloatRed() < 0.45f;
                });
                check (cyan > 30, "console lamps are cyan (" + String (cyan) + " px)");
                const int tape = pixelsLike (crop (skinImage, { 0, 400, 10, 300 }), [] (Colour c)
                {
                    return c.getFloatRed() > 0.55f && c.getFloatGreen() > 0.35f && c.getFloatBlue() < 0.35f;
                });
                check (tape > 100, "console carries hazard tape down the edge ("
                       + String (tape) + " px)");
            }

            if (skin == 2)
            {
                check (face.meanR > face.meanG && face.meanG > face.meanB
                       && face.meanR - face.meanB > 0.06,
                       "walnut face is a warm brown (r " + String (face.meanR, 2) + " g "
                       + String (face.meanG, 2) + " b " + String (face.meanB, 2) + ")");
                const double sat = (face.meanR - face.meanB) / std::max (face.meanR + face.meanB, 0.01);
                check (sat > 0.10 && sat < 0.55, "walnut stays a brown, not an orange ("
                       + String (sat, 2) + ")");
                check (face.spread > 0.004, "walnut grain breaks up the plate (spread "
                       + String (face.spread, 4) + ")");
                check (panel.meanR > panel.meanB, "the pocket is the cream aluminium, not the wood");
            }

            if (skin == 3)
            {
                const double sat = (face.meanR - face.meanB) / std::max (face.meanR + face.meanB, 0.01);
                check (std::abs (face.meanR - face.meanG) < 0.05 && std::abs (face.meanG - face.meanB) < 0.06,
                       "anodised plate is neutral grey (r " + String (face.meanR, 2) + " g "
                       + String (face.meanG, 2) + " b " + String (face.meanB, 2) + ")");
                check (sat < 0.12, "the metal skin does not tint warm (" + String (sat, 2) + ")");
                const int screws = pixelsLike (crop (skinImage, { 12, 52, 424, 200 }), [] (Colour c)
                {
                    return c.getFloatRed() > 0.62f && c.getFloatRed() - c.getFloatBlue() < 0.12f
                           && c.getFloatGreen() > 0.60f;
                });
                check (screws > 20, "metal pockets are screwed down (" + String (screws) + " screw px)");
            }

            // The cut has to read as a cut: brightness has to turn around once per
            // rib as you walk the rim. Only the knurled finish claims a rib count.
            if (th.knurlCount > 0)
            {
                const int ribs = ribSignChanges (skinImage, knobCx, knobCy, knobBodyR - 2.0f);
                check (ribs >= th.knurlCount / 2,
                       tag + " rim reads as " + String (ribs) + " brightness turns, at least half of the "
                       + String (th.knurlCount) + " ribs");
            }
        }
        Skins::select (0);

        // A cabinet is only paint: two identical machines, one of them dressed
        // in walnut, have to hand back the same samples block for block.
        {
            auto renderWithSkin = [] (int skin)
            {
                auto p = makeGuiProcessor();
                p->setRateAndBufferSizeDetails (48000.0, 512);
                p->prepareToPlay (48000.0, 512);
                if (auto* sync = dynamic_cast<AudioParameterChoice*> (
                        p->state().getParameter (ParamIDs::syncMode)))
                    sync->setValueNotifyingHost (1.0f);
                if (auto* play = dynamic_cast<AudioParameterBool*> (
                        p->state().getParameter (ParamIDs::manualPlay)))
                    play->setValueNotifyingHost (1.0f);
                setSkin (*p, skin);
                AudioBuffer<float> buf (2, 512);
                MidiBuffer midi;
                std::vector<float> out;
                for (int b = 0; b < 24; ++b)
                {
                    buf.clear();
                    p->processBlock (buf, midi);
                    const float* ch = buf.getReadPointer (0);
                    out.insert (out.end(), ch, ch + 512);
                }
                return out;
            };
            const auto plain = renderWithSkin (0);
            const auto walnut = renderWithSkin (2);
            double peak = 0.0;
            for (const float s : plain)
                peak = jmax (peak, static_cast<double> (std::abs (s)));
            check (peak > 0.001, "the skin A/B actually heard the synth ("
                   + String (peak, 3) + " peak)");
            check (plain.size() == walnut.size()
                     && std::equal (plain.begin(), plain.end(), walnut.begin()),
                   "no skin changes a single audio sample");

            // AUv2 numbers parameters by position, so a new one must stay last.
            auto p = makeGuiProcessor();
            const String last = p->getParameters().getLast()->getName (64);
            check (last == "Skin", "UiSkin is the appended-last parameter (" + last + ")");
            Skins::select (0);
        }

        // Drive and Dist Mix are rotary knobs again; the knob body paints in
        // its slot with the accent colour on ring/indicator.
        int drivePixels = 0;
        for (int y = 300; y < 346; ++y)
            if (editorImage.getPixelAt (48, y) != Colour (0xff22262d))
                ++drivePixels;
        check (drivePixels > 20, "the Drive knob paints a body in its slot ("
               + String (drivePixels) + " pixels)");
        int mixPixels = 0;
        for (int x = 106; x < 162; ++x)
            if (editorImage.getPixelAt (x, 322) != Colour (0xff22262d))
                ++mixPixels;
        check (mixPixels > 20, "the Dist Mix knob paints a body in its slot ("
               + String (mixPixels) + " pixels)");
        // The editor paints and rescales at several widths like the sequence
        // editor does; the knobs must keep painting at low uiScale too.
        for (int w : { 300, 700, 1180 })
        {
            auto p = makeGuiProcessor();
            PluginEditor ed (*p);
            ed.setSize (w, 780 * w / 1180);
            auto scaledImage = paintOffscreen (ed, w, 780 * w / 1180);
            check (scaledImage.isValid(), "the editor paints offscreen at width " + String (w));
            int knobPixels = 0;
            const int yBand = 300 * w / 1180, xColumn = 48 * w / 1180;
            for (int y = yBand; y < yBand + 46 * w / 1180; ++y)
                if (scaledImage.getPixelAt (xColumn, y) != Colour (0xff181b20)
                    && scaledImage.getPixelAt (xColumn, y) != Colour (0xff22262d))
                    ++knobPixels;
            check (knobPixels > (46 * w / 1180) / 2, "the Drive knob stays visible at width " + String (w)
                   + " (" + String (knobPixels) + " pixels)");
        }

        // The level meter paints in the header row, right of the tempo readout.
        int meterPixels = 0;
        for (int y = 10; y < 42; ++y)
        {
            const auto c = editorImage.getPixelAt (1090, y);
            if (c != Colour (0xff22262d) && c != Colour (0xff181b20))
                ++meterPixels;
        }
        check (meterPixels > 10, "the header meter paints its bar ("
               + String (meterPixels) + " pixels)");

        // The Lim toggle sits in the same header row, right of the meter, and
        // switches the limiter parameter both ways.
        int limPixels = 0;
        for (int y = 10; y < 40; ++y)
            for (int x = 1140; x < 1176; ++x)
            {
                const auto c = editorImage.getPixelAt (x, y);
                if (c != Colour (0xff22262d) && c != Colour (0xff181b20))
                    ++limPixels;
            }
        check (limPixels > 60, "the Lim button paints in the header row next to the meter ("
               + String (limPixels) + " pixels)");
        {
            // The Lim button flips the LimOn parameter on every click, whatever
            // state the host or a preset left it in.
            auto* limOn = dynamic_cast<AudioParameterBool*> (p->state().getParameter (ParamIDs::limiterOn));
            const bool before = limOn->get();
            auto& lim = ed.limiterButtonForTests();
            lim.onClick();
            check (limOn->get() != before, "the Lim button turns the limiter on");
            lim.onClick();
            check (limOn->get() == before, "the Lim button turns the limiter off again");
        }

        // The preset control lives in the header row: field plus prev/next
        // arrows, in the slot the plain preset button used to occupy. Counted
        // against the panel colour, not a blue level — the two font engines
        // hint the same name into different ink weights and the old threshold
        // failed on Windows for that reason alone.
        int headerInk = 0;
        for (int y = 12; y < 38; ++y)
            for (int x = 470; x < 660; ++x)
            {
                const auto c = editorImage.getPixelAt (x, y);
                if (c != Colour (0xff22262d) && c != Colour (0xff181b20))
                    ++headerInk;
            }
        check (headerInk > 400, "the header preset field and arrows paint ("
               + String (headerInk) + " ink pixels)");
        check (ed.presetFieldForTests().getBounds()
                    == Rectangle<int> (ed.presetPrevButtonForTests().getBounds().getRight() + 4, 10, 132, 30)
                 && ed.presetNextButtonForTests().getBounds().getX() == ed.presetFieldForTests().getBounds().getRight() + 2
                 && ed.presetNextButtonForTests().getBounds().getRight() <= ed.playButtonForTests().getBounds().getX(),
               "the preset field and its arrows fill the old header button slot");

        // The Lim toggle ends on the same vertical as the GATE panel (x=1168)
        // and the meter stops short of it.
        {
            const auto lim = ed.limiterButtonForTests().getBounds();
            check (lim.getRight() == 884 + 284, "the Lim button ends at the GATE panel edge ("
                   + String (lim.getRight()) + ")");
            check (lim.getRight() <= ed.getWidth() - 12, "Lim stays inside the window");
            check (ed.meterForTests().getBounds().getRight() <= lim.getX(),
                   "the meter does not touch the Lim button");
        }

        // Nothing is painted in the column between the gate and the grid any
        // more: that is where the PRESET panel used to bleed into the sequencer.
        int strayPanel = 0;
        for (int y = 400; y < 466; y += 2)
            for (int x = 884; x < 1168; x += 2)
                if (editorImage.getPixelAt (x, y) == Colour (0xff22262d))
                    ++strayPanel;
        check (strayPanel == 0, "the removed PRESET panel leaves that column empty ("
               + String (strayPanel) + " panel pixels)");

        // The FX strip lives under the scopes: the combo and the five faders
        // sit on their native cells, and each fader is the whole cell.
        {
            auto* fxMix = ed.knobForTests (ParamIDs::fxDelMix);
            check (fxMix != nullptr && fxMix->getBounds() == Rectangle<int> (514, 382, 62, 20),
                   "the delay mix fader fills its FX cell ("
                   + (fxMix != nullptr ? fxMix->getBounds().toString() : String ("missing")) + ")");
            auto* revDecay = ed.knobForTests (ParamIDs::fxRevDecay);
            check (revDecay != nullptr && revDecay->getBounds() == Rectangle<int> (786, 382, 62, 20),
                   "the reverb decay fader sits at the end of the FX strip");
        }

        // The preset menu marks the loaded row by inverting it: light band with
        // dark glyphs, where a plain row is the opposite way round.
        {
            auto rowImage = [] (Component& item)
            {
                Image image (Image::ARGB, 260, 24, true);
                Graphics g (image);
                g.fillAll (Skins::current().panel);
                item.setBounds (0, 0, 260, 24);
                item.paint (g);
                return image;
            };
            PresetMenuItem plainRow { "Acid Line", &ed, false };
            PresetMenuItem loadedRow { "Acid Line", &ed, true };
            auto plain = rowImage (plainRow);
            auto loaded = rowImage (loadedRow);

            const auto light = [] (Colour c) { return c.getFloatRed() > 0.7f && c.getFloatBlue() > 0.6f; };
            // The band covers most of the row, so the loaded row is mostly light
            // while an ordinary row has light pixels only where the glyphs are.
            check (pixelsLike (loaded, light) > 500,
                   "the loaded preset row inverts to a light band");
            check (pixelsLike (plain, light) < 60,
                   "an ordinary preset row stays dark");
            save (loaded, "menu-row-loaded.png");
            save (plain, "menu-row-plain.png");
        }

        // The note is ordinary text in its column: no password mask, what is
        // typed lands on the processor, and the column that shows it is the
        // column that edits it.
        {
            auto& note = ed.presetNoteEditorForTests();
            check (note.getPasswordCharacter() == 0, "the preset note is not masked");
            const String before = p->getPresetNote();
            note.setText ("fresh line", dontSendNotification);
            note.onTextChange();
            check (p->getPresetNote() == "fresh line", "typing the note stores it");

            // Painted as letters, not dots, in the credit column: while the edit
            // is open that rectangle gains readable glyphs. Ink is counted
            // against the panel, not by brightness — the two font engines hint
            // the same glyphs into different weights.
            ed.beginPresetNoteEdit();
            check (note.getBounds() == ed.creditForTests().getBounds(),
                   "the note editor opens over the column that shows the note");
            auto withNote = paintOffscreen (ed, 1180, 780);
            const auto fieldInk = withNote.getPixelAt (886, 471);
            int noteGlyphs = 0;
            for (int y = 470; y < 492; ++y)
                for (int x = 886; x < 1166; ++x)
                    if (withNote.getPixelAt (x, y).getBrightness() > fieldInk.getBrightness() + 0.15f)
                        ++noteGlyphs;
            check (noteGlyphs > 30, "the note renders as plain text in the credit column ("
                   + String (noteGlyphs) + " glyph pixels)");
            save (withNote, "editor-preset-note.png");
            ed.endPresetNoteEdit();

            // The whole note still lives in the header field's tooltip too.
            ed.timerCallback();
            check (ed.presetFieldForTests().getTooltip().contains ("fresh line"),
                   "the field tooltip carries the whole note");

            // Enter saves: the file carries the new comment and the column shows
            // it without a program change having to happen.
            auto pc = makeGuiProcessor();
            check (pc->saveUserPreset ("GuiNoteColumn"), "the note-column preset is written");
            pc->setCurrentProgram (0);   // a save marks its preset as loaded; start clean
            pc->loadUserPreset ("GuiNoteColumn");
            PluginEditor edc (*pc);
            edc.setSize (1180, 780);
            edc.beginPresetNoteEdit();
            auto& colNote = edc.presetNoteEditorForTests();
            colNote.setText ("saved column note", dontSendNotification);
            colNote.onTextChange();
            colNote.onReturnKey();
            edc.timerCallback();
            check (edc.creditForTests().getText() == "saved column note",
                   "Enter pushes the note into the column that shows it");
            {
                PresetDef def;
                String error;
                const auto file = PluginProcessor::userPresetFolder()
                                      .getChildFile ("GuiNoteColumn.sp303preset");
                check (PresetFormat::parseText (file.loadFileAsString(), def, error)
                         && def.comment == "saved column note",
                       "the note column edit lands in the preset file");
            }

            // An empty note falls back to the factory attribution, so the credit
            // of a borrowed line never disappears with the note.
            pc->setPresetNote ({});
            pc->saveUserPreset ("GuiNoteColumn");
            pc->setCurrentProgram (0);
            edc.timerCallback();
            check (edc.creditForTests().getText() == factoryPresets()[0].credit,
                   "a preset without a note shows its factory credit");
            pc->deleteUserPreset ("GuiNoteColumn");

            note.setText (before, dontSendNotification);
            note.onTextChange();
            check (! ed.presetNoteEditorForTests().isVisible(),
                   "closing the edit hides the note editor again");
        }

        // The header field is the name editor: one click starts it, and the
        // commit callback the editor fires on Enter renames the preset on disk.
        // Headless there is no mouse, so setText with a notification is the same
        // seam as the editor handing its text back.
        {
            auto pr = makeGuiProcessor();
            const String src = "Gui Field Src", dst = "Gui Field Dst";
            pr->deleteUserPreset (src);
            pr->deleteUserPreset (dst);
            check (pr->saveUserPreset (src), "the rename source preset exists");
            pr->loadUserPreset (src);

            PluginEditor edr (*pr);
            edr.setSize (1180, 780);
            auto& field = edr.presetFieldForTests();
            check (field.isEditable() && field.isEditableOnSingleClick(),
                   "one click puts the preset name in edit mode");
            check (field.getText() == src, "the field shows the loaded preset name");

            // Dirtying the state adds the marker to the display only: what the
            // editor gets is the bare name, or the star would land in the file.
            setFloat (*pr, ParamIDs::cutoff, 1234.0f);
            edr.timerCallback();
            check (field.getText() == src + " *", "a dirty preset shows the modified marker");
            check (edr.presetNameForEdit() == src, "the marker stays out of the edited name");

            const int bankBefore = pr->getUserPresetNames().size();
            field.setText (dst, sendNotification);
            check (pr->currentUserPresetName() == dst, "Enter renames the loaded preset");
            check (pr->getUserPresetNames().contains (dst)
                     && ! pr->getUserPresetNames().contains (src),
                   "the bank carries the new name and drops the old one");
            check (pr->getUserPresetNames().size() == bankBefore,
                   "a rename does not add a preset to the bank");
            // A rename moves the file, it is not a save: the pending knob edit
            // stays pending, so the marker has to survive under the new name.
            check (pr->isProgramDirty() && field.getText() == dst + " *",
                   "a rename keeps the modified marker of the live state");

            // An empty name is a mis-click, not a rename to nothing.
            field.setText ("   ", sendNotification);
            check (pr->currentUserPresetName() == dst, "a blank name keeps the preset as it was");

            // A factory program is never edited in place: the typed name saves a
            // user copy and leaves the bank alone.
            pr->setCurrentProgram (2);
            const String factoryName = pr->getProgramName (2);
            std::ignore = pr->deleteUserPreset ("Gui Factory Copy");
            field.setText ("Gui Factory Copy", sendNotification);
            check (pr->currentUserPresetName() == "Gui Factory Copy",
                   "renaming a factory preset saves it as a user preset");
            check (pr->getProgramName (2) == factoryName, "the factory program keeps its own name");
            std::ignore = pr->deleteUserPreset ("Gui Factory Copy");
            std::ignore = pr->deleteUserPreset (dst);
            std::ignore = pr->deleteUserPreset (src);
        }

        {
            auto p2 = makeGuiProcessor();
            // The walk has to cross the factory/user boundary, so the combined
            // bank needs a user half even on a machine with no saved presets.
            const bool created = p2->getUserPresetNames().isEmpty();
            if (created)
                check (p2->saveUserPreset ("GuiCycleTest"),
                       "a temporary user preset is written for the cycle walk");
            p2->setCurrentProgram (0);   // a save marks its preset as loaded; start clean
            PluginEditor ed2 (*p2);
            ed2.setSize (1180, 780);
            const int total = p2->getNumPrograms() + static_cast<int> (p2->getUserPresetNames().size());

            ed2.cyclePreset (1);
            check (p2->getCurrentProgram() == 1, "the next arrow steps to another preset");
            ed2.cyclePreset (-1);
            check (p2->getCurrentProgram() == 0, "the previous arrow steps back");
            ed2.cyclePreset (-1);
            check (p2->isUserPresetLoaded() && p2->getCurrentProgram() == 0,
                   "backwards from the first program wraps into the user presets");
            ed2.cyclePreset (1);
            check (! p2->isUserPresetLoaded() && p2->getCurrentProgram() == 0,
                   "forward from the last user preset returns to the factory bank");
            for (int i = 0; i < total; ++i)
                ed2.cyclePreset (1);
            check (p2->getCurrentProgram() == 0 && ! p2->isUserPresetLoaded(),
                   "a full cycle through the bank returns to the start");
            if (created)
                p2->deleteUserPreset ("GuiCycleTest");
        }

        // The MIDI map combo is gone and its corridor took the two transport
        // knobs, so the row has to stay inside its panel at every width; the
        // 16 px floor on the knob size makes the small end the worst.
        for (int w : { 1180, 700, 300 })
        {
            auto p3 = makeGuiProcessor();
            PluginEditor ed3 (*p3);
            ed3.setSize (w, 780 * w / 1180);
            auto* swing = ed3.knobForTests (ParamIDs::swing);
            auto* bpm = ed3.knobForTests (ParamIDs::bpm);
            check (swing != nullptr && bpm != nullptr, "the transport knobs are there");
            const auto clear = ed3.clearButtonForTests().getBounds();
            const auto midi = ed3.midiButtonForTests().getBounds();
            const auto random = ed3.randomButtonForTests().getBounds();
            // The same fit-and-centre maths the editor applies in resized():
            // integer height first, then the smaller axis drives the scale.
            const int h = 780 * w / 1180;
            const float s = jmin (static_cast<float> (w) / 1180.0f,
                                  static_cast<float> (h) / 780.0f);
            const int ox = (w - static_cast<int> (1180 * s)) / 2;
            const auto panel = Rectangle<int> (ox + static_cast<int> (12 * s), 0,
                                               static_cast<int> (860 * s), h);
            check (! swing->getBounds().intersects (clear)
                     && ! swing->getBounds().intersects (random)
                     && ! swing->getBounds().intersects (midi)
                     && ! bpm->getBounds().intersects (swing->getBounds())
                     && ! clear.intersects (random) && ! clear.intersects (midi)
                     && ! midi.intersects (random),
                   "the Swing and BPM knobs clear the buttons at width " + String (w));
            check (panel.contains (bpm->getBounds()) && panel.contains (swing->getBounds())
                     && panel.contains (clear) && panel.contains (midi) && panel.contains (random),
                   "the whole sequencer row stays inside its panel at width " + String (w));
            check (clear.getX() > midi.getRight() && midi.getX() > random.getRight()
                     && swing->getBounds().getX() >= clear.getRight(),
                   "the row reads left to right: Random, MIDI, Clear, Swing, BPM at width " + String (w));
        }
        {
            // Geometry says no overlap; this says the same about the pixels —
            // the strip between the button and the knob must be bare panel.
            auto* swing = ed.knobForTests (ParamIDs::swing);
            const auto clear = ed.clearButtonForTests().getBounds();
            const int y = clear.getY() + clear.getHeight() / 2;
            int firstInk = clear.getRight() + 4;
            for (int x = clear.getRight() + 1; x < swing->getBounds().getX(); ++x)
                if (editorImage.getPixelAt (x, y) != Colour (0xff22262d))
                {
                    firstInk = x;
                    break;
                }
            check (firstInk >= clear.getRight() + 4,
                   "the Swing arc keeps its distance from the Clear button (first ink at "
                   + String (firstInk) + ", button ends " + String (clear.getRight()) + ")");
        }
        {
            // Geometry alone would pass for an invisible button, so the label has
            // to paint too. Counted against the panel and the window background,
            // not a brightness threshold, which the two font engines defeat.
            const auto midi = ed.midiButtonForTests().getBounds();
            int midiInk = 0;
            for (int y = midi.getY(); y < midi.getBottom(); ++y)
                for (int x = midi.getX(); x < midi.getRight(); ++x)
                {
                    const auto c = editorImage.getPixelAt (x, y);
                    if (c != Colour (0xff22262d) && c != Colour (0xff181b20))
                        ++midiInk;
                }
            check (midiInk > 120, "the MIDI button paints its label (" + String (midiInk) + " ink pixels)");
        }
        {
            // An import has to behave like every other sequencer edit: it goes
            // through the processor, so the version moves and the editor refreshes.
            auto p3 = makeGuiProcessor();
            PluginEditor ed3 (*p3);
            ed3.setSize (1180, 780);
            const int before = p3->getSequenceVersion();

            const auto dir = File::getSpecialLocation (File::tempDirectory)
                .getChildFile ("spiral303_gui_midi");
            dir.deleteRecursively();
            dir.createDirectory();

            String error;
            const File junk = dir.getChildFile ("junk.mid");
            junk.replaceWithData ("this is not a midi file at all", 30);
            check (! ed3.importMidi (junk, error) && error.isNotEmpty()
                     && p3->getSequenceVersion() == before,
                   "a file that is not MIDI leaves the pattern untouched and says why");

            juce::MidiFile mf;
            mf.setTicksPerQuarterNote (480);
            MidiMessageSequence track;
            for (int i = 0; i < 8; ++i)
            {
                const int pitch = 36 + (i % 4) * 3;
                track.addEvent (MidiMessage::noteOn (1, pitch, static_cast<juce::uint8> (110)), i * 240.0);
                track.addEvent (MidiMessage::noteOff (1, pitch), i * 240.0 + 120.0);
            }
            mf.addTrack (track);
            const File line = dir.getChildFile ("line.mid");
            {
                FileOutputStream out (line);
                check (mf.writeTo (out, 1), "the imported test line is written");
            }
            check (ed3.importMidi (line, error) && error.isEmpty()
                     && p3->getSequenceVersion() > before,
                   "the import is an ordinary sequencer edit: the version moves");

            const auto seq = p3->getSequence();
            int attacks = 0;
            for (int i = 0; i < seq.numSteps(); ++i)
                if (seq.isNote (i))
                    ++attacks;
            check (attacks == 8 && seq.numSteps() == 16, "eight notes of the file become eight attacks");
            check (seq.baseMidi == 36 && seq.scaleIndex == 2,
                   "and the key and scale of the pattern survive the import (base "
                   + String (seq.baseMidi) + ", scale " + String (seq.scaleIndex) + ")");
            dir.deleteRecursively();
        }

        // Enter in the note field is the save gesture: the preset goes to disk.
        {
            auto p3 = makeGuiProcessor();
            check (p3->saveUserPreset ("GuiNoteSave"), "the note-save preset is written");
            p3->setCurrentProgram (0);   // a save marks its preset as loaded; start clean
            check (p3->loadUserPreset ("GuiNoteSave"), "the note-save preset loads back");
            PluginEditor ed3 (*p3);
            ed3.setSize (1180, 780);
            ed3.beginPresetNoteEdit();
            auto& note = ed3.presetNoteEditorForTests();
            note.setText ("Enter saved me", dontSendNotification);
            note.onTextChange();
            note.onReturnKey();
            check (! note.isVisible(), "Enter ends the note edit");

            PresetDef def;
            String error;
            const auto file = PluginProcessor::userPresetFolder().getChildFile ("GuiNoteSave.sp303preset");
            check (PresetFormat::parseText (file.loadFileAsString(), def, error)
                     && def.comment == "Enter saved me",
                   "Enter writes the note into the user preset file");
            check (! p3->isProgramDirty(), "saving clears the edited marker");
            check (p3->currentUserPresetName() == "GuiNoteSave", "the preset keeps its own name");
            p3->deleteUserPreset ("GuiNoteSave");
        }
        {
            // A factory program is never edited in place, so its note travels
            // into a copy — and a second Enter rewrites that copy instead of
            // nesting "(edited) (edited)".
            auto p4 = makeGuiProcessor();
            p4->setCurrentProgram (2);
            const String copy = p4->getProgramName (2) + " (edited)";
            PluginEditor ed4 (*p4);
            ed4.setSize (1180, 780);
            ed4.beginPresetNoteEdit();
            auto& note = ed4.presetNoteEditorForTests();
            note.setText ("factory copy", dontSendNotification);
            note.onTextChange();
            note.onReturnKey();
            check (p4->getUserPresetNames().contains (copy),
                   "a factory note is saved into an (edited) copy");
            check (p4->currentUserPresetName() == copy, "the copy becomes the loaded preset");

            ed4.beginPresetNoteEdit();
            note.setText ("still one copy", dontSendNotification);
            note.onTextChange();
            note.onReturnKey();
            check (! p4->getUserPresetNames().contains (copy + " (edited)"),
                   "the second Enter rewrites the copy rather than nesting it");
            PresetDef def;
            String error;
            const auto file = PluginProcessor::userPresetFolder().getChildFile (copy + ".sp303preset");
            check (PresetFormat::parseText (file.loadFileAsString(), def, error)
                     && def.comment == "still one copy",
                   "the rewritten copy carries the latest note");
            p4->deleteUserPreset (copy);
        }
        {
            // An Enter that changed nothing must not mint a preset.
            auto p5 = makeGuiProcessor();
            PluginEditor ed5 (*p5);
            ed5.setSize (1180, 780);
            p5->setCurrentProgram (1);
            ed5.beginPresetNoteEdit();
            ed5.presetNoteEditorForTests().onReturnKey();
            check (! p5->getUserPresetNames().contains (p5->getProgramName (1) + " (edited)"),
                   "an untouched note edit saves nothing");
        }

        // JUCE only treats Return as "press the default button" while an
        // AlertWindow holds exactly one button, and the save dialog has two, so
        // it wires the key itself. The click cannot be observed without a modal
        // loop — the wiring can.
        {
            AlertWindow alert ("Spiral303", "Save Preset", AlertWindow::NoIcon);
            alert.addTextEditor ("presetName", {}, "Preset name:");
            alert.addButton ("OK", 1);
            alert.addButton ("Cancel", 0);
            commitOnReturn (alert, "presetName", "OK");
            auto* editor = alert.getTextEditor ("presetName");
            check (editor != nullptr && editor->onReturnKey != nullptr,
                   "the save dialog hooks Return up to its name field");
            if (editor != nullptr && editor->onReturnKey)
                editor->onReturnKey();   // must not throw with no modal loop running
        }

        save (editorImage, "editor-full.png");
    }
}
