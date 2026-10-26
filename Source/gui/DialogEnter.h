#pragma once

#include <JuceHeader.h>

/** JUCE only treats Return as "press the default button" while an AlertWindow
    holds exactly one button (see AlertWindow::checkKeyPress), and both dialogs
    here have OK plus Cancel, so Enter used to do nothing at all. */
inline void commitOnReturn (juce::AlertWindow& alert, const juce::String& editorId,
                            const juce::String& buttonId)
{
    if (auto* editor = alert.getTextEditor (editorId))
    {
        // A raw pointer or a captured shared_ptr would keep the window alive:
        // the editor is owned by the alert, and the alert by the editor's
        // own callback.
        juce::Component::SafePointer<juce::AlertWindow> box (&alert);
        editor->onReturnKey = [box, buttonId]
        {
            if (box != nullptr)
                box->triggerButtonClick (buttonId);
        };
    }
}
