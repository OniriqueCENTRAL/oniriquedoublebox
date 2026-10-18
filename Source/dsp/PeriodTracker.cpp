#include "PeriodTracker.h"

#include <algorithm>
#include <cmath>

namespace dbx
{

namespace
{
    constexpr float kDip = 0.15f;       // the first dip under this is the period
    constexpr float kVoiced = 0.35f;    // above this, there is no period to speak of
    constexpr float kStillVoiced = 0.5f; // ... unless the stretch before had one: a voice does not stop for one frame
    constexpr double kSilence = 1.0e-9; // mean power under which nothing is looked for
}

void PeriodTracker::prepare (double sampleRate)
{
    factor = std::max (1, (int) std::lround (sampleRate / 12000.0));

    const auto rate = sampleRate / (double) factor;

    tauMin = std::max (2, (int) std::floor (rate / kMaxHz));
    tauMax = (int) std::ceil (rate / kMinHz) + 1;
    window = tauMax;
    frame = window + tauMax + 1;
    hop = std::max (1, (int) std::lround (rate * 0.0027));
    spacing = hop * factor;

    int size = 1;

    while (size < frame + hop)
        size <<= 1;

    ring.assign ((size_t) size, 0.0f);
    mask = size - 1;
    scratch.assign ((size_t) frame, 0.0f);
    difference.assign ((size_t) tauMax + 2, 0.0f);

    // Second-order low-pass, well under the half of the rate it leaves at.
    const auto w = 2.0 * M_PI * std::min (3000.0, 0.4 * rate) / sampleRate;
    const auto alpha = std::sin (w) / (2.0 * 0.7071);
    const auto a0 = 1.0 + alpha;

    b0 = (1.0 - std::cos (w)) * 0.5 / a0;
    b1 = (1.0 - std::cos (w)) / a0;
    b2 = b0;
    a1 = -2.0 * std::cos (w) / a0;
    a2 = (1.0 - alpha) / a0;

    reset();
}

void PeriodTracker::reset() noexcept
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    z1 = z2 = sum = 0.0;
    phase = writePos = filled = sinceHop = 0;
    head = count = 0;
    latestCentre = 0;
    recent[0] = recent[1] = 0.0f;
    wasVoiced = false;

    for (auto& e : entries)
        e = {};
}

void PeriodTracker::push (float sample, int64_t index) noexcept
{
    const auto y = b0 * (double) sample + z1;
    z1 = b1 * (double) sample - a1 * y + z2;
    z2 = b2 * (double) sample - a2 * y;

    sum += y;

    if (++phase < factor)
        return;

    ring[(size_t) (writePos & mask)] = (float) (sum / (double) factor);
    ++writePos;
    sum = 0.0;
    phase = 0;

    filled = std::min (filled + 1, frame);

    if (++sinceHop >= hop && filled >= frame)
    {
        sinceHop = 0;
        analyse (index);
    }
}

void PeriodTracker::analyse (int64_t index) noexcept
{
    auto* x = scratch.data();
    double power = 0.0;

    for (int i = 0; i < frame; ++i)
    {
        x[i] = ring[(size_t) ((writePos - frame + i) & mask)];
        power += (double) x[i] * x[i];
    }

    Estimate found;

    // Whatever the last period was, it is kept: an unvoiced stretch says
    // nothing about it, and whoever asks may want something to go on.
    if (count > 0)
        found.period = entries[(head - 1) & (kHistory - 1)].period;

    if (power / (double) frame > kSilence)
    {
        // The difference between the signal and itself moved along by tau,
        // then each value against the mean of those before it.
        auto* d = difference.data();
        double running = 0.0;

        d[0] = 1.0f;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float total = 0.0f;

            for (int j = 0; j < window; ++j)
            {
                const auto delta = x[j] - x[j + tau];
                total += delta * delta;
            }

            running += (double) total;
            d[tau] = running > 0.0 ? (float) ((double) total * tau / running) : 1.0f;
        }

        int best = -1;

        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (d[tau] < kDip)
            {
                while (tau + 1 < tauMax && d[tau + 1] < d[tau])
                    ++tau;

                best = tau;
                break;
            }
        }

        if (best < 0)
        {
            best = tauMin;

            for (int tau = tauMin + 1; tau < tauMax; ++tau)
                if (d[tau] < d[best])
                    best = tau;
        }

        // A voice that was there a moment ago is given the benefit of the
        // doubt: one rough frame in the middle of a note does not make it a
        // breath, and the grains do not lose their footing over it.
        if (d[best] < (wasVoiced ? kStillVoiced : kVoiced))
        {
            // The dip lies between the values that were computed.
            auto exact = (float) best;

            if (best > 1 && best < tauMax)
            {
                const auto curve = d[best - 1] - 2.0f * d[best] + d[best + 1];

                if (curve > 1.0e-9f)
                    exact += std::max (-0.5f, std::min (0.5f, 0.5f * (d[best - 1] - d[best + 1]) / curve));
            }

            found.period = exact * (float) factor;
            found.voiced = true;
        }
    }

    // One frame alone an octave or a fifth away from the two around it is
    // a mistake, not a leap: the middle of the last three is kept.
    if (found.voiced && recent[0] > 0.0f && recent[1] > 0.0f)
    {
        const auto raw = found.period;
        found.period = std::max (std::min (raw, recent[0]), std::min (std::max (raw, recent[0]), recent[1]));
        recent[1] = recent[0];
        recent[0] = raw;
    }
    else
    {
        recent[1] = found.voiced ? recent[0] : 0.0f;
        recent[0] = found.voiced ? found.period : 0.0f;
    }

    wasVoiced = found.voiced;
    entries[head] = found;
    head = (head + 1) & (kHistory - 1);
    count = std::min (count + 1, kHistory);
    latestCentre = index - (int64_t) ((frame / 2) * factor) - factor / 2;
}

PeriodTracker::Estimate PeriodTracker::at (int64_t sample) const noexcept
{
    if (count == 0)
        return {};

    const auto behind = latestCentre - sample + spacing / 2;
    const auto back = (int) std::max<int64_t> (0, std::min<int64_t> (count - 1, behind > 0 ? behind / spacing : 0));

    return entries[(head - 1 - back) & (kHistory - 1)];
}

} // namespace dbx
