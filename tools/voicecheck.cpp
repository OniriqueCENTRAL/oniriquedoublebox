// The doubler run over a real recording, to be listened to: a few settings,
// each written out as a file, with what can be measured printed beside it.
//
//   doublebox_voicecheck voice.wav outputFolder
//
// The recording is read as it is (16, 24 or 32 bit PCM, or float), its first
// channel only.
#include "../Source/dsp/Doubler.h"
#include "TestSignal.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace
{
struct Recording
{
    std::vector<float> samples;
    double sampleRate = 0.0;
};

template <typename Value>
Value read (const std::vector<char>& data, size_t at)
{
    Value v {};
    std::memcpy (&v, data.data() + at, sizeof (Value));
    return v;
}

Recording readWav (const std::string& path)
{
    std::ifstream file (path, std::ios::binary);
    const std::vector<char> data ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char>());

    Recording out;

    if (data.size() < 44 || std::memcmp (data.data(), "RIFF", 4) != 0)
        return out;

    int format = 1, channels = 1, bits = 16;

    for (size_t at = 12; at + 8 <= data.size();)
    {
        const auto size = (size_t) read<uint32_t> (data, at + 4);
        const auto body = at + 8;

        if (std::memcmp (data.data() + at, "fmt ", 4) == 0)
        {
            format = read<uint16_t> (data, body);
            channels = read<uint16_t> (data, body + 2);
            out.sampleRate = (double) read<uint32_t> (data, body + 4);
            bits = read<uint16_t> (data, body + 14);

            // The extensible form says what it really is further on.
            if (format == 0xfffe && size >= 26)
                format = read<uint16_t> (data, body + 24);
        }
        else if (std::memcmp (data.data() + at, "data", 4) == 0)
        {
            const auto frame = (size_t) (channels * bits / 8);
            const auto count = std::min (size, data.size() - body) / frame;

            out.samples.resize (count);

            for (size_t i = 0; i < count; ++i)
            {
                const auto p = body + i * frame;

                if (format == 3)
                    out.samples[i] = read<float> (data, p);
                else if (bits == 16)
                    out.samples[i] = (float) read<int16_t> (data, p) / 32768.0f;
                else if (bits == 24)
                    out.samples[i] = (float) ((read<int32_t> (data, p - 1) >> 8)) / 8388608.0f;
                else
                    out.samples[i] = (float) read<int32_t> (data, p) / 2147483648.0f;
            }
        }

        at = body + size + (size & 1);
    }

    return out;
}

void writeWav (const std::string& path, const std::vector<float>& left, const std::vector<float>& right, double sampleRate)
{
    std::ofstream file (path, std::ios::binary);

    const auto frames = (uint32_t) left.size();
    const uint32_t dataSize = frames * 6;

    auto put = [&file] (uint32_t value, int bytes) { file.write ((const char*) &value, bytes); };

    file.write ("RIFF", 4);
    put (36 + dataSize, 4);
    file.write ("WAVEfmt ", 8);
    put (16, 4);
    put (1, 2);
    put (2, 2);
    put ((uint32_t) sampleRate, 4);
    put ((uint32_t) sampleRate * 6, 4);
    put (6, 2);
    put (24, 2);
    file.write ("data", 4);
    put (dataSize, 4);

    for (uint32_t i = 0; i < frames; ++i)
        for (const auto* channel : { &left, &right })
            put ((uint32_t) (int32_t) std::lround (std::max (-1.0f, std::min (1.0f, (*channel)[i])) * 8388607.0f), 3);
}

/** How much of a signal's power is in its jumps from one sample to the next:
    a click or a rough join between two grains shows here before it shows
    anywhere else. */
double roughnessDb (const std::vector<float>& x)
{
    double jumps = 0.0, power = 1.0e-20;

    for (size_t i = 1; i < x.size(); ++i)
    {
        jumps += ((double) x[i] - x[i - 1]) * ((double) x[i] - x[i - 1]);
        power += (double) x[i] * x[i];
    }

    return 10.0 * std::log10 (jumps / power + 1.0e-20);
}
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: doublebox_voicecheck voice.wav outputFolder\n");
        return 2;
    }

    const auto voice = readWav (argv[1]);

    if (voice.samples.empty() || voice.sampleRate <= 0.0)
    {
        std::printf ("could not read %s\n", argv[1]);
        return 1;
    }

    const std::string folder (argv[2]);

    std::printf ("%.1f s at %.0f Hz: level %.1f dB, roughness %.1f dB\n\n", (double) voice.samples.size() / voice.sampleRate,
                 voice.sampleRate, dbxtest::rmsDb (voice.samples.data(), voice.samples.size()), roughnessDb (voice.samples));

    auto render = [&] (const char* name, const dbx::Settings& settings)
    {
        dbx::Doubler doubler;
        doubler.setSettings (settings);
        doubler.prepare (voice.sampleRate, 1, 2);

        auto left = voice.samples;
        auto right = voice.samples;

        for (size_t at = 0; at < left.size(); at += 512)
        {
            const auto count = (int) std::min<size_t> (512, left.size() - at);
            const float* in[2] = { voice.samples.data() + at, nullptr };
            float* out[2] = { left.data() + at, right.data() + at };

            doubler.setSettings (settings);
            doubler.process (in, out, count);
        }

        writeWav (folder + "/" + name + ".wav", left, right, voice.sampleRate);

        std::printf ("  %-34s level %+5.1f dB / %+5.1f dB, roughness %+5.1f dB / %+5.1f dB against the recording\n", name,
                     dbxtest::rmsDb (left.data(), left.size()) - dbxtest::rmsDb (voice.samples.data(), voice.samples.size()),
                     dbxtest::rmsDb (right.data(), right.size()) - dbxtest::rmsDb (voice.samples.data(), voice.samples.size()),
                     roughnessDb (left) - roughnessDb (voice.samples), roughnessDb (right) - roughnessDb (voice.samples));
    };

    /** One double on its own, in the middle, so that whatever is wrong with
        it is not covered by the voice. */
    auto alone = [] (float cents, float pitch, float formant)
    {
        dbx::Settings s;

        for (int i = 0; i < dbx::kNumVoices; ++i)
            s.voices[i].on = i == 3;

        s.voices[3] = { true, 0.0f, 0.0f, 0.0f, cents, pitch != 0.0f || formant != 0.0f };
        s.pitchSemitones = pitch;
        s.formantSemitones = formant;
        return s;
    };

    render ("1 as it opens", dbx::Settings());

    auto drifting = dbx::Settings();
    drifting.drift = 0.5f;
    render ("2 with drift", drifting);

    render ("3 one double alone, +7 cents", alone (7.0f, 0.0f, 0.0f));
    render ("4 one double alone, pitch +5", alone (0.0f, 5.0f, 0.0f));
    render ("5 one double alone, pitch -12", alone (0.0f, -12.0f, 0.0f));
    render ("6 one double alone, formant -3", alone (0.0f, 0.0f, -3.0f));
    render ("7 one double alone, formant +3", alone (0.0f, 0.0f, 3.0f));

    auto octave = dbx::Settings();
    octave.pitchSemitones = -12.0f;
    octave.voices[1].shift = octave.voices[3].shift = false;
    octave.voices[0].levelDb = octave.voices[4].levelDb = -10.0f;
    render ("8 an octave below on the far sides", octave);

    auto shadow = dbx::Settings();
    shadow.formantSemitones = -3.0f;
    shadow.voices[3].shift = shadow.voices[4].shift = false;
    shadow.lowDb = 2.0f;
    shadow.highDb = 3.0f;
    render ("9 formant down on the left, EQ", shadow);

    /** One double alone again, put through one of the manipulations. */
    auto manipulated = [&alone] (float harmonics, float fm, float alternator, float smear)
    {
        auto s = alone (0.0f, 0.0f, 0.0f);
        s.voices[3].manipulate = true;
        s.harmonics = harmonics;
        s.fm = fm;
        s.alternator = alternator;
        s.smear = smear;
        return s;
    };

    render ("10 one double alone, harmonics +50", manipulated (0.5f, 0.0f, 0.0f, 0.0f));
    render ("11 one double alone, harmonics -50", manipulated (-0.5f, 0.0f, 0.0f, 0.0f));
    render ("12 one double alone, FM 60", manipulated (0.0f, 0.6f, 0.0f, 0.0f));
    render ("13 one double alone, alternator full", manipulated (0.0f, 0.0f, 1.0f, 0.0f));
    render ("14 one double alone, smear 60", manipulated (0.0f, 0.0f, 0.0f, 0.6f));

    auto mixed = dbx::Settings();
    mixed.harmonics = 0.3f;
    mixed.smear = 0.4f;
    render ("15 as it opens, doubles manipulated", mixed);

    return 0;
}
