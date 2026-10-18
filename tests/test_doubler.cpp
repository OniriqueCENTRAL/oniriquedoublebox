// The whole audio path, offline: what the plugin does to a signal is decided
// in Doubler, and this is where it is held to it.
#include "dsp/Doubler.h"
#include "TestSignal.h"

#include <chrono>
#include <cstdio>

namespace
{
constexpr double kSr = 48000.0;
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);

    if (! ok)
        ++failures;
}

struct Stereo { std::vector<float> left, right; };

/** Runs a signal through: one channel in, or two when a second is given. */
Stereo run (const dbx::Settings& settings, const std::vector<float>& left, int block = 512,
            const std::vector<float>* right = nullptr)
{
    dbx::Doubler doubler;
    doubler.setSettings (settings);
    doubler.prepare (kSr, right != nullptr ? 2 : 1, 2);

    Stereo out { left, right != nullptr ? *right : left };

    for (size_t at = 0; at < left.size(); at += (size_t) block)
    {
        const auto count = (int) std::min ((size_t) block, left.size() - at);
        float* io[2] = { out.left.data() + at, out.right.data() + at };

        doubler.setSettings (settings);
        doubler.process (io, io, count);
    }

    return out;
}

const int kLatency = dbx::Doubler::latencyFor (kSr);

/** The largest difference between the output and the input delayed, in dB. */
double worstErrorDb (const std::vector<float>& in, const std::vector<float>& out, int delay)
{
    double worst = 0.0;

    for (size_t i = (size_t) delay; i < out.size(); ++i)
        worst = std::max (worst, (double) std::abs (out[i] - in[i - (size_t) delay]));

    return 20.0 * std::log10 (worst + 1.0e-12);
}

double worstDifferenceDb (const std::vector<float>& a, const std::vector<float>& b)
{
    double worst = 0.0;

    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, (double) std::abs (a[i] - b[i]));

    return 20.0 * std::log10 (worst + 1.0e-12);
}

/** Every voice off but one, which is put in the middle at full level with
    nothing done to it. */
dbx::Settings only (int voice)
{
    dbx::Settings s;

    for (int i = 0; i < dbx::kNumVoices; ++i)
        s.voices[i].on = i == voice;

    auto& v = s.voices[voice];
    v.levelDb = 0.0f;
    v.pan = 0.0f;
    v.delayMs = 0.0f;
    v.detuneCents = 0.0f;
    v.shift = false;

    return s;
}

// What is measured is the last second and a bit, once everything has settled.
constexpr size_t kSpan = 32768;

const float* tail (const std::vector<float>& x) { return x.data() + x.size() - kSpan; }

double pitchOf (const std::vector<float>& x) { return dbxtest::pitchOf (tail (x), kSpan / 2, kSr); }
double levelOf (const std::vector<float>& x) { return dbxtest::rmsDb (tail (x), kSpan); }

/** How far one cycle of the tail is from the next, against the signal, in
    dB: the cycle before read at its exact place, between samples, at the
    length of cycle that fits best. */
double cycleErrorDb (const std::vector<float>& x, double lag)
{
    auto at = [&x] (double position)
    {
        const auto whole = (size_t) std::floor (position);
        const auto f = position - (double) whole;
        const double y0 = x[whole - 1], y1 = x[whole], y2 = x[whole + 1], y3 = x[whole + 2];
        return y1 + 0.5 * f * (y2 - y0 + f * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + f * (3.0 * (y1 - y2) + y3 - y0)));
    };

    auto errorFor = [&] (double length)
    {
        double error = 0.0, power = 1.0e-20;

        for (size_t i = x.size() - kSpan; i < x.size() - 4; ++i)
        {
            const auto d = (double) x[i] - at ((double) i - length);
            error += d * d;
            power += (double) x[i] * x[i];
        }

        return error / power;
    };

    auto best = errorFor (lag);

    for (double length = lag - 1.0; length <= lag + 1.0; length += 0.02)
        best = std::min (best, errorFor (length));

    return 10.0 * std::log10 (best + 1.0e-20);
}

double f0Of (const std::vector<float>& x) { return dbxtest::pitchOf (x.data() + x.size() - kSpan, kSpan / 2, kSr); }

/** The breath of a held note: the power half way between its harmonics,
    against the power on them, from 1 to 5 kHz, in dB. A double should keep
    as much of it as the voice had; one that turns it into more harmonics
    sounds buzzy, one that loses it sounds hard. */
double breathDb (const std::vector<float>& x)
{
    const auto f0 = f0Of (x);
    double on = 1.0e-20, between = 1.0e-20;

    for (int h = (int) (1000.0 / f0); (h + 1) * f0 < 5000.0; ++h)
    {
        on += std::pow (dbxtest::toneLevel (tail (x), kSpan, h * f0, kSr), 2.0);
        between += std::pow (dbxtest::toneLevel (tail (x), kSpan, (h + 0.5) * f0, kSr), 2.0);
    }

    return 10.0 * std::log10 (between / on);
}

double centroidOf (const std::vector<float>& x, double f0)
{
    return dbxtest::harmonicCentroid (tail (x), kSpan, f0, kSr);
}
}

int main()
{
    const auto voice = dbxtest::makeVoice (kSr, 3.0, 220.0);
    const auto voiceRight = dbxtest::makeVoice (kSr, 3.0, 165.0);
    const auto f0 = pitchOf (voice);
    const auto centroid = centroidOf (voice, f0);

    std::printf ("latency %d samples (%.1f ms); the test voice is at %.2f Hz, its weight at %.0f Hz\n\n",
                 kLatency, 1000.0 * kLatency / kSr, f0, centroid);

    std::printf ("the voice itself\n");
    {
        const auto s = only (dbx::kCentreVoice);

        const auto mono = run (s, voice);
        check (worstErrorDb (voice, mono.left, kLatency) < -120.0 && worstErrorDb (voice, mono.right, kLatency) < -120.0,
               "alone and untouched, the centre voice is the input, late by the latency (one channel in)");

        const auto stereo = run (s, voice, 512, &voiceRight);
        check (worstErrorDb (voice, stereo.left, kLatency) < -120.0 && worstErrorDb (voiceRight, stereo.right, kLatency) < -120.0,
               "and keeps its two channels apart when there are two");

        auto bypassed = dbx::Settings();
        bypassed.bypass = true;
        bypassed.pitchSemitones = 5.0f;
        const auto through = run (bypassed, voice, 512, &voiceRight);
        check (worstErrorDb (voice, through.left, kLatency) < -120.0 && worstErrorDb (voiceRight, through.right, kLatency) < -120.0,
               "bypassed, the plugin gives the input back, as late as when it is not");

        auto silent = dbx::Settings();

        for (auto& v : silent.voices)
            v.on = false;

        const auto nothing = run (silent, voice);
        check (levelOf (nothing.left) < -150.0 && levelOf (nothing.right) < -150.0, "with every voice off there is nothing");
    }

    std::printf ("\nplace and lateness\n");
    {
        auto s = only (1);
        s.voices[1].delayMs = 20.0f;
        s.voices[1].pan = -1.0f;

        const auto out = run (s, voice);
        check (worstErrorDb (voice, out.left, kLatency + 960) < -120.0, "a voice 20 ms late is 20 ms late, to the sample");
        check (levelOf (out.right) < -150.0, "and one put hard left is not on the right");

        s.voices[1].pan = 1.0f;
        s.width = 0.0f;
        const auto narrow = run (s, voice);
        check (worstDifferenceDb (narrow.left, narrow.right) < -120.0, "with no width, a voice put on one side is in the middle");

        s = only (1);
        s.voices[1].levelDb = -6.0f;
        const auto quiet = run (s, voice);
        s.doublesDb = -6.0f;
        const auto quieter = run (s, voice);
        std::printf ("    level %.2f dB, then %.2f dB with the doubles turned down\n",
                     levelOf (quiet.left) - levelOf (voice), levelOf (quieter.left) - levelOf (voice));
        check (std::abs (levelOf (quiet.left) - levelOf (voice) + 6.0) < 0.05
                   && std::abs (levelOf (quieter.left) - levelOf (voice) + 12.0) < 0.05,
               "a double's level is its own and that of the doubles together");
    }

    std::printf ("\ndetune\n");
    {
        for (const auto cents : { 50.0f, 12.0f, -7.0f, -50.0f })
        {
            auto s = only (3);
            s.voices[3].detuneCents = cents;

            const auto out = run (s, voice);
            const auto wanted = f0 * std::pow (2.0, cents / 1200.0);
            const auto found = pitchOf (out.left);
            const auto errorCents = 1200.0 * std::log2 (found / wanted);

            std::printf ("    %+.0f cents: %.2f Hz wanted, %.2f Hz found (%+.1f cents off), level %+.2f dB\n",
                         cents, wanted, found, errorCents, levelOf (out.left) - levelOf (voice));
            check (std::abs (errorCents) < 2.0, "the voice is out of tune by what was asked");
            check (std::abs (levelOf (out.left) - levelOf (voice)) < 1.0, "and as loud as it was");
        }
    }

    std::printf ("\nhow clean a double is\n");
    {
        // A double is the same voice a few cents away. Held, it should repeat
        // itself from one cycle to the next as exactly as the voice does, and
        // the breath in it should stay breath, as much of it as there was.
        const auto held = dbxtest::makeVoice (kSr, 3.0, 196.0);
        const auto breath = dbxtest::makeNoise (3.0, kSr, 1.0f, 99);
        const auto scale = std::sqrt (std::pow (10.0, levelOf (held) / 10.0));

        auto breathy = held;

        for (size_t i = 0; i < breathy.size(); ++i)
            breathy[i] += 0.05f * (float) scale * breath[i];

        const auto voiceCycle = cycleErrorDb (held, kSr / f0Of (held));
        const auto voiceBreath = breathDb (breathy);

        for (const auto cents : { 7.0f, 25.0f, -40.0f })
        {
            auto s = only (3);
            s.voices[3].detuneCents = cents;

            const auto steady = run (s, held);
            const auto airy = run (s, breathy);
            const auto cycle = cycleErrorDb (steady.left, kSr / f0Of (steady.left));

            // The same detune through the grains, for comparison: a formant
            // moved by nothing sends a voice there.
            auto g = s;
            g.voices[3].shift = true;
            g.formantSemitones = 0.001f;
            const auto grains = run (g, held);
            const auto grainsAiry = run (g, breathy);

            std::printf ("    %+.0f cents: cycle to cycle %.1f dB (the voice %.1f, through grains %.1f); "
                         "breath %.1f dB (the voice %.1f, through grains %.1f)\n",
                         cents, cycle, voiceCycle, cycleErrorDb (grains.left, kSr / f0Of (grains.left)),
                         breathDb (airy.left), voiceBreath, breathDb (grainsAiry.left));
            check (cycle < voiceCycle + 10.0, "a held double repeats itself as exactly as the voice, splices included");
            check (std::abs (breathDb (airy.left) - voiceBreath) < 0.5, "and keeps the breath that was in it");
        }

        // A splice on a breath keeps its level: two pieces of noise with
        // little in common do not add up to as much as one.
        const auto noise = dbxtest::makeNoise (6.0, kSr, 0.2f);
        auto s = only (3);
        s.voices[3].detuneCents = 50.0f;
        const auto out = run (s, noise);
        const size_t frame = (size_t) (0.005 * kSr);
        auto lowest = 0.0, highest = -200.0;

        for (size_t at = (size_t) kSr; at + frame < out.left.size(); at += frame)
        {
            const auto level = dbxtest::rmsDb (out.left.data() + at, frame) - dbxtest::rmsDb (noise.data() + at, frame);
            lowest = std::min (lowest, level);
            highest = std::max (highest, level);
        }

        std::printf ("    noise 50 cents out, through its splices: level from %+.1f to %+.1f dB in 5 ms stretches\n", lowest, highest);
        check (lowest > -4.0, "a splice on a breath leaves no hole in it");
    }

    std::printf ("\npitch and formant\n");
    {
        for (const auto semitones : { 7.0f, 12.0f, -5.0f, -12.0f })
        {
            auto s = only (3);
            s.voices[3].shift = true;
            s.pitchSemitones = semitones;

            const auto out = run (s, voice);
            const auto wanted = f0 * std::pow (2.0, semitones / 12.0);
            const auto found = pitchOf (out.left);
            const auto weight = centroidOf (out.left, found);

            std::printf ("    pitch %+.0f st: %.1f Hz wanted, %.1f Hz found; weight at %.0f Hz (was %.0f); level %+.2f dB\n",
                         semitones, wanted, found, weight, centroid, levelOf (out.left) - levelOf (voice));
            check (std::abs (1200.0 * std::log2 (found / wanted)) < 5.0, "the pitch moves by what was asked");
            // Not to the hertz: the harmonics are elsewhere, and so read the
            // same vowel at other places. An octave up, every other one of
            // them is gone, the one nearest the first formant included.
            check (std::abs (weight / centroid - 1.0) < 0.2, "and the formants stay where they were");
            check (std::abs (levelOf (out.left) - levelOf (voice)) < 3.0, "at about the same level");
        }

        for (const auto semitones : { 4.0f, -4.0f })
        {
            auto s = only (3);
            s.voices[3].shift = true;
            s.formantSemitones = semitones;

            const auto out = run (s, voice);
            const auto found = pitchOf (out.left);
            const auto moved = centroidOf (out.left, found) / centroid;
            const auto wanted = std::pow (2.0, semitones / 12.0);

            std::printf ("    formant %+.0f st: pitch %.2f Hz; weight moved by %.3f (%.3f wanted); level %+.2f dB\n",
                         semitones, found, moved, wanted, levelOf (out.left) - levelOf (voice));
            check (std::abs (1200.0 * std::log2 (found / f0)) < 3.0, "the formant moves and the pitch does not");
            check (std::abs (moved / wanted - 1.0) < 0.12, "by about what was asked");
        }

        // The same pitch for the voices that ask for it, and only for them.
        auto s = dbx::Settings();

        for (int i = 0; i < dbx::kNumVoices; ++i)
        {
            s.voices[i].on = i == 0 || i == 4;
            s.voices[i].detuneCents = 0.0f;
            s.voices[i].levelDb = 0.0f;
        }

        s.voices[0].shift = true;
        s.voices[4].shift = false;
        s.pitchSemitones = 12.0f;

        const auto out = run (s, voice);
        std::printf ("    pitch +12 given to the far left only: %.1f Hz on the left, %.1f Hz on the right\n",
                     pitchOf (out.left), pitchOf (out.right));
        check (std::abs (pitchOf (out.left) / f0 - 2.0) < 0.01 && std::abs (pitchOf (out.right) / f0 - 1.0) < 0.002,
               "the pitch goes to the voices it is given to, and to no other");
    }

    std::printf ("\nmanipulations\n");
    {
        /** One double alone, with the manipulations given to it. */
        auto manipulated = [] (float harmonics, float fm, float alternator, float smear)
        {
            auto s = only (3);
            s.voices[3].manipulate = true;
            s.harmonics = harmonics;
            s.fm = fm;
            s.alternator = alternator;
            s.smear = smear;
            return s;
        };

        for (const auto amount : { 0.5f, -0.5f })
        {
            const auto out = run (manipulated (amount, 0.0f, 0.0f, 0.0f), voice);
            const auto found = pitchOf (out.left);
            const auto moved = centroidOf (out.left, found) - centroid;

            std::printf ("    harmonics %+.1f: pitch %.2f Hz; weight moved by %+.0f Hz (one harmonic is %.0f Hz); level %+.2f dB\n",
                         amount, found, moved, f0, levelOf (out.left) - levelOf (voice));
            check (std::abs (1200.0 * std::log2 (found / f0)) < 3.0, "harmonics leave the pitch where it was");
            // Up, by the harmonic asked for. Down, by less: the lowest ones have
            // nowhere to go, and fold back.
            check (moved * amount > 0.0 && std::abs (moved) > 0.15 * f0 && std::abs (moved) < 1.6 * f0,
                   "and slide the weight of the voice the way that was asked, by a harmonic at most");
        }

        {
            const auto out = run (manipulated (0.0f, 0.0f, 1.0f, 0.0f), voice);
            const auto found = pitchOf (out.left);

            std::printf ("    alternator at full: %.2f Hz (the octave below is %.2f Hz); level %+.2f dB\n",
                         found, 0.5 * f0, levelOf (out.left) - levelOf (voice));
            check (std::abs (1200.0 * std::log2 (found / (0.5 * f0))) < 5.0, "the alternator at full is the octave below");

            const auto half = run (manipulated (0.0f, 0.0f, 0.5f, 0.0f), voice);
            check (levelOf (half.left) < levelOf (voice) - 2.0, "half way, every other cycle is gone");
        }

        {
            const auto out = run (manipulated (0.0f, 1.0f, 0.0f, 0.0f), voice);
            const auto plain = run (only (3), voice);
            const auto found = pitchOf (out.left);

            const auto apart = worstDifferenceDb (out.left, plain.left) - worstDifferenceDb (plain.left, std::vector<float> (plain.left.size(), 0.0f));

            std::printf ("    FM at full: pitch %.2f Hz; level %+.2f dB; %.1f dB from the voice untouched, against its peak\n",
                         found, levelOf (out.left) - levelOf (voice), apart);
            check (std::abs (1200.0 * std::log2 (found / f0)) < 3.0, "FM leaves the pitch where it was");
            check (apart > -20.0 && std::abs (levelOf (out.left) - levelOf (voice)) < 3.0,
                   "and changes the sound, at about the same level");
        }

        {
            const auto out = run (manipulated (0.0f, 0.0f, 0.0f, 1.0f), voice);
            const auto found = pitchOf (out.left);

            std::printf ("    smear at full: pitch %.2f Hz; level %+.2f dB\n", found, levelOf (out.left) - levelOf (voice));
            check (std::abs (1200.0 * std::log2 (found / f0)) < 5.0 && std::abs (levelOf (out.left) - levelOf (voice)) < 3.0,
                   "smear keeps a held note where it was, and as loud");

            const auto noise = dbxtest::makeNoise (3.0, kSr, 0.2f);
            const auto breath = run (manipulated (0.0f, 0.0f, 1.0f, 1.0f), noise);
            // A little is lost: grains taken from here and there no longer add up
            // to the breath they were cut from, only to one like it.
            check (std::abs (levelOf (breath.left) - levelOf (noise)) < 3.0, "and neither it nor the alternator takes a breath away");
        }

        // The rest of what the module does: how the voice follows the one
        // it copies, and where it is put.
        {
            auto s = manipulated (0.0f, 0.0f, 0.0f, 0.0f);
            s.octave = -1.0f;
            const auto below = run (s, voice);
            s.octave = 1.0f;
            const auto above = run (s, voice);

            std::printf ("    octave -1: %.2f Hz; octave +1: %.2f Hz\n", pitchOf (below.left), pitchOf (above.left));
            check (std::abs (pitchOf (below.left) / f0 - 0.5) < 0.005 && std::abs (pitchOf (above.left) / f0 - 2.0) < 0.02,
                   "octave moves the voice by whole octaves");
        }

        {
            // A voice that slides up a fourth over the take: how far apart
            // its start and its end come out.
            std::vector<float> sliding;
            {
                const auto low = dbxtest::makeVoice (kSr, 3.0, 180.0, 300.0, 0.25);
                sliding = low;
            }

            auto early = [] (const std::vector<float>& x) { return dbxtest::pitchOf (x.data() + 30000, kSpan / 2, kSr); };
            auto travel = [&] (const std::vector<float>& x) { return 1200.0 * std::log2 (pitchOf (x) / early (x)); };

            const auto sung = travel (sliding);

            auto s = manipulated (0.0f, 0.0f, 0.0f, 0.0f);
            s.ratio = 0.0f;
            const auto held = travel (run (s, sliding).left);
            s.ratio = 2.0f;
            const auto wider = travel (run (s, sliding).left);
            s.ratio = 1.0f;
            s.glideMs = 400.0f;
            const auto gliding = run (s, sliding);

            std::printf ("    a voice that moves by %+.0f cents: %+.0f with ratio 0, %+.0f with ratio 2\n", sung, held, wider);
            check (std::abs (sung) > 100.0 && std::abs (held) < 0.15 * std::abs (sung), "ratio at nothing holds one note");
            check (std::abs (wider / sung - 2.0) < 0.5, "ratio at two sings the melody twice as wide");
            check (std::abs (levelOf (gliding.left) - levelOf (sliding)) < 3.0, "glide follows, at about the same level");
        }

        {
            auto s = dbx::Settings();

            for (int i = 0; i < dbx::kNumVoices; ++i)
            {
                s.voices[i].on = i == 0 || i == 4;
                s.voices[i].detuneCents = 0.0f;
                s.voices[i].levelDb = 0.0f;
                s.voices[i].shift = false;
            }

            s.spreadCents = 30.0f;
            const auto out = run (s, voice);
            const auto left = 1200.0 * std::log2 (pitchOf (out.left) / f0), right = 1200.0 * std::log2 (pitchOf (out.right) / f0);

            std::printf ("    detune 30: the far left is %+.1f cents off, the far right %+.1f\n", left, right);
            check (std::abs (left + 30.0) < 2.0 && std::abs (right - 30.0) < 2.0, "detune pulls the left voices flat and the right ones sharp");
        }

        {
            // Both through the grains: a detune alone would go to the tape.
            auto s = manipulated (0.0f, 0.0f, 0.0f, 0.0f);
            s.voices[3].detuneCents = 9.0f;
            s.glideMs = 1.0f;
            const auto narrow = run (s, voice);
            s.stereo = 1.0f;
            const auto wide = run (s, voice);

            auto middle = wide.left;

            for (size_t i = 0; i < middle.size(); ++i)
                middle[i] = 0.5f * (wide.left[i] + wide.right[i]);

            const auto scale = worstDifferenceDb (narrow.left, std::vector<float> (narrow.left.size(), 0.0f));

            std::printf ("    stereo at full: the two sides are %.1f dB apart, their middle %.1f dB from the voice without it (against its peak)\n",
                         worstDifferenceDb (wide.left, wide.right) - scale, worstDifferenceDb (middle, narrow.left) - scale);
            check (worstDifferenceDb (wide.left, wide.right) - scale > -20.0, "stereo makes the two sides differ");
            check (worstDifferenceDb (middle, narrow.left) - scale < -100.0, "and their middle is the voice as it was");
        }

        {
            auto s = manipulated (0.7f, 0.5f, 1.0f, 0.5f);
            s.octave = -1.0f;
            s.ratio = 0.5f;
            s.wet = 0.0f;
            const auto dry = run (s, voice);
            check (worstErrorDb (voice, dry.left, kLatency) < -120.0, "with dry / wet at dry, a manipulated voice is the input again");

            auto snapped = manipulated (0.4f, 0.0f, 0.0f, 0.0f);
            snapped.snap = true;
            const auto a = run (snapped, voice);
            const auto b = run (manipulated (0.5f, 0.0f, 0.0f, 0.0f), voice);
            check (worstDifferenceDb (a.left, b.left) < -120.0, "with snap, the harmonics slide by whole harmonics");
        }

        // The manipulations are for the voices they are given to.
        auto s = manipulated (0.7f, 0.5f, 1.0f, 0.5f);
        s.voices[3].manipulate = false;
        const auto spared = run (s, voice);
        check (worstErrorDb (voice, spared.left, kLatency) < -120.0, "a voice they are not given to is left alone, to the sample");
    }

    std::printf ("\nwhat has no pitch, and what moves\n");
    {
        const auto noise = dbxtest::makeNoise (3.0, kSr, 0.2f);

        auto s = only (3);
        s.voices[3].detuneCents = 10.0f;

        const auto out = run (s, noise);
        const auto error = worstErrorDb (noise, out.left, kLatency);

        std::printf ("    noise through a detuned voice: %.1f dB from the input at worst, level %+.2f dB\n",
                     error, levelOf (out.left) - levelOf (noise));
        check (std::abs (levelOf (out.left) - levelOf (noise)) < 1.0, "a sound with no pitch keeps its level");

        const auto vibrato = dbxtest::makeVoice (kSr, 3.0, 220.0, 60.0, 5.5);
        s.voices[3].detuneCents = 0.0f;
        s.voices[3].shift = true;
        s.pitchSemitones = 3.0f;

        const auto sung = run (s, vibrato);
        std::printf ("    a voice with vibrato, up three semitones: level %+.2f dB\n", levelOf (sung.left) - levelOf (vibrato));
        check (std::abs (levelOf (sung.left) - levelOf (vibrato)) < 2.0, "a voice that moves is followed");

        auto drifting = dbx::Settings();
        drifting.drift = 1.0f;
        const auto wandering = run (drifting, voice);
        const auto still = run (dbx::Settings(), voice);
        check (worstDifferenceDb (wandering.left, still.left) > -40.0 && std::abs (levelOf (wandering.left) - levelOf (still.left)) < 3.0,
               "drift makes the doubles wander, and no louder");
    }

    std::printf ("\nblocks\n");
    {
        auto s = dbx::Settings();
        s.pitchSemitones = 3.0f;
        s.formantSemitones = -2.0f;
        s.drift = 0.5f;
        s.lowDb = 3.0f;
        s.harmonics = 0.3f;
        s.fm = 0.4f;
        s.alternator = 0.2f;
        s.smear = 0.5f;
        s.ratio = 0.6f;
        s.glideMs = 80.0f;
        s.stereo = 0.5f;
        s.spreadCents = 10.0f;

        const auto reference = run (s, voice, 512, &voiceRight);
        bool same = true;

        for (const auto block : { 64, 333, 4096 })
        {
            const auto out = run (s, voice, block, &voiceRight);
            same = same && worstDifferenceDb (out.left, reference.left) < -120.0
                        && worstDifferenceDb (out.right, reference.right) < -120.0;
        }

        check (same, "the same samples come out however the host cuts its blocks (64, 333, 512, 4096)");
    }

    std::printf ("\nEQ\n");
    {
        auto s = only (dbx::kCentreVoice);
        s.lowDb = 6.0f;
        s.highDb = 6.0f;

        auto gain = [&] (double hz)
        {
            const auto tone = dbxtest::makeTone (2.0, kSr, hz, 0.25f);
            return levelOf (run (s, tone).left) - levelOf (tone);
        };

        const auto low = gain (40.0), mid = gain (1000.0), high = gain (16000.0);
        std::printf ("    +6 dB on both shelves: %+.2f dB at 40 Hz, %+.2f dB at 1 kHz, %+.2f dB at 16 kHz\n", low, mid, high);
        check (std::abs (low - 6.0) < 0.7 && std::abs (high - 6.0) < 0.7, "each shelf lifts its end by what was asked");
        check (std::abs (mid) < 0.5, "and leaves the middle alone");
        check (std::abs (dbx::eqDb (s, 40.0f, kSr) - low) < 0.1 && std::abs (dbx::eqDb (s, 16000.0f, kSr) - high) < 0.1,
               "the curve the editor draws is the one that is heard");

        s.eqDoublesOnly = true;
        const auto spared = run (s, voice);
        check (worstErrorDb (voice, spared.left, kLatency) < -120.0, "kept to the doubles, the EQ does not touch the centre voice");

        s.eqDoublesOnly = false;
        s.eqOn = false;
        const auto off = run (s, voice);
        check (worstErrorDb (voice, off.left, kLatency) < -120.0, "switched off, it touches nothing");
    }

    std::printf ("\nspeed\n");
    {
        const auto longVoice = dbxtest::makeVoice (kSr, 10.0, 180.0, 40.0, 5.0);
        auto s = dbx::Settings();
        s.pitchSemitones = -4.0f;
        s.formantSemitones = 2.0f;
        s.drift = 0.5f;

        for (auto& v : s.voices)
            v.shift = true;

        for (const auto block : { 64, 512 })
        {
            const auto start = std::chrono::steady_clock::now();
            const auto out = run (s, longVoice, block, &longVoice);
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

            std::printf ("    block %4d: %.0fx real time, every voice shifted, two channels in\n", block, 10.0 / seconds);
            check (10.0 / seconds > 10.0, "fast enough");
        }
    }

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures;
}
