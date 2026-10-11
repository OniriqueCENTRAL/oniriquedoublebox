// Onirique DoubleBox — editor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/EqDisplay.h"
#include "ui/StageDisplay.h"
#include "ui/Theme.h"

namespace onirique
{

/** Four zones, each with one job: the header is which settings are loaded,
    the column on the left is what is done to the voices that ask for it and
    to the sound as a whole, the stage is where and when each voice is heard,
    and the foot is what becomes of the result. */
class DoubleBoxEditor final : public AudioProcessorEditor,
                              private Timer
{
public:
    explicit DoubleBoxEditor (DoubleBoxProcessor&);
    ~DoubleBoxEditor() override;

    void paint (Graphics&) override;
    void resized() override;

    StageDisplay& getStage() noexcept { return stage; }

    /** Turns the preset menu into the field its new name is typed in. */
    void beginSaving();

private:
    using ButtonAttachment = AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void paintHeader (Graphics&, Rectangle<int> header);
    int titleRight() const;

    /** Fills the preset menu from what is built in and what is on disk. */
    void refreshPresets();
    void stepPreset (int direction);
    void deleteClicked();

    DoubleBoxProcessor& processor;

    StageDisplay stage;
    EqDisplay eq;

    // The header: presets, the two slots, and bypass.
    TextButton previousButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DELETE" };
    ComboBox presetBox;
    Label presetNameEditor;
    StringArray presetNames;
    int stockCount = 0;
    int deleteArmed = 0;    // ticks left in which a second click deletes

    TextButton slotAButton { "A" }, slotBButton { "B" }, copyButton;
    TextButton bypassButton { "BYPASS" };

    // The column: the pitch and the formant with the voices they are for,
    // how much the doubles wander, and the EQ.
    Knob pitch { "PITCH" }, formant { "FORMANT" }, drift { "DRIFT" }, rate { "RATE" };
    TextButton applyButtons[dbx::kNumVoices];
    TextButton eqButton { "EQ" }, eqAllButton { "ALL" }, eqDoublesButton { "DOUBLES" };
    std::unique_ptr<ParameterAttachment> eqTargetAttachment;

    // The foot.
    ValueField doubles { "DOUBLES" }, width { "WIDTH" }, output { "OUT" };

    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;

    // Where the column's headings are written, set by resized().
    Rectangle<int> shiftHeading, applyHeading, driftRule, eqRule;

    //==============================================================================
    /** The description of whatever the pointer is resting on.

        Shown only once the pointer has been still on a control for two full
        seconds. From then on it stays for as long as the pointer is on that
        control, moving or not, and goes when it leaves. */
    struct HoverTip final : Component
    {
        void paint (Graphics& g) override
        {
            getLookAndFeel().drawTooltip (g, text, getWidth(), getHeight());
        }

        String text;
    };

    struct Described
    {
        Component* component;
        String text;
    };

    void describe (std::initializer_list<Component*>, const char* text);
    void updateHoverTip();

    std::vector<Described> described;
    HoverTip hoverTip;
    int resting = -1;           // which of the described, or none
    Point<int> restingAt;
    uint32 restingSince = 0;

    /** The sigil with its ember glow already baked in, at twice the size it is
        drawn so it stays sharp on a scaled display. */
    Image logo;

    /** When the editor opened: the light that runs along the foot of the
        header, and the stars beside the name, keep time from here. */
    double openedAt = Time::getMillisecondCounterHiRes();

    // Last, so that it is destroyed first and drawn over everything else.
    std::unique_ptr<license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DoubleBoxEditor)
};

} // namespace onirique
