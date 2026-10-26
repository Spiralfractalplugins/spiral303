#pragma once

#include "../PluginEditor.h"

/** A popup menu item for a preset: left click (or Enter) loads it, right click
    deletes it (user presets only — the menu ignores the flag elsewhere). The row
    of the preset that is currently loaded inverts text and background instead of
    carrying a tick glyph, so the marker reads on any row. The item only records
    which button was used into the owning editor; the menu's result callback — the
    single place that also holds the preset list — performs the load or the delete
    once the menu has closed. */
class PresetMenuItem : public juce::PopupMenu::CustomComponent
{
public:
    PresetMenuItem (const juce::String& name,
                    juce::Component::SafePointer<PluginEditor> ownerLink,
                    bool selectedRow)
        : juce::PopupMenu::CustomComponent (true), // auto-triggered: Enter works
          presetName (name),
          owner (ownerLink),
          selected (selectedRow)
    {}

    void getIdealSize (int& idealWidth, int& idealHeight) override
    {
        idealWidth = 260; idealHeight = 24;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& th = Skins::current();
        auto bounds = getLocalBounds().reduced (8, 4);
        // The loaded preset inverts its text/background — the marker is
        // unmistakable on any row and needs no extra glyph.
        if (isItemHighlighted())
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (bounds.toFloat(), th.corner);
        }
        else if (selected)
        {
            g.setColour (th.text);
            g.fillRoundedRectangle (bounds.toFloat(), th.corner);
        }

        g.setColour (isItemHighlighted()
                         ? findColour (juce::PopupMenu::highlightedTextColourId)
                         : selected ? th.background : th.text);
        g.setFont (Theme::font (13.0f));
        g.drawText (presetName, bounds.reduced (4, 0).toFloat(),
                    juce::Justification::centredLeft);

        // The delete hint only shows while hovering: the menu stays readable
        // from a distance, the hint appears for the row under the mouse.
        if (isItemHighlighted())
        {
            g.setColour (th.textDim);
            g.setFont (Theme::font (10.0f));
            g.drawText ("right-click = delete", bounds.toFloat().removeFromRight (110.0f),
                        juce::Justification::centredRight);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (owner != nullptr && contains (e.position))
            owner->presetMenuArmDelete (e.mods.isRightButtonDown());
        // Triggering/dismissal is handled by the menu window itself.
    }

private:
    juce::String presetName;
    juce::Component::SafePointer<PluginEditor> owner;
    bool selected = false;
};
