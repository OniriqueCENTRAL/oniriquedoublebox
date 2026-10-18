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
    bool manipulate = false;    // whether the four manipulations below are
};

struct Settings
{
    Settings() noexcept;

    Voice voices[kNumVoices];

    // One pitch and one formant, for whichever voices ask for them.
    float pitchSemitones = 0.0f;
    float formantSemitones = 0.0f;

    // What can be done to a voice beyond moving it, for whichever voices ask
    // for it. All four are done to the grains the voice is rebuilt from.
    float harmonics = 0.0f;     // -1..1: slides what is inside each cycle down or up, by two harmonics at the ends
    float fm = 0.0f;            // 0..1: each cycle read with a wobble at the voice's own pitch
    float alternator = 0.0f;    // 0..1: every other cycle turned down, then upside down: the octave below
    float smear = 0.0f;         // 0..1: cycles taken from further and further back, at random
    bool snap = false;          // the harmonics slide by whole harmonics only

    // And what is done to the way such a voice follows the one it copies.
    float ratio = 1.0f;         // how much of the melody is kept: 1 all of it, 0 one held note, beyond 1 more than was sung
    float octave = 0.0f;        // whole octaves up or down, on top of the pitch
    float glideMs = 0.0f;       // how long the voice takes to reach a new note
    float stereo = 0.0f;        // 0..1: one cycle to the left, the next to the right
    float spreadCents = 0.0f;   // the left voices flat and the right ones sharp, the outer pair by this much
    float wet = 1.0f;           // 0 = these voices untouched, 1 = everything above

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
        std::vector<float> accRight; // the same for the right, when a voice is spread over the two
        double pitch = 0.0;         // where the glide has got to, in octaves
        bool gliding = false;
        bool odd = false;           // every other grain, for the alternator and the stereo
        uint32_t chance = 1;        // where the smear's dice are

        // The tape: how much later than its place the voice is being read,
        // and, while it is spliced, where the other reading still is.
        double slip = 0.0, slipBefore = 0.0;
        int fadeLeft = 0;
        float alike = 1.0f;         // how much the two readings of a splice have in common
    };

    /** A signal and the same signal a quarter of a cycle behind at every
        frequency: what it takes to slide a spectrum along instead of
        stretching it. Two chains of all-pass filters, whose phases stay
        ninety degrees apart over all but the very bottom of the band. */
    struct Quadrature
    {
        void reset() noexcept;
        void process (float in, float& ahead, float& behind) noexcept;

        double x[2][4][2] = {}, y[2][4][2] = {};
        double held = 0.0;
    };

    struct VoiceState
    {
        Stream streams[kMaxChannels];

        float gainL = 0.0f, gainR = 0.0f, targetL = 0.0f, targetR = 0.0f;
        double delay = 0.0, delayTarget = 0.0;          // samples
        float cents = 0.0f, centsTarget = 0.0f;
        float tapeCents = 0.0f, tapeCentsTarget = 0.0f; // what the tape is out of tune by
        float formant = 0.0f, formantTarget = 0.0f;     // semitones
        float harmonics = 0.0f, harmonicsTarget = 0.0f;
        float fm = 0.0f, fmTarget = 0.0f;
        float alternator = 0.0f, alternatorTarget = 0.0f;
        float smear = 0.0f, smearTarget = 0.0f;
        float ratio = 1.0f, ratioTarget = 1.0f;
        float stereo = 0.0f, stereoTarget = 0.0f;
        float glideMs = 0.0f;
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
    void render (VoiceState&, Stream&, int source, float& left, float& right) noexcept;
    float tape (VoiceState&, Stream&, int source) noexcept;
    double spliceJump (double position, bool ahead, float& alike) const noexcept;
    void fire (VoiceState&, Stream&, int source) noexcept;
    double advance (const float* source, double from, double period, int reach) const noexcept;
    double pulseBefore (const float* source, double target, double period, int reach) const noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    int numIn = 2, numOut = 2;
    int base = 1344;                // the latency
    int search = 38;                // how far either side of a cycle its start is looked for
    double budget = 1303.0;         // what a grain may reach across, both ways together
    double minPeriod = 48.0, maxPeriod = 640.0, unvoicedPeriod = 240.0;
    double tapeSpan = 1152.0, tapeMiddle = 576.0;   // how far the tape may slip, and where it starts
    int spliceLength = 960, matchLength = 384;
    int64_t now = 0;

    // The note the voice turns about, in octaves: a slow average of what it
    // sings. With the ratio at nothing, this is the one note that is left.
    double home = 0.0;
    bool hasReference = false;
    bool dirty = true, snap = true;

    PeriodTracker tracker;

    // What came in: the two channels as one, then each of them.
    std::vector<float> history[1 + kMaxChannels];
    int historyMask = 0, accMask = 0;

    // The same three again, as the pair the harmonics are slid with.
    Quadrature quadrature[1 + kMaxChannels];
    std::vector<float> leading[1 + kMaxChannels], lagging[1 + kMaxChannels];

    VoiceState voices[kNumVoices];

    Shelf shelves[2];
    double eqState[2][kMaxChannels][2] = {};

    float glide = 0.001f, delayGlide = 0.0007f, blendStep = 0.001f, meterFall = 0.9995f;
    float outNow = 1.0f, bypassNow = 0.0f;
};

} // namespace dbx
