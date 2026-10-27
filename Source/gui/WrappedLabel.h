#pragma once

#include <JuceHeader.h>

#include "DarkLookAndFeel.h"
#include "Palette.h"

/** A block of dim text that flows across as many lines as the box allows.
    JUCE 8 took word wrap out of Label, and the preset credits are exactly the
    sort of line that needs it. */
class WrappedLabel : public juce::Component
{
public:
    /** An unset colour follows the skin; a colour given here is kept as written.
        `onFace` picks the ink the cabinet is printed with instead of the pocket
        one: the credit column stands on the bare plate, and a walnut plate needs
        the cream a silkscreen would use. */
    explicit WrappedLabel (juce::Colour colour = {}, bool onFace = false)
        : textColour (colour), faceInk (onFace) {}

    void setText (const juce::String& t) { text = t; repaint(); }
    const juce::String& getText() const { return text; }

    void setScale (float s)
    {
        uiScale = s;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& th = Skins::current();
        g.setColour (textColour.getAlpha() == 0 ? (faceInk ? th.faceInk : th.textDim) : textColour);
        g.setFont (Theme::font (9.0f * uiScale));
        g.drawMultiLineText (text, 1, static_cast<int> (11.0f * uiScale),
                             jmax (10, getWidth() - 2));
    }

private:
    juce::String text;
    juce::Colour textColour;
    bool faceInk = false;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WrappedLabel)
};
