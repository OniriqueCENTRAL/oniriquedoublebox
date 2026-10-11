// Onirique DoubleBox — plugin entry point
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Doubler.h"

namespace onirique
{

using namespace juce;

/** Parameter identifiers for one of the five voices. */
struct VoiceIds { String on, level, pan, delay, detune, shift; };
VoiceIds voiceIds (int voice);

/** What a voice is called: on its point, and in full. */
String voiceTag (int voice);
String voiceName (int voice);

/** The plugin instance: parameters in, a block through the doubler, and the
    level of each voice for the editor to draw. It also keeps what is not a
    parameter: the two settings being compared, and the presets. */
class DoubleBoxProcessor final : public AudioProcessor
{
public:
    DoubleBoxProcessor();
    ~DoubleBoxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override     { return false; }
    bool producesMidi() const override    { return false; }
    bool isMidiEffect() const override    { return false; }

    // The latest a voice can be, on top of the latency.
    double getTailLengthSeconds() const override { return 0.15; }

    // The plugin's own bypass keeps the latency, so switching it in a host
    // does not shift the track.
    AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    int getNumPrograms() override            { return 1; }
    int getCurrentProgram() override         { return 0; }
    void setCurrentProgram (int) override    {}
    const String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** The settings as the parameters stand now. */
    dbx::Settings readSettings() const noexcept;

    /** How loud a voice has just been, as an amplitude. */
    float getVoiceLevel (int voice) const noexcept { return voiceLevels[voice].load (std::memory_order_relaxed); }

    /** The rate the EQ's curve is to be drawn for. */
    double getEqSampleRate() const noexcept { return getSampleRate() > 0.0 ? getSampleRate() : 48000.0; }

    //==============================================================================
    // A and B: two settings held side by side. Everything below is for the
    // message thread.
    int getActiveSlot() const noexcept { return activeSlot; }

    /** Puts the current settings away and brings the other ones back. */
    void switchToSlot (int slot);

    /** Makes the other slot the same as the one being heard. */
    void copyToOtherSlot();

    //==============================================================================
    // Presets. The stock ones are built in; the user's are files, one each.
    StringArray getStockPresets() const;
    StringArray getUserPresets() const;
    String getPresetName() const;
    bool isUserPreset (const String& name) const;

    void loadPreset (const String& name);

    /** Writes the current settings under a name, and returns the name it was
        given: a stock preset's name cannot be taken. */
    String saveUserPreset (const String& name);
    void deleteUserPreset (const String& name);

    static File getUserPresetFolder();

    AudioProcessorValueTreeState apvts;

private:
    /** The values of everything a preset or a slot holds, by parameter. */
    ValueTree capture() const;
    void apply (const ValueTree&);
    void setPresetName (const String&);

    dbx::Doubler doubler;

    struct Raw;
    std::unique_ptr<Raw> raw;
    AudioProcessorParameter* bypassParameter = nullptr;

    std::atomic<float> voiceLevels[dbx::kNumVoices];

    CriticalSection slotLock;
    ValueTree slots[2];
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DoubleBoxProcessor)
};

} // namespace onirique
