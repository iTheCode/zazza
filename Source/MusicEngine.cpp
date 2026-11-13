#include "MusicEngine.h"
#include <algorithm>
#include <cmath>

namespace zazza {
namespace {
constexpr double pi = 3.14159265358979323846;
float randomUnit(std::uint32_t& state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return static_cast<float>(state & 0xffffffu) / 16777216.0f;
}
double polyBlep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
}
Pattern generate(Style style, float density, std::uint32_t seed) {
    Pattern result {};
    auto state = seed == 0 ? 1u : seed;
    density = std::clamp(density, 0.0f, 1.0f);
    const auto kind = std::clamp(static_cast<int>(style), 0, 4);
    // Rhythmic anchors, shared harmony and a repeated motif give each layer a role.
    constexpr unsigned bassMasks[] {0x4141, 0x4949, 0x0481, 0x1041, 0x9999};
    constexpr unsigned chordMasks[] {0x4444, 0x0404, 0x4004, 0x0101, 0x0404};
    constexpr int secondRoots[5][3] {{0, 3, 4}, {0, 0, 3}, {0, 0, 4}, {3, 5, 4}, {0, 0, 3}};
    const int variation = static_cast<int>(randomUnit(state) * 3.0f);
    const int responseRoot = secondRoots[kind][variation];
    const int motifPosition = 1 + 2 * static_cast<int>(randomUnit(state) * 4.0f);
    const int motifDegrees[] {0, 2, 4, 2};
    const int motifOffset = static_cast<int>(randomUnit(state) * 4.0f);
    for (int i = 0; i < 16; ++i) {
        const bool reserved = i == motifPosition || i == motifPosition + 8;
        const bool bassAnchor = i == 0 || i == 8;
        const bool chordAnchor = i == (kind == 3 ? 0 : 2);
        const float sparse = kind == 2 ? 0.55f : 1.0f;
        auto& bass = result[0][static_cast<size_t>(i)];
        auto& chord = result[1][static_cast<size_t>(i)];
        auto& top = result[2][static_cast<size_t>(i)];
        bass.on = !reserved && (bassAnchor || randomUnit(state) < sparse * (((bassMasks[kind] >> i) & 1u) ? 0.45f + 0.5f * density : 0.18f * density));
        chord.on = !reserved && (chordAnchor || randomUnit(state) < sparse * (((chordMasks[kind] >> i) & 1u) ? 0.35f + 0.55f * density : 0.035f * density));
        top.on = reserved || (!bass.on && !chord.on && randomUnit(state) < density * 0.18f * sparse);
        bass.degree = bassAnchor || randomUnit(state) < 0.8f ? 0 : 4;
        chord.degree = 0;
        top.degree = motifDegrees[(i / 4 + motifOffset) % 4];
        bass.velocity = (bassAnchor ? 0.86f : 0.64f) + randomUnit(state) * 0.08f;
        chord.velocity = 0.62f + randomUnit(state) * 0.1f;
        top.velocity = 0.5f + randomUnit(state) * 0.12f;
        bass.gate = kind == 4 ? 0.48f : kind == 3 ? 0.86f : 0.62f;
        chord.gate = kind == 3 ? 0.96f : kind == 2 ? 0.32f : 0.58f;
        top.gate = kind == 2 ? 0.3f : 0.52f;
        for (int layer = 0; layer < layers; ++layer) {
            auto response = result[static_cast<size_t>(layer)][static_cast<size_t>(i)];
            response.degree += responseRoot;
            // Keep the groove recognisable; only the final melodic answer changes.
            if (layer == 2 && i == motifPosition + 8) response.degree = responseRoot;
            result[static_cast<size_t>(layer)][static_cast<size_t>(i + 16)] = response;
        }
    }
    return result;
}
Arrangement arrange(Style style, float density, std::uint32_t seed) {
    Arrangement result;
    result.pattern = generate(style, density, seed);
    auto state = (seed == 0 ? 1u : seed) ^ 0x9e3779b9u;
    const int kind = std::clamp(static_cast<int>(style), 0, 4);
    const int palette = static_cast<int>(randomUnit(state) * 3.0f);
    constexpr int waves[5][3][3] {
        {{1, 2, 3}, {0, 1, 2}, {2, 3, 1}}, {{1, 2, 3}, {2, 1, 3}, {0, 3, 2}},
        {{0, 2, 3}, {2, 3, 0}, {1, 0, 3}}, {{0, 3, 1}, {1, 2, 3}, {0, 1, 2}},
        {{1, 2, 3}, {2, 1, 0}, {0, 3, 2}}
    };
    constexpr float swings[] {0.12f, 0.18f, 0.27f, 0.1f, 0.22f};
    result.swing = swings[kind] + randomUnit(state) * 0.05f;
    result.duck = kind == 3 ? 0.18f : 0.25f + randomUnit(state) * 0.12f;
    result.drive = kind == 2 || kind == 3 ? 0.06f : 0.1f + randomUnit(state) * 0.08f;
    for (int layer = 0; layer < layers; ++layer) {
        auto& sound = result.sounds[static_cast<size_t>(layer)];
        sound.wave = waves[kind][palette][layer];
        sound.cutoff = (layer == 0 ? 450.0f : layer == 1 ? 1800.0f : 3500.0f) * (0.8f + randomUnit(state) * 0.65f);
        sound.decay = (kind == 3 ? 0.5f : kind == 2 ? 0.14f : 0.25f) * (0.8f + randomUnit(state) * 0.5f);
        sound.gain = layer == 0 ? 0.8f : layer == 1 ? 0.48f : 0.36f;
        sound.pan = layer == 0 ? 0.0f : (layer == 1 ? -1.0f : 1.0f) * (0.15f + randomUnit(state) * 0.15f);
    }
    return result;
}
int scaleNote(int root, Scale scale, int degree, int octaveBase) {
    constexpr int intervals[4][7] {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10},
                                  {0, 2, 4, 5, 7, 9, 11}, {0, 3, 5, 7, 10, 0, 0}};
    const int index = std::clamp(static_cast<int>(scale), 0, 3);
    const int count = index == 3 ? 5 : 7;
    const int oct = static_cast<int>(std::floor(static_cast<double>(degree) / count));
    const int slot = degree - oct * count;
    auto note = octaveBase + std::clamp(root, 0, 11) + oct * 12 + intervals[index][slot];
    // Fold extreme registers by octaves: clipping to 0/127 would leave the scale.
    while (note < 0) note += 12;
    while (note > 127) note -= 12;
    return note;
}
int noteCount(int layer) { return layer == 1 ? 4 : 1; }
std::array<int, 4> notesForStep(int layer, const Step& step, int root, Scale scale, int octave) {
    std::array<int, 4> result {};
    const int base = (layer == 0 ? 36 : layer == 1 ? 48 : 60) + octave * 12;
    for (int i = 0; i < noteCount(layer); ++i)
        result[static_cast<size_t>(i)] = scaleNote(root, scale, step.degree + i * 2, base);
    return result;
}
double stepBeat(int step, double swing) {
    return step * 0.25 + (step % 2 != 0 ? std::clamp(swing, 0.0, 0.6) * 0.25 : 0.0);
}
double stepDuration(const Step& step, double swing) {
    // Short enough to leave a gap before the next step even at maximum swing.
    return 0.25 * (1.0 - std::clamp(swing, 0.0, 0.6)) * std::clamp(static_cast<double>(step.gate), 0.1, 0.98);
}
int stepAt(double ppq, double swing) {
    double local = std::fmod(ppq, loopBeats);
    if (local < 0.0) local += loopBeats;
    auto step = std::clamp(static_cast<int>(local * 4.0), 0, steps - 1);
    if ((step % 2) != 0 && local < stepBeat(step, swing)) --step;
    return step;
}
void Synth::prepare(double rate) { sampleRate = std::max(8000.0, rate); reset(); }
void Synth::reset() {
    for (auto& lane : voices) for (auto& voice : lane) voice = {};
    smoothed = {};
}
void Synth::releaseAll() { for (auto& lane : voices) for (auto& voice : lane) voice.released = true; }
void Synth::noteOn(int layer, int note, float velocity) {
    auto& lane = voices[static_cast<size_t>(layer)];
    auto* selected = &lane[0];
    for (auto& voice : lane) {
        if (voice.note < 0) { selected = &voice; break; }
        if (voice.envelope < selected->envelope) selected = &voice;
    }
    *selected = {};
    selected->note = note;
    selected->velocity = std::clamp(velocity, 0.0f, 1.0f);
}
void Synth::noteOff(int layer, int note) {
    for (auto& voice : voices[static_cast<size_t>(layer)]) if (voice.note == note) voice.released = true;
}
std::array<float, 2> Synth::render(const std::array<LayerSound, layers>& sounds, const std::array<SampleView, layers>& samples) {
    std::array<float, 2> output {};
    const float smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.01)));
    for (int layer = 0; layer < layers; ++layer) {
        const auto index = static_cast<size_t>(layer);
        const auto& target = sounds[index];
        auto& sound = smoothed[index];
        sound.gain += smoothing * (target.gain - sound.gain);
        sound.cutoff += smoothing * (target.cutoff - sound.cutoff);
        sound.pan += smoothing * (target.pan - sound.pan);
        const auto filterCoefficient = static_cast<float>(1.0 - std::exp(-2.0 * pi * std::clamp(sound.cutoff, 40.0f, static_cast<float>(sampleRate * 0.4)) / sampleRate));
        const auto decayCoefficient = static_cast<float>(std::exp(-1.0 / (sampleRate * std::max(0.03f, target.decay))));
        const auto releaseCoefficient = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.018)));
        std::array<float, 2> sum {};
        for (auto& voice : voices[index]) {
            if (voice.note < 0) continue;
            voice.age += static_cast<float>(1.0 / sampleRate);
            if (voice.released) voice.envelope *= releaseCoefficient;
            else if (voice.age < 0.004f) voice.envelope = voice.age / 0.004f;
            else voice.envelope *= decayCoefficient;
            if (voice.age >= 0.004f && voice.envelope < 0.00005f) { voice.note = -1; continue; }
            const double hz = 440.0 * std::exp2((voice.note - 69) / 12.0);
            const double dt = std::min(0.45, hz / sampleRate);
            const double phase = voice.phase;
            double wave = std::sin(2.0 * pi * phase);
            if (target.wave == 1) wave = 2.0 * phase - 1.0 - polyBlep(phase, dt);
            if (target.wave == 2) {
                wave = (phase < 0.5 ? 1.0 : -1.0) + polyBlep(phase, dt) - polyBlep(std::fmod(phase + 0.5, 1.0), dt);
            }
            if (target.wave == 3) wave = std::sin(2.0 * pi * phase + 1.6 * voice.envelope * std::sin(4.0 * pi * phase));
            voice.phase += dt;
            if (voice.phase >= 1.0) voice.phase -= 1.0;
            std::array<float, 2> source {{static_cast<float>(wave), static_cast<float>(wave)}};
            const auto& sample = samples[index];
            if (sample.enabled) {
                if (sample.left == nullptr || sample.length < 2 || voice.samplePosition >= sample.length - 1) { voice.note = -1; continue; }
                const auto position = static_cast<int>(voice.samplePosition);
                const auto fraction = static_cast<float>(voice.samplePosition - position);
                const auto fade = std::min(1.0f, static_cast<float>(sample.length - 1 - voice.samplePosition) / 64.0f);
                for (size_t channel = 0; channel < 2; ++channel) {
                    const auto* data = channel == 0 || sample.right == nullptr ? sample.left : sample.right;
                    source[channel] = (data[position] + fraction * (data[position + 1] - data[position])) * fade;
                }
                voice.samplePosition += sample.rate / sampleRate * std::exp2((voice.note - sample.rootNote) / 12.0);
            }
            for (size_t channel = 0; channel < 2; ++channel) {
                voice.filter[channel] += filterCoefficient * (source[channel] - voice.filter[channel]);
                sum[channel] += voice.filter[channel] * voice.envelope * voice.velocity;
            }
        }
        const auto gain = sound.gain * (layer == 1 ? 0.12f : 0.28f);
        output[0] += sum[0] * gain * std::sqrt((1.0f - sound.pan) * 0.5f);
        output[1] += sum[1] * gain * std::sqrt((1.0f + sound.pan) * 0.5f);
    }
    return output;
}
}
