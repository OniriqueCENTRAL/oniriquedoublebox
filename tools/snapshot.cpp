// Renders the editor to a PNG without a host, with a voice running through
// it, so a change to the interface can be looked at straight from the build.
//
//   doublebox_snapshot out.png [width height]
//   doublebox_snapshot out.png --states    several voices selected, a preset being named
//   doublebox_snapshot --selftest      the slots, the presets and the saved state, without a window
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "TestSignal.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;

std::unique_ptr<onirique::DoubleBoxProcessor> makeProcessor()
{
    return std::unique_ptr<onirique::DoubleBoxProcessor> (static_cast<onirique::DoubleBoxProcessor*> (createPluginFilter()));
}

/** Sets the plugin up, plays into it on the message thread one timer tick at
    a time so the stage can follow, then takes the picture and ends the
    program. */
struct Director final : juce::Timer
{
    Director (juce::File destination, int width, int height, bool states = false)
        : file (std::move (destination)), showStates (states)
    {
        processor = makeProcessor();
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        processor->prepareToPlay (kSr, kBlock);

        set ("pitch", -12.0f);
        set ("formant", -2.0f);
        set ("v2_shift", 0.0f);
        set ("v4_shift", 0.0f);
        set ("drift", 35.0f);
        set ("harm", 35.0f);
        set ("smear", 40.0f);
        set ("ratio", 60.0f);
        set ("octave", -1.0f);
        set ("stereo", 50.0f);
        set ("v1_manip", 0.0f);
        set ("lo_gain", 3.0f);
        set ("hi_gain", 4.5f);

        signal = dbxtest::makeVoice (kSr, 4.0, 196.0, 40.0, 5.0);

        editor.reset (processor->createEditor());
        editor->setSize (width, height);

        if (auto* doubleBox = dynamic_cast<onirique::DoubleBoxEditor*> (editor.get()))
        {
            doubleBox->getStage().select (3);

            if (showStates)
            {
                processor->loadPreset ("Tight Double");
                doubleBox->getStage().select (1);
                doubleBox->getStage().setSelected (3, true);
                doubleBox->getStage().setSelected (4, true);
                doubleBox->beginSaving();
            }
        }

        startTimer (30);
    }

    void set (const juce::String& id, float value)
    {
        if (auto* p = processor->apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void timerCallback() override
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;

        // About a tick's worth of audio per tick.
        for (int i = 0; i < 3 && position + kBlock <= signal.size(); ++i)
        {
            buffer.copyFrom (0, 0, signal.data() + position, kBlock);
            buffer.copyFrom (1, 0, signal.data() + position, kBlock);
            processor->processBlock (buffer, midi);
            position += kBlock;
        }

        if (++ticks > 60)
            finish();
    }

    void finish()
    {
        stopTimer();

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

        file.deleteFile();

        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (image, *stream);

        juce::MessageManager::getInstance()->stopDispatchLoop();
    }

    juce::File file;
    bool showStates = false;
    std::unique_ptr<onirique::DoubleBoxProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::vector<float> signal;
    size_t position = 0;
    int ticks = 0;
};
}

/** What the processor keeps that is not audio: the two slots, the presets,
    and the state a host saves; and the plugin as a host drives it, on one
    channel and on two. Returns the number of things found wrong. */
int selfTest()
{
    int failures = 0;

    auto check = [&failures] (bool ok, const char* what)
    {
        std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        failures += ok ? 0 : 1;
    };

    auto processor = makeProcessor();

    auto get = [&] (const char* id) { return processor->apvts.getRawParameterValue (id)->load(); };
    auto set = [&] (const char* id, float value)
    {
        auto* p = processor->apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // A and B.
    set ("pitch", 3.0f);
    processor->switchToSlot (1);
    check (std::abs (get ("pitch") - 3.0f) < 0.01f, "B starts as a copy of A");
    set ("pitch", -7.0f);
    set ("v1_on", 0.0f);
    processor->switchToSlot (0);
    check (std::abs (get ("pitch") - 3.0f) < 0.01f && get ("v1_on") > 0.5f, "back on A, A is as it was left");
    processor->switchToSlot (1);
    check (std::abs (get ("pitch") + 7.0f) < 0.01f && get ("v1_on") < 0.5f, "and B as it was left, switched-off voice included");
    processor->copyToOtherSlot();
    processor->switchToSlot (0);
    check (std::abs (get ("pitch") + 7.0f) < 0.01f, "B > A makes A the same as B");

    set ("bypass", 1.0f);
    set ("pitch", 2.0f);
    processor->switchToSlot (1);
    check (get ("bypass") > 0.5f, "switching slots leaves bypass alone");
    set ("bypass", 0.0f);

    // The saved state carries both slots.
    juce::MemoryBlock saved;
    processor->getStateInformation (saved);

    auto second = makeProcessor();
    second->setStateInformation (saved.getData(), (int) saved.getSize());

    auto get2 = [&] (const char* id) { return second->apvts.getRawParameterValue (id)->load(); };
    check (second->getActiveSlot() == 1 && std::abs (get2 ("pitch") + 7.0f) < 0.01f, "a restored state is on the same slot, with its settings");
    second->switchToSlot (0);
    check (std::abs (get2 ("pitch") - 2.0f) < 0.01f, "and has the other slot too");

    // Through the plugin as a host drives it.
    {
        const auto voice = dbxtest::makeVoice (kSr, 2.0, 220.0);

        // With only the centre voice on, what comes out is what went in, as
        // late as the plugin says it is.
        auto worst = [&] (int channelsIn)
        {
            auto host = makeProcessor();

            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (channelsIn == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());

            if (! host->setBusesLayout (layout))
                return 0.0;

            for (const auto* id : { "v1_on", "v2_on", "v4_on", "v5_on" })
            {
                auto* p = host->apvts.getParameter (id);
                p->setValueNotifyingHost (0.0f);
            }

            host->setRateAndBufferSizeDetails (kSr, kBlock);
            host->prepareToPlay (kSr, kBlock);

            const auto latency = (size_t) host->getLatencySamples();
            juce::AudioBuffer<float> buffer (2, kBlock);
            juce::MidiBuffer midi;
            double error = 0.0;

            for (size_t at = 0; at + kBlock <= voice.size(); at += kBlock)
            {
                buffer.clear();

                for (int c = 0; c < channelsIn; ++c)
                    buffer.copyFrom (c, 0, voice.data() + at, kBlock);

                host->processBlock (buffer, midi);

                for (int i = 0; i < kBlock; ++i)
                {
                    const auto n = at + (size_t) i;

                    if (n < latency)
                        continue;

                    for (int c = 0; c < 2; ++c)
                        error = std::max (error, (double) std::abs (buffer.getSample (c, i) - voice[n - latency]));
                }
            }

            return 20.0 * std::log10 (error + 1.0e-12);
        };

        const auto mono = worst (1), stereo = worst (2);
        std::printf ("    the centre voice alone, against the input: %.1f dB from one channel, %.1f dB from two\n", mono, stereo);
        check (mono < -120.0, "a voice on one channel comes out on both, to the sample");
        check (stereo < -120.0, "and so does one on two");

        auto host = makeProcessor();
        juce::AudioProcessor::BusesLayout folded;
        folded.inputBuses.add (juce::AudioChannelSet::stereo());
        folded.outputBuses.add (juce::AudioChannelSet::mono());
        check (! host->checkBusesLayoutSupported (folded), "two channels are not folded into one");
    }

    // Presets.
    check (processor->getStockPresets().size() >= 5, "there are stock presets");
    processor->loadPreset ("Octave Below");
    check (processor->getPresetName() == "Octave Below" && std::abs (get ("pitch") + 12.0f) < 0.01f
               && get ("v1_shift") > 0.5f && get ("v2_shift") < 0.5f && get ("eq_target") > 0.5f,
           "a stock preset sets what it names");
    processor->loadPreset ("Default");
    check (std::abs (get ("pitch")) < 0.01f && get ("v2_shift") > 0.5f && get ("v1_on") > 0.5f && get ("eq_target") < 0.5f,
           "and Default puts everything back");

    const juce::String name ("zz selftest " + juce::String (juce::Time::currentTimeMillis()));
    set ("formant", 3.25f);
    set ("v5_delay", 61.0f);
    set ("v3_shift", 1.0f);
    check (processor->saveUserPreset (name) == name && processor->getUserPresets().contains (name), "a user preset is saved under its name");
    check (processor->saveUserPreset ("Default") == "Default (user)", "a stock preset's name cannot be taken");
    processor->deleteUserPreset ("Default (user)");

    processor->loadPreset ("Default");
    processor->loadPreset (name);
    check (std::abs (get ("formant") - 3.25f) < 0.01f && std::abs (get ("v5_delay") - 61.0f) < 0.05f && get ("v3_shift") > 0.5f,
           "and comes back as it was saved");

    processor->deleteUserPreset (name);
    check (! processor->getUserPresets().contains (name) && processor->getPresetName() == "Default", "a user preset can be deleted");
    processor->deleteUserPreset ("Wide Stack");
    check (processor->getStockPresets().contains ("Wide Stack"), "a stock preset cannot");

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures;
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;

    const juce::ScopedJuceInitialiser_GUI gui;

    if (juce::String (argv[1]) == "--selftest")
        return selfTest();

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    const auto states = argc > 2 && juce::String (argv[2]) == "--states";
    const auto width = argc > 3 ? juce::String (argv[2]).getIntValue() : 1490;
    const auto height = argc > 3 ? juce::String (argv[3]).getIntValue() : 720;

    {
        Director director (file, width, height, states);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }

    return file.existsAsFile() ? 0 : 1;
}
