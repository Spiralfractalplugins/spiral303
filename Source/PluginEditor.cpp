#include "PluginEditor.h"

#include "gui/ManualWindow.h"
#include "gui/PresetMenuItem.h"
#include "gui/SkinTextures.h"
#include "Presets.h"
#include "dsp/MidiExport.h"
#include "dsp/MidiImport.h"
#include "dsp/WavExport.h"

using namespace juce;

namespace
{
    const StringArray kClockDivisionNames { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T" };
    const StringArray kStepCounts { "8", "12", "16", "24", "32" };

    const int kBaseNoteFirst = 24; // C1
    const int kBaseNoteLast = 72;  // C5

    String noteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[midi % 12] + String (midi / 12 - 1);
    }

    /** ComboBox ids start at 1, and JUCE caps them below 0x3FFFFFFF. */
    int baseComboId (int midi) { return midi - kBaseNoteFirst + 1; }
    int midiFromBaseComboId (int id) { return id - 1 + kBaseNoteFirst; }

    int stepCountForIndex (int index)
    {
        static const int counts[] = { 8, 12, 16, 24, 32 };
        return counts[jlimit (0, 4, index)];
    }

    int indexForStepCount (int steps)
    {
        for (int i = 0; i < 5; ++i)
            if (stepCountForIndex (i) == steps)
                return i;
        return 2; // 16
    }

    void drawPanel (Graphics& g, Rectangle<int> r, const String& title, float scale)
    {
        const auto& th = Skins::current();
        const Rectangle<float> body = r.toFloat().reduced (1.0f);
        const bool material = th.panelMaterial != Material::Flat;

        g.setColour (th.panel);
        g.fillRoundedRectangle (body, th.corner);

        if (material)
        {
            // The pocket is machined out of the face, so the face's shadow lands
            // on its top lip and the light catches the bottom one.
            if (auto tex = SkinTextures::panel (th, r.getWidth(), r.getHeight()); tex.isValid())
            {
                g.saveState();
                Path clip;
                clip.addRoundedRectangle (body, th.corner);
                g.reduceClipRegion (clip);
                g.drawImage (tex, body);
                g.restoreState();
            }
            g.setColour (th.bevelHi.withAlpha (0.45f));
            g.drawRoundedRectangle (body, th.corner, 1.0f);
            g.saveState();
            g.reduceClipRegion (body.getX(), body.getY(), body.getWidth(), body.getHeight() * 0.5f);
            g.setColour (th.bevelLo.withAlpha (0.70f));
            g.drawRoundedRectangle (body, th.corner, 1.0f);
            g.restoreState();
        }

        g.setColour (th.panelOutline);
        g.drawRoundedRectangle (body, th.corner, 1.0f);

        if (material && th.screwsPerPanel > 0)
        {
            const int px = jmax (5, roundToInt (7.0f * scale));
            const Image head = SkinTextures::screw (th, px);
            const float inset = 6.0f * scale;
            const Point<float> corners[] = { { body.getX() + inset, body.getY() + inset },
                                             { body.getRight() - inset - px, body.getY() + inset },
                                             { body.getX() + inset, body.getBottom() - inset - px },
                                             { body.getRight() - inset - px, body.getBottom() - inset - px } };
            if (head.isValid())
                for (const auto& c : corners)
                    g.drawImageAt (head, roundToInt (c.getX()), roundToInt (c.getY()));
        }

        if (title.isEmpty())
            return;

        g.setFont (Theme::font (10.0f * scale, Font::bold));
        // A corner screw sits exactly where the plain panel puts its title, so
        // the machined cabinets start the legend one screw-width further in.
        const int titleInset = static_cast<int> ((material && th.screwsPerPanel > 0 ? 21.0f : 10.0f) * scale);
        const Rectangle<int> titleRect = r.reduced (titleInset, static_cast<int> (6 * scale));
        if (th.engravedLabel)
        {
            g.setColour (th.bevelHi.withAlpha (0.5f));
            g.drawText (title, titleRect.translated (0, 1), Justification::topLeft, false);
            g.setColour (th.ink.withAlpha (0.85f));
            g.drawText (title, titleRect, Justification::topLeft, false);
        }
        else if (th.glowingLabel)
        {
            g.setColour (th.ink.withAlpha (0.22f));
            const Point<int> offsets[] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
            for (const auto& off : offsets)
                g.drawText (title, titleRect.translated (off.x, off.y), Justification::topLeft, false);
            g.setColour (th.ink);
            g.drawText (title, titleRect, Justification::topLeft, false);
        }
        else
        {
            g.setColour (th.textDim);
            g.drawText (title, titleRect, Justification::topLeft, false);
        }
    }

    const String presetFieldHint = "Click = edit the name · right-click = preset list · "
                                   "left/right arrows = previous/next preset.";

    String formatParam (RangedAudioParameter* param, float rawValue)
    {
        if (auto* f = dynamic_cast<AudioParameterFloat*> (param))
        {
            const float normalised = f->getNormalisableRange().convertTo0to1 (rawValue);
            return static_cast<juce::AudioProcessorParameter*> (f)->getText (normalised, 6);
        }
        return param->getText (rawValue, 6);
    }
}

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processor (p), seqEditor (p), gateCircle (p),
  waveScope (p, ScopeComponent::WaveOnly), filterScope (p, ScopeComponent::FilterOnly)
{
    setLookAndFeel (&lookAndFeel);
    setResizable (true, true);
    setResizeLimits (900, 600, 3200, 2000);
    setSize (baseW, baseH);

    addAndMakeVisible (seqEditor);
    addAndMakeVisible (gateCircle);
    addAndMakeVisible (waveScope);
    addAndMakeVisible (filterScope);

    logoButton.onClick = [this] { showInfoMenu(); };

    playButton.setClickingTogglesState (true);
    playButton.setTooltip ("Starts and stops the internal sequencer.\n"
                           "Ignored while Sync is set to Host.");
    playButtonAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
        processor.state(), ParamIDs::manualPlay, playButton);

    // SEQ OFF turns the plugin into a plain MIDI instrument: incoming notes play
    // live and the sequencer stands down, in every sync mode.
    seqButton.setClickingTogglesState (true);
    seqButton.setTooltip ("SEQ OFF = live MIDI mode: the internal sequencer stands\n"
                          "down and notes you play from a keyboard sound right away.\n"
                          "Works with the host transport running or stopped.");
    seqButtonAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
        processor.state(), ParamIDs::seqOn, seqButton);

    gateOnButton.setClickingTogglesState (true);
    gateOnButton.setTooltip ("Bypasses the whole gate layer.");
    gateOnAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
        processor.state(), ParamIDs::gateOn, gateOnButton);

    analogOnButton.setClickingTogglesState (true);
    analogOnButton.setTooltip ("Bypasses the analog drift, cutoff and level flicker.");
    analogOnAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
        processor.state(), ParamIDs::analogOn, analogOnButton);

    hissButton.setClickingTogglesState (true);
    hissButton.setTooltip ("Analog noise-floor hiss, scaled by Warmth. Off by default.");
    hissButtonAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
        processor.state(), ParamIDs::analogHiss, hissButton);

    exportButton.onClick = [this] { exportWav(); };

    randomButton.onClick = [this]
    {
        applyToSequence ([] (Sequence& s) { s.randomise (static_cast<int> (Time::getMillisecondCounter())); });
    };
    clearButton.onClick = [this]
    {
        applyToSequence ([] (Sequence& s) { s.clearAll(); });
    };
    midiButton.setTooltip ("MIDI file: import a .mid as the pattern (the lowest track, on the 1/16 grid, "
                           "snapped to the current scale), or export the pattern as a one-loop .mid.");
    midiButton.onClick = [this]
    {
        PopupMenu menu;
        menu.addItem (1, "Import MIDI file...");
        menu.addItem (2, "Export pattern as MIDI...");
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&midiButton),
                            [this] (int choice)
        {
            if (choice == 1)
                chooseMidiFile();
            else if (choice == 2)
                exportMidi();
        });
    };

    // Sequencer state lives outside the parameter tree, so these combos read and write
    // the Sequence itself rather than a parameter.
    for (int midi = kBaseNoteFirst; midi <= kBaseNoteLast; ++midi)
        baseCombo.addItem (noteName (midi), baseComboId (midi));
    for (int i = 0; i < Sequence::scaleNames().size(); ++i)
        scaleCombo.addItem (Sequence::scaleNames()[i], i + 1);
    for (int i = 0; i < kStepCounts.size(); ++i)
        stepsCombo.addItem (kStepCounts[i], i + 1);

    baseCombo.onChange = [this]
    {
        applyToSequence ([this] (Sequence& s) { s.baseMidi = midiFromBaseComboId (baseCombo.getSelectedId()); });
    };
    scaleCombo.onChange = [this]
    {
        applyToSequence ([this] (Sequence& s) { s.scaleIndex = scaleCombo.getSelectedId() - 1; });
    };
    stepsCombo.onChange = [this]
    {
        applyToSequence ([this] (Sequence& s) { s.setNumSteps (stepCountForIndex (stepsCombo.getSelectedId() - 1)); });
    };

    clockCombo.setTooltip ("How fast the pattern steps run against the beat.");
    syncCombo.setTooltip ("Host follows the transport and tempo; Manual uses the BPM knob and PLAY.");
    gateDivCombo.setTooltip ("How long the gate wheel dwells on each sector.");
    for (int i = 0; i < kClockDivisionNames.size(); ++i)
        clockCombo.addItem (kClockDivisionNames[i], i + 1);
    syncCombo.addItem ("Host", 1);
    syncCombo.addItem ("Manual", 2);
    static const StringArray kGateDivNames { "16th", "8th", "32nd", "16T" };
    for (int i = 0; i < kGateDivNames.size(); ++i)
        gateDivCombo.addItem (kGateDivNames[i], i + 1);

    addAndMakeVisible (baseCombo);
    addAndMakeVisible (scaleCombo);
    addAndMakeVisible (stepsCombo);
    addAndMakeVisible (clockCombo);
    addAndMakeVisible (syncCombo);
    addAndMakeVisible (gateDivCombo);

    clockAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.state(), ParamIDs::clockDiv, clockCombo);
    syncAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.state(), ParamIDs::syncMode, syncCombo);
    gateDivAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.state(), ParamIDs::gateDiv, gateDivCombo);

    if (auto* sat = dynamic_cast<AudioParameterChoice*> (processor.state().getParameter (ParamIDs::satType)))
        for (int i = 0; i < sat->choices.size(); ++i)
            satTypeCombo.addItem (sat->choices[i], i + 1);
    satTypeCombo.setTooltip ("Selects the distortion character.");
    addAndMakeVisible (satTypeCombo);
    satTypeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.state(), ParamIDs::satType, satTypeCombo);

    if (auto* am = dynamic_cast<AudioParameterChoice*> (processor.state().getParameter (ParamIDs::analogMode)))
        for (int i = 0; i < am->choices.size(); ++i)
            analogModeCombo.addItem (am->choices[i], i + 1);
    analogModeCombo.setTooltip ("Standard drift or thicker Moog-style analog mode. Warmth micro-moves pitch, cutoff and resonance.");
    addAndMakeVisible (analogModeCombo);
    analogModeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.state(), ParamIDs::analogMode, analogModeCombo);

    if (auto* ft = dynamic_cast<AudioParameterChoice*> (processor.state().getParameter (ParamIDs::filterType)))
        for (int i = 0; i < ft->choices.size(); ++i)
            filterTypeCombo.addItem (ft->choices[i], i + 1);
    filterTypeCombo.setTooltip ("Selects the lowpass model: classic 303 contour, diode ladder, Moog ladder, TPT SVF, Sallen-Key or one-pole.\n"
                                "The selector lives under the Cutoff knob.");
    addAndMakeVisible (filterTypeCombo);
    filterTypeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.state(), ParamIDs::filterType, filterTypeCombo);

    helpButton.setTooltip ("Opens the user manual (RU/EN).");
    helpButton.onClick = [] { showManualWindow(); };

    // The skin selector: it only redraws the interface, so nothing here touches
    // the signal path. The value is a parameter, which is what puts it in presets.
    if (auto* sk = dynamic_cast<AudioParameterChoice*> (processor.state().getParameter (ParamIDs::uiSkin)))
    {
        for (int i = 0; i < sk->choices.size(); ++i)
            skinCombo.addItem (sk->choices[i], i + 1);
        skinCombo.setTooltip ("Selects the interface skin. Nothing here changes the sound.");
        addAndMakeVisible (skinCombo);
        skinAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.state(), ParamIDs::uiSkin, skinCombo);
    }

    addAndMakeVisible (helpButton);
    addAndMakeVisible (playButton);
    addAndMakeVisible (seqButton);
    addAndMakeVisible (exportButton);
    addAndMakeVisible (randomButton);
    addAndMakeVisible (midiButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (gateOnButton);
    addAndMakeVisible (analogOnButton);
    addAndMakeVisible (hissButton);
    addAndMakeVisible (logoButton);

    tempoLabel.setJustificationType (Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    creditLabel.setScale (1.0f);
    addAndMakeVisible (creditLabel);
    addAndMakeVisible (meter);
    addAndMakeVisible (limiterButton);
    limiterButton.setTooltip ("Master limiter (post-FX).");
    limiterButton.onClick = [this]
    {
        // Flip the parameter itself: the button mirrors LimOn through the
        // timer sync, so it must not read its own (possibly stale) toggle.
        if (auto* p = dynamic_cast<juce::AudioParameterBool*> (processor.state().getParameter (ParamIDs::limiterOn)))
            p->setValueNotifyingHost (p->get() ? 0.0f : 1.0f);
    };

    // Header preset field: it mirrors the loaded preset, the arrows step
    // through the whole bank, and one click renames the preset in place.
    presetField.setJustificationType (Justification::centred);
    presetField.setTooltip (presetFieldHint);
    addAndMakeVisible (presetField);

    presetPrevButton.setTooltip ("Previous preset");
    presetNextButton.setTooltip ("Next preset");
    presetPrevButton.onClick = [this] { cyclePreset (-1); };
    presetNextButton.onClick = [this] { cyclePreset (1); };
    addAndMakeVisible (presetPrevButton);
    addAndMakeVisible (presetNextButton);

    presetNoteEditor.setMultiLine (false);
    presetNoteEditor.setCaretVisible (true);
    presetNoteEditor.setInputFilter (new TextEditor::LengthAndCharacterRestriction (255, {}), true);
    presetNoteEditor.setTooltip ("Preset note (max 255). Saved with the preset and the state.");
    presetNoteEditor.onTextChange = [this]
    {
        processor.setPresetNote (presetNoteEditor.getText().substring (0, 255));
    };
    // Enter is the save gesture: the text goes to the processor and the preset
    // goes to disk. Focus is not handed anywhere — the column keeps showing the
    // note it just stored, and the next tick repaints it.
    presetNoteEditor.onReturnKey = [this]
    {
        endPresetNoteEdit();
        commitPresetNote();
        return true;
    };
    addChildComponent (presetNoteEditor);

    // A knob is coloured by its role, not by a literal: applySkinColours() can
    // then re-derive the accent of every knob when the skin changes.
    constexpr InkRole amber = InkRole::Accent, cyan = InkRole::AccentAlt,
                      violet = InkRole::Pitch, red = InkRole::Gate;

    // OSCILLATOR & FILTER
    addKnob (ParamIDs::wave, "Wave",
             "Morphs sine (0) through saw (0.5) to square (1).\n"
             "Band-limited, so it stays clean at any pitch.",
             { 22, 104, 80, 78 }, amber);
    addKnob (ParamIDs::attack, "Attack",
             "Slow attack for pads: how softly (0.02..1.2 s) each note fades in.\n"
             "0 keeps the instant 303 attack.",
             { 104, 104, 80, 78 }, amber);
    addKnob (ParamIDs::cutoff, "Cutoff",
             "Lowpass corner, 40-8000 Hz. The contour envelope opens it on every note.",
             { 186, 104, 80, 78 }, amber);
    addKnob (ParamIDs::res, "Res",
             "Filter resonance. Above roughly 0.7 the 303 starts to squeal.",
             { 268, 104, 80, 78 }, amber);
    addKnob (ParamIDs::env, "Env",
             "How far the envelope opens the filter - the squelch depth.",
             { 350, 104, 80, 78 }, amber);

    // ENVELOPE & ANALOG
    addKnob (ParamIDs::contour, "Contour",
             "Decay time of the filter envelope, as a fraction of the step length.\n"
             "Low is a pluck, high holds the note open.",
             { 464, 104, 72, 78 }, cyan);
    addKnob (ParamIDs::accent, "Accent",
             "Level and extra filter opening AC steps get over normal ones.",
             { 564, 104, 72, 78 }, cyan);
    addKnob (ParamIDs::glide, "Glide",
             "Length of the pitch slide on SL steps, as a fraction of the step.",
             { 664, 104, 72, 78 }, cyan);
    addKnob (ParamIDs::tone, "Slope",
             "Fades one resonant section into two: 12 dB/oct left, 24 dB/oct right.",
             { 764, 104, 72, 78 }, cyan);

    // DISTORTION & OUTPUT
    addKnob (ParamIDs::drive, "Drive",
             "Input level into the tanh soft-clipper.",
             { 20, 300, 72, 78 }, violet);
    addKnob (ParamIDs::satMix, "Dist Mix",
             "Dry/wet blend of the saturator.",
             { 102, 300, 72, 78 }, violet);
    addKnob (ParamIDs::warmth, "Warmth",
             "Analog noise floor: the hiss and flicker of the original circuit.",
             { 184, 300, 72, 78 }, violet);
    addKnob (ParamIDs::masterGain, "Gain",
             "Master output (-24 to +12 dB).",
             { 266, 300, 72, 78 }, violet);

    // GATE
    gateKnobs.push_back (addKnob (ParamIDs::gateAtk, "Atk",
             "How fast the gate opens, in ms.",
             { 890, 278, 72, 78 }, red));
    gateKnobs.push_back (addKnob (ParamIDs::gateRel, "Rel",
             "How fast it closes again. Long releases blur sectors into rolls.",
             { 978, 278, 72, 78 }, red));
    gateKnobs.push_back (addKnob (ParamIDs::gateMix, "Mix",
             "Dry/wet of the gate effect: 0 leaves the signal untouched, 1 chops fully.",
             { 1066, 278, 72, 78 }, red));

    // TRANSPORT
    // The row ends where the MIDI map combo used to stand: its 116 px went to
    // Random and Clear, and the two knobs took the freed corridor at the right
    // edge. The 44 px slider is centred in its cell, so a cell never needs more
    // than its own width — at the smallest scale the knob shrinks to the cell.
    addKnob (ParamIDs::swing, "Swing",
             "Delays every second step. 0 is straight, 0.25 is a shuffle.",
             { 744, 412, 58, 78 }, cyan);
    bpmKnob = addKnob (ParamIDs::bpm, "BPM",
             "Internal tempo. Only used while Sync is set to Manual; "
             "while Sync is Host the knob follows the host tempo and is greyed out.",
             { 802, 412, 60, 78 }, cyan);

    // FX strip: one row under the scopes, same shape the scopes panel has
    // always had. Captions are painted in paint() — six controls over 424 px
    // leave no room for a label part per cell.
    const String delayTimeTip = "Echo length in beats, synced to the tempo: quarter, "
         "eighth, dotted eighth, sixteenth, eighth triplet.\nA tempo change slides the "
         "repeats instead of retriggering them.";
    fxDivCombo.setTooltip (delayTimeTip);
    fxDivCombo.addItem ("1/4", 1);
    fxDivCombo.addItem ("1/8", 2);
    fxDivCombo.addItem ("1/8.", 3);
    fxDivCombo.addItem ("1/16", 4);
    fxDivCombo.addItem ("1/8T", 5);
    addAndMakeVisible (fxDivCombo);
    fxDelDivAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.state(), ParamIDs::fxDelDiv, fxDivCombo);

    addFader (ParamIDs::fxDelMix, "Delay Mix",
              "Dry/wet of the tempo echo. 0 bypasses the stage; the repeats are "
              "darkened by the tone lowpass of the feedback path.",
              { 514, 382, 62, 20 }, violet);
    addFader (ParamIDs::fxDelFb, "Delay Fb",
              "Echo feedback. Above about a half the repeats pile up; the line "
              "cannot ring away, the gain is capped at 0.9.",
              { 582, 382, 62, 20 }, violet);
    addFader (ParamIDs::fxDelTone, "Delay Tone",
              "Lowpass of the echo: darker at 0, the classic ~3 kHz warmth at "
              "0.65, fully open at 1 (8 kHz).",
              { 650, 382, 62, 20 }, violet);
    addFader (ParamIDs::fxRevMix, "Reverb Mix",
              "Send into the short Schroeder room. The wet tail is added back to "
              "both channels, so the stereo image keeps its width.",
              { 718, 382, 62, 20 }, violet);
    addFader (ParamIDs::fxRevDecay, "Reverb Decay",
              "Room decay: the comb feedback rises from a small room to the "
              "damped cap; the tail always dies out.",
              { 786, 382, 62, 20 }, violet);

    // A restored session or a preset can already ask for a skin other than Dark,
    // so take it from the parameter rather than waiting for the first tick.
    int skin = 0;
    if (auto* p = dynamic_cast<AudioParameterChoice*> (processor.state().getParameter (ParamIDs::uiSkin)))
        skin = p->getIndex();
    applySkin (skin);
    updateSyncEnablement();
    syncPresetLabel();
    startTimerHz (30);
    resized();
}

PluginEditor::~PluginEditor()
{
    setLookAndFeel (nullptr);
}

PluginEditor::Knob* PluginEditor::addKnob (const String& paramId, const String& title,
                                           const String& tooltip, Rectangle<int> baseCell, InkRole accentRole)
{
    auto knob = std::make_unique<Knob>();
    knob->baseCell = baseCell;
    knob->accentRole = accentRole;

    auto slider = std::make_unique<KnobSlider>();
    slider->setName (title);
    slider->setTooltip (tooltip.isNotEmpty() ? tooltip + "\n[double-click = exact value, arrows when focused, wheel = fine]"
                                            : title + "\n[double-click = exact value, arrows when focused, wheel = fine]");
    addAndMakeVisible (slider.get());

    knob->param = processor.state().getParameter (paramId);
    attachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
        processor.state(), paramId, *slider));

    auto nameLabel = std::make_unique<Label>();
    nameLabel->setText (title, dontSendNotification);
    nameLabel->setJustificationType (Justification::centred);
    addAndMakeVisible (nameLabel.get());

    auto valueLabel = std::make_unique<Label>();
    valueLabel->setJustificationType (Justification::centred);
    addAndMakeVisible (valueLabel.get());

    knob->slider = std::move (slider);
    knob->nameLabel = std::move (nameLabel);
    knob->valueLabel = std::move (valueLabel);
    auto* raw = knob.get();
    knobs.push_back (std::move (knob));
    return raw;
}

KnobSlider* PluginEditor::knobForTests (const String& paramId) const
{
    for (const auto& k : knobs)
        if (k->param != nullptr && k->param->getParameterID() == paramId)
            return k->slider.get();
    return nullptr;
}

PluginEditor::Knob* PluginEditor::addFader (const String& paramId, const String& title,
                                            const String& tooltip,
                                            Rectangle<int> baseCell, InkRole accentRole)
{
    auto knob = std::make_unique<Knob>();
    knob->baseCell = baseCell;
    knob->accentRole = accentRole;
    knob->isFader = true;

    auto slider = std::make_unique<KnobSlider> (Slider::LinearHorizontal, Slider::NoTextBox);
    slider->setName (title);
    slider->setTooltip (tooltip + "\n[double-click = exact value, wheel = 1%, Shift = 5%]");
    addAndMakeVisible (slider.get());

    knob->param = processor.state().getParameter (paramId);
    attachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
        processor.state(), paramId, *slider));

    // No label parts: the strip's captions live in paint(), where they share
    // the look of the NOTE/SCALE row instead of kicking labels per cell.
    knob->slider = std::move (slider);
    auto* raw = knob.get();
    knobs.push_back (std::move (knob));
    return raw;
}

/** Everything the editor paints through JUCE's colour ids is set here rather than
    in the constructor: a `setColour` on a component shadows whatever the look and
    feel would have drawn, so it has to be re-applied when the skin changes. */
void PluginEditor::applySkinColours()
{
    const auto& th = Skins::current();

    playButton.setColour (TextButton::buttonOnColourId, th.accent.withAlpha (0.30f));
    playButton.setColour (TextButton::textColourOnId, th.accent);
    seqButton.setColour (TextButton::buttonOnColourId, th.accent.withAlpha (0.30f));
    seqButton.setColour (TextButton::textColourOnId, th.accent);

    gateOnButton.setColour (TextButton::buttonColourId, th.chip);
    gateOnButton.setColour (TextButton::buttonOnColourId, th.gate.withAlpha (0.30f));
    gateOnButton.setColour (TextButton::textColourOffId, th.chipText);
    gateOnButton.setColour (TextButton::textColourOnId, th.gate);
    analogOnButton.setColour (TextButton::buttonColourId, th.chip);
    analogOnButton.setColour (TextButton::buttonOnColourId, th.accentAlt.withAlpha (0.30f));
    hissButton.setColour (TextButton::buttonColourId, th.chip);
    hissButton.setColour (TextButton::buttonOnColourId, th.accentAlt.withAlpha (0.30f));

    limiterButton.setColour (TextButton::buttonColourId, th.panel);
    presetPrevButton.setColour (TextButton::buttonColourId, th.panel);
    presetNextButton.setColour (TextButton::buttonColourId, th.panel);

    presetField.setColour (Label::backgroundColourId, th.panel);
    presetField.setColour (Label::textColourId, th.text);
    presetField.setColour (Label::outlineColourId, th.panelOutline);

    presetNoteEditor.setColour (TextEditor::backgroundColourId, th.panel);
    presetNoteEditor.setColour (TextEditor::textColourId, th.text);
    presetNoteEditor.setColour (TextEditor::outlineColourId, th.panelOutline);
    presetNoteEditor.setColour (TextEditor::focusedOutlineColourId, th.accent);

    for (auto& k : knobs)
    {
        k->slider->setColour (Slider::rotarySliderFillColourId, Skins::ink (th, k->accentRole));
        if (k->nameLabel != nullptr)
            k->nameLabel->setColour (Label::textColourId, th.textDim);
        if (k->valueLabel != nullptr)
            k->valueLabel->setColour (Label::textColourId, th.text);
        // The fader fill reads the same colour id the knob arc does, so a skin
        // change repaints the FX row through this same loop.
        k->slider->setColour (Slider::trackColourId, th.knobTrack);
    }
}

/** Switches the process-wide palette and pushes the change into every pixel the
    editor owns. `sendLookAndFeelChange` is needed because JUCE's base
    `lookAndFeelChanged` does nothing: without it the hand-painted components would
    keep their old frame until the next value change forced a repaint. */
void PluginEditor::applySkin (int skin)
{
    lastSkin = skin;
    Skins::select (skin);
    SkinTextures::invalidate();
    lookAndFeel.applyPalette();
    applySkinColours();
    for (auto* child : getChildren())
        child->repaint();
    sendLookAndFeelChange();
    repaint();
}

void PluginEditor::applyToSequence (const std::function<void (Sequence&)>& edit)
{
    auto seq = processor.getSequence();
    edit (seq);
    processor.setSequence (seq);
    seqEditor.refresh();
}

void PluginEditor::showPresetMenu()
{
    const int numFactory = processor.getNumPrograms();
    auto userNames = processor.getUserPresetNames();
    const bool userLoaded = processor.isUserPresetLoaded();
    const String loadedName = processor.currentUserPresetName();

    PopupMenu menu;
    int nextId = 1;

    const bool dirty = processor.isProgramDirty();
    for (int i = 0; i < numFactory; ++i)
    {
        const String name = processor.getProgramName (i);
        const bool loaded = i == processor.getCurrentProgram() && ! dirty && ! userLoaded;
        juce::PopupMenu::Item item;
        item.text = name;
        item.itemID = nextId++;
        item.isTicked = loaded;
        item.customComponent = *new PresetMenuItem (name, this, loaded);
        menu.addItem (std::move (item));
    }

    menu.addSeparator();
    const int saveId = nextId++;
    menu.addItem (saveId, "Save current settings as preset...");

    int userStart = 0;
    if (! userNames.isEmpty())
    {
        menu.addSeparator();
        userStart = nextId;
        for (const auto& name : userNames)
        {
            // Right-click deletes the preset (user presets only); a plain click
            // or the keyboard still loads it.
            const bool loaded = name.equalsIgnoreCase (loadedName) && ! dirty;
            juce::PopupMenu::Item item;
            item.text = name;
            item.itemID = nextId++;
            item.isTicked = loaded;
            item.customComponent = *new PresetMenuItem (name, this, loaded);
            menu.addItem (std::move (item));
        }
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (presetField),
                        [this, numFactory, saveId, userStart, userNames] (int result)
    {
        if (result <= 0)
            return;
        if (result <= numFactory)
        {
            presetDeleteArmed = false;
            processor.setCurrentProgram (result - 1);
            seqEditor.refresh();
        }
        else if (result == saveId)
        {
            promptSavePreset();
            seqEditor.refresh();
        }
        else if (result >= userStart)
        {
            const String presetName = userNames[result - userStart];
            if (presetDeleteArmed)
            {
                presetDeleteArmed = false;
                // Confirm before the file is gone: a stray right click must not
                // wipe a preset.
                auto confirm = std::make_shared<juce::AlertWindow> ("Delete preset",
                        "Delete the user preset \"" + presetName + "\"?",
                        juce::AlertWindow::NoIcon);
                confirm->addButton ("Delete", 1);
                confirm->addButton ("Cancel", 0);
                juce::Component::SafePointer<PluginEditor> self (this);
                confirm->enterModalState (true, juce::ModalCallbackFunction::create(
                    [self, confirm, presetName] (int result)
                    {
                        if (result != 1 || self == nullptr)
                            return;
                        if (self->processor.deleteUserPreset (presetName))
                            self->repaint();
                        else
                            AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Spiral303",
                                                              "Cannot delete that preset.");
                    }));
            }
            else
            {
                if (! processor.loadUserPreset (presetName))
                    AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Spiral303",
                                                      "Cannot load that preset.");
                seqEditor.refresh();
            }
        }
    });
}

void PluginEditor::loadPresetByIndex (int index)
{
    const int numFactory = processor.getNumPrograms();
    const auto userNames = processor.getUserPresetNames();
    if (index < 0 || index >= numFactory + userNames.size())
        return;

    if (index < numFactory)
        processor.setCurrentProgram (index);
    else if (! processor.loadUserPreset (userNames[index - numFactory]))
        AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Spiral303",
                                          "Cannot load that preset.");
    seqEditor.refresh();
}

void PluginEditor::cyclePreset (int delta)
{
    const int total = processor.getNumPrograms() + static_cast<int> (processor.getUserPresetNames().size());
    if (total <= 0)
        return;

    int index;
    if (processor.isUserPresetLoaded())
    {
        const auto userNames = processor.getUserPresetNames();
        index = processor.getNumPrograms() + jmax (0, userNames.indexOf (processor.currentUserPresetName()));
    }
    else
    {
        index = processor.getCurrentProgram();
    }
    index = (index + delta % total + total) % total;
    loadPresetByIndex (index);
}

void PluginEditor::promptSavePreset()
{
    auto alert = std::make_shared<AlertWindow> ("Save Preset", "Enter a name for the new preset:",
                                                AlertWindow::NoIcon);
    alert->setLookAndFeel (&lookAndFeel);
    alert->addTextEditor ("presetName", {}, "Preset name:");
    alert->addButton ("OK", 1);
    alert->addButton ("Cancel", 0);
    commitOnReturn (*alert, "presetName", "OK");

    alert->enterModalState (true, ModalCallbackFunction::create(
        [this, alert] (int result)
        {
            if (result == 1)
            {
                auto name = alert->getTextEditorContents ("presetName");
                if (name.isNotEmpty() && ! processor.saveUserPreset (name))
                    AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Spiral303",
                        "Cannot save the preset to disk.");
            }
            alert->setVisible (false);
        }));
}

void PluginEditor::showInfoMenu()
{
    PopupMenu menu;
    menu.addItem (1, "About Spiral303...");
    menu.addItem (2, "User manual (RU / EN)...");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (logoButton),
                        [] (int result)
    {
        if (result == 1)
        {
            AlertWindow::showMessageBoxAsync (AlertWindow::InfoIcon, "About Spiral303",
                "Spiral303 " SPIRAL303_VERSION " - acid synthesizer and sequencer\n\n"
                "Author: Spiralfractal Konstantin Chizhov\n"
                "Contact: konstantin.qi@gmail.com\n\n"
                "Morphing oscillator, resonant contour filter, distortion,\n"
                "a 303-style pattern editor with slide, accent and tie,\n"
                "analog drift and a 16-square gate grid.");
        }
        else if (result == 2)
        {
            showManualWindow();
        }
    });
}

void PluginEditor::exportWav()
{
    const String preset = processor.isProgramDirty()
        ? String ("user")
        : processor.getProgramName (processor.getCurrentProgram()).toLowerCase().replaceCharacter (' ', '-');

    auto chooser = std::make_shared<FileChooser> ("Export the pattern as WAV",
        File::getSpecialLocation (File::userHomeDirectory)
            .getChildFile ("spiral303-" + preset + ".wav"),
        "*.wav");

    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
        [this, chooser] (const FileChooser& fc)
    {
        auto results = fc.getResults();
        if (results.isEmpty())
            return;

        File file = results[0];
        if (file.getFileExtension().isEmpty())
            file = file.withFileExtension ("wav");

        if (! WavExport::renderPattern (processor, file, 2))
            AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Export WAV",
                "Could not write " + file.getFileName());
        else
            AlertWindow::showMessageBoxAsync (AlertWindow::InfoIcon, "Spiral303",
                "Saved 44.1 kHz / 24-bit mono, two bars:\n" + file.getFullPathName());
    });
}

void PluginEditor::exportMidi()
{
    const String preset = processor.isProgramDirty()
        ? String ("user")
        : processor.getProgramName (processor.getCurrentProgram()).toLowerCase().replaceCharacter (' ', '-');

    auto chooser = std::make_shared<FileChooser> ("Export the pattern as MIDI",
        File::getSpecialLocation (File::userHomeDirectory)
            .getChildFile ("spiral303-" + preset + ".mid"),
        "*.mid");

    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
        [this, chooser] (const FileChooser& fc)
    {
        auto results = fc.getResults();
        if (results.isEmpty())
            return;

        File file = results[0];
        if (file.getFileExtension().isEmpty())
            file = file.withFileExtension ("mid");

        // The same grid the importer fits onto: one step in quarter notes, from
        // the clock's own division and the tempo the plugin is running at.
        const double stepBeats = processor.getStepLengthMs() * processor.getTempoBpm() / 60000.0;
        const auto midi = MidiExport::build (processor.getSequence(), stepBeats, processor.getTempoBpm());

        if (! MidiExport::writeFile (midi, file))
            AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Export MIDI",
                "Could not write " + file.getFileName());
        else
            AlertWindow::showMessageBoxAsync (AlertWindow::InfoIcon, "Spiral303",
                "Saved one loop of the pattern as a standard MIDI file:\n" + file.getFullPathName());
    });
}

void PluginEditor::chooseMidiFile()
{
    auto chooser = std::make_shared<FileChooser> ("Import a MIDI sequence",
        File::getSpecialLocation (File::userHomeDirectory), "*.mid;*.midi");

    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
        [this, chooser] (const FileChooser& fc)
    {
        const auto results = fc.getResults();
        if (results.isEmpty())
            return;

        String error;
        if (! importMidi (results[0], error))
            AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Import MIDI", error);
    });
}

bool PluginEditor::importMidi (const File& file, String& error)
{
    std::vector<MidiImport::Note> notes;
    if (! MidiImport::readFile (file, notes, error))
        return false;

    // The grid is the clock's own step expressed in quarter notes, so the line keeps
    // its rhythm under any tempo, in either sync mode and on every clock division.
    const double stepBeats = processor.getStepLengthMs() * processor.getTempoBpm() / 60000.0;
    if (stepBeats <= 0.0)
    {
        error = "The sequencer has no clock to fit the file onto yet.";
        return false;
    }

    applyToSequence ([&] (Sequence& s)
    {
        s.clearAll();
        MidiImport::fit (s, notes, stepBeats);
    });
    return true;
}

void PluginEditor::filesDropped (const StringArray& files, int, int)
{
    for (const auto& f : files)
    {
        File file { f };
        const String extension = file.getFileExtension().toLowerCase();
        if (extension == ".mid" || extension == ".midi")
        {
            String error;
            if (! importMidi (file, error))
                AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Import MIDI", error);
            return;
        }
        if (extension == ".json" || extension == ".sp303preset")
        {
            if (! processor.loadPresetFile (file))
                AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Spiral303",
                    "Not a Spiral303 preset: " + file.getFileName());
            seqEditor.refresh();
            return;
        }
    }
}

void PluginEditor::syncPresetLabel()
{
    // The field names what is actually loaded: a user preset (with a modified
    // marker when it drifted), otherwise the factory program. The note is not
    // shown here — it belongs to the note column, so long notes stay readable
    // instead of being cut off by the fixed header slot.
    const bool dirty = processor.isProgramDirty();
    const String name = processor.isUserPresetLoaded()
            ? processor.currentUserPresetName() + (dirty ? " *" : "")
            : (dirty ? String ("User") + "*" : processor.getProgramName (processor.getCurrentProgram()));
    // Label::setText unconditionally hides a live editor, and this runs thirty
    // times a second — without the guard the rename box would vanish on the
    // next tick.
    if (! presetField.isBeingEdited() && presetField.getText() != name)
        presetField.setText (name, dontSendNotification);

    // The note does not fit the fixed header slot, so the whole text lives in
    // the tooltip — refreshed only when it actually changes.
    if (const String note = processor.getPresetNote(); note != shownPresetNote)
    {
        shownPresetNote = note;
        presetField.setTooltip (note.isNotEmpty() ? presetFieldHint + "\n\nNote: " + note
                                                  : presetFieldHint);
    }

    // Keep the note field in sync unless the user is typing in it right now.
    if (! presetNoteEditor.hasKeyboardFocus (false)
         && presetNoteEditor.getText() != processor.getPresetNote())
        presetNoteEditor.setText (processor.getPresetNote(), dontSendNotification);

    // The column at the right reads the note where there is one and the factory
    // attribution where there is not, so a credit never disappears. It is written
    // only when the text changes, and never over a live edit — Enter on the note
    // has to reach the column, which an old program-change check would have
    // hidden.
    String credit = processor.getPresetNote();
    if (credit.isEmpty())
    {
        const int program = processor.getCurrentProgram();
        const auto& factory = factoryPresets();
        credit = (! dirty && program >= 0 && program < static_cast<int> (factory.size()))
                   ? factory[static_cast<size_t> (program)].credit : String();
    }
    if (credit != shownCreditText && ! presetNoteEditor.isVisible())
    {
        shownCreditText = credit;
        creditLabel.setText (credit);
    }
}

String PluginEditor::presetNameForEdit() const
{
    // The display carries the dirty marker; the editor must not inherit it.
    return processor.isUserPresetLoaded() ? processor.currentUserPresetName()
                                          : processor.getProgramName (processor.getCurrentProgram());
}

void PluginEditor::commitPresetName (const String& raw)
{
    const String wanted = raw.trim();

    // An emptied name is a mis-click, not a rename to nothing: put the old one back.
    if (wanted.isEmpty())
        { syncPresetLabel(); return; }

    // A user preset is renamed on disk; a factory program is never edited in
    // place, so the typed name becomes the user copy it is saved as — but only
    // if that name is still free, otherwise the copy would stomp a preset the
    // user wrote earlier.
    const bool ok = processor.isUserPresetLoaded()
        ? processor.renameUserPreset (processor.currentUserPresetName(), wanted)
        : (! processor.getUserPresetNames().contains (wanted, true)
            && processor.saveUserPreset (wanted));

    if (! ok)
        AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Spiral303",
            "Cannot save the preset as \"" + wanted + "\": the name is taken or the disk refused it.");

    syncPresetLabel();
    repaint();
}

void PluginEditor::beginPresetNoteEdit()
{
    // The note is read in the credit column, so it is edited there: the editor
    // lends that column's rectangle instead of jumping back to the header.
    presetNoteEditor.setText (processor.getPresetNote(), dontSendNotification);
    presetNoteEditor.setBounds (creditLabel.getBounds());
    presetNoteEditor.setVisible (true);
    presetNoteEditor.grabKeyboardFocus();
    presetNoteEditor.selectAll();
}

void PluginEditor::commitPresetNote()
{
    // Nothing changed, nothing to write: an untouched Enter must not mint a
    // "(edited)" copy of a factory preset.
    if (! processor.isProgramDirty())
        return;

    // A user preset is rewritten under its own name; a factory program is
    // never edited in place, so its note travels into a copy.
    const String name = processor.isUserPresetLoaded()
        ? processor.currentUserPresetName()
        : processor.getProgramName (processor.getCurrentProgram()) + " (edited)";

    if (! processor.saveUserPreset (name))
        AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Spiral303",
            "Cannot save the preset to disk.");
    syncPresetLabel();
}

void PluginEditor::endPresetNoteEdit()
{
    if (! presetNoteEditor.isVisible())
        return;
    // onTextChange already pushes every keystroke; this only catches a paste that
    // skipped it. Marking the program dirty for an untouched note would lie.
    if (presetNoteEditor.getText() != processor.getPresetNote())
        processor.setPresetNote (presetNoteEditor.getText().substring (0, 255));
    presetNoteEditor.setVisible (false);
}

void PluginEditor::timerCallback()
{
    // The skin can change from anywhere — the combo box, a preset load, a host
    // state recall — so it is polled like the other out-of-band state.
    if (auto* pSkin = dynamic_cast<AudioParameterChoice*> (processor.state().getParameter (ParamIDs::uiSkin)))
    {
        const int skin = pSkin->getIndex();
        if (skin != lastSkin)
        {
            lastSkin = skin;
            applySkin (skin);
        }
    }

    syncPresetLabel();

    const bool running = processor.isSequencerRunning();
    playButton.setButtonText (running ? "STOP" : "PLAY");

    // SEQ reads as a mode switch: on = sequencer, off = live MIDI.
    if (auto* seq = dynamic_cast<AudioParameterBool*> (processor.state().getParameter (ParamIDs::seqOn)))
        seqButton.setButtonText (seq->get() ? "SEQ" : "MIDI");

    updateSyncEnablement();
    const bool manual = lastManualSync.value_or (false);

    if (auto* p = dynamic_cast<juce::AudioParameterBool*> (processor.state().getParameter (ParamIDs::limiterOn)))
    {
        if (limiterButton.getToggleState() != p->get())
            limiterButton.setToggleState (p->get(), juce::dontSendNotification);
        limiterButton.setAlpha (p->get() ? 1.0f : 0.45f);
    }

    // The hiss lives inside the analog layer, so an analog bypass greys its button
    // along with the mode combo next to it.
    auto* analogOn = dynamic_cast<AudioParameterBool*> (processor.state().getParameter (ParamIDs::analogOn));
    const bool analogEnabled = analogOn == nullptr || analogOn->get();
    if (analogEnabled != lastAnalogOn)
    {
        lastAnalogOn = analogEnabled;
        hissButton.setEnabled (analogEnabled);
        // setAlpha is what greys a TextButton visibly here.
        hissButton.setAlpha (analogEnabled ? 1.0f : 0.45f);
    }

    // The pattern is not a parameter, so a preset load or a host state recall has to
    // pull the sequencer combos back into line.
    const int seqVersion = processor.getSequenceVersion();
    if (seqVersion != lastSequenceVersion)
    {
        lastSequenceVersion = seqVersion;
        const auto seq = processor.getSequence();
        baseCombo.setSelectedId (baseComboId (jlimit (kBaseNoteFirst, kBaseNoteLast, seq.baseMidi)),
                                 dontSendNotification);
        scaleCombo.setSelectedId (jlimit (0, Sequence::scaleNames().size() - 1, seq.scaleIndex) + 1,
                                 dontSendNotification);
        stepsCombo.setSelectedId (indexForStepCount (seq.numSteps()) + 1, dontSendNotification);
    }

    // Tempo comes from the audio thread via processBlock; do not touch the host
    // playhead here - some AU hosts are not safe to query outside the audio
    // callback and this has been a source of intermittent host crashes.
    const double bpm = processor.getTempoBpm();
    tempoLabel.setText (manual ? String::formatted ("%.1f BPM  manual", bpm)
                               : String::formatted ("%.1f BPM  host", bpm),
                        dontSendNotification);
    const auto& th = Skins::current();
    // The readout stands in a window of its own, like the preset field beside
    // it: the strip along the top of a cabinet is the one surface no skin's text
    // colour is chosen against.
    tempoLabel.setColour (Label::backgroundColourId, th.panel);
    tempoLabel.setColour (Label::outlineColourId, th.panelOutline);
    tempoLabel.setColour (Label::textColourId, manual ? th.accentAlt : th.textDim);

    for (auto& k : knobs)
    {
        if (k->param == nullptr)
            continue;
        // Faders paint no value labels — their captions live in the strip.
        if (k->valueLabel == nullptr)
            continue;
        // the attachment keeps the slider in parameter units, so no conversion here
        k->valueLabel->setText (formatParam (k->param, static_cast<float> (k->slider->getValue())),
                                dontSendNotification);
    }

    updateGateEnablement ();

    seqEditor.repaint();
    gateCircle.repaint();
    waveScope.repaint();
    filterScope.repaint();
}

void PluginEditor::updateSyncEnablement()
{
    auto* sync = dynamic_cast<AudioParameterChoice*> (processor.state().getParameter (ParamIDs::syncMode));
    const bool manual = sync != nullptr && sync->getIndex() == 1;

    if (manual != lastManualSync)
    {
        lastManualSync = manual;
        const float alpha = manual ? 1.0f : 0.45f;

        playButton.setEnabled (manual);
        playButton.setAlpha (alpha);

        if (bpmKnob != nullptr)
        {
            Component* parts[] = { bpmKnob->slider.get(), bpmKnob->nameLabel.get(), bpmKnob->valueLabel.get() };
            for (auto* c : parts)
            {
                c->setEnabled (manual);
                // setAlpha is what actually greys these: the custom slider and the
                // labels paint without consulting isEnabled().
                c->setAlpha (alpha);
            }
        }
    }

    // While the host drives the clock the knob mirrors the host tempo instead of the
    // (unused) BPM parameter. dontSendNotification keeps the slider from writing the
    // host tempo back through its attachment.
    if (! manual && bpmKnob != nullptr && bpmKnob->slider != nullptr)
        bpmKnob->slider->setValue (processor.getTempoBpm(), dontSendNotification);
}

void PluginEditor::updateGateEnablement()
{
    const bool live = processor.gateActive();
    if (live == lastGateEnabled)
        return;
    lastGateEnabled = live;

    auto apply = [] (Component* c, bool on)
    {
        c->setEnabled (on);
        // setAlpha is what actually greys anything here: the custom slider, the labels and
        // the combo box all paint without consulting isEnabled().
        c->setAlpha (on ? 1.0f : 0.45f);
    };

    apply (&gateCircle, live);
    apply (&gateDivCombo, live);

    for (auto* k : gateKnobs)
    {
        apply (k->slider.get(), live);
        apply (k->nameLabel.get(), live);
        apply (k->valueLabel.get(), live);
    }
}

void PluginEditor::paint (Graphics& g)
{
    const auto& th = Skins::current();
    g.fillAll (th.background);

    if (th.faceTexture)
    {
        if (const Image plate = SkinTextures::face (th, getWidth(), getHeight()); plate.isValid())
        {
            g.setImageResamplingQuality (Graphics::lowResamplingQuality);
            g.drawImage (plate, getLocalBounds().toFloat());
        }

        // A real 303 is two offcut cheeks glued to a lighter field. The layout
        // starts its panels 12 px in, so the boards cannot get wider than that
        // without moving geometry — instead the joint between board and insert
        // is cut as a rebate: a bright chamfer on the wood and the face's
        // shadow just inside it.
        if (th.faceMaterial == Material::Wood && uiScale > 0.2f)
        {
            const auto b = getLocalBounds().toFloat();
            const float m = 12.0f * uiScale;
            auto cheek = [&] (Rectangle<float> r)
            {
                g.setColour (th.faceLo.withAlpha (0.55f));
                g.fillRect (r);
                g.setColour (th.bevelHi.withAlpha (0.22f));
                g.drawRect (r, 1.0f);
            };
            cheek ({ 0.0f, 0.0f, m, b.getHeight() });
            cheek ({ b.getRight() - m, 0.0f, m, b.getHeight() });
            cheek ({ 0.0f, 0.0f, b.getWidth(), m });
            cheek ({ 0.0f, b.getBottom() - m, b.getWidth(), m });

            const Rectangle<float> joint (m, m, b.getWidth() - 2.0f * m, b.getHeight() - 2.0f * m);
            g.setColour (th.bevelHi.withAlpha (0.55f));
            g.drawRect (joint, 1.0f);
            g.setColour (th.faceLo.withAlpha (0.45f));
            g.drawRect (joint.reduced (1.0f), 1.0f);
        }
    }

    drawPanel (g, scaled ({ 12, 52, 424, 200 }), "OSCILLATOR & FILTER", uiScale);
    drawPanel (g, scaled ({ 448, 52, 424, 200 }), "ENVELOPE & ANALOG", uiScale);
    drawPanel (g, scaled ({ 12, 258, 424, 146 }), "DISTORTION & OUTPUT", uiScale);
    drawPanel (g, scaled ({ 448, 258, 424, 146 }), {}, uiScale);
    drawPanel (g, scaled ({ 884, 52, 284, 344 }), "GATE", uiScale);
    drawPanel (g, scaled ({ 12, 408, 860, 88 }), "SEQUENCER", uiScale);
    drawPanel (g, scaled ({ 12, 504, 1156, 268 }), {}, uiScale);

    g.setColour (th.faceInk);
    g.setFont (Theme::font (14.0f * uiScale, Font::bold));
    g.drawText (String ("Spiralfractal's Spiral303  v") + SPIRAL303_VERSION,
                scaled ({ 156, 8, 284, 30 }), Justification::centredLeft);

    // Small captions over the sequencer controls.
    g.setFont (Theme::font (9.0f * uiScale, Font::bold));
    g.setColour (th.textDim);
    auto caption = [&g, this] (const String& text, int x)
    {
        const auto r = scaled ({ x, 424, 90, 12 });
        g.drawText (text, r, Justification::centredLeft, false);
    };
    caption ("NOTE", 20);
    caption ("SCALE", 96);
    caption ("STEPS", 258);
    caption ("CLOCK", 330);
    caption ("SYNC", 408);

    // The FX strip: small captions over each control, same dim style as the
    // NOTE/SCALE row. The div combo gets no extra title — its text is the value.
    auto fxCaption = [&g, this] (const String& text, int x, int w)
    {
        const auto r = scaled ({ x, 372, w, 9 });
        g.drawText (text, r, Justification::centred, false);
    };
    fxCaption ("DIV", 452, 56);
    fxCaption ("DEL MIX", 514, 62);
    fxCaption ("FBK", 582, 62);
    fxCaption ("TONE", 650, 62);
    fxCaption ("REV MIX", 718, 62);
    fxCaption ("DECAY", 786, 62);

    // The filter model selector lives right under the Cutoff knob it describes.
    // Caption painted in the same small dim style as NOTE / SCALE / SYNC.
    g.drawText ("FILTER", scaled ({ 186, 186, 112, 12 }), Justification::centredLeft, false);
}

void PluginEditor::resized()
{
    uiScale = jmin (static_cast<float> (getWidth()) / static_cast<float> (baseW),
                    static_cast<float> (getHeight()) / static_cast<float> (baseH));
    originX = (getWidth()  - static_cast<int> (baseW * uiScale)) / 2;
    originY = (getHeight() - static_cast<int> (baseH * uiScale)) / 2;

    lookAndFeel.uiScale = uiScale;
    seqEditor.setScale (uiScale);
    gateCircle.setScale (uiScale);
    waveScope.setScale (uiScale);
    filterScope.setScale (uiScale);
    creditLabel.setScale (uiScale);

    logoButton.setBounds (scaled ({ 12, 8, 34, 34 }));
    skinCombo.setBounds (scaled ({ 52, 10, 96, 26 }));
    helpButton.setBounds (scaled ({ 444, 12, 24, 26 }));
    // The preset field with its prev/next arrows is the header row's preset
    // control: one click opens the list, a double-click edits the note in place.
    presetPrevButton.setBounds (scaled ({ 474, 10, 24, 30 }));
    presetField.setBounds (scaled ({ 502, 10, 132, 30 }));
    presetNextButton.setBounds (scaled ({ 636, 10, 24, 30 }));
    presetField.setFont (Theme::font (11.0f * uiScale, Font::bold));
    // The note editor overlays the credit column, so it wears the column's
    // type: small, left-aligned, and it follows the column's resized rectangle.
    presetNoteEditor.setFont (Theme::font (9.0f * uiScale));
    presetNoteEditor.setJustification (Justification::centredLeft);
    playButton.setBounds (scaled ({ 668, 10, 84, 30 }));
    seqButton.setBounds (scaled ({ 756, 10, 44, 30 }));
    exportButton.setBounds (scaled ({ 804, 10, 110, 30 }));
    tempoLabel.setBounds (scaled ({ 914, 14, 142, 22 }));
    tempoLabel.setFont (Theme::font (11.0f * uiScale, Font::bold));
    // The master meter lives in the header row, right of the tempo readout;
    // the Lim toggle sits in the same row, right of the meter. Lim ends at
    // x=1168, the right edge of the GATE panel below it.
    meter.setBounds (scaled ({ 1060, 10, 72, 30 }));
    limiterButton.setBounds (scaled ({ 1136, 10, 32, 30 }));

    gateOnButton.setBounds (scaled ({ 1108, 58, 52, 22 }));

    seqEditor.setBounds (scaled ({ 16, 508, 1148, 260 }));
    // The scopes stand a little shorter than they used to: the freed 24 px
    // carry the FX strip below them, inside the same panel.
    waveScope.setBounds (scaled ({ 452, 268, 202, 100 }));
    filterScope.setBounds (scaled ({ 662, 268, 202, 100 }));
    gateCircle.setBounds (scaled ({ 888, 80, 276, 190 }));
    gateDivCombo.setBounds (scaled ({ 890, 360, 100, 22 }));

    // The filter model selector moved under the Cutoff knob; the distortion row
    // shifted left to fill the gap.
    filterTypeCombo.setBounds (scaled ({ 186, 198, 112, 22 }));
    satTypeCombo.setBounds (scaled ({ 16, 378, 112, 22 }));
    analogOnButton.setBounds (scaled ({ 134, 378, 48, 22 }));
    analogModeCombo.setBounds (scaled ({ 188, 378, 94, 22 }));
    hissButton.setBounds (scaled ({ 288, 378, 34, 22 }));

    // The FX strip: the div combo and the five mini faders own their cells
    // whole (they are LinearHorizontal faders, not 44 px knobs).
    fxDivCombo.setBounds (scaled ({ 452, 382, 56, 20 }));
    for (auto& k : knobs)
        if (k->isFader)
            k->slider->setBounds (scaled (k->baseCell));

    // The preset note is edited in the column that shows it, between the
    // sequencer row and the grid, clear of every panel.
    creditLabel.setBounds (scaled ({ 884, 470, 284, 22 }));
    presetNoteEditor.setBounds (creditLabel.getBounds());

    baseCombo.setBounds (scaled ({ 20, 438, 74, 24 }));
    scaleCombo.setBounds (scaled ({ 96, 438, 158, 24 }));
    stepsCombo.setBounds (scaled ({ 258, 438, 68, 24 }));
    clockCombo.setBounds (scaled ({ 330, 438, 74, 24 }));
    syncCombo.setBounds (scaled ({ 408, 438, 88, 24 }));
    randomButton.setBounds (scaled ({ 502, 438, 74, 24 }));
    midiButton.setBounds (scaled ({ 582, 438, 74, 24 }));
    clearButton.setBounds (scaled ({ 662, 438, 74, 24 }));

    for (auto& k : knobs)
    {
        if (k->isFader)
        {
            // The fader owns the entire cell: a linear slider has no size cap
            // dance of its own.
            k->slider->setBounds (scaled (k->baseCell));
            continue;
        }

        auto cell = scaled (k->baseCell);
        if (k->nameLabel == nullptr || k->valueLabel == nullptr)
            continue;
        // The floor keeps a knob usable in a tiny window; the cap is what keeps
        // it inside its own cell, so no knob can grow into its neighbour.
        const int knobSize = jmin (jmax (16, static_cast<int> (44 * uiScale)),
                                   cell.getWidth());
        k->slider->setBounds (cell.getX() + (cell.getWidth() - knobSize) / 2, cell.getY(),
                              knobSize, knobSize);
        k->nameLabel->setBounds (cell.getX(), cell.getY() + knobSize + 2, cell.getWidth(),
                                 static_cast<int> (13 * uiScale));
        k->nameLabel->setFont (Theme::font (10.0f * uiScale, Font::bold));
        k->valueLabel->setBounds (cell.getX(), cell.getY() + knobSize + 2 + static_cast<int> (13 * uiScale),
                                  cell.getWidth(), static_cast<int> (13 * uiScale));
        k->valueLabel->setFont (Theme::font (10.0f * uiScale));
    }

    repaint();
}

