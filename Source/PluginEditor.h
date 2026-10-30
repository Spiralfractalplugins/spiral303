#pragma once

#include <functional>
#include <optional>

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "gui/DarkLookAndFeel.h"
#include "gui/GateCircleComponent.h"
#include "gui/KnobSlider.h"
#include "gui/MeterComponent.h"
#include "gui/ScopeComponent.h"
#include "gui/SequenceEditorComponent.h"
#include "gui/WrappedLabel.h"

/** Header button drawn as the Spiral303 logo: a decaying saw with the filter knee closing over it. */
class LogoButton : public juce::Button
{
public:
    LogoButton() : juce::Button ("logo") { setTooltip ("Menu"); }

    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
    {
        const auto& th = Skins::current();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (isDown ? th.accent.withAlpha (0.30f)
                            : isOver ? th.panel.brighter (0.15f) : th.panel);
        g.fillRoundedRectangle (r, th.corner);
        g.setColour (isOver ? th.accent : th.panelOutline.brighter (0.25f));
        g.drawRoundedRectangle (r, th.corner, 1.3f);

        auto inner = r.reduced (r.getWidth() * 0.20f, r.getHeight() * 0.26f);
        juce::Path wave;
        constexpr int steps = 64;
        for (int i = 0; i <= steps; ++i)
        {
            float t = static_cast<float> (i) / steps;
            float x = inner.getX() + t * inner.getWidth();
            float saw = 2.0f * (t * 5.0f - std::floor (t * 5.0f)) - 1.0f;
            float amp = std::exp (-2.6f * t) * saw;
            float y = inner.getCentreY() - amp * inner.getHeight() * 0.48f;
            i == 0 ? wave.startNewSubPath (x, y) : wave.lineTo (x, y);
        }
        g.setColour (isOver ? th.accent : th.text);
        g.strokePath (wave, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
};

class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     public juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

    /** Lay a standard MIDI file over the pattern: the lowest track, on the grid the
        clock runs, in the scale that is already set. No dialog here, so a test can
        drive the whole import; the caller decides what a refusal looks like. */
    bool importMidi (const juce::File&, juce::String& error);

    void timerCallback() override;

    /** Tab lands on the preset field, where the arrows step through the bank. */
    std::unique_ptr<juce::ComponentTraverser> createFocusTraverser() override
    {
        struct OnlyField : juce::ComponentTraverser
        {
            explicit OnlyField (Component* f) : field (f) {}
            Component* getDefaultComponent (Component*) override { return field; }
            Component* getNextComponent (Component*) override { return nullptr; }
            Component* getPreviousComponent (Component*) override { return nullptr; }
            std::vector<Component*> getAllComponents (Component*) override { return { field }; }
            Component* field;
        };
        return std::make_unique<OnlyField> (&presetField);
    }

    /** Called by the preset popup's custom items to mark which mouse button was
        used; the menu result callback reads this to choose load vs delete. */
    void presetMenuArmDelete (bool deleteArmed) { presetDeleteArmed = deleteArmed; }
    bool isPresetDeleteArmed() const { return presetDeleteArmed; }
    void setPresetDeleteArmed (bool v) { presetDeleteArmed = v; }

    /** The header preset field: one click edits the name in place (Enter or a
        click away renames the preset on disk), the right button opens the bank
        menu, and left/right arrows step through it while the field is focused. */
    class PresetField : public juce::Label
    {
    public:
        explicit PresetField (PluginEditor& owner) : editor (owner)
        {
            // The label hosts its own text editor as a child component, so
            // unlike the old passive field it must let clicks through to it.
            setInterceptsMouseClicks (true, true);
            setEditable (true, false, false);
            onTextChange = [this] { editor.commitPresetName (getText()); };
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                { editor.showPresetMenu(); return; }

            // The displayed text carries the dirty marker; the editor works on
            // the name alone, or the marker would end up inside the new name.
            Label::setText (editor.presetNameForEdit(), dontSendNotification);
            Label::mouseUp (e);
        }

        /** Tab is meant to land on the field so the arrows can page the bank;
            the label's own habit of opening the editor on a tab focus would
            hand those arrows to the caret instead. */
        void focusGained (FocusChangeType cause) override
        {
            if (cause != focusChangedByTabKey)
                Label::focusGained (cause);
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            // While the caret is in the text the arrows move the caret.
            if (! isBeingEdited())
            {
                if (key == juce::KeyPress::leftKey)
                    { editor.cyclePreset (-1); return true; }
                if (key == juce::KeyPress::rightKey)
                    { editor.cyclePreset (1); return true; }
            }
            return Label::keyPressed (key);
        }

    private:
        PluginEditor& editor;
    };

    /** The note editor that borrows the credit column's rectangle while typing.
        Losing focus (Enter, a click elsewhere, the menu) is what ends the edit. */
    class NoteEditor : public juce::TextEditor
    {
    public:
        explicit NoteEditor (PluginEditor& owner) : editor (owner) {}
        void focusLost (FocusChangeType cause) override
        {
            TextEditor::focusLost (cause);
            editor.endPresetNoteEdit();
        }
    private:
        PluginEditor& editor;
    };

    /** The right-hand column: the preset note where there is one, the factory
        attribution where there is not. Clicking it edits the note in place. */
    class CreditField : public WrappedLabel
    {
    public:
        explicit CreditField (PluginEditor& owner) : editor (owner) {}

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                editor.beginPresetNoteEdit();
        }

    private:
        PluginEditor& editor;
    };

    /** Shows the note editor over the credit column and puts the caret in it. */
    void beginPresetNoteEdit();
    /** Hides the note editor again; the text is already on the processor. */
    void endPresetNoteEdit();
    /** Writes the current state to disk: a user preset under its own name, a
        factory program into a "(edited)" copy. */
    void commitPresetNote();

    /** The name without the dirty marker — what the in-place editor should show. */
    juce::String presetNameForEdit() const;
    /** Renames the loaded user preset, or saves a factory/anonymous state as a
        user preset under the typed name. An empty name is reverted, not written. */
    void commitPresetName (const juce::String& raw);

    /** Loads the combined-bank preset at the given index (factory programs
        first, then the user presets in menu order). */
    void loadPresetByIndex (int index);
    /** Steps through the combined bank with wrap-around. */
    void cyclePreset (int delta);

private:
    struct Knob
    {
        std::unique_ptr<KnobSlider> slider;
        std::unique_ptr<juce::Label> nameLabel, valueLabel;
        juce::RangedAudioParameter* param = nullptr;
        juce::Rectangle<int> baseCell;
        InkRole accentRole = InkRole::Accent;
        // Faders fill their whole cell (the slider owns the rectangle) and
        // carry no label components: their captions are painted by the editor.
        bool isFader = false;
    };

    Knob* addKnob (const juce::String& paramId, const juce::String& title,
                   const juce::String& tooltip,
                   juce::Rectangle<int> baseCell, InkRole accentRole);

    /** The horizontal mini fader of the FX strip: same attachment route as the
        knobs, but the whole cell is the slider and there are no label parts. */
    Knob* addFader (const juce::String& paramId, const juce::String& title,
                    const juce::String& tooltip,
                    juce::Rectangle<int> baseCell, InkRole accentRole);

    /** Re-applies every colour set directly on a component: those shadow whatever
        the look and feel would paint, so a skin change has to write them again. */
    void applySkinColours();

    /** Makes `skin` the live palette and repaints the whole editor with it. */
    void applySkin (int skin);

    void showInfoMenu();
    void promptSavePreset();
    void exportWav();
    void chooseMidiFile();
    void exportMidi();
    void applyToSequence (const std::function<void (Sequence&)>&);

    /** Greys the gate controls while the layer is bypassed. */
    void updateGateEnablement();

    /** Greys the BPM knob and PLAY while the host drives the clock, and parks the
        knob on the host tempo so its readout matches what is actually playing. */
    void updateSyncEnablement();

    juce::Rectangle<int> scaled (juce::Rectangle<int> base) const
    {
        return { originX + static_cast<int> (base.getX() * uiScale),
                 originY + static_cast<int> (base.getY() * uiScale),
                 static_cast<int> (base.getWidth() * uiScale),
                 static_cast<int> (base.getHeight() * uiScale) };
    }

    static constexpr int baseW = 1180, baseH = 780;

    PluginProcessor& processor;
    DarkLookAndFeel lookAndFeel;

    SequenceEditorComponent seqEditor;
    GateCircleComponent gateCircle;
    ScopeComponent waveScope, filterScope;

    juce::TextButton playButton { "PLAY" },
                 exportButton { "Export WAV" }, randomButton { "Random" },
                 midiButton { "MIDI" },
                 clearButton { "Clear" }, gateOnButton { "ON" }, analogOnButton { "Analog" },
                 hissButton { "Hiss" }, helpButton { "?" }, seqButton { "SEQ" },
                 recButton { "REC" };
    juce::ComboBox baseCombo, scaleCombo, stepsCombo, clockCombo, syncCombo, gateDivCombo, satTypeCombo, analogModeCombo,
                   filterTypeCombo, skinCombo, fxDivCombo;
    LogoButton logoButton;
    juce::Label tempoLabel;
    CreditField creditLabel { *this };
    MeterComponent meter { processor, true };
    juce::TextButton limiterButton { "Lim" };
    PresetField presetField { *this };
    juce::TextButton presetPrevButton { "<" }, presetNextButton { ">" };
    NoteEditor presetNoteEditor { *this };

public:
    void showPresetMenu();
    juce::TextButton& limiterButtonForTests() { return limiterButton; }
    juce::TextEditor& presetNoteEditorForTests() { return presetNoteEditor; }
    PresetField& presetFieldForTests() { return presetField; }
    juce::TextButton& presetPrevButtonForTests() { return presetPrevButton; }
    juce::TextButton& presetNextButtonForTests() { return presetNextButton; }
    MeterComponent& meterForTests() { return meter; }
    ScopeComponent& filterScopeForTests() { return filterScope; }
    juce::TextButton& playButtonForTests() { return playButton; }
    /** The slider of the knob bound to `paramId`, so a test can measure where
        a knob actually paints instead of trusting the cell it was given. */
    KnobSlider* knobForTests (const juce::String& paramId) const;
    juce::TextButton& randomButtonForTests() { return randomButton; }
    juce::TextButton& clearButtonForTests() { return clearButton; }
    juce::TextButton& midiButtonForTests() { return midiButton; }
    /** The right-hand note/credit column, so a test can read what it shows. */
    CreditField& creditForTests() { return creditLabel; }

private:
    void syncPresetLabel();
    juce::String shownPresetNote;
    juce::String shownCreditText;

    std::vector<std::unique_ptr<Knob>> knobs;

    std::vector<Knob*> gateKnobs;
    Knob* bpmKnob = nullptr;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateOnAttachment,
                                                                          playButtonAttachment,
                                                                          seqButtonAttachment,
                                                                          analogOnAttachment,
                                                                          hissButtonAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> clockAttachment,
                                                                            syncAttachment,
                                                                            gateDivAttachment,
                                                                            satTypeAttachment,
                                                                            analogModeAttachment,
                                                                            filterTypeAttachment,
                                                                            skinAttachment,
                                                                            fxDelDivAttachment;

    bool lastGateEnabled = true;
    bool lastAnalogOn = true;
    bool presetDeleteArmed = false; // armed by the preset popup, message thread only
    std::optional<bool> lastManualSync; // unset until the first check, so load-time state applies
    int lastSequenceVersion = -1;
    int lastSkin = -1;   // the skin the components were last coloured for

    float uiScale = 1.0f;
    int originX = 0, originY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
