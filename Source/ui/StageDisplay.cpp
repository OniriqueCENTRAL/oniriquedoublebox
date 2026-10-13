#include "StageDisplay.h"

namespace onirique
{

using namespace theme;

namespace
{
    constexpr float kPointRadius = 7.5f;
    constexpr int kPanelHeight = 132;

    // The range of a voice's level, which is the height of the stage.
    constexpr float kTopDb = 6.0f;
    constexpr float kBottomDb = -36.0f;

    // Room kept inside the plot, so that a point at the edge of its range
    // still has its name over it and its values under it.
    constexpr float kInsetX = 66.0f;
    constexpr float kInsetTop = 34.0f;
    constexpr float kInsetBottom = 44.0f;

    const Colour kVoiceColours[dbx::kNumVoices] = { Colour (0xffa98bff), Colour (0xff58d5e3), Colour (0xffffecee),
                                                    Colour (0xffffb454), Colour (0xffff5f9e) };

    /** A number that says which way it goes, without decimals it has no use for. */
    String signedText (float value)
    {
        const auto whole = std::abs (value - std::round (value)) < 0.005f;
        return (value > 0.0f ? "+" : "") + (whole ? String (roundToInt (value)) : String (value, 2));
    }
}

Colour StageDisplay::voiceColour (int voice)
{
    return isPositiveAndBelow (voice, dbx::kNumVoices) ? kVoiceColours[voice] : ember;
}

//==============================================================================
/** The small drawn buttons of the panel. */
class IconButton final : public Button
{
public:
    enum class Icon { power, previous, next };

    explicit IconButton (Icon i) : Button ({}), icon (i) {}

    Colour accent { ember };

    void paintButton (Graphics& g, bool over, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto centre = bounds.getCentre();
        const auto r = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto lit = getToggleState() || down;

        auto colour = lit ? accent.brighter (0.2f) : (over ? Colours::white : Colour (0xffcdb6ba));
        colour = colour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.3f);

        const PathStrokeType stroke (1.6f, PathStrokeType::curved, PathStrokeType::rounded);
        Path p;

        if (icon == Icon::power)
        {
            // The one button with a rim, as the main switch of the voice.
            g.setColour (Colour (0x59000000));
            g.fillEllipse (bounds.reduced (1.5f));
            g.setColour (colour.withMultipliedAlpha (0.55f));
            g.drawEllipse (bounds.reduced (1.5f), 1.0f);

            const auto a = r * 0.36f;
            p.addCentredArc (centre.x, centre.y + a * 0.1f, a, a, 0.0f, 0.75f, MathConstants<float>::twoPi - 0.75f, true);
            p.startNewSubPath (centre.x, centre.y - a * 1.3f);
            p.lineTo (centre.x, centre.y - a * 0.15f);
        }
        else
        {
            const auto a = r * 0.34f * (icon == Icon::next ? 1.0f : -1.0f);
            p.startNewSubPath (centre.x - a * 0.5f, centre.y - std::abs (a));
            p.lineTo (centre.x + a * 0.5f, centre.y);
            p.lineTo (centre.x - a * 0.5f, centre.y + std::abs (a));
        }

        g.setColour (colour);
        g.strokePath (p, stroke);
    }

private:
    Icon icon;
};

/** Everything about a voice, under the stage: whether it is heard, how late,
    how far out of tune, how loud and where, and whether the pitch and the
    formant are for it.

    For one voice it carries that voice's values. For several it shows those
    of the one clicked last and moves them all together: each dial by the same
    amount for every voice, the two switches alike. */
class StageDisplay::VoicePanel final : public Component
{
public:
    explicit VoicePanel (AudioProcessorValueTreeState& s) : state (s)
    {
        for (auto* b : { &power, &previous, &next })
            addAndMakeVisible (*b);

        power.onClick = [this] { toggle (&VoiceIds::on); };
        shiftButton.onClick = [this] { toggle (&VoiceIds::shift); };
        manipButton.onClick = [this] { toggle (&VoiceIds::manip); };

        previous.onClick = [this] { if (onStep != nullptr) onStep (-1); };
        next.onClick = [this] { if (onStep != nullptr) onStep (1); };

        title.setJustificationType (Justification::centredLeft);
        title.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (title);

        caption.setJustificationType (Justification::centredLeft);
        caption.setColour (Label::textColourId, haze);
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (caption);

        controls[0].knob = &delay;
        controls[1].knob = &detune;
        controls[2].knob = &level;
        controls[3].knob = &pan;

        for (auto& control : controls)
        {
            auto* c = &control;

            addAndMakeVisible (*c->knob);

            auto& slider = c->knob->slider;
            slider.onDragStart = [this, c] { c->dragging = true; begin (*c); };
            slider.onDragEnd = [this, c] { end (*c); c->dragging = false; };
            slider.onValueChange = [this, c]
            {
                // A wheel, a typed value or a double click is a whole gesture
                // of its own.
                if (c->dragging)
                {
                    apply (*c);
                }
                else
                {
                    begin (*c);
                    apply (*c);
                    end (*c);
                }

                c->knob->updateText();
            };
        }

        addAndMakeVisible (shiftButton);
        addAndMakeVisible (manipButton);
    }

    /** What the panel now stands for. primary is the voice it shows. */
    void setVoices (const std::vector<int>& selection, int primaryVoice)
    {
        voices = selection;
        primary = primaryVoice;

        const auto several = voices.size() > 1;
        const auto colour = voiceColour (primary);

        for (int i = 0; i < kNumControls; ++i)
        {
            // Detune and pan turn about their middle; delay and level do not.
            if (auto* p = state.getParameter (idFor (primary, i)))
                controls[i].knob->speak (*p, i == 1 || i == 3);

            controls[i].knob->setAccent (colour);
        }

        for (auto* b : { &power, &previous, &next })
            b->accent = colour;

        title.setText (several ? String ("SEVERAL") : voiceName (primary).toUpperCase(), dontSendNotification);
        title.setColour (Label::textColourId, colour);
        caption.setText (several ? String ((int) voices.size()) + " voices"
                                 : String (primary == dbx::kCentreVoice ? "the voice itself" : "double"),
                         dontSendNotification);

        refresh (true);
        repaint();
    }

    /** Follows the parameters, which automation, a preset or a drag on the
        stage may have moved. */
    void refresh (bool force = false)
    {
        const auto ids = voiceIds (primary);
        const auto on = real (ids.on) > 0.5f;

        power.setToggleState (on, dontSendNotification);
        shiftButton.setToggleState (real (ids.shift) > 0.5f, dontSendNotification);
        shiftButton.setAlpha (on ? 1.0f : 0.4f);
        manipButton.setToggleState (real (ids.manip) > 0.5f, dontSendNotification);
        manipButton.setAlpha (on ? 1.0f : 0.4f);

        for (int i = 0; i < kNumControls; ++i)
        {
            auto& c = controls[i];

            c.knob->setAlpha (on ? 1.0f : 0.4f);

            if (c.dragging)
                continue;

            if (auto* p = state.getParameter (idFor (primary, i)))
            {
                if (force || std::abs ((double) p->getValue() - c.knob->slider.getValue()) > 1.0e-6)
                {
                    c.knob->slider.setValue (p->getValue(), dontSendNotification);
                    c.knob->updateText();
                }
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18, 8);

        auto left = area.removeFromLeft (150).withSizeKeepingCentre (150, 60);
        auto top = left.removeFromTop (30);
        power.setBounds (top.removeFromLeft (30));
        top.removeFromLeft (10);
        title.setBounds (top);
        left.removeFromTop (6);
        caption.setBounds (left.removeFromTop (16).withTrimmedLeft (40));

        auto right = area.removeFromRight (176).withSizeKeepingCentre (176, 98);
        auto steps = right.removeFromTop (26).removeFromRight (56);
        previous.setBounds (steps.removeFromLeft (26));
        next.setBounds (steps.removeFromRight (26));
        right.removeFromTop (8);
        shiftButton.setBounds (right.removeFromTop (29));
        right.removeFromTop (6);
        manipButton.setBounds (right.removeFromTop (29));

        area.reduce (8, 0);

        const auto each = jmin (124, area.getWidth() / kNumControls);
        auto knobs = area.withSizeKeepingCentre (kNumControls * each, area.getHeight());

        for (auto& c : controls)
            c.knob->setBounds (knobs.removeFromLeft (each).reduced (4, 0));
    }

    std::function<void (int)> onStep;

    std::vector<std::pair<Component*, const char*>> getDescriptions()
    {
        return { { &power, "Marche / arrêt : si cette voix s'entend ou non. Avec plusieurs voix sélectionnées, toutes." },
                 { &delay, "Delay : le retard de la voix sur celle du centre, en millisecondes." },
                 { &detune, "Detune : de combien la voix est désaccordée, en cents. Quelques cents d'un côté ou de l'autre, c'est ce qui fait un double." },
                 { &level, "Level : le volume de la voix. Pareil que glisser son point de haut en bas." },
                 { &pan, "Pan : où la voix s'entend, de gauche à droite. Pareil que glisser son point de gauche à droite." },
                 { &shiftButton, "Pitch / formant : donne à cette voix le pitch et le formant réglés dans la colonne de gauche." },
                 { &manipButton, "Manipulate : fait passer cette voix par les manipulations réglées dans la colonne de droite." },
                 { &previous, "La voix à gauche." },
                 { &next, "La voix à droite." } };
    }

private:
    static constexpr int kNumControls = 4;

    struct Control
    {
        Knob* knob = nullptr;
        std::vector<float> starts;  // where each voice was when the dial was taken
        float leadStart = 0.0f;
        bool dragging = false;
    };

    static String idFor (int voice, int control)
    {
        const auto ids = voiceIds (voice);
        return control == 0 ? ids.delay : control == 1 ? ids.detune : control == 2 ? ids.level : ids.pan;
    }

    int indexOf (const Control& c) const { return (int) (&c - controls); }

    float real (const String& id) const { return state.getRawParameterValue (id)->load(); }

    void set (const String& id, float value)
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value)));
    }

    /** One of the two switches, for every voice the panel stands for: all of
        them go the way the one on show goes. */
    void toggle (String VoiceIds::* which)
    {
        const auto value = real (voiceIds (primary).*which) > 0.5f ? 0.0f : 1.0f;

        for (const auto voice : voices)
        {
            if (auto* p = state.getParameter (voiceIds (voice).*which))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (value);
                p->endChangeGesture();
            }
        }

        refresh();
    }

    void begin (Control& c)
    {
        const auto which = indexOf (c);

        c.starts.clear();
        c.leadStart = real (idFor (primary, which));

        for (const auto voice : voices)
        {
            c.starts.push_back (real (idFor (voice, which)));

            if (auto* p = state.getParameter (idFor (voice, which)))
                p->beginChangeGesture();
        }
    }

    void apply (Control& c)
    {
        const auto which = indexOf (c);
        auto* lead = state.getParameter (idFor (primary, which));

        if (lead == nullptr || c.starts.size() != voices.size())
            return;

        // Milliseconds, cents, dB or pan: every voice moves by as much as
        // the one on show has.
        const auto moved = lead->convertFrom0to1 ((float) c.knob->slider.getValue()) - c.leadStart;

        for (size_t i = 0; i < voices.size(); ++i)
            set (idFor (voices[i], which), c.starts[i] + moved);
    }

    void end (Control& c)
    {
        for (const auto voice : voices)
            if (auto* p = state.getParameter (idFor (voice, indexOf (c))))
                p->endChangeGesture();
    }

    AudioProcessorValueTreeState& state;
    std::vector<int> voices { dbx::kCentreVoice };
    int primary = dbx::kCentreVoice;

    IconButton power { IconButton::Icon::power }, previous { IconButton::Icon::previous }, next { IconButton::Icon::next };
    Label title, caption;
    Knob delay { "DELAY" }, detune { "DETUNE" }, level { "LEVEL" }, pan { "PAN" };
    TextButton shiftButton { "PITCH / FORMANT" }, manipButton { "MANIPULATE" };
    Control controls[kNumControls];
};

//==============================================================================
StageDisplay::StageDisplay (DoubleBoxProcessor& p) : processor (p)
{
    panel = std::make_unique<VoicePanel> (processor.apvts);
    panel->onStep = [this] (int direction)
    {
        select ((selected + direction + dbx::kNumVoices) % dbx::kNumVoices);
    };
    addAndMakeVisible (*panel);

    select (selected);
    startTimerHz (30);
}

StageDisplay::~StageDisplay() = default;

std::vector<std::pair<Component*, const char*>> StageDisplay::getDescriptions()
{
    std::vector<std::pair<Component*, const char*>> all {
        { this, "Les cinq voix : la voix elle-même au centre, deux doubles de chaque côté. "
                "Glisse un point de gauche à droite pour son pan, de haut en bas pour son volume. Molette : désaccord. "
                "Double-clic : marche / arrêt. Clic droit : retour au départ. "
                "Ctrl ou Maj + clic, ou un cadre tracé dans le vide, en sélectionne plusieurs." },
        { panel.get(), "Tout ce qui concerne la voix sélectionnée. Avec plusieurs, chaque potard les déplace toutes de la même quantité." } };

    for (auto& d : panel->getDescriptions())
        all.push_back (d);

    return all;
}

void StageDisplay::resized()
{
    auto bounds = getLocalBounds();

    panel->setBounds (bounds.removeFromBottom (kPanelHeight).reduced (1));
    plot = bounds.toFloat().withTrimmedLeft (52.0f).withTrimmedTop (14.0f).withTrimmedRight (16.0f).withTrimmedBottom (26.0f);
}

void StageDisplay::timerCallback()
{
    for (int i = 0; i < dbx::kNumVoices; ++i)
        shownLevel[(size_t) i] = jmax (processor.getVoiceLevel (i), shownLevel[(size_t) i] * 0.86f);

    panel->refresh();
    repaint (getLocalBounds().withTrimmedBottom (kPanelHeight));
}

//==============================================================================
void StageDisplay::select (int voice)
{
    inSelection.fill (false);

    selected = jlimit (0, dbx::kNumVoices - 1, voice);
    inSelection[(size_t) selected] = true;

    selectionChanged();
}

void StageDisplay::setSelected (int voice, bool shouldBeSelected)
{
    if (! isPositiveAndBelow (voice, dbx::kNumVoices))
        return;

    inSelection[(size_t) voice] = shouldBeSelected;

    if (shouldBeSelected)
        selected = voice;

    selectionChanged();
}

void StageDisplay::selectionChanged()
{
    std::vector<int> voices;

    for (int i = 0; i < dbx::kNumVoices; ++i)
        if (inSelection[(size_t) i])
            voices.push_back (i);

    // The panel reads from the voice clicked last, as long as it is still one
    // of those selected; with none selected, that voice stays in it alone.
    if (voices.empty() && ! framing)
    {
        inSelection[(size_t) selected] = true;
        voices.push_back (selected);
    }
    else if (! voices.empty() && ! isSelected (selected))
    {
        selected = voices.front();
    }

    if (! voices.empty() && (voices != shownVoices || selected != shownPrimary))
    {
        shownVoices = voices;
        shownPrimary = selected;
        panel->setVoices (voices, selected);
    }

    repaint();
}

std::vector<int> StageDisplay::withSelection (int voice) const
{
    if (! isSelected (voice))
        return { voice };

    std::vector<int> voices;

    for (int i = 0; i < dbx::kNumVoices; ++i)
        if (inSelection[(size_t) i])
            voices.push_back (i);

    return voices;
}

//==============================================================================
float StageDisplay::xForPan (float pan) const
{
    return plot.getX() + kInsetX + (pan + 100.0f) / 200.0f * (plot.getWidth() - 2.0f * kInsetX);
}

float StageDisplay::panForX (float x) const
{
    return (x - plot.getX() - kInsetX) / (plot.getWidth() - 2.0f * kInsetX) * 200.0f - 100.0f;
}

float StageDisplay::yForLevel (float db) const
{
    const auto travel = plot.getHeight() - kInsetTop - kInsetBottom;
    return plot.getY() + kInsetTop + (kTopDb - db) / (kTopDb - kBottomDb) * travel;
}

float StageDisplay::levelForY (float y) const
{
    const auto travel = plot.getHeight() - kInsetTop - kInsetBottom;
    return kTopDb - (y - plot.getY() - kInsetTop) / travel * (kTopDb - kBottomDb);
}

Point<float> StageDisplay::voicePosition (int voice) const
{
    const auto ids = voiceIds (voice);
    return { xForPan (parameter (ids.pan)), yForLevel (parameter (ids.level)) };
}

int StageDisplay::voiceAt (Point<float> position) const
{
    int best = -1;
    auto nearest = 17.0f;

    for (int i = 0; i < dbx::kNumVoices; ++i)
    {
        // A selected voice wins a tie, so that it can be pulled out from
        // under another one.
        const auto distance = voicePosition (i).getDistanceFrom (position) - (isSelected (i) ? 3.0f : 0.0f);

        if (distance < nearest)
        {
            nearest = distance;
            best = i;
        }
    }

    return best;
}

float StageDisplay::parameter (const String& id) const
{
    return processor.apvts.getRawParameterValue (id)->load();
}

void StageDisplay::setParameter (const String& id, float value)
{
    if (auto* p = processor.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value)));
}

void StageDisplay::gesture (const String& id, bool begin)
{
    if (auto* p = processor.apvts.getParameter (id))
    {
        if (begin)
            p->beginChangeGesture();
        else
            p->endChangeGesture();
    }
}

void StageDisplay::changeParameter (const String& id, float value)
{
    gesture (id, true);
    setParameter (id, value);
    gesture (id, false);
}

//==============================================================================
void StageDisplay::mouseMove (const MouseEvent& e)
{
    const auto over = voiceAt (e.position);

    if (over != hovered)
    {
        hovered = over;
        setMouseCursor (over >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
        repaint();
    }
}

void StageDisplay::mouseExit (const MouseEvent&)
{
    hovered = -1;
    repaint();
}

void StageDisplay::mouseDown (const MouseEvent& e)
{
    const auto voice = voiceAt (e.position);

    // A right click puts a voice back where it started, and the rest of the
    // selection with it.
    if (e.mods.isPopupMenu())
    {
        if (voice < 0)
            return;

        for (const auto i : withSelection (voice))
        {
            const auto ids = voiceIds (i);

            for (const auto& id : { ids.pan, ids.level, ids.delay, ids.detune })
                if (auto* p = processor.apvts.getParameter (id))
                    changeParameter (id, p->convertFrom0to1 (p->getDefaultValue()));
        }

        return;
    }

    const auto adding = e.mods.isCtrlDown() || e.mods.isShiftDown() || e.mods.isCommandDown();
    dragOrigin = e.position;

    // In the empty: the start of a frame, which adds to the selection with
    // Ctrl or Shift held and replaces it without.
    if (voice < 0)
    {
        if (! plot.contains (e.position))
            return;

        framing = true;

        if (! adding)
            inSelection.fill (false);

        frameBase = inSelection;
        frame = { e.position, e.position };
        selectionChanged();
        return;
    }

    if (adding)
    {
        inSelection[(size_t) voice] = ! inSelection[(size_t) voice];

        if (inSelection[(size_t) voice])
            selected = voice;

        selectionChanged();

        if (! inSelection[(size_t) voice])
            return;
    }
    else if (! inSelection[(size_t) voice])
    {
        // A click on a voice that is not selected selects it alone; on one
        // that is, the selection stays, to be dragged as a whole.
        select (voice);
    }
    else
    {
        selected = voice;
        selectionChanged();
    }

    dragging = voice;

    for (const auto i : withSelection (voice))
    {
        const auto ids = voiceIds (i);

        dragStart[(size_t) i] = { parameter (ids.pan), parameter (ids.level) };
        gesture (ids.pan, true);
        gesture (ids.level, true);
    }
}

void StageDisplay::mouseDrag (const MouseEvent& e)
{
    if (framing)
    {
        frame = { dragOrigin, e.position };

        for (int i = 0; i < dbx::kNumVoices; ++i)
            inSelection[(size_t) i] = frameBase[(size_t) i] || frame.contains (voicePosition (i));

        selectionChanged();
        return;
    }

    if (dragging < 0)
        return;

    const auto across = panForX (e.position.x) - panForX (dragOrigin.x);
    const auto lift = levelForY (e.position.y) - levelForY (dragOrigin.y);

    for (const auto i : withSelection (dragging))
    {
        const auto ids = voiceIds (i);

        setParameter (ids.pan, dragStart[(size_t) i].pan + across);
        setParameter (ids.level, dragStart[(size_t) i].level + lift);
    }

    repaint();
}

void StageDisplay::mouseUp (const MouseEvent&)
{
    if (framing)
    {
        framing = false;
        selectionChanged();
        return;
    }

    if (dragging < 0)
        return;

    for (const auto i : withSelection (dragging))
    {
        gesture (voiceIds (i).pan, false);
        gesture (voiceIds (i).level, false);
    }

    dragging = -1;
}

void StageDisplay::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    const auto voice = voiceAt (e.position);

    if (voice < 0)
        return;

    // Switch it off, or bring it back, and the rest of the selection with it.
    const auto on = parameter (voiceIds (voice).on) > 0.5f ? 0.0f : 1.0f;

    for (const auto i : withSelection (voice))
        changeParameter (voiceIds (i).on, on);
}

void StageDisplay::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    auto voice = voiceAt (e.position);

    if (voice < 0 && plot.contains (e.position))
        voice = selected;

    if (voice < 0 || wheel.deltaY == 0.0f)
        return;

    // A cent a notch; a tenth with Shift held.
    const auto step = (e.mods.isShiftDown() ? 0.1f : 1.0f) * (wheel.deltaY > 0.0f ? 1.0f : -1.0f);

    for (const auto i : withSelection (voice))
    {
        const auto id = voiceIds (i).detune;
        changeParameter (id, parameter (id) + step);
    }
}

//==============================================================================
void StageDisplay::paintGrid (Graphics& g)
{
    for (const auto pan : { -100.0f, -50.0f, 0.0f, 50.0f, 100.0f })
    {
        g.setColour (Colour (0xffffd2d6).withAlpha (pan == 0.0f ? 0.16f : 0.06f));
        g.fillRect (xForPan (pan), plot.getY(), 1.0f, plot.getHeight());
    }

    for (auto db = kTopDb; db >= kBottomDb; db -= 6.0f)
    {
        g.setColour (Colour (0xffffd2d6).withAlpha (db == 0.0f ? 0.2f : 0.06f));
        g.fillRect (plot.getX(), yForLevel (db), plot.getWidth(), 1.0f);
    }
}

void StageDisplay::paintVoices (Graphics& g)
{
    const auto pitch = parameter ("pitch");
    const auto formant = parameter ("formant");
    const auto floor = plot.getBottom();
    const auto manipulating = parameter ("harm") != 0.0f || parameter ("fm") != 0.0f
                              || parameter ("alt") != 0.0f || parameter ("smear") != 0.0f
                              || parameter ("ratio") != 100.0f || parameter ("octave") != 0.0f
                              || parameter ("glide") != 0.0f || parameter ("stereo") != 0.0f
                              || parameter ("spread") != 0.0f;

    // A small diamond.
    auto markAt = [] (Point<float> c)
    {
        Path p;
        p.addQuadrilateral (c.x, c.y - 4.0f, c.x + 4.0f, c.y, c.x, c.y + 4.0f, c.x - 4.0f, c.y);
        return p;
    };

    // The voice in the panel is drawn last, over the others.
    for (int order = 0; order < dbx::kNumVoices; ++order)
    {
        const auto i = order < dbx::kNumVoices - 1 ? (order >= selected ? order + 1 : order) : selected;
        const auto ids = voiceIds (i);
        const auto on = parameter (ids.on) > 0.5f;
        const auto centre = voicePosition (i);
        const auto colour = voiceColour (i);
        const auto shape = Rectangle<float> (2.0f * kPointRadius, 2.0f * kPointRadius).withCentre (centre);

        // How loud it is set: a stem from the floor of the stage.
        g.setGradientFill (ColourGradient (colour.withAlpha (on ? 0.45f : 0.18f), centre.x, centre.y,
                                           colour.withAlpha (0.0f), centre.x, floor, false));
        g.fillRect (centre.x - 0.75f, centre.y + kPointRadius, 1.5f, jmax (0.0f, floor - centre.y - kPointRadius));

        // How loud it is at this moment: a glow that swells with the voice.
        if (on)
        {
            const auto db = Decibels::gainToDecibels (shownLevel[(size_t) i], -60.0f);
            const auto amount = jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);

            if (amount > 0.0f)
            {
                const auto reach = kPointRadius + 6.0f + 24.0f * amount;

                g.setGradientFill (ColourGradient (colour.withAlpha (0.1f + 0.3f * amount), centre.x, centre.y,
                                                   colour.withAlpha (0.0f), centre.x + reach, centre.y, true));
                g.fillEllipse (Rectangle<float> (2.0f * reach, 2.0f * reach).withCentre (centre));
            }
        }

        if (isSelected (i))
        {
            g.setColour (colour.withAlpha (0.22f));
            g.fillEllipse (shape.expanded (6.0f));
            g.setColour (colour.withAlpha (0.8f));
            g.drawEllipse (shape.expanded (4.0f), 1.2f);
        }
        else if (i == hovered)
        {
            g.setColour (colour.withAlpha (0.2f));
            g.fillEllipse (shape.expanded (5.0f));
        }

        g.setColour (Colour (0xff120607));
        g.fillEllipse (shape.expanded (1.5f));

        if (on)
        {
            g.setColour (colour);
            g.fillEllipse (shape);
            g.setColour (Colours::white.withAlpha (0.85f));
            g.drawEllipse (shape, 1.2f);
        }
        else
        {
            // Switched off: the ring stays where it was, empty.
            g.setColour (colour.withAlpha (0.6f));
            g.drawEllipse (shape, 1.4f);
        }

        // Its name over it.
        const auto tag = Rectangle<float> (26.0f, 15.0f).withCentre ({ centre.x, centre.y - kPointRadius - 15.0f });

        g.setColour (Colour (0xcc120607));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (colour.withAlpha (on ? 1.0f : 0.6f));
        g.setFont (monoFont (10.5f).boldened());
        g.drawText (voiceTag (i), tag, Justification::centred, false);

        // And under it what the stage does not show: how late and how far
        // out of tune, then the pitch and the formant when they are for it.
        const auto alpha = on ? 0.85f : 0.4f;
        auto text = Rectangle<float> (124.0f, 12.0f).withCentre ({ centre.x, centre.y + kPointRadius + 13.0f });

        // A voice at the edge of the stage still has all of its numbers on it.
        text.setX (jlimit (plot.getX() + 2.0f, plot.getRight() - text.getWidth() - 2.0f, text.getX()));

        const auto& state = processor.apvts;

        g.setFont (monoFont (10.0f));
        g.setColour (mist.withAlpha (alpha));
        g.drawText (state.getParameter (ids.delay)->getCurrentValueAsText() + "  "
                        + state.getParameter (ids.detune)->getCurrentValueAsText(),
                    text, Justification::centred, false);

        if (parameter (ids.shift) > 0.5f && (pitch != 0.0f || formant != 0.0f))
        {
            StringArray parts;

            if (pitch != 0.0f)
                parts.add ("P " + signedText (pitch));

            if (formant != 0.0f)
                parts.add ("F " + signedText (formant));

            g.setColour (emberLit.withAlpha (on ? 1.0f : 0.5f));
            g.drawText (parts.joinIntoString ("  "), text.translated (0.0f, 12.0f), Justification::centred, false);
        }

        // A voice that is being manipulated wears a mark beside its name.
        if (manipulating && parameter (ids.manip) > 0.5f)
        {
            g.setColour (emberLit.withAlpha (on ? 1.0f : 0.5f));
            g.fillPath (markAt ({ tag.getRight() + 7.0f, tag.getCentreY() }));
        }
    }
}

void StageDisplay::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setGradientFill (ColourGradient (Colour (0xff24090d), 0.0f, bounds.getY(),
                                       Colour (0xff0d0506), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 14.0f);

    {
        Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());

        g.setColour (Colour (0x40000000));
        g.fillRect (plot);

        paintGrid (g);

        // The gestures, along the foot.
        g.setColour (haze.withAlpha (0.45f));
        g.setFont (capsFont (8.5f));
        g.drawText (CharPointer_UTF8 ("DRAG: PAN / LEVEL   \xc2\xb7   WHEEL: DETUNE   \xc2\xb7   DOUBLE-CLICK: ON / OFF   \xc2\xb7   "
                                      "RIGHT-CLICK: RESET   \xc2\xb7   CTRL / SHIFT OR A FRAME: SEVERAL"),
                    plot.reduced (10.0f, 6.0f), Justification::bottomLeft, false);

        paintVoices (g);

        if (framing)
        {
            g.setColour (ember.withAlpha (0.12f));
            g.fillRect (frame);
            g.setColour (emberLit.withAlpha (0.75f));
            g.drawRect (frame, 1.0f);
        }
    }

    g.setColour (Colour (0x33ffd2d6));
    g.drawRect (plot.expanded (0.5f), 1.0f);

    // The rule between the stage and the panel under it.
    const auto foot = bounds.withTop (bounds.getBottom() - (float) kPanelHeight);

    g.setColour (line);
    g.fillRect (foot.getX() + 14.0f, foot.getY(), foot.getWidth() - 28.0f, 1.0f);
    g.drawRoundedRectangle (bounds, 14.0f, 1.0f);

    // Left and right along the foot of the plot.
    g.setFont (monoFont (10.0f));
    g.setColour (haze);

    const std::pair<float, const char*> places[] = { { -100.0f, "L" }, { -50.0f, "50" }, { 0.0f, "C" },
                                                     { 50.0f, "50" }, { 100.0f, "R" } };

    for (const auto& place : places)
        g.drawText (place.second, Rectangle<float> (44.0f, 14.0f).withCentre ({ xForPan (place.first), plot.getBottom() + 12.0f }),
                    Justification::centred, false);

    // The level, down the side.
    for (auto db = kTopDb; db >= kBottomDb; db -= 6.0f)
    {
        const auto value = roundToInt (db);

        g.setColour (mist.withAlpha (value == 0 ? 0.8f : 0.55f));
        g.drawText (value == 0 ? String ("0 dB") : (value > 0 ? "+" : "") + String (value),
                    Rectangle<float> (0.0f, yForLevel (db) - 7.0f, plot.getX() - 7.0f, 14.0f),
                    Justification::centredRight, false);
    }
}

} // namespace onirique
