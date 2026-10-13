#include "Doubler.h"

#include <algorithm>
#include <cmath>

namespace dbx
{

namespace
{
    constexpr double kLatencySeconds = 0.028;
    constexpr double kSearchSeconds = 0.0008;
    constexpr double kUnvoicedSeconds = 0.005;
    constexpr int kControl = 32;        // samples between two looks at the settings
    constexpr int kMaxSteps = 4;        // cycles a grain may jump ahead of the last one

    // The manipulations at full: how many harmonics the spectrum is slid by,
    // how far the reading of a cycle wobbles (in radians at the fundamental),
    // and how far back the smear reaches, in cycles and in seconds.
    constexpr double kHarmonicsSpan = 2.0;
    constexpr double kFmIndex = 0.6;
    constexpr double kSmearCycles = 10.0;
    constexpr double kSmearSeconds = 0.05;

    // How far the doubles wander with drift at full.
    constexpr float kDriftCents = 14.0f;
    constexpr float kDriftMs = 3.0f;

    // Each voice wanders at its own pace and from its own place, so that no
    // two of them ever move together.
    const float kWanderPace[kNumVoices] = { 0.83f, 1.19f, 1.0f, 0.93f, 1.31f };
    const float kWanderPhase[kNumVoices] = { 0.0f, 2.1f, 0.0f, 4.4f, 1.3f };

    float gainFor (float db) noexcept
    {
        return std::pow (10.0f, db / 20.0f);
    }

    int powerOfTwoAbove (int n) noexcept
    {
        int size = 1;

        while (size < n)
            size <<= 1;

        return size;
    }

    /** The signal between its samples: a cubic through the four around. On a
        sample, the sample itself. */
    inline float readAt (const float* ring, int mask, double position) noexcept
    {
        const auto whole = (int64_t) std::floor (position);
        const auto f = (float) (position - (double) whole);
        const auto y1 = ring[(size_t) (whole & mask)];

        if (f == 0.0f)
            return y1;

        const auto y0 = ring[(size_t) ((whole - 1) & mask)];
        const auto y2 = ring[(size_t) ((whole + 1) & mask)];
        const auto y3 = ring[(size_t) ((whole + 2) & mask)];

        const auto c1 = 0.5f * (y2 - y0);
        const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    /** One step of a value towards where it is wanted, arriving for good once
        it is close enough not to matter. */
    template <typename Value>
    inline void approach (Value& value, Value target, Value rate, Value close) noexcept
    {
        value += rate * (target - value);

        if (std::abs (target - value) < close)
            value = target;
    }
}

//==============================================================================
Settings::Settings() noexcept
{
    // A voice in the middle, and two takes of it on each side: the outer pair
    // later, lower in level and further out of tune than the inner one.
    const float pan[kNumVoices] = { -1.0f, -0.55f, 0.0f, 0.55f, 1.0f };
    const float delay[kNumVoices] = { 24.0f, 13.0f, 0.0f, 16.0f, 28.0f };
    const float detune[kNumVoices] = { -11.0f, -6.0f, 0.0f, 7.0f, 12.0f };
    const float level[kNumVoices] = { -8.0f, -6.0f, 0.0f, -6.0f, -8.0f };

    for (int i = 0; i < kNumVoices; ++i)
    {
        voices[i].pan = pan[i];
        voices[i].delayMs = delay[i];
        voices[i].detuneCents = detune[i];
        voices[i].levelDb = level[i];
        voices[i].shift = i != kCentreVoice;
        voices[i].manipulate = i != kCentreVoice;
    }
}

//==============================================================================
void Doubler::Shelf::set (bool high, double hz, double gainDb, double rate) noexcept
{
    flat = std::abs (gainDb) < 0.01;

    if (flat)
    {
        b0 = 1.0;
        b1 = b2 = a1 = a2 = 0.0;
        return;
    }

    const auto a = std::pow (10.0, gainDb / 40.0);
    const auto w = 2.0 * M_PI * std::min (hz, 0.45 * rate) / rate;
    const auto cosine = std::cos (w);
    const auto beta = 2.0 * std::sqrt (a) * std::sin (w) * 0.5 * std::sqrt (2.0);
    const auto sign = high ? -1.0 : 1.0;

    // The two shelves are the same filter seen from either end.
    const auto a0 = (a + 1.0) + sign * (a - 1.0) * cosine + beta;

    b0 = a * ((a + 1.0) - sign * (a - 1.0) * cosine + beta) / a0;
    b1 = sign * 2.0 * a * ((a - 1.0) - sign * (a + 1.0) * cosine) / a0;
    b2 = a * ((a + 1.0) - sign * (a - 1.0) * cosine - beta) / a0;
    a1 = -sign * 2.0 * ((a - 1.0) + sign * (a + 1.0) * cosine) / a0;
    a2 = ((a + 1.0) + sign * (a - 1.0) * cosine - beta) / a0;
}

double Doubler::Shelf::magnitudeDb (double hz, double rate) const noexcept
{
    if (flat)
        return 0.0;

    const auto w = 2.0 * M_PI * hz / rate;
    const auto c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);

    const auto topRe = b0 + b1 * c1 + b2 * c2, topIm = b1 * s1 + b2 * s2;
    const auto bottomRe = 1.0 + a1 * c1 + a2 * c2, bottomIm = a1 * s1 + a2 * s2;

    return 10.0 * std::log10 ((topRe * topRe + topIm * topIm) / (bottomRe * bottomRe + bottomIm * bottomIm) + 1.0e-20);
}

float eqDb (const Settings& s, float hz, double sampleRate) noexcept
{
    if (! s.eqOn)
        return 0.0f;

    Doubler::Shelf low, high;
    low.set (false, s.lowHz, s.lowDb, sampleRate);
    high.set (true, s.highHz, s.highDb, sampleRate);

    return (float) (low.magnitudeDb (hz, sampleRate) + high.magnitudeDb (hz, sampleRate));
}

//==============================================================================
void Doubler::Quadrature::reset() noexcept
{
    *this = {};
}

void Doubler::Quadrature::process (float in, float& aheadOut, float& behindOut) noexcept
{
    // Each section is an all-pass on every other sample; the two chains
    // differ only in where their sections turn.
    static const double turns[2][4] = { { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 },
                                        { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 } };

    double out[2];

    for (int chain = 0; chain < 2; ++chain)
    {
        double v = (double) in;

        for (int s = 0; s < 4; ++s)
        {
            const auto a = turns[chain][s] * turns[chain][s];
            const auto result = a * (v + y[chain][s][1]) - x[chain][s][1];

            x[chain][s][1] = x[chain][s][0];
            x[chain][s][0] = v;
            y[chain][s][1] = y[chain][s][0];
            y[chain][s][0] = result;
            v = result;
        }

        out[chain] = v;
    }

    // The first chain is the one that is a sample early.
    aheadOut = (float) held;
    held = out[0];
    behindOut = (float) out[1];
}

//==============================================================================
int Doubler::latencyFor (double sampleRate) noexcept
{
    return (int) std::lround (kLatencySeconds * sampleRate);
}

void Doubler::prepare (double rate, int numInputChannels, int numOutputChannels)
{
    sampleRate = rate;
    numIn = std::max (1, std::min (kMaxChannels, numInputChannels));
    numOut = std::max (1, std::min (kMaxChannels, numOutputChannels));

    base = latencyFor (rate);
    search = std::max (2, (int) std::lround (kSearchSeconds * rate));
    budget = (double) (base - search - 3);
    minPeriod = rate / PeriodTracker::kMaxHz;
    maxPeriod = rate / PeriodTracker::kMinHz;
    unvoicedPeriod = std::round (kUnvoicedSeconds * rate);

    tracker.prepare (rate);

    // Long enough for the latest voice, the wander on top of it, and the
    // cycles a grain reaches back across.
    const auto historySize = powerOfTwoAbove ((int) (rate * (0.2 + kMaxDelayMs * 0.001)) + 4096);

    for (auto& h : history)
        h.assign ((size_t) historySize, 0.0f);

    for (int i = 0; i <= kMaxChannels; ++i)
    {
        leading[i].assign ((size_t) historySize, 0.0f);
        lagging[i].assign ((size_t) historySize, 0.0f);
    }

    historyMask = historySize - 1;

    const auto accSize = powerOfTwoAbove (2 * base + 64);
    accMask = accSize - 1;

    for (auto& v : voices)
        for (auto& stream : v.streams)
            stream.acc.assign ((size_t) accSize, 0.0f);

    glide = 1.0f - std::exp (-1.0f / (0.02f * (float) rate));
    delayGlide = 1.0f - std::exp (-1.0f / (0.04f * (float) rate));
    blendStep = 1.0f / (0.02f * (float) rate);
    meterFall = std::exp (-1.0f / (0.25f * (float) rate));

    reset();
}

void Doubler::reset() noexcept
{
    for (auto& h : history)
        std::fill (h.begin(), h.end(), 0.0f);

    for (int i = 0; i <= kMaxChannels; ++i)
    {
        std::fill (leading[i].begin(), leading[i].end(), 0.0f);
        std::fill (lagging[i].begin(), lagging[i].end(), 0.0f);
        quadrature[i].reset();
    }

    uint32_t chance = 0x2545f491u;

    for (auto& v : voices)
    {
        for (auto& stream : v.streams)
        {
            std::fill (stream.acc.begin(), stream.acc.end(), 0.0f);
            stream.running = stream.synced = stream.odd = false;

            // The same dice every time, and different ones for each stream.
            stream.chance = chance;
            chance = chance * 1664525u + 1013904223u;
        }

        v.meter = 0.0f;
    }

    for (auto& band : eqState)
        for (auto& channel : band)
            channel[0] = channel[1] = 0.0;

    tracker.reset();
    now = 0;
    dirty = snap = true;
}

void Doubler::setSettings (const Settings& s) noexcept
{
    settings = s;
    dirty = true;
}

//==============================================================================
void Doubler::updateTargets() noexcept
{
    const auto& s = settings;
    // The wander is read on a grid of its own, so that it does not depend on
    // where the host happens to cut its blocks.
    const auto seconds = (double) (now & ~(int64_t) (kControl - 1)) / sampleRate;
    const auto doubles = gainFor (s.doublesDb);

    for (int i = 0; i < kNumVoices; ++i)
    {
        auto& v = voices[i];
        const auto& voice = s.voices[i];
        const auto centre = i == kCentreVoice;

        float wanderCents = 0.0f, wanderMs = 0.0f;

        if (! centre && s.drift > 0.0f)
        {
            const auto a = 2.0 * M_PI * (double) (s.driftHz * kWanderPace[i]) * seconds + (double) kWanderPhase[i];

            wanderCents = s.drift * kDriftCents * (float) (0.62 * std::sin (a) + 0.38 * std::sin (1.618 * a + 1.0));
            wanderMs = s.drift * kDriftMs * (float) (0.6 * std::sin (0.71 * a + 2.0) + 0.4 * std::sin (1.37 * a + 0.5));
        }

        v.centsTarget = voice.detuneCents + wanderCents + (voice.shift ? 100.0f * s.pitchSemitones : 0.0f);
        v.formantTarget = voice.shift ? s.formantSemitones : 0.0f;

        // A delay that is not moving is a whole number of samples, so that a
        // voice read from the delay line is read on the samples.
        const auto delay = (double) (voice.delayMs + wanderMs) * 0.001 * sampleRate;
        v.delayTarget = std::max (0.0, wanderMs != 0.0f ? delay : std::round (delay));

        v.harmonicsTarget = voice.manipulate ? s.harmonics : 0.0f;
        v.fmTarget = voice.manipulate ? s.fm : 0.0f;
        v.alternatorTarget = voice.manipulate ? s.alternator : 0.0f;
        v.smearTarget = voice.manipulate ? s.smear : 0.0f;

        // Anything done to a voice is done to its grains; with nothing to do,
        // it is read straight from the delay line.
        const auto untouched = v.centsTarget == 0.0f && v.formantTarget == 0.0f && v.harmonicsTarget == 0.0f
                               && v.fmTarget == 0.0f && v.alternatorTarget == 0.0f && v.smearTarget == 0.0f;

        v.blendTarget = untouched ? 0.0f : 1.0f;

        const auto level = voice.on ? gainFor (voice.levelDb) * (centre ? 1.0f : doubles) : 0.0f;
        const auto pan = std::max (-1.0f, std::min (1.0f, voice.pan * s.width));

        // Full on both sides in the middle, and one side fading as the voice
        // moves to the other.
        v.targetL = numOut > 1 ? level * std::min (1.0f, 1.0f - pan) : level;
        v.targetR = numOut > 1 ? level * std::min (1.0f, 1.0f + pan) : 0.0f;

        const auto silent = v.targetL == 0.0f && v.targetR == 0.0f;

        if (snap || (v.idle && ! silent))
        {
            // Coming in from nothing, a voice is already where it should be.
            v.delay = v.delayTarget;
            v.cents = v.centsTarget;
            v.formant = v.formantTarget;
            v.harmonics = v.harmonicsTarget;
            v.fm = v.fmTarget;
            v.alternator = v.alternatorTarget;
            v.smear = v.smearTarget;
            v.blend = v.blendTarget;

            for (auto& stream : v.streams)
                stream.running = false;
        }

        if (snap)
        {
            v.gainL = v.targetL;
            v.gainR = v.targetR;
        }

        v.idle = silent && v.gainL == 0.0f && v.gainR == 0.0f;
    }

    shelves[0].set (false, s.lowHz, s.eqOn ? s.lowDb : 0.0, sampleRate);
    shelves[1].set (true, s.highHz, s.eqOn ? s.highDb : 0.0, sampleRate);

    if (snap)
    {
        outNow = gainFor (s.outDb);
        bypassNow = s.bypass ? 1.0f : 0.0f;
    }

    dirty = snap = false;
}

/** The start of the cycle after the one at `from`: a period further on, give
    or take, wherever the signal looks most like it did at `from`. */
double Doubler::advance (const float* source, double from, double period, int reach) const noexcept
{
    const auto a = (int64_t) std::llround (from);
    const auto step = (int64_t) std::llround (period);
    const auto half = std::min ((int) (period * 0.5), reach);

    if (half < 8)
        return from + period;

    const auto range = std::max (2, std::min (search, (int) (period * 0.06)));
    const auto stride = std::max (1, half / 128);

    float scores[3] = { 0.0f, 0.0f, 0.0f };     // the best, and the one either side of it
    float best = 0.0f, previous = 0.0f;
    int bestOffset = 0;
    bool awaitingNext = false;

    for (int offset = -range; offset <= range; ++offset)
    {
        const auto b = a + step + offset;
        float match = 0.0f, energy = 1.0e-12f;

        for (int j = -half; j <= half; j += stride)
        {
            const auto reference = source[(size_t) ((a + j) & historyMask)];
            const auto candidate = source[(size_t) ((b + j) & historyMask)];

            match += reference * candidate;
            energy += candidate * candidate;
        }

        const auto score = match / std::sqrt (energy);

        if (awaitingNext)
        {
            scores[2] = score;
            awaitingNext = false;
        }

        if (score > best)
        {
            best = score;
            bestOffset = offset;
            scores[0] = previous;
            scores[1] = score;
            scores[2] = score;
            awaitingNext = offset < range;
        }

        previous = score;
    }

    // Nothing there resembles anything: trust the period.
    if (best <= 0.0f)
        return from + period;

    double fine = 0.0;

    if (bestOffset > -range && bestOffset < range)
    {
        const auto curve = scores[0] - 2.0f * scores[1] + scores[2];

        if (curve < -1.0e-12f)
            fine = std::max (-0.5, std::min (0.5, 0.5 * (double) (scores[0] - scores[2]) / (double) curve));
    }

    return from + (double) (step + bestOffset) + fine;
}

/** Where, in the cycle that ends at `target`, the voice puts its energy: the
    pulse the cycle starts with. A grain has to be centred there. Centred
    between two pulses it holds half of each, and laid down at another pitch
    the two halves no longer meet: the vowel changes colour and loses level. */
double Doubler::pulseBefore (const float* source, double target, double period, int reach) const noexcept
{
    const auto end = (int64_t) std::floor (target);
    const auto span = (int64_t) period;
    const auto half = (int64_t) std::max (2.0, period / 8.0);

    if (reach < half)
        return target;

    auto at = [&] (int64_t i)
    {
        const auto x = (double) source[(size_t) (i & historyMask)];
        return x * x;
    };

    double energy = 0.0;

    for (auto j = -half; j <= half; ++j)
        energy += at (end - span + j);

    auto best = energy;
    auto bestAt = end - span;

    for (auto c = end - span + 1; c <= end; ++c)
    {
        energy += at (c + half) - at (c - half - 1);

        if (energy > best)
        {
            best = energy;
            bestAt = c;
        }
    }

    return (double) bestAt;
}

/** Lays down one grain, and decides when the next is due. */
void Doubler::fire (VoiceState& v, Stream& stream, int sourceIndex) noexcept
{
    const auto* source = history[sourceIndex].data();
    const auto centre = stream.nextCentre;
    const auto ahead = std::max (0.0, centre - (double) now);
    const auto target = centre - (double) base - v.delay;

    const auto estimate = tracker.at ((int64_t) target);
    const auto voiced = estimate.voiced;
    const auto period = voiced ? std::max (minPeriod, std::min (maxPeriod, (double) estimate.period)) : unvoicedPeriod;

    const auto ratio = std::pow (2.0, (double) v.cents / 1200.0);
    const auto formant = std::pow (2.0, (double) v.formant / 12.0);

    // Half the length of the grain as it is laid down. One cycle either side
    // of its middle, as the voice had it; less if that would have it start
    // before now, or read ahead of what has come in.
    // With FM the reading of the grain swings either side of where it would
    // have been, and needs that much more of the signal ahead of it.
    const auto swing = (double) v.fm * kFmIndex * period / (2.0 * M_PI);
    const auto room = budget - swing - 1.0;

    auto half = voiced ? period / formant : period;
    half = std::max (2.0, std::min ({ half, std::max (ahead, 2.0), (room - ahead) / formant }));

    // How far past the target the signal has already come in.
    const auto reach = (int) ((double) base + v.delay - ahead) - search - 2;

    auto from = target;

    if (voiced)
    {
        auto lost = ! stream.synced;

        if (! lost)
        {
            // The grain comes from a whole number of cycles after the last
            // one, so that every grain holds the same part of the cycle: none
            // if the output is not there yet, two if it has gone past one.
            from = stream.source;

            for (int steps = 0; steps < kMaxSteps && from + period <= target; ++steps)
                from = advance (source, from, period, reach);

            // Left behind, or sent ahead by a delay that jumped.
            lost = from + 2.0 * period <= target || from > target + period;
        }

        if (lost)
            from = pulseBefore (source, target, period, reach);
    }

    from = std::min (from, (double) now - formant * half - swing - 3.0);

    stream.source = from;
    stream.synced = voiced;

    // Smear: the grain is taken from some way back instead, a whole number of
    // cycles so that it still lines up with its neighbours. Where the walk
    // has got to is not changed by it.
    auto taken = from;

    if (v.smear > 0.0f)
    {
        stream.chance = stream.chance * 1664525u + 1013904223u;
        const auto dice = (double) (stream.chance >> 8) / 16777216.0;

        taken -= voiced ? std::floor (dice * (double) v.smear * (kSmearCycles + 1.0)) * period
                        : dice * (double) v.smear * kSmearSeconds * sampleRate;
    }

    // A sound with no pitch is laid down as it was taken: the grains then
    // add up to the signal itself.
    const auto spacing = voiced ? period / ratio : period;

    // Grains closer together than they are long pile up; this keeps the
    // level where it was.
    auto gain = (float) std::min (2.0, std::sqrt (spacing / half));

    // Alternator: every other cycle fades, then comes back upside down. The
    // voice then only repeats every two cycles, which is the octave below.
    // Only where there are cycles: on a breath it would cancel the signal.
    if (voiced)
    {
        stream.odd = ! stream.odd;

        if (stream.odd)
            gain *= 1.0f - 2.0f * v.alternator;
    }

    const auto first = std::max (now, (int64_t) std::ceil (centre - half));
    const auto last = (int64_t) std::floor (centre + half);
    const auto scale = M_PI / half;
    auto* acc = stream.acc.data();

    if (v.harmonics == 0.0f && swing == 0.0)
    {
        for (auto i = first; i <= last; ++i)
        {
            const auto offset = (double) i - centre;
            const auto window = 0.5f + 0.5f * (float) std::cos (scale * offset);

            acc[(size_t) (i & accMask)] += gain * window * readAt (source, historyMask, taken + offset * formant);
        }
    }
    else
    {
        // Harmonics: what is in the grain is slid along the spectrum by so
        // many times the voice's own pitch, starting afresh at the middle of
        // every grain. The grains still come one cycle apart, so the pitch
        // stays, and it is the weight of the harmonics that moves.
        // FM: the grain is read with a wobble, once a cycle.
        const auto slide = 2.0 * M_PI * (double) v.harmonics * kHarmonicsSpan / period;
        const auto wobble = 2.0 * M_PI / period;
        const auto* early = leading[sourceIndex].data();
        const auto* late = lagging[sourceIndex].data();

        for (auto i = first; i <= last; ++i)
        {
            const auto offset = (double) i - centre;
            const auto window = 0.5f + 0.5f * (float) std::cos (scale * offset);
            const auto along = offset * formant;
            const auto position = taken + along + (swing != 0.0 ? swing * std::sin (wobble * along) : 0.0);

            float sample;

            if (v.harmonics != 0.0f)
            {
                const auto angle = slide * offset;

                sample = readAt (early, historyMask, position) * (float) std::cos (angle)
                         + readAt (late, historyMask, position) * (float) std::sin (angle);
            }
            else
            {
                sample = readAt (source, historyMask, position);
            }

            acc[(size_t) (i & accMask)] += gain * window * sample;
        }
    }

    stream.nextCentre = centre + spacing;

    const auto planned = std::min ({ voiced ? period / formant : period,
                                     room / (1.0 + formant),
                                     room - 0.5 * period });

    stream.fireAt = std::max (now + 1, (int64_t) std::floor (stream.nextCentre - planned));
}

float Doubler::render (VoiceState& v, Stream& stream, int source) noexcept
{
    float direct = 0.0f, grains = 0.0f;

    if (v.blend < 1.0f)
        direct = readAt (history[source].data(), historyMask, (double) (now - base) - v.delay);

    if (v.blend > 0.0f)
    {
        if (! stream.running)
        {
            std::fill (stream.acc.begin(), stream.acc.end(), 0.0f);
            stream.running = true;
            stream.synced = false;
            stream.nextCentre = (double) now + unvoicedPeriod;
            stream.fireAt = now;
        }

        while (now >= stream.fireAt)
            fire (v, stream, source);

        auto& slot = stream.acc[(size_t) (now & accMask)];
        grains = slot;
        slot = 0.0f;
    }
    else
    {
        stream.running = false;
    }

    return direct + v.blend * (grains - direct);
}

//==============================================================================
void Doubler::process (const float* const* in, float* const* out, int numSamples) noexcept
{
    const auto stereoIn = numIn > 1;
    const auto outTarget = gainFor (settings.outDb);
    const auto bypassTarget = settings.bypass ? 1.0f : 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto left = in[0][n];
        const auto right = stereoIn ? in[1][n] : left;
        const auto at = (size_t) (now & historyMask);

        history[0][at] = stereoIn ? 0.5f * (left + right) : left;

        if (stereoIn)
        {
            history[1][at] = left;
            history[2][at] = right;
        }

        for (int i = 0; i < (stereoIn ? 1 + kMaxChannels : 1); ++i)
            quadrature[i].process (history[i][at], leading[i][at], lagging[i][at]);

        tracker.push (history[0][at], now);

        if (dirty || (now & (kControl - 1)) == 0)
            updateTargets();

        float centre[2] = { 0.0f, 0.0f }, doubles[2] = { 0.0f, 0.0f };

        for (int i = 0; i < kNumVoices; ++i)
        {
            auto& v = voices[i];

            if (v.idle)
            {
                v.meter *= meterFall;
                continue;
            }

            approach (v.gainL, v.targetL, glide, 1.0e-6f);
            approach (v.gainR, v.targetR, glide, 1.0e-6f);
            approach (v.delay, v.delayTarget, (double) delayGlide, 1.0e-3);
            approach (v.cents, v.centsTarget, glide, 1.0e-3f);
            approach (v.formant, v.formantTarget, glide, 1.0e-4f);
            approach (v.harmonics, v.harmonicsTarget, glide, 1.0e-4f);
            approach (v.fm, v.fmTarget, glide, 1.0e-4f);
            approach (v.alternator, v.alternatorTarget, glide, 1.0e-4f);
            approach (v.smear, v.smearTarget, glide, 1.0e-4f);

            if (v.blend != v.blendTarget)
                v.blend = v.blend < v.blendTarget ? std::min (v.blendTarget, v.blend + blendStep)
                                                  : std::max (v.blendTarget, v.blend - blendStep);

            float l, r;

            if (i == kCentreVoice && stereoIn)
            {
                // The voice itself keeps its two channels.
                l = v.gainL * render (v, v.streams[0], 1);
                r = v.gainR * render (v, v.streams[1], 2);
            }
            else
            {
                const auto y = render (v, v.streams[0], 0);
                l = v.gainL * y;
                r = v.gainR * y;
            }

            v.meter = std::max ({ v.meter * meterFall, std::abs (l), std::abs (r) });

            auto* bus = i == kCentreVoice ? centre : doubles;
            bus[0] += l;
            bus[1] += r;

            if (v.targetL == 0.0f && v.targetR == 0.0f && v.gainL == 0.0f && v.gainR == 0.0f)
                v.idle = true;
        }

        approach (outNow, outTarget, glide, 1.0e-6f);
        approach (bypassNow, bypassTarget, glide, 1.0e-5f);

        for (int c = 0; c < numOut; ++c)
        {
            // The EQ takes the doubles alone, or everything.
            double wet = settings.eqDoublesOnly ? (double) doubles[c] : (double) (centre[c] + doubles[c]);

            for (int band = 0; band < 2; ++band)
            {
                const auto& shelf = shelves[band];
                auto* z = eqState[band][c];

                if (shelf.flat)
                {
                    z[0] = z[1] = 0.0;
                    continue;
                }

                const auto y = shelf.b0 * wet + z[0];
                z[0] = shelf.b1 * wet - shelf.a1 * y + z[1];
                z[1] = shelf.b2 * wet - shelf.a2 * y;
                wet = y;
            }

            auto result = outNow * ((float) wet + (settings.eqDoublesOnly ? centre[c] : 0.0f));

            // Bypassed, the input comes out as late as everything else does.
            if (bypassNow > 0.0f)
            {
                const auto dry = history[stereoIn ? 1 + c : 0][(size_t) ((now - base) & historyMask)];
                result = bypassNow < 1.0f ? result + bypassNow * (dry - result) : dry;
            }

            out[c][n] = result;
        }

        ++now;
    }
}

} // namespace dbx
