#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace onirique
{

VoiceIds voiceIds (int voice)
{
    const auto prefix = "v" + String (voice + 1) + "_";
    return { prefix + "on", prefix + "level", prefix + "pan", prefix + "delay", prefix + "detune", prefix + "shift",
             prefix + "manip" };
}

String voiceTag (int voice)
{
    static const char* const tags[dbx::kNumVoices] = { "LL", "L", "C", "R", "RR" };
    return isPositiveAndBelow (voice, dbx::kNumVoices) ? tags[voice] : "";
}

String voiceName (int voice)
{
    static const char* const names[dbx::kNumVoices] = { "Far Left", "Left", "Centre", "Right", "Far Right" };
    return isPositiveAndBelow (voice, dbx::kNumVoices) ? names[voice] : "";
}

namespace
{
    using Attributes = AudioParameterFloatAttributes;

    const Identifier kPresetProperty ("preset");
    const Identifier kSlotProperty ("slot");
    const Identifier kOtherSlot ("OtherSlot");
    const Identifier kSnapshot ("Snapshot");
    const char* const kPresetExtension = ".doublebox";

    /** What a preset or an A/B slot leaves as it is: how the plugin is being
        listened to, not what it does to the sound. */
    bool isStored (const String& id)
    {
        return id != "bypass";
    }

    /** A range that is even to the ear: each octave, or each doubling of a
        time, takes the same travel. */
    NormalisableRange<float> logRange (float low, float high)
    {
        return { low, high,
                 [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 [] (float a, float b, float v) { return jlimit (a, b, v); } };
    }

    float parseNumber (const String& text)
    {
        return text.retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getFloatValue();
    }

    /** A value that can go either way says which way it went. */
    Attributes signedValue (int decimals, const String& unit)
    {
        return Attributes()
            .withStringFromValueFunction ([decimals, unit] (float v, int)
                                          {
                                              const auto text = String (v, decimals);
                                              return (v > 0.0f && text.containsAnyOf ("123456789") ? "+" : "") + text + " " + unit;
                                          })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    Attributes hertz()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int)
                                          {
                                              return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz"
                                                                  : v < 10.0f ? String (v, 2) + " Hz"
                                                                              : String (roundToInt (v)) + " Hz";
                                          })
            .withValueFromStringFunction ([] (const String& t)
                                          {
                                              const auto v = parseNumber (t);
                                              return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
                                          });
    }

    Attributes milliseconds()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    /** A setting kept in hundredths and shown as the number it stands for:
        1.00 where a percentage would say 100. */
    Attributes hundredths()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int)
                                          {
                                              const auto text = String (v * 0.01f, 2);
                                              return text == "-0.00" ? String ("0.00") : text;
                                          })
            .withValueFromStringFunction ([] (const String& t) { return 100.0f * parseNumber (t); });
    }

    Attributes percent()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    Attributes pan()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int)
                                          {
                                              const auto amount = roundToInt (std::abs (v));
                                              return amount == 0 ? String ("C") : String (amount) + (v < 0.0f ? " L" : " R");
                                          })
            .withValueFromStringFunction ([] (const String& t)
                                          {
                                              const auto v = std::abs (parseNumber (t));
                                              return t.containsIgnoreCase ("l") || t.contains ("-") ? -v : v;
                                          });
    }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;
        const dbx::Settings defaults;

        auto number = [&layout] (const String& id, const String& name, NormalisableRange<float> range,
                                 float value, const Attributes& attributes)
        {
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, value, attributes));
        };

        auto toggle = [&layout] (const String& id, const String& name, bool value)
        {
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, value));
        };

        number ("pitch", "Pitch", { -12.0f, 12.0f, 0.01f }, defaults.pitchSemitones, signedValue (2, "st"));
        number ("formant", "Formant", { -12.0f, 12.0f, 0.01f }, defaults.formantSemitones, signedValue (2, "st"));

        for (int i = 0; i < dbx::kNumVoices; ++i)
        {
            const auto ids = voiceIds (i);
            const auto name = voiceName (i);
            const auto& voice = defaults.voices[i];

            toggle (ids.on, name, voice.on);
            number (ids.level, name + " Level", { -36.0f, 6.0f, 0.1f }, voice.levelDb, signedValue (1, "dB"));
            number (ids.pan, name + " Pan", { -100.0f, 100.0f, 1.0f }, voice.pan * 100.0f, pan());
            number (ids.delay, name + " Delay", { 0.0f, dbx::kMaxDelayMs, 0.1f }, voice.delayMs, milliseconds());
            number (ids.detune, name + " Detune", { -50.0f, 50.0f, 0.1f }, voice.detuneCents, signedValue (1, "ct"));
            toggle (ids.shift, name + " Pitch / Formant", voice.shift);
            toggle (ids.manip, name + " Manipulate", voice.manipulate);
        }

        number ("harm", "Harmonics", { -100.0f, 100.0f, 1.0f }, defaults.harmonics * 100.0f, hundredths());
        number ("fm", "FM", { 0.0f, 100.0f, 1.0f }, defaults.fm * 100.0f, hundredths());
        number ("alt", "Alternator", { 0.0f, 100.0f, 1.0f }, defaults.alternator * 100.0f, hundredths());
        number ("smear", "Smear", { 0.0f, 100.0f, 1.0f }, defaults.smear * 100.0f, hundredths());
        toggle ("snap", "Snap", defaults.snap);
        number ("ratio", "Ratio", { -100.0f, 200.0f, 1.0f }, defaults.ratio * 100.0f, hundredths());
        number ("octave", "Octave", { -2.0f, 2.0f, 1.0f }, defaults.octave, signedValue (0, ""));
        number ("glide", "Glide", { 0.0f, 500.0f, 1.0f }, defaults.glideMs, milliseconds());
        number ("stereo", "Stereo", { 0.0f, 100.0f, 1.0f }, defaults.stereo * 100.0f, hundredths());
        number ("spread", "Manipulate Detune", { 0.0f, 50.0f, 0.1f }, defaults.spreadCents, signedValue (1, "ct"));
        number ("wet", "Manipulate Dry / Wet", { 0.0f, 100.0f, 1.0f }, defaults.wet * 100.0f, percent());

        number ("drift", "Drift", { 0.0f, 100.0f, 1.0f }, defaults.drift * 100.0f, percent());
        number ("rate", "Drift Rate", logRange (0.05f, 3.0f), defaults.driftHz, hertz());
        number ("doubles", "Doubles", { -24.0f, 12.0f, 0.1f }, defaults.doublesDb, signedValue (1, "dB"));
        number ("width", "Width", { 0.0f, 100.0f, 1.0f }, defaults.width * 100.0f, percent());

        toggle ("eq_on", "EQ", defaults.eqOn);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "eq_target", 1 }, "EQ Target",
                                                            StringArray { "All", "Doubles" }, defaults.eqDoublesOnly ? 1 : 0));
        number ("lo_freq", "Low Shelf Frequency", logRange (30.0f, 600.0f), defaults.lowHz, hertz());
        number ("lo_gain", "Low Shelf Gain", { -12.0f, 12.0f, 0.1f }, defaults.lowDb, signedValue (1, "dB"));
        number ("hi_freq", "High Shelf Frequency", logRange (1500.0f, 18000.0f), defaults.highHz, hertz());
        number ("hi_gain", "High Shelf Gain", { -12.0f, 12.0f, 0.1f }, defaults.highDb, signedValue (1, "dB"));

        number ("out", "Output", { -24.0f, 24.0f, 0.1f }, defaults.outDb, signedValue (1, "dB"));
        toggle ("bypass", "Bypass", false);

        return layout;
    }

    //==============================================================================
    struct StockPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;   // whatever differs from the defaults
    };

    using Values = std::vector<std::pair<const char*, float>>;

    /** The centre voice alone, with the pitch, the formant and the
        manipulations given to it, and whatever else a preset asks for. */
    Values solo (Values more)
    {
        Values values { { "v1_on", 0.0f }, { "v2_on", 0.0f }, { "v4_on", 0.0f }, { "v5_on", 0.0f },
                        { "v3_shift", 1.0f }, { "v3_manip", 1.0f } };

        values.insert (values.end(), more.begin(), more.end());
        return values;
    }

    // Starting points, set by reasoning rather than by ear.
    const std::vector<StockPreset>& stockPresets()
    {
        static const std::vector<StockPreset> presets
        {
            { "Default", {} },

            { "Tight Double", { { "v1_on", 0.0f }, { "v5_on", 0.0f },
                                { "v2_pan", -70.0f }, { "v2_delay", 9.0f }, { "v2_detune", -5.0f }, { "v2_level", -4.0f },
                                { "v4_pan", 70.0f }, { "v4_delay", 12.0f }, { "v4_detune", 5.0f }, { "v4_level", -4.0f },
                                { "drift", 20.0f } } },

            { "Wide Stack", { { "v1_delay", 31.0f }, { "v1_detune", -14.0f }, { "v2_delay", 17.0f }, { "v2_detune", -8.0f },
                              { "v4_delay", 21.0f }, { "v4_detune", 9.0f }, { "v5_delay", 36.0f }, { "v5_detune", 15.0f },
                              { "doubles", 2.0f }, { "drift", 40.0f } } },

            { "Mono Thickener", { { "width", 0.0f }, { "v1_on", 0.0f }, { "v5_on", 0.0f },
                                  { "v2_delay", 11.0f }, { "v2_detune", -8.0f }, { "v4_delay", 15.0f }, { "v4_detune", 8.0f },
                                  { "drift", 25.0f } } },

            { "Slap Double", { { "v1_delay", 78.0f }, { "v2_delay", 55.0f }, { "v4_delay", 62.0f }, { "v5_delay", 86.0f },
                               { "v1_detune", -6.0f }, { "v2_detune", -3.0f }, { "v4_detune", 4.0f }, { "v5_detune", 7.0f },
                               { "doubles", -3.0f } } },

            { "Octave Below", { { "pitch", -12.0f },
                                { "v1_shift", 1.0f }, { "v2_shift", 0.0f }, { "v4_shift", 0.0f }, { "v5_shift", 1.0f },
                                { "v1_level", -10.0f }, { "v5_level", -10.0f },
                                { "eq_target", 1.0f }, { "lo_gain", 2.0f } } },

            { "Octave Air", { { "pitch", 12.0f },
                              { "v1_shift", 1.0f }, { "v2_shift", 0.0f }, { "v4_shift", 0.0f }, { "v5_shift", 1.0f },
                              { "v1_level", -14.0f }, { "v5_level", -14.0f },
                              { "eq_target", 1.0f }, { "hi_gain", 3.0f } } },

            { "Fifth Up Left", { { "pitch", 7.0f },
                                 { "v1_shift", 1.0f }, { "v2_shift", 0.0f }, { "v4_shift", 0.0f }, { "v5_shift", 0.0f },
                                 { "v1_level", -9.0f } } },

            { "Other Singers", { { "formant", 2.0f },
                                 { "v1_shift", 1.0f }, { "v2_shift", 0.0f }, { "v4_shift", 0.0f }, { "v5_shift", 1.0f },
                                 { "drift", 55.0f }, { "rate", 0.6f } } },

            { "Deep Shadow", { { "formant", -3.0f },
                               { "v1_shift", 1.0f }, { "v2_shift", 1.0f }, { "v4_shift", 0.0f }, { "v5_shift", 0.0f },
                               { "v1_level", -6.0f }, { "v2_level", -5.0f } } },

            { "Bright Sides", { { "eq_target", 1.0f }, { "hi_gain", 4.0f }, { "hi_freq", 7000.0f }, { "lo_gain", 2.0f },
                                { "drift", 30.0f } } },

            // The voice itself transformed, with no double beside it: the
            // centre voice alone, given the pitch, the formant and the
            // manipulations.
            { "Deep Voice", solo ({ { "pitch", -5.0f }, { "formant", -4.0f } }) },
            { "Giant", solo ({ { "pitch", -12.0f }, { "formant", -6.0f }, { "lo_gain", 3.0f } }) },
            { "Helium", solo ({ { "pitch", 7.0f }, { "formant", 6.0f } }) },
            { "Small Voice", solo ({ { "pitch", 3.0f }, { "formant", 4.0f } }) },
            { "Same Note Other Mouth", solo ({ { "formant", -5.0f } }) },
            { "Robot", solo ({ { "ratio", 0.0f }, { "fm", 25.0f } }) },
            { "Robot Low", solo ({ { "ratio", 0.0f }, { "octave", -1.0f }, { "formant", -3.0f }, { "fm", 35.0f } }) },
            { "Half Robot", solo ({ { "ratio", 40.0f }, { "glide", 60.0f } }) },
            { "Upside Down", solo ({ { "ratio", -100.0f } }) },
            { "Alien", solo ({ { "harm", 60.0f }, { "fm", 40.0f }, { "formant", 3.0f } }) },
            { "Metal Throat", solo ({ { "harm", 50.0f }, { "snap", 1.0f }, { "formant", -2.0f } }) },
            { "Hollow", solo ({ { "harm", -50.0f }, { "snap", 1.0f } }) },
            { "Demon", solo ({ { "octave", -1.0f }, { "alt", 60.0f }, { "formant", -5.0f }, { "fm", 20.0f } }) },
            { "Growl", solo ({ { "alt", 45.0f }, { "fm", 55.0f }, { "formant", -2.0f } }) },
            { "Lazy Singer", solo ({ { "glide", 280.0f } }) },
            { "Smeared", solo ({ { "smear", 75.0f }, { "glide", 120.0f } }) },

            // The same things done to the doubles, around a voice left alone.
            { "Android Choir", { { "ratio", 0.0f }, { "fm", 15.0f }, { "spread", 20.0f }, { "stereo", 60.0f },
                                 { "doubles", 2.0f } } },
            { "Ghost Pad", { { "smear", 90.0f }, { "stereo", 80.0f }, { "glide", 300.0f }, { "harm", 15.0f },
                             { "v3_level", -4.0f }, { "doubles", 3.0f } } },
            { "Sub Shadows", { { "octave", -1.0f }, { "v2_manip", 0.0f }, { "v4_manip", 0.0f },
                               { "v1_level", -10.0f }, { "v5_level", -10.0f } } },
            { "High Shadows", { { "octave", 1.0f }, { "v2_manip", 0.0f }, { "v4_manip", 0.0f },
                                { "v1_level", -14.0f }, { "v5_level", -14.0f }, { "smear", 30.0f } } },
            { "Wide Unison", { { "spread", 25.0f }, { "stereo", 70.0f }, { "drift", 30.0f } } },
            { "Grit Doubles", { { "fm", 35.0f }, { "alt", 15.0f } } },
            { "Whisper Cloud", { { "smear", 70.0f }, { "harm", 20.0f }, { "wet", 80.0f }, { "stereo", 50.0f } } },
            { "Alien Crowd", { { "harm", 45.0f }, { "formant", 3.0f }, { "ratio", 130.0f }, { "spread", 15.0f },
                               { "drift", 50.0f } } },
        };

        return presets;
    }

    const StockPreset* findStock (const String& name)
    {
        for (const auto& preset : stockPresets())
            if (name.equalsIgnoreCase (preset.name))
                return &preset;

        return nullptr;
    }

    File presetFile (const String& name)
    {
        return DoubleBoxProcessor::getUserPresetFolder()
                   .getChildFile (File::createLegalFileName (name) + kPresetExtension);
    }
}

/** The parameters' values, looked up once. */
struct DoubleBoxProcessor::Raw
{
    explicit Raw (AudioProcessorValueTreeState& state)
    {
        auto get = [&state] (const String& id) { return state.getRawParameterValue (id); };

        pitch = get ("pitch");       formant = get ("formant");     drift = get ("drift");
        rate = get ("rate");         doubles = get ("doubles");     width = get ("width");
        eqOn = get ("eq_on");        eqTarget = get ("eq_target");
        lowFreq = get ("lo_freq");   lowGain = get ("lo_gain");
        highFreq = get ("hi_freq");  highGain = get ("hi_gain");
        out = get ("out");           bypass = get ("bypass");
        harmonics = get ("harm");    fm = get ("fm");
        alternator = get ("alt");    smear = get ("smear");
        snap = get ("snap");         ratio = get ("ratio");
        octave = get ("octave");     glide = get ("glide");
        stereo = get ("stereo");     spread = get ("spread");
        wet = get ("wet");

        for (int i = 0; i < dbx::kNumVoices; ++i)
        {
            const auto ids = voiceIds (i);

            on[i] = get (ids.on);
            level[i] = get (ids.level);
            pan[i] = get (ids.pan);
            delay[i] = get (ids.delay);
            detune[i] = get (ids.detune);
            shift[i] = get (ids.shift);
            manip[i] = get (ids.manip);
        }
    }

    std::atomic<float> *pitch, *formant, *drift, *rate, *doubles, *width, *eqOn, *eqTarget,
                       *lowFreq, *lowGain, *highFreq, *highGain, *out, *bypass;
    std::atomic<float> *on[dbx::kNumVoices], *level[dbx::kNumVoices], *pan[dbx::kNumVoices],
                       *delay[dbx::kNumVoices], *detune[dbx::kNumVoices], *shift[dbx::kNumVoices], *manip[dbx::kNumVoices];
    std::atomic<float> *harmonics, *fm, *alternator, *smear, *snap, *ratio, *octave, *glide, *stereo, *spread, *wet;
};

//==============================================================================
DoubleBoxProcessor::DoubleBoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DoubleBox", createLayout()),
      raw (std::make_unique<Raw> (apvts))
{
    bypassParameter = apvts.getParameter ("bypass");

    for (auto& level : voiceLevels)
        level.store (0.0f);
}

DoubleBoxProcessor::~DoubleBoxProcessor() = default;

bool DoubleBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;

    // A voice recorded on one channel can be spread over two; two channels
    // cannot be folded into one.
    return in == out || (in == AudioChannelSet::mono() && out == AudioChannelSet::stereo());
}

dbx::Settings DoubleBoxProcessor::readSettings() const noexcept
{
    dbx::Settings s;
    const auto& r = *raw;

    for (int i = 0; i < dbx::kNumVoices; ++i)
    {
        auto& voice = s.voices[i];

        voice.on = r.on[i]->load() > 0.5f;
        voice.levelDb = r.level[i]->load();
        voice.pan = r.pan[i]->load() * 0.01f;
        voice.delayMs = r.delay[i]->load();
        voice.detuneCents = r.detune[i]->load();
        voice.shift = r.shift[i]->load() > 0.5f;
        voice.manipulate = r.manip[i]->load() > 0.5f;
    }

    s.pitchSemitones = r.pitch->load();
    s.formantSemitones = r.formant->load();
    s.harmonics = r.harmonics->load() * 0.01f;
    s.fm = r.fm->load() * 0.01f;
    s.alternator = r.alternator->load() * 0.01f;
    s.smear = r.smear->load() * 0.01f;
    s.snap = r.snap->load() > 0.5f;
    s.ratio = r.ratio->load() * 0.01f;
    s.octave = r.octave->load();
    s.glideMs = r.glide->load();
    s.stereo = r.stereo->load() * 0.01f;
    s.spreadCents = r.spread->load();
    s.wet = r.wet->load() * 0.01f;
    s.drift = r.drift->load() * 0.01f;
    s.driftHz = r.rate->load();
    s.doublesDb = r.doubles->load();
    s.width = r.width->load() * 0.01f;

    s.eqOn = r.eqOn->load() > 0.5f;
    s.eqDoublesOnly = r.eqTarget->load() > 0.5f;
    s.lowHz = r.lowFreq->load();
    s.lowDb = r.lowGain->load();
    s.highHz = r.highFreq->load();
    s.highDb = r.highGain->load();

    s.outDb = r.out->load();
    s.bypass = r.bypass->load() > 0.5f;

    return s;
}

void DoubleBoxProcessor::prepareToPlay (double sampleRate, int)
{
    // Many hosts call this just before a bounce: the licence is read again then.
    license.refresh();

    doubler.setSettings (readSettings());
    doubler.prepare (sampleRate, getMainBusNumInputChannels(), getMainBusNumOutputChannels());

    setLatencySamples (doubler.latency());
}

void DoubleBoxProcessor::reset()
{
    doubler.reset();
}

void DoubleBoxProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const auto numIn = jmin (getMainBusNumInputChannels(), (int) dbx::kMaxChannels);
    const auto numOut = jmin (getMainBusNumOutputChannels(), (int) dbx::kMaxChannels);
    const auto numSamples = buffer.getNumSamples();

    if (numIn < 1 || numOut < 1 || buffer.getNumChannels() < jmax (numIn, numOut))
        return;

    // The one channel of a mono voice is also the first of the two it comes
    // out on: the doubler reads each sample before it writes it.
    const float* in[dbx::kMaxChannels] = {};
    float* out[dbx::kMaxChannels] = {};

    for (int c = 0; c < numIn; ++c)
        in[c] = buffer.getReadPointer (c);

    for (int c = 0; c < numOut; ++c)
        out[c] = buffer.getWritePointer (c);

    doubler.setSettings (readSettings());
    doubler.process (in, out, numSamples);

    for (int c = numOut; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    for (int i = 0; i < dbx::kNumVoices; ++i)
        voiceLevels[i].store (doubler.voiceLevel (i), std::memory_order_relaxed);

    // Without a licence the output is replaced by noise: see License/DemoNoise.h.
    license.process (buffer, getSampleRate(), isNonRealtime());
}

AudioProcessorEditor* DoubleBoxProcessor::createEditor()
{
    return new DoubleBoxEditor (*this);
}

//==============================================================================
ValueTree DoubleBoxProcessor::capture() const
{
    ValueTree snapshot (kSnapshot);

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<RangedAudioParameter*> (parameter))
            if (isStored (ranged->paramID))
                snapshot.setProperty (ranged->paramID, ranged->convertFrom0to1 (ranged->getValue()), nullptr);

    return snapshot;
}

void DoubleBoxProcessor::apply (const ValueTree& snapshot)
{
    for (auto* parameter : getParameters())
    {
        auto* ranged = dynamic_cast<RangedAudioParameter*> (parameter);

        if (ranged == nullptr || ! isStored (ranged->paramID))
            continue;

        // What a snapshot does not mention goes back to its default, so that
        // an old preset still means the same thing after a parameter is added.
        const auto wanted = snapshot.hasProperty (ranged->paramID)
                                ? ranged->convertTo0to1 ((float) snapshot[ranged->paramID])
                                : ranged->getDefaultValue();

        if (std::abs (wanted - ranged->getValue()) > 1.0e-6f)
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (wanted);
            ranged->endChangeGesture();
        }
    }
}

void DoubleBoxProcessor::switchToSlot (int slot)
{
    slot = jlimit (0, 1, slot);

    if (slot == activeSlot)
        return;

    ValueTree wanted;

    {
        const ScopedLock lock (slotLock);
        slots[activeSlot] = capture();
        wanted = slots[slot];
        activeSlot = slot;
    }

    // A slot never visited starts out as a copy of the one just left.
    if (wanted.isValid())
        apply (wanted);
}

void DoubleBoxProcessor::copyToOtherSlot()
{
    const ScopedLock lock (slotLock);
    slots[1 - activeSlot] = capture();
}

//==============================================================================
File DoubleBoxProcessor::getUserPresetFolder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
               .getChildFile ("Onirique").getChildFile ("DoubleBox").getChildFile ("Presets");
}

StringArray DoubleBoxProcessor::getStockPresets() const
{
    StringArray names;

    for (const auto& preset : stockPresets())
        names.add (preset.name);

    return names;
}

StringArray DoubleBoxProcessor::getUserPresets() const
{
    StringArray names;

    for (const auto& file : getUserPresetFolder().findChildFiles (File::findFiles, false,
                                                                  String ("*") + kPresetExtension))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

String DoubleBoxProcessor::getPresetName() const
{
    return apvts.state.getProperty (kPresetProperty, "Default").toString();
}

void DoubleBoxProcessor::setPresetName (const String& name)
{
    apvts.state.setProperty (kPresetProperty, name, nullptr);
}

bool DoubleBoxProcessor::isUserPreset (const String& name) const
{
    return findStock (name) == nullptr && presetFile (name).existsAsFile();
}

void DoubleBoxProcessor::loadPreset (const String& name)
{
    if (const auto* stock = findStock (name))
    {
        ValueTree snapshot (kSnapshot);

        for (const auto& [id, value] : stock->values)
            snapshot.setProperty (id, value, nullptr);

        apply (snapshot);
        setPresetName (stock->name);
        return;
    }

    if (const auto xml = parseXML (presetFile (name)))
    {
        const auto snapshot = ValueTree::fromXml (*xml);

        if (snapshot.hasType (kSnapshot))
        {
            apply (snapshot);
            setPresetName (name);
        }
    }
}

String DoubleBoxProcessor::saveUserPreset (const String& requested)
{
    auto name = File::createLegalFileName (requested.trim());

    if (name.isEmpty())
        return {};

    // The stock presets are not the user's to overwrite.
    if (findStock (name) != nullptr)
        name << " (user)";

    const auto file = presetFile (name);

    if (! file.getParentDirectory().createDirectory().wasOk())
        return {};

    if (const auto xml = capture().createXml())
    {
        if (xml->writeTo (file))
        {
            setPresetName (name);
            return name;
        }
    }

    return {};
}

void DoubleBoxProcessor::deleteUserPreset (const String& name)
{
    if (! isUserPreset (name))
        return;

    presetFile (name).deleteFile();

    // The settings stay as they are; they are just no longer anyone's.
    if (getPresetName() == name)
        setPresetName ("Default");
}

//==============================================================================
StringArray DoubleBoxProcessor::getPresetTypeNames()
{
    return { "Doubler", "Pitch", "Formant", "Manipulate", "Robot" };
}

int DoubleBoxProcessor::typesOf (const ValueTree& snapshot) const
{
    // What a snapshot does not mention is at its default, as when it is loaded.
    auto value = [&] (const String& id)
    {
        if (snapshot.hasProperty (id))
            return (float) snapshot[id];

        auto* parameter = apvts.getParameter (id);
        return parameter != nullptr ? parameter->convertFrom0to1 (parameter->getDefaultValue()) : 0.0f;
    };

    // The pitch and the manipulations only count on a voice that is heard
    // and that they are given to.
    bool shifted = false, handled = false;

    for (int i = 0; i < dbx::kNumVoices; ++i)
    {
        const auto ids = voiceIds (i);
        const auto on = value (ids.on) > 0.5f;

        shifted = shifted || (on && value (ids.shift) > 0.5f);
        handled = handled || (on && value (ids.manip) > 0.5f);
    }

    int types = 0;

    if ((shifted && value ("pitch") != 0.0f) || (handled && value ("octave") != 0.0f))
        types |= typePitch;

    if (shifted && value ("formant") != 0.0f)
        types |= typeFormant;

    if (handled)
    {
        // A voice that hardly follows the melody any more is a robot.
        if (std::abs (value ("ratio")) <= 40.0f)
            types |= typeRobot;

        for (const auto* id : { "harm", "fm", "alt", "smear", "glide", "stereo", "spread" })
            if (value (id) != 0.0f)
                types |= typeManipulate;

        if (value ("ratio") != 100.0f)
            types |= typeManipulate;
    }

    return types != 0 ? types : (int) typeDoubler;
}

int DoubleBoxProcessor::getPresetTypes (const String& name) const
{
    if (const auto* stock = findStock (name))
    {
        ValueTree snapshot (kSnapshot);

        for (const auto& [id, v] : stock->values)
            snapshot.setProperty (id, v, nullptr);

        return typesOf (snapshot);
    }

    if (const auto xml = parseXML (presetFile (name)))
    {
        const auto snapshot = ValueTree::fromXml (*xml);

        if (snapshot.hasType (kSnapshot))
            return typesOf (snapshot);
    }

    return 0;
}

StringArray DoubleBoxProcessor::getFilteredPresets() const
{
    StringArray names;

    auto add = [&] (const StringArray& from)
    {
        for (const auto& name : from)
            if (typeFilter == 0 || (getPresetTypes (name) & typeFilter) != 0)
                names.add (name);
    };

    if (sourceFilter != userOnly)
        add (getStockPresets());

    if (sourceFilter != stockOnly)
        add (getUserPresets());

    return names;
}

void DoubleBoxProcessor::restoreSettings (const ValueTree& settings, const String& presetName)
{
    apply (settings);
    setPresetName (presetName);
}

//==============================================================================
void DoubleBoxProcessor::getStateInformation (MemoryBlock& destination)
{
    auto state = apvts.copyState();

    {
        const ScopedLock lock (slotLock);

        state.setProperty (kSlotProperty, activeSlot, nullptr);
        state.removeChild (state.getChildWithName (kOtherSlot), nullptr);

        if (slots[1 - activeSlot].isValid())
        {
            ValueTree other (kOtherSlot);
            other.copyPropertiesFrom (slots[1 - activeSlot], nullptr);
            state.appendChild (other, nullptr);
        }
    }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void DoubleBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = ValueTree::fromXml (*xml);
    const auto other = state.getChildWithName (kOtherSlot);

    {
        const ScopedLock lock (slotLock);

        activeSlot = jlimit (0, 1, (int) state.getProperty (kSlotProperty, 0));
        slots[activeSlot] = {};
        slots[1 - activeSlot] = {};

        if (other.isValid())
        {
            ValueTree snapshot (kSnapshot);
            snapshot.copyPropertiesFrom (other, nullptr);
            slots[1 - activeSlot] = snapshot;
        }
    }

    state.removeChild (other, nullptr);
    apvts.replaceState (state);
}

} // namespace onirique

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new onirique::DoubleBoxProcessor();
}
