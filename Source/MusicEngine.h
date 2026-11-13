#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace zazza {
constexpr int layers = 3, steps = 32;
constexpr double loopBeats = 8.0;
enum class Style { house, techHouse, minimal, deepHouse, rollin };
enum class Scale { minor, dorian, major, pentatonic };
struct Step { bool on = false; int degree = 0; float velocity = 0.8f; float gate = 0.65f; };
using Pattern = std::array<std::array<Step, steps>, layers>;
Pattern generate(Style, float density, std::uint32_t seed);
int scaleNote(int root, Scale, int degree, int octaveBase);
std::array<int, 4> notesForStep(int layer, const Step&, int root, Scale, int octave);
int noteCount(int layer);
double stepBeat(int step, double swing);
double stepDuration(const Step&, double swing);
int stepAt(double ppq, double swing);

struct LayerSound {
    float gain = 0.6f, cutoff = 1800.0f, decay = 0.3f, pan = 0.0f;
    int wave = 1;
};
struct Arrangement {
    Pattern pattern;
    std::array<LayerSound, layers> sounds;
    float swing = 0.16f, duck = 0.25f, drive = 0.1f;
};
Arrangement arrange(Style, float density, std::uint32_t seed);
struct SampleView {
    const float* left = nullptr;
    const float* right = nullptr;
    int length = 0, rootNote = 60;
    double rate = 48000.0;
    bool enabled = false;
    float start = 0.0f, end = 1.0f;
    bool oneShot = false, pitched = true;
};
class Synth {
public:
    void prepare(double rate);
    void reset();
    void noteOn(int layer, int note, float velocity);
    void noteOff(int layer, int note);
    void releaseAll();
    std::array<float, 2> render(const std::array<LayerSound, layers>& sounds,
                              const std::array<SampleView, layers>& samples = {});
private:
    struct Voice {
        int note = -1;
        double phase = 0.0, samplePosition = 0.0;
        float envelope = 0.0f, velocity = 0.0f, age = 0.0f;
        std::array<float, 2> filter {};
        bool released = false;
        bool forcedRelease = false, oneShot = false;
        int sampleEnd = -1;
        double sampleIncrement = 1.0;
        std::uint64_t order = 0;
        int fadeRemaining = 0;
    };
    // Twelve playable voices and twelve short retirement tails; no audio-thread allocation.
    static constexpr std::size_t polyphony = 12;
    std::array<std::array<Voice, polyphony * 2>, layers> voices {};
    std::uint64_t nextOrder = 0;
    int stealFadeSamples = 240;
    std::array<LayerSound, layers> smoothed {};
    double sampleRate = 48000.0;
};
}
