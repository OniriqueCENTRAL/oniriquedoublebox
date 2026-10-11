#include "EqDisplay.h"

#include "Theme.h"

namespace onirique
{

using namespace theme;

namespace
{
    constexpr float kMinHz = 20.0f;
    constexpr float kMaxHz = 20000.0f;
    constexpr float kSpan = 15.0f;      // dB from the middle line to either edge
    constexpr float kPointRadius = 5.5f;
}

const EqDisplay::Band& EqDisplay::band (int index)
{
    static const Band bands[2] = { { "lo_freq", "lo_gain", "LOW", Colour (0xffffb454) },
                                   { "hi_freq", "hi_gain", "HIGH", Colour (0xff58d5e3) } };
    return bands[jlimit (0, 1, index)];
}

EqDisplay::EqDisplay (DoubleBoxProcessor& p) : processor (p)
{
    setMouseCursor (MouseCursor::CrosshairCursor);
}

void EqDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (15.0f);
}

float EqDisplay::xForHz (float hz) const
{
    return plot.getX() + plot.getWidth() * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz);
}

float EqDisplay::hzForX (float x) const
{
    return kMinHz * std::pow (kMaxHz / kMinHz, (x - plot.getX()) / plot.getWidth());
}

float EqDisplay::yForDb (float db) const
{
    return plot.getCentreY() - db / kSpan * plot.getHeight() * 0.5f;
}

float EqDisplay::dbForY (float y) const
{
    return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * kSpan;
}

float EqDisplay::parameter (const String& id) const
{
    return processor.apvts.getRawParameterValue (id)->load();
}

void EqDisplay::setParameter (const String& id, float value)
{
    if (auto* p = processor.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void EqDisplay::gesture (const String& id, bool begin)
{
    if (auto* p = processor.apvts.getParameter (id))
    {
        if (begin)
            p->beginChangeGesture();
        else
            p->endChangeGesture();
    }
}

Point<float> EqDisplay::pointOf (int index) const
{
    // The point stands at the height the shelf reaches, not at the half of
    // it the curve has got to by that frequency.
    return { xForHz (parameter (band (index).freq)), yForDb (parameter (band (index).gain)) };
}

int EqDisplay::bandAt (Point<float> position) const
{
    int best = -1;
    auto nearest = 14.0f;

    for (int i = 0; i < 2; ++i)
    {
        const auto distance = pointOf (i).getDistanceFrom (position);

        if (distance < nearest)
        {
            nearest = distance;
            best = i;
        }
    }

    return best;
}

//==============================================================================
void EqDisplay::mouseMove (const MouseEvent& e)
{
    const auto over = bandAt (e.position);

    if (over != hovered)
    {
        hovered = over;
        repaint();
    }
}

void EqDisplay::mouseExit (const MouseEvent&)
{
    hovered = -1;
    repaint();
}

void EqDisplay::mouseDown (const MouseEvent& e)
{
    dragging = bandAt (e.position);

    if (dragging >= 0)
    {
        gesture (band (dragging).freq, true);
        gesture (band (dragging).gain, true);
    }
}

void EqDisplay::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0)
        return;

    setParameter (band (dragging).freq, hzForX (e.position.x));
    setParameter (band (dragging).gain, dbForY (e.position.y));
    repaint();
}

void EqDisplay::mouseUp (const MouseEvent&)
{
    if (dragging >= 0)
    {
        gesture (band (dragging).freq, false);
        gesture (band (dragging).gain, false);
    }

    dragging = -1;
}

void EqDisplay::mouseDoubleClick (const MouseEvent& e)
{
    const auto index = bandAt (e.position);

    if (index < 0)
        return;

    gesture (band (index).gain, true);
    setParameter (band (index).gain, 0.0f);
    gesture (band (index).gain, false);
    repaint();
}

//==============================================================================
void EqDisplay::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f).withTrimmedBottom (14.0f);
    const auto settings = processor.readSettings();
    const auto rate = processor.getEqSampleRate();
    const auto on = settings.eqOn;

    g.setGradientFill (ColourGradient (Colour (0xff24090d), 0.0f, bounds.getY(),
                                       Colour (0xff0d0506), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 9.0f);

    {
        Graphics::ScopedSaveState clip (g);
        Path frame;
        frame.addRoundedRectangle (bounds, 9.0f);
        g.reduceClipRegion (frame);

        for (const auto hz : { 100.0f, 1000.0f, 10000.0f })
        {
            g.setColour (Colour (0xffffd2d6).withAlpha (0.08f));
            g.fillRect (xForHz (hz), plot.getY(), 1.0f, plot.getHeight());
        }

        g.setColour (Colour (0xffffd2d6).withAlpha (0.2f));
        g.fillRect (plot.getX(), yForDb (0.0f), plot.getWidth(), 1.0f);

        // The curve as it is heard, both shelves together.
        auto drawn = settings;
        drawn.eqOn = true;

        Path curve;
        curve.startNewSubPath (plot.getX(), yForDb (dbx::eqDb (drawn, kMinHz, rate)));

        for (auto x = plot.getX() + 2.0f; x <= plot.getRight(); x += 2.0f)
            curve.lineTo (x, yForDb (dbx::eqDb (drawn, hzForX (x), rate)));

        auto fill = curve;
        fill.lineTo (plot.getRight(), yForDb (0.0f));
        fill.lineTo (plot.getX(), yForDb (0.0f));
        fill.closeSubPath();

        g.setColour (ember.withAlpha (on ? 0.2f : 0.06f));
        g.fillPath (fill);
        g.setColour (Colours::white.withAlpha (on ? 0.95f : 0.3f));
        g.strokePath (curve, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));

        for (int i = 0; i < 2; ++i)
        {
            const auto centre = pointOf (i);
            const auto colour = band (i).colour.withMultipliedAlpha (on ? 1.0f : 0.45f);
            const auto shape = Rectangle<float> (2.0f * kPointRadius, 2.0f * kPointRadius).withCentre (centre);

            if (i == hovered || i == dragging)
            {
                g.setColour (colour.withAlpha (0.22f));
                g.fillEllipse (shape.expanded (5.0f));
            }

            g.setColour (Colour (0xff120607));
            g.fillEllipse (shape.expanded (1.5f));
            g.setColour (colour);
            g.fillEllipse (shape);
            g.setColour (Colours::white.withAlpha (on ? 0.85f : 0.4f));
            g.drawEllipse (shape, 1.1f);
        }
    }

    g.setColour (line);
    g.drawRoundedRectangle (bounds, 9.0f, 1.0f);

    // Under the curve: what the point in hand is set to, or the two gains.
    g.setFont (monoFont (9.5f));

    const auto foot = Rectangle<float> (bounds.getX() + 2.0f, bounds.getBottom() + 1.0f, bounds.getWidth() - 4.0f, 13.0f);
    const auto shown = dragging >= 0 ? dragging : hovered;

    auto describe = [this] (int i)
    {
        const auto& state = processor.apvts;
        return state.getParameter (band (i).freq)->getCurrentValueAsText() + "  "
               + state.getParameter (band (i).gain)->getCurrentValueAsText();
    };

    if (shown >= 0)
    {
        g.setColour (band (shown).colour);
        g.drawText (String (band (shown).name) + "  " + describe (shown), foot, Justification::centred, false);
    }
    else
    {
        g.setColour (band (0).colour.withAlpha (0.8f));
        g.drawText (processor.apvts.getParameter ("lo_gain")->getCurrentValueAsText(), foot, Justification::centredLeft, false);
        g.setColour (band (1).colour.withAlpha (0.8f));
        g.drawText (processor.apvts.getParameter ("hi_gain")->getCurrentValueAsText(), foot, Justification::centredRight, false);
    }
}

} // namespace onirique
