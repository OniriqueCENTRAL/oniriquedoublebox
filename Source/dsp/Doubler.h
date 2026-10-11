// Onirique DoubleBox — five-voice doubler with pitch and formant (no dependency)
#pragma once

#include <cstdint>
#include <vector>

#include "PeriodTracker.h"

namespace dbx
{

constexpr int kNumVoices = 5;       // far left, left, centre, right, far right
constexpr int kCentreVoice = 2;
constexpr int kMaxChannels = 2;
constexpr float kMaxDelayMs = 100.0f;

/** One of the five points. The centre one is the voice itself; the other four
    are its doubles. */
struct Voice
{
    bool on = true;
    float levelDb = 0.0f;
    float pan = 0.0f;           // -1 left .. +1 right
    float delayMs = 0.0f;       // how late it comes in
    float detuneCents = 0.0f;
    bool shift = false;         // whether the pitch and the formant below are for this voice
};

struct Settings
{
    Settings() noexcept;

    Voice voices[kNumVoices];

    // One pitch and one formant, for whichever voices ask for them.
    float pitchSemitones = 0.0f;
    float formantSemitones = 0.0f;

    // The doubles wander, as a second take would: 0 none, 1 a lot.
    float drift = 0.0f;
    float driftHz = 0.4f;

    float doublesDb = 0.0f;     // the four doubles together
    float width = 1.0f;         // 0 = every voice in the middle, 1 = where each was put

    bool eqOn = true;
    bool eqDoublesOnly = false; // leave the centre voice out of the EQ
    float lowHz = 120.0f, lowDb = 0.0f;
    float highHz = 8000.0f, highDb = 0.0f;

    float outDb = 0.0f;
    bool bypass = false;
};

/** The EQ drawn in the editor: both shelves together, in dB, at one frequency. */
float eqDb (const Settings&, float hz, double sampleRate) noexcept;

/** Five copies of a voice, each with its own place, its own lateness and its
    own tuning.

    A copy that is neither detuned nor shifted is read straight from a delay
    line and is the input to the sample. One that is, is rebuilt from grains:
    two cycles of the voice under a window, laid down again one new cycle
    apart. Closer together the pitch goes up, further apart it goes down, and
    because each grain still holds one whole pulse of the voice, the vowel
    stays what it was. Reading the grain faster or slower moves the vowel
    instead, and leaves the pitch alone: that is the formant.

    Where there is no pitch to find, in a breath or a consonant, the grains
    are laid down as they were taken and the signal passes through.
*/
class Doubler
{
public:
    /** Allocates. Everything after this is free of allocation. */
    void prepare (double sampleRate, int numInputChannels, int numOutputChannels);
    void reset() noexcept;

    void setSettings (const Settings&) noexcept;

    /** The delay through process(): a voice with no delay of its own comes
        out this late, to the sample. */
    int latency() const noexcept { return base; }
    static int latencyFor (double sampleRate) noexcept;

    /** in and out may be the same buffers. */
    void process (const float* const* in, float* const* out, int numSamples) noexcept;

    /** How loud a voice has just been, for the editor: a peak that falls back. */
    float voiceLevel (int voice) const noexcept { return voices[voice].meter; }

private:
    /** One stream of grains: a voice, or one side of the centre voice when
        the input is stereo. */
    struct Stream
    {
        std::vector<float> acc;     // what the grains add up to, ahead of the output
        double nextCentre = 0.0;    // where the next grain goes, in output time
        double source = 0.0;        // where the last one was taken from
        int64_t fireAt = 0;
        bool running = false;
        bool synced = false;        // the last grain was one cycle of a voiced sound
    };

    struct VoiceState
    {
        Stream streams[kMaxChannels];

        float gainL = 0.0f, gainR = 0.0f, targetL = 0.0f, targetR = 0.0f;
        double delay = 0.0, delayTarget = 0.0;          // samples
        float cents = 0.0f, centsTarget = 0.0f;
        float formant = 0.0f, formantTarget = 0.0f;     // semitones
        float blend = 0.0f, blendTarget = 0.0f;         // 0 = the delay line, 1 = the grains
        float meter = 0.0f;
        bool idle = true;
    };

    struct Shelf
    {
        void set (bool high, double hz, double gainDb, double sampleRate) noexcept;
        double magnitudeDb (double hz, double sampleRate) const noexcept;

        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        bool flat = true;
    };

    friend float eqDb (const Settings&, float, double) noexcept;

    void updateTargets() noexcept;
    float render (VoiceState&, Stream&, const float* source) noexcept;
    void fire (VoiceState&, Stream&, const float* source) noexcept;
    double advance (const float* source, double from, double period, int reach) const noexcept;
    double pulseBefore (const float* source, double target, double period, int reach) const noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    int numIn = 2, numOut = 2;
    int base = 1344;                // the latency
    int search = 38;                // how far either side of a cycle its start is looked for
    double budget = 1303.0;         // what a grain may reach across, both ways together
    double minPeriod = 48.0, maxPeriod = 640.0, unvoicedPeriod = 240.0;
    int64_t now = 0;
    bool dirty = true, snap = true;

    PeriodTracker tracker;

    // What came in: the two channels as one, then each of them.
    std::vector<float> history[1 + kMaxChannels];
    int historyMask = 0, accMask = 0;

    VoiceState voices[kNumVoices];

    Shelf shelves[2];
    double eqState[2][kMaxChannels][2] = {};

    float glide = 0.001f, delayGlide = 0.0007f, blendStep = 0.001f, meterFall = 0.9995f;
    float outNow = 1.0f, bypassNow = 0.0f;
};

} // namespace dbx
