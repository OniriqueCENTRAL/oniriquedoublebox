// Onirique DoubleBox — the length of the voice's cycle, as it is sung (no dependency)
#pragma once

#include <cstdint>
#include <vector>

namespace dbx
{

/** Follows the period of a voice a few hundred times a second.

    The grains a voice is rebuilt from are one cycle apart, so the doubler has
    to know how long a cycle is at every moment. This is YIN on a copy of the
    signal brought down to about 12 kHz: the fundamental of a voice is far
    below that, and the search costs a sixteenth of what it would at the full
    rate. What comes out is good to a sample or so; the doubler then finds the
    exact place itself, by comparing one cycle with the next.

    Each answer describes the middle of the stretch it was read from, which is
    in the past. They are kept, so that the answer for a given sample can be
    asked for later, when a grain is taken from there.
*/
class PeriodTracker
{
public:
    static constexpr double kMinHz = 75.0;
    static constexpr double kMaxHz = 1000.0;

    struct Estimate
    {
        float period = 0.0f;    // in samples of the signal as it came in
        bool voiced = false;
    };

    /** Allocates. Everything after this is free of allocation. */
    void prepare (double sampleRate);
    void reset() noexcept;

    /** One sample in; index is its place in the stream. */
    void push (float sample, int64_t index) noexcept;

    /** What was found nearest to a sample. Asked about one the tracker has not
        reached yet, it gives the last thing it knows. */
    Estimate at (int64_t sample) const noexcept;

    /** How far behind the stream the newest answer is. */
    int delay() const noexcept { return (frame / 2) * factor + factor; }

private:
    void analyse (int64_t index) noexcept;

    static constexpr int kHistory = 128;

    int factor = 4;
    int tauMin = 12, tauMax = 162, window = 162, frame = 325, hop = 32;

    // Low-pass ahead of the decimation.
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
    double sum = 0.0;
    int phase = 0;

    std::vector<float> ring, scratch, difference;
    int writePos = 0, mask = 0, filled = 0, sinceHop = 0;

    Estimate entries[kHistory];
    int head = 0, count = 0;
    int64_t latestCentre = 0;
    int spacing = 128;

    // The last two raw periods, for the median, and whether the last frame
    // had a voice in it.
    float recent[2] = { 0.0f, 0.0f };
    bool wasVoiced = false;
};

} // namespace dbx
