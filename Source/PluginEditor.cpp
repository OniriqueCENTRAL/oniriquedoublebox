#include "PluginEditor.h"

#include <BinaryData.h>

namespace onirique
{

using namespace theme;

namespace
{
    OniriqueLookAndFeel& sharedLookAndFeel()
    {
        static OniriqueLookAndFeel laf;
        return laf;
    }

    constexpr int kHeaderHeight = 68;
    constexpr int kTitleLeft = 72;
    constexpr int kColumnWidth = 244;
    constexpr int kSideWidth = 310;     // the column on the right, three dials across
    constexpr int kFootHeight = 54;
    constexpr int kEqHeight = 170;

    Font titleFont (bool heavy)
    {
        return displayFont (23.0f, heavy).withExtraKerningFactor (0.09f);
    }

    /** A four-pointed star, as on the Onirique sigil. */
    Path star (Point<float> centre, float radius)
    {
        Path p;
        const auto waist = radius * 0.16f;

        p.startNewSubPath (centre.x, centre.y - radius);
        p.lineTo (centre.x + waist, centre.y - waist);
        p.lineTo (centre.x + radius, centre.y);
        p.lineTo (centre.x + waist, centre.y + waist);
        p.lineTo (centre.x, centre.y + radius);
        p.lineTo (centre.x - waist, centre.y + waist);
        p.lineTo (centre.x - radius, centre.y);
        p.lineTo (centre.x - waist, centre.y - waist);
        p.closeSubPath();
        return p;
    }

    constexpr int kLogoWidth = 44;
    constexpr int kLogoMargin = 8;   // room for the glow around it

    Image makeLogo()
    {
        const auto sigil = ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);

        if (! sigil.isValid())
            return {};

        const auto width = 2 * kLogoWidth;
        const auto height = width * sigil.getHeight() / sigil.getWidth();
        const auto margin = 2 * kLogoMargin;

        Image padded (Image::ARGB, width + 2 * margin, height + 2 * margin, true);

        {
            Graphics g (padded);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (sigil, Rectangle<int> (margin, margin, width, height).toFloat());
        }

        Image result (Image::ARGB, padded.getWidth(), padded.getHeight(), true);
        Graphics g (result);

        GlowEffect glow;
        glow.setGlowProperties (7.0f, ember.withAlpha (0.8f));
        glow.applyEffect (padded, g, 1.0f, 1.0f);

        return result;
    }
}

DoubleBoxEditor::DoubleBoxEditor (DoubleBoxProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      stage (p),
      eq (p)
{
    setLookAndFeel (&sharedLookAndFeel());
    logo = makeLogo();

    auto& state = processor.apvts;

    addAndMakeVisible (stage);
    addAndMakeVisible (eq);

    // Presets.
    for (auto* b : { &previousButton, &nextButton, &saveButton, &deleteButton,
                     &slotAButton, &slotBButton, &copyButton, &eqAllButton, &eqDoublesButton })
    {
        b->getProperties().set ("plain", true);
        addAndMakeVisible (*b);
    }

    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    saveButton.onClick = [this] { beginSaving(); };
    deleteButton.onClick = [this] { deleteClicked(); };

    presetBox.setTextWhenNothingSelected ("Default");
    presetBox.onChange = [this]
    {
        const auto index = presetBox.getSelectedId() - 1;

        if (isPositiveAndBelow (index, presetNames.size()))
            processor.loadPreset (presetNames[index]);
    };
    addAndMakeVisible (presetBox);

    // The name of a preset being saved is typed where its name is shown.
    // It lies over the menu and looks like it, with the text where the menu
    // had it, so that the name seems to be typed into the menu itself.
    presetNameEditor.getProperties().set ("box", true);
    presetNameEditor.setJustificationType (Justification::centredLeft);
    presetNameEditor.setBorderSize ({ 0, 13, 0, 30 });
    presetNameEditor.setEditable (true);
    presetNameEditor.onTextChange = [this]
    {
        const auto saved = processor.saveUserPreset (presetNameEditor.getText());

        if (saved.isNotEmpty())
            refreshPresets();
    };
    presetNameEditor.onEditorHide = [this] { presetNameEditor.setVisible (false); };
    addChildComponent (presetNameEditor);

    refreshPresets();

    // A and B.
    slotAButton.onClick = [this] { processor.switchToSlot (0); };
    slotBButton.onClick = [this] { processor.switchToSlot (1); };
    copyButton.onClick = [this] { processor.copyToOtherSlot(); };

    const std::pair<Knob*, const char*> knobs[] = { { &pitch, "pitch" }, { &formant, "formant" },
                                                    { &drift, "drift" }, { &rate, "rate" },
                                                    { &harmonics, "harm" }, { &fm, "fm" },
                                                    { &alternator, "alt" }, { &smear, "smear" },
                                                    { &ratio, "ratio" }, { &octave, "octave" },
                                                    { &glide, "glide" }, { &stereo, "stereo" },
                                                    { &spread, "spread" } };

    for (const auto& [knob, id] : knobs)
    {
        knob->attach (state, id);
        addAndMakeVisible (*knob);
    }

    pitch.setEnds ("-12", "+12");
    formant.setEnds ("-12", "+12");

    // The voices the pitch and the formant are for, one button each, laid
    // out as the voices are: from the far left to the far right.
    for (int i = 0; i < dbx::kNumVoices; ++i)
    {
        auto& button = applyButtons[i];

        button.setButtonText (voiceTag (i));
        button.getProperties().set ("plain", true);
        button.setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, voiceIds (i).shift, button));
        addAndMakeVisible (button);

        // And the same row again on the right, for the manipulations.
        auto& manip = manipButtons[i];

        manip.setButtonText (voiceTag (i));
        manip.getProperties().set ("plain", true);
        manip.setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, voiceIds (i).manip, manip));
        addAndMakeVisible (manip);
    }

    const std::pair<TextButton*, const char*> buttons[] = { { &bypassButton, "bypass" }, { &eqButton, "eq_on" },
                                                            { &snapButton, "snap" } };

    for (const auto& [button, id] : buttons)
    {
        button->setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, id, *button));
        addAndMakeVisible (*button);
    }

    // All and doubles are the two halves of one switch.
    eqTargetAttachment = std::make_unique<ParameterAttachment> (*state.getParameter ("eq_target"), [this] (float value)
    {
        eqAllButton.setToggleState (value < 0.5f, dontSendNotification);
        eqDoublesButton.setToggleState (value >= 0.5f, dontSendNotification);
    });

    eqAllButton.onClick = [this] { eqTargetAttachment->setValueAsCompleteGesture (0.0f); };
    eqDoublesButton.onClick = [this] { eqTargetAttachment->setValueAsCompleteGesture (1.0f); };
    eqTargetAttachment->sendInitialUpdate();

    const std::pair<ValueField*, const char*> fields[] = { { &doubles, "doubles" }, { &width, "width" }, { &output, "out" },
                                                           { &wet, "wet" } };

    for (const auto& [field, id] : fields)
    {
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, id, *field));
        addAndMakeVisible (*field);
    }

    // What each control does, for a pointer that rests on it. A control that
    // lies on another is described after it, and so found first.
    for (const auto& [component, text] : stage.getDescriptions())
        describe ({ component }, text);

    describe ({ &presetBox }, "Presets : ceux d'origine, puis les tiens.");
    describe ({ &previousButton, &nextButton }, "Le preset d'avant, le preset d'après.");
    describe ({ &saveButton }, "Enregistre les réglages comme un preset à toi : tape son nom, puis Entrée.");
    describe ({ &deleteButton }, "Supprime le preset affiché, si c'est l'un des tiens. Il demande une confirmation.");
    describe ({ &slotAButton, &slotBButton }, "A et B : deux réglages côte à côte, pour comparer.");
    describe ({ &copyButton }, "Copie les réglages qu'on écoute dans l'autre slot.");
    describe ({ &bypassButton }, "Bypass : l'entrée telle qu'elle est arrivée, avec le même retard que le plugin.");
    describe ({ &pitch }, "Pitch : monte ou descend les voix auxquelles il est donné, en demi-tons, en gardant leurs voyelles.");
    describe ({ &formant }, "Formant : déplace les voyelles des voix auxquelles il est donné, plus graves ou plus fines, en gardant leur hauteur.");
    describe ({ &applyButtons[0], &applyButtons[1], &applyButtons[2], &applyButtons[3], &applyButtons[4] },
              "Apply to : les voix qui reçoivent le pitch et le formant, de l'extrême gauche à l'extrême droite.");
    describe ({ &drift }, "Drift : de combien les doubles dérivent en hauteur et en temps, chacun de son côté, comme une seconde prise.");
    describe ({ &rate }, "Rate : la vitesse à laquelle les doubles dérivent.");
    describe ({ &eqButton }, "EQ en marche ou à l'arrêt.");
    describe ({ &eqAllButton }, "All : l'EQ traite toutes les voix.");
    describe ({ &eqDoublesButton }, "Doubles : l'EQ ne traite que les quatre doubles et laisse la voix du centre telle quelle.");
    describe ({ &eq }, "Un plateau grave et un plateau aigu. Glisse un point de gauche à droite pour sa fréquence, de haut en bas pour son gain. Double-clic : gain à zéro.");
    describe ({ &doubles }, "Doubles : le niveau des quatre doubles ensemble.");
    describe ({ &width }, "Width : l'écart entre les voix. À 0, elles sont toutes au centre.");
    describe ({ &output }, "Out : le niveau de tout ce qui sort.");
    describe ({ &harmonics }, "Harmonics : fait glisser les harmoniques de la voix vers l'aigu ou vers le grave sans changer sa hauteur. Le timbre devient métallique, creux ou nasal.");
    describe ({ &fm }, "FM : chaque cycle de la voix est lu avec une oscillation à sa propre hauteur. Ça ajoute du grain, puis un côté robot, sans changer la note.");
    describe ({ &alternator }, "Alternator : un cycle sur deux est baissé, puis retourné. À fond, la voix sonne une octave plus bas, creuse.");
    describe ({ &smear }, "Smear : les cycles sont pris de plus en plus loin en arrière, au hasard. Les attaques et les consonnes se fondent en nappe.");
    describe ({ &ratio }, "Ratio : ce qui reste de la mélodie chantée. À 100 % la voix la suit, à 0 % elle tient une seule note (robot), au-delà elle l'exagère, en négatif elle l'inverse.");
    describe ({ &octave }, "Octave : déplace la voix d'octaves entières, vers le bas ou vers le haut, en gardant ses voyelles.");
    describe ({ &glide }, "Glide : le temps que met la voix pour rejoindre une nouvelle note. Elle glisse d'une note à l'autre au lieu de sauter.");
    describe ({ &stereo }, "Stereo : un cycle de la voix part à gauche, le suivant à droite. La voix s'élargit, et reste entière en mono.");
    describe ({ &spread }, "Detune : désaccorde en plus les voix manipulées, celles de gauche vers le bas et celles de droite vers le haut.");
    describe ({ &snapButton }, "Snap : Harmonics ne glisse plus que par harmoniques entières, pour un timbre net plutôt que métallique.");
    describe ({ &wet }, "Dry / wet : entre les voix manipulées telles qu'elles arrivent (dry) et avec tout ce qui leur est fait (wet).");
    describe ({ &manipButtons[0], &manipButtons[1], &manipButtons[2], &manipButtons[3], &manipButtons[4] },
              "Apply to : les voix qui passent par les quatre manipulations, de l'extrême gauche à l'extrême droite.");
    hoverTip.setInterceptsMouseClicks (false, false);
    hoverTip.setAlwaysOnTop (true);
    addChildComponent (hoverTip);

    setResizable (true, true);
    setResizeLimits (1350, 640, 2300, 1250);
    setSize (1490, 720);

    timerCallback();
    // The licence pill and the activation page, over everything else.
    licenseOverlay = std::make_unique<license::Overlay> ("ONIRIQUE DOUBLEBOX", processor.license);
    addAndMakeVisible (*licenseOverlay);
    resized();

    startTimerHz (30);
}

DoubleBoxEditor::~DoubleBoxEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void DoubleBoxEditor::refreshPresets()
{
    const auto stock = processor.getStockPresets();
    const auto user = processor.getUserPresets();

    presetNames = stock;
    stockCount = stock.size();
    presetNames.addArray (user);

    presetBox.clear (dontSendNotification);
    presetBox.addSectionHeading ("STOCK");

    for (int i = 0; i < stock.size(); ++i)
        presetBox.addItem (stock[i], i + 1);

    if (! user.isEmpty())
    {
        presetBox.addSectionHeading ("USER");

        for (int i = 0; i < user.size(); ++i)
            presetBox.addItem (user[i], stock.size() + i + 1);
    }

    presetBox.setSelectedId (presetNames.indexOf (processor.getPresetName()) + 1, dontSendNotification);
}

void DoubleBoxEditor::stepPreset (int direction)
{
    if (presetNames.isEmpty())
        return;

    const auto count = presetNames.size();
    const auto current = presetNames.indexOf (processor.getPresetName());
    const auto next = current < 0 ? 0 : (current + direction + count) % count;

    processor.loadPreset (presetNames[next]);
}

void DoubleBoxEditor::beginSaving()
{
    const auto current = processor.getPresetName();

    presetNameEditor.setText ({}, dontSendNotification);
    presetNameEditor.setVisible (true);
    presetNameEditor.showEditor();

    // The name on show is there, selected: Enter keeps it, typing replaces
    // it, Escape gives up. A stock preset's name comes back with "(user)".
    if (auto* editor = presetNameEditor.getCurrentTextEditor())
    {
        // Where the menu wrote it: the same inset, on the middle line.
        editor->setBorder ({ 0, 13, 0, 30 });
        editor->setIndents (0, 0);
        editor->setJustification (Justification::centredLeft);
        editor->setText (current, false);
        editor->selectAll();
    }
}

void DoubleBoxEditor::deleteClicked()
{
    // Deleting is for good, so it takes two clicks: the first asks.
    if (deleteArmed == 0)
    {
        deleteArmed = 90;
        return;
    }

    deleteArmed = 0;
    processor.deleteUserPreset (processor.getPresetName());
    refreshPresets();
}

void DoubleBoxEditor::timerCallback()
{
    // What a preset, the other slot or the host may have changed under us.
    const auto name = processor.getPresetName();
    const auto index = presetNames.indexOf (name);

    if (presetBox.getSelectedId() != index + 1)
        presetBox.setSelectedId (index + 1, dontSendNotification);

    if (deleteArmed > 0)
        --deleteArmed;

    deleteButton.setEnabled (index >= stockCount);
    deleteButton.setButtonText (deleteArmed > 0 ? "SURE ?" : "DELETE");
    deleteButton.setToggleState (deleteArmed > 0, dontSendNotification);

    const auto slot = processor.getActiveSlot();

    slotAButton.setToggleState (slot == 0, dontSendNotification);
    slotBButton.setToggleState (slot == 1, dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "A > B" : "B > A");

    // With the EQ off, what it would have been for is of no account.
    const auto eqOn = eqButton.getToggleState();

    for (auto* b : { &eqAllButton, &eqDoublesButton })
        b->setAlpha (eqOn ? 1.0f : 0.4f);

    // The rate is that of a wander there may be none of.
    rate.setAlpha (drift.slider.getValue() > 0.0 ? 1.0f : 0.4f);

    eq.repaint();
    updateHoverTip();

    // The header is the only part of the frame that moves by itself.
    repaint (0, 0, getWidth(), kHeaderHeight);
}

void DoubleBoxEditor::describe (std::initializer_list<Component*> components, const char* text)
{
    // The descriptions are written in French, accents and all.
    for (auto* c : components)
        described.push_back ({ c, String::fromUTF8 (text) });
}

void DoubleBoxEditor::updateHoverTip()
{
    const auto now = Time::getMillisecondCounter();
    const auto at = getMouseXYRelative();

    // Only while the pointer is over this window with no button held.
    auto* under = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    const auto here = under != nullptr && (under == this || isParentOf (under))
                      && ! ModifierKeys::currentModifiers.isAnyMouseButtonDown();

    int over = -1;

    if (here)
        for (int i = 0; i < (int) described.size(); ++i)
            if (auto* c = described[(size_t) i].component; c->isShowing() && getLocalArea (c, c->getLocalBounds()).contains (at))
                over = i;

    // Leaving the control takes the description away, and starts the wait
    // for the next one.
    if (over != resting)
    {
        resting = over;
        restingAt = at;
        restingSince = now;
        hoverTip.setVisible (false);
        return;
    }

    // Once it is there it stays for as long as the pointer is on the
    // control, moving or not.
    if (resting < 0 || hoverTip.isVisible())
        return;

    // Until then, any movement starts the two seconds again.
    if (at.getDistanceFrom (restingAt) > 3)
    {
        restingAt = at;
        restingSince = now;
        return;
    }

    if (now - restingSince < 2000)
        return;

    const auto& text = described[(size_t) resting].text;

    hoverTip.text = text;
    hoverTip.setBounds (getLookAndFeel().getTooltipBounds (text, at, getLocalBounds().reduced (6)));
    hoverTip.setVisible (true);
    hoverTip.toFront (false);
    hoverTip.repaint();
}

int DoubleBoxEditor::titleRight() const
{
    return kTitleLeft
           + roundToInt (GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE")
                         + GlyphArrangement::getStringWidth (titleFont (false), "DOUBLEBOX"))
           + 12;
}

void DoubleBoxEditor::paintHeader (Graphics& g, Rectangle<int> header)
{
    const auto area = header.toFloat();
    const auto seconds = (Time::getMillisecondCounterHiRes() - openedAt) * 0.001;

    // The crimson of the range: darkest at the sigil, brightest behind the
    // name, and falling away again to the right and towards the foot.
    ColourGradient ground (Colour (0xff2c060b), 0.0f, 0.0f, Colour (0xff3b080e), area.getRight(), 0.0f, false);
    ground.addColour (0.32, Colour (0xff7a0e1c));
    ground.addColour (0.7, Colour (0xff5a0a14));
    g.setGradientFill (ground);
    g.fillRect (area);

    g.setGradientFill (ColourGradient (Colours::transparentBlack, 0.0f, area.getY(),
                                       Colour (0x73000000), 0.0f, area.getBottom(), false));
    g.fillRect (area);

    if (logo.isValid())
    {
        const auto logoWidth = (float) logo.getWidth() * 0.5f;
        const auto logoHeight = (float) logo.getHeight() * 0.5f;

        g.drawImage (logo, { 16.0f - (float) kLogoMargin, area.getCentreY() - logoHeight * 0.5f, logoWidth, logoHeight });
    }

    // The name: the house in white, the product in a paler weight beside it.
    const auto nameWidth = GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE");

    g.setColour (Colours::white);
    g.setFont (titleFont (true));
    g.drawText ("ONIRIQUE", area.withTrimmedLeft ((float) kTitleLeft).withWidth (nameWidth + 4.0f),
                Justification::centredLeft, false);

    g.setColour (Colour (0xffe3b5ba));
    g.setFont (titleFont (false));
    g.drawText ("DOUBLEBOX", area.withTrimmedLeft ((float) kTitleLeft + nameWidth + 10.0f).withWidth (280.0f),
                Justification::centredLeft, false);

    // Stars beside it, each breathing at its own pace.
    struct Spark { float x, y, radius, rate, offset; };
    static const Spark sparks[] = { { 10.0f, 17.0f, 5.5f, 0.9f, 0.0f },  { 27.0f, 11.0f, 3.0f, 1.3f, 1.7f },
                                    { 38.0f, 24.0f, 4.0f, 0.7f, 3.1f },  { 18.0f, 42.0f, 4.5f, 1.1f, 4.4f },
                                    { 34.0f, 48.0f, 2.5f, 1.6f, 2.3f },  { 46.0f, 38.0f, 2.0f, 1.0f, 5.2f } };

    const auto sparkLeft = (float) titleRight();

    for (const auto& s : sparks)
    {
        const auto glow = 0.5f + 0.5f * (float) std::sin (seconds * s.rate * 2.0 + s.offset);

        g.setColour (Colours::white.withAlpha (0.25f + 0.65f * glow));
        g.fillPath (star ({ sparkLeft + s.x, area.getY() + s.y }, s.radius * (0.8f + 0.2f * glow)));
    }

    // The foot of the header: a faint rule, and a streak of light that runs
    // along it from left to right and comes round again.
    const auto footY = area.getBottom() - 2.0f;

    g.setColour (Colour (0x30ffd2d6));
    g.fillRect (0.0f, footY, area.getWidth(), 1.0f);

    const auto length = jmax (160.0f, area.getWidth() * 0.22f);
    const auto cycle = 3.6;
    const auto travelled = (float) (std::fmod (seconds, cycle) / cycle);
    const auto head = -40.0f + travelled * (area.getWidth() + length + 80.0f);
    const auto tail = head - length;

    // Bright at the head, trailing off behind it, with a halo around the lot.
    ColourGradient halo (Colours::transparentBlack, tail, 0.0f, Colours::transparentBlack, head + 14.0f, 0.0f, false);
    halo.addColour (0.82, ember.withAlpha (0.55f));
    g.setGradientFill (halo);
    g.fillRect (tail, footY - 3.0f, length + 14.0f, 7.0f);

    ColourGradient streak (Colours::transparentWhite, tail, 0.0f, Colours::transparentWhite, head + 6.0f, 0.0f, false);
    streak.addColour (0.55, emberLit.withAlpha (0.9f));
    streak.addColour (0.93, Colours::white);
    g.setGradientFill (streak);
    g.fillRoundedRectangle (tail, footY - 0.5f, length + 6.0f, 2.0f, 1.0f);
}

void DoubleBoxEditor::paint (Graphics& g)
{
    g.fillAll (night);

    paintHeader (g, getLocalBounds().removeFromTop (kHeaderHeight));

    // The column's ground, a shade redder than the rest.
    auto body = getLocalBounds().withTop (kHeaderHeight);
    const auto column = body.removeFromLeft (kColumnWidth);

    g.setColour (deep);
    g.fillRect (column);
    g.setColour (line);
    g.drawVerticalLine (column.getRight(), (float) column.getY(), (float) column.getBottom());

    // The same ground for the column on the right.
    const auto right = body.removeFromRight (kSideWidth);

    g.setColour (deep);
    g.fillRect (right);
    g.setColour (line);
    g.drawVerticalLine (right.getX(), (float) right.getY(), (float) right.getBottom());

    // A rule between the three families of the column: what is done to the
    // chosen voices, how the doubles move, and the EQ.
    for (const auto& rule : { driftRule, eqRule })
        g.fillRect (rule);

    g.setColour (haze);
    g.setFont (capsFont (9.5f).withExtraKerningFactor (0.3f));
    g.drawText ("PITCH / FORMANT", shiftHeading, Justification::centred, false);
    g.drawText ("APPLY TO", applyHeading, Justification::centred, false);
    g.drawText ("MANIPULATE", manipHeading, Justification::centred, false);
    g.drawText ("APPLY TO", manipApplyHeading, Justification::centred, false);

    // And between the two families of the foot.
    g.setColour (line);
    g.fillRect ((float) output.getX() - 14.0f, (float) output.getY() + 3.0f, 1.0f, (float) output.getHeight() - 6.0f);
}

void DoubleBoxEditor::resized()
{
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), 1.0f, { 560, getHeight() - 46, 248, 26 });

    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (kHeaderHeight);

    // Controls sit in the upper part of the header, clear of the light that
    // runs along its foot.
    auto row = header.withTrimmedBottom (6).withSizeKeepingCentre (header.getWidth(), 30).withTrimmedRight (14);

    bypassButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (18);

    copyButton.setBounds (row.removeFromRight (64));
    row.removeFromRight (4);
    slotBButton.setBounds (row.removeFromRight (34));
    row.removeFromRight (4);
    slotAButton.setBounds (row.removeFromRight (34));
    row.removeFromRight (18);

    deleteButton.setBounds (row.removeFromRight (74));
    row.removeFromRight (4);
    saveButton.setBounds (row.removeFromRight (60));
    row.removeFromRight (8);

    // The name takes what is left between the title and the buttons.
    row.removeFromLeft (titleRight() + 62);
    previousButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (4);
    nextButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (6);
    presetBox.setBounds (row);
    presetNameEditor.setBounds (row);

    auto column = bounds.removeFromLeft (kColumnWidth).reduced (14, 16);

    // From the foot of the column up: the EQ keeps its size, small, and what
    // is above it shares the rest.
    eq.setBounds (column.removeFromBottom (jmin (kEqHeight, column.getHeight() / 3)));
    column.removeFromBottom (8);

    auto eqRow = column.removeFromBottom (28);
    eqButton.setBounds (eqRow.removeFromLeft (62));
    eqRow.removeFromLeft (8);
    eqDoublesButton.setBounds (eqRow.removeFromRight (84));
    eqRow.removeFromRight (4);
    eqAllButton.setBounds (eqRow);

    column.removeFromBottom (12);
    eqRule = column.removeFromBottom (1).reduced (8, 0);

    shiftHeading = column.removeFromTop (14);
    column.removeFromTop (4);

    // What is left goes to the two rows of knobs, the upper one the larger.
    const auto fixed = 10 + 13 + 5 + 26 + 14 + 1 + 10 + 10;
    const auto knobs = jmax (160, column.getHeight() - fixed);
    const auto upperHeight = jlimit (96, 150, knobs * 11 / 20);
    const auto lowerHeight = jlimit (84, 124, knobs - upperHeight);

    auto upper = column.removeFromTop (upperHeight);
    pitch.setBounds (upper.removeFromLeft (upper.getWidth() / 2));
    formant.setBounds (upper);
    column.removeFromTop (10);

    applyHeading = column.removeFromTop (13);
    column.removeFromTop (5);

    auto apply = column.removeFromTop (26);
    const auto each = (apply.getWidth() - 4 * 4) / dbx::kNumVoices;

    for (auto& button : applyButtons)
    {
        button.setBounds (apply.removeFromLeft (each));
        apply.removeFromLeft (4);
    }

    column.removeFromTop (14);
    driftRule = column.removeFromTop (1).reduced (8, 0);
    column.removeFromTop (10);

    auto lower = column.removeFromTop (lowerHeight);
    drift.setBounds (lower.removeFromLeft (lower.getWidth() / 2));
    rate.setBounds (lower);

    // The column on the right: two rows of dials, then the voices they are
    // for, as on the left.
    auto side = bounds.removeFromRight (kSideWidth).reduced (14, 16);

    manipHeading = side.removeFromTop (14);
    side.removeFromTop (4);

    // Three rows of three: what is done to the sound of a voice, how it
    // follows the one it copies, and where it is put.
    Knob* const dials[3][3] = { { &harmonics, &fm, &alternator }, { &ratio, &octave, &glide }, { &smear, &stereo, &spread } };
    const auto dialHeight = jlimit (92, 124, (side.getHeight() - 14 - 30 - 12 - 13 - 5 - 26 - 3 * 8) / 3);

    for (const auto& three : dials)
    {
        auto strip = side.removeFromTop (dialHeight);
        const auto third = strip.getWidth() / 3;

        three[0]->setBounds (strip.removeFromLeft (third));
        three[1]->setBounds (strip.removeFromLeft (third));
        three[2]->setBounds (strip);
        side.removeFromTop (8);
    }

    side.removeFromTop (6);

    auto last = side.removeFromTop (30);
    snapButton.setBounds (last.removeFromLeft (84).withSizeKeepingCentre (84, 28));
    wet.setBounds (last.removeFromRight (150));
    side.removeFromTop (12);

    manipApplyHeading = side.removeFromTop (13);
    side.removeFromTop (5);

    auto manipRow = side.removeFromTop (26);
    const auto manipEach = (manipRow.getWidth() - 4 * 4) / dbx::kNumVoices;

    for (auto& button : manipButtons)
    {
        button.setBounds (manipRow.removeFromLeft (manipEach));
        manipRow.removeFromLeft (4);
    }

    auto foot = bounds.removeFromBottom (kFootHeight).reduced (14, 0).withTrimmedBottom (12);

    // From the left: how much of the doubles and how far apart; from the
    // right: what comes out.
    doubles.setBounds (foot.removeFromLeft (136));
    foot.removeFromLeft (16);
    width.setBounds (foot.removeFromLeft (116));
    output.setBounds (foot.removeFromRight (100));

    stage.setBounds (bounds.reduced (14, 12));
}

} // namespace onirique
