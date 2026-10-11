// Onirique DoubleBox — the stage: the five voices, where each is heard and how loud
#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** The five voices as five points. Across is where a voice is heard, from
    left to right; up is how loud it is.

    A point is dragged to move its voice both ways at once. The wheel detunes
    it, a double click switches it off (the ring stays, empty), and a right
    click puts it back where it started. How late a voice comes in is not on
    the stage: it is set, with everything else about the voice, in the panel
    underneath.

    Several voices can be selected at once, with Ctrl or Shift and a click or
    with a frame drawn in the empty. They are then dragged together, and the
    wheel, the double click, the right click and the panel act on all of them.
*/
class StageDisplay final : public Component,
                           private Timer
{
public:
    explicit StageDisplay (DoubleBoxProcessor&);
    ~StageDisplay() override;

    void paint (Graphics&) override;
    void resized() override;

    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseDoubleClick (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;

    /** Selects one voice and nothing else. */
    void select (int voice);

    /** Adds a voice to the selection, or takes it out. */
    void setSelected (int voice, bool shouldBeSelected);

    /** The voice the panel reads its values from: the one clicked last among
        those selected. There is always one. */
    int getSelected() const noexcept { return selected; }
    bool isSelected (int voice) const noexcept { return isPositiveAndBelow (voice, dbx::kNumVoices) && inSelection[(size_t) voice]; }

    static Colour voiceColour (int voice);

    /** The stage and the controls of its panel, each with what it does, for
        the editor to show when the pointer rests on one. The stage comes
        first, so that a control on it is found after it. */
    std::vector<std::pair<Component*, const char*>> getDescriptions();

private:
    class VoicePanel;

    void timerCallback() override;

    float xForPan (float pan) const;
    float panForX (float x) const;
    float yForLevel (float db) const;
    float levelForY (float y) const;

    Point<float> voicePosition (int voice) const;
    int voiceAt (Point<float>) const;

    /** Brings the panel in line with the selection. The panel is never left
        with nothing: emptied, the selection goes back to the voice it had. */
    void selectionChanged();

    /** The voice acted on and, when it is one of several selected, the others. */
    std::vector<int> withSelection (int voice) const;

    float parameter (const String& id) const;
    void setParameter (const String& id, float value);
    void changeParameter (const String& id, float value);
    void gesture (const String& id, bool begin);

    void paintGrid (Graphics&);
    void paintVoices (Graphics&);

    DoubleBoxProcessor& processor;
    std::unique_ptr<VoicePanel> panel;

    Rectangle<float> plot;
    std::array<bool, dbx::kNumVoices> inSelection {};
    int selected = dbx::kCentreVoice, hovered = -1, dragging = -1;
    std::vector<int> shownVoices;
    int shownPrimary = -1;

    // A drag moves every selected voice by the same distance across and the
    // same number of dB, from where each of them started.
    struct Start { float pan = 0.0f, level = 0.0f; };
    std::array<Start, dbx::kNumVoices> dragStart;
    Point<float> dragOrigin;

    // A frame being drawn in the empty, and what was selected before it.
    bool framing = false;
    Rectangle<float> frame;
    std::array<bool, dbx::kNumVoices> frameBase {};

    // The level of each voice, falling back slowly.
    std::array<float, dbx::kNumVoices> shownLevel {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageDisplay)
};

} // namespace onirique
