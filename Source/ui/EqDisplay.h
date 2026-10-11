// Onirique DoubleBox — the EQ: two shelves on a small curve
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"

namespace onirique
{

/** A low shelf and a high shelf, each a point to drag: across for its
    frequency, up and down for its gain. A double click on a point puts its
    gain back to nothing. It is kept small on purpose: two points are all
    there is to it. */
class EqDisplay final : public Component
{
public:
    explicit EqDisplay (DoubleBoxProcessor&);

    void paint (Graphics&) override;
    void resized() override;

    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseDoubleClick (const MouseEvent&) override;

private:
    struct Band { const char* freq; const char* gain; const char* name; Colour colour; };
    static const Band& band (int index);

    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;

    Point<float> pointOf (int index) const;
    int bandAt (Point<float>) const;

    float parameter (const String& id) const;
    void setParameter (const String& id, float value);
    void gesture (const String& id, bool begin);

    DoubleBoxProcessor& processor;
    Rectangle<float> plot;
    int hovered = -1, dragging = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqDisplay)
};

} // namespace onirique
