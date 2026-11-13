#include "MusicEngine.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
void require(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
}
int main() {
    try {
        for (int style = 0; style < 5; ++style) for (int seed = 1; seed < 30; ++seed) {
            const auto a = zazza::generate(static_cast<zazza::Style>(style), 0.6f, static_cast<unsigned>(seed));
            const auto b = zazza::generate(static_cast<zazza::Style>(style), 0.6f, static_cast<unsigned>(seed));
            for (int l = 0; l < 3; ++l) {
                int active = 0;
                for (int s = 0; s < 32; ++s) {
                    const auto& x = a[static_cast<size_t>(l)][static_cast<size_t>(s)];
                    const auto& y = b[static_cast<size_t>(l)][static_cast<size_t>(s)];
                    require(x.on == y.on && x.degree == y.degree && x.gate == y.gate && x.velocity == y.velocity, "Generator not reproducible");
                    require(x.gate >= 0.1f && x.gate <= 0.98f && x.velocity > 0 && x.velocity <= 1, "Invalid step");
                    if (x.on) ++active;
                }
                require(active > 0, "Empty generated layer");
            }
        }
        constexpr int masks[] {0b010110101101, 0b011010101101, 0b101010110101, 0b010010101001};
        for (int scale = 0; scale < 4; ++scale) for (int root = 0; root < 12; ++root)
            for (int degree = -7; degree <= 20; ++degree) {
                const auto note = zazza::scaleNote(root, static_cast<zazza::Scale>(scale), degree, 48);
                require((masks[scale] & (1 << ((note - root + 120) % 12))) != 0, "Note outside selected scale");
            }
        require(zazza::scaleNote(9, zazza::Scale::minor, 0, 60) == 69, "A4 must be MIDI 69");
        for (int scale = 0; scale < 4; ++scale) for (int root = 0; root < 12; ++root)
            for (int layer = 0; layer < 3; ++layer) for (int octave : {-2, 2}) for (int degree : {-7, 14}) {
                const auto notes = zazza::notesForStep(layer, {true, degree, 1.0f, 0.5f}, root, static_cast<zazza::Scale>(scale), octave);
                for (int n = 0; n < zazza::noteCount(layer); ++n) {
                    const auto note = notes[static_cast<size_t>(n)];
                    require(note >= 0 && note <= 127 && (masks[scale] & (1 << ((note - root + 132) % 12))) != 0, "Extreme register leaves scale");
                    for (int j = 0; j < n; ++j) require(note != notes[static_cast<size_t>(j)], "Extreme chord has duplicate MIDI notes");
                }
            }
        for (double swing : {0.0, 0.16, 0.6}) for (int step = 0; step < 32; ++step) {
            const auto beat = zazza::stepBeat(step, swing);
            require(zazza::stepAt(beat + 1e-8, swing) == step, "Swing onset wrong");
            require(zazza::stepAt(beat + 8.0 + 1e-8, swing) == step, "Loop timing wrong");
            require(beat + zazza::stepDuration({true, 0, 1, 0.98f}, swing) < zazza::stepBeat(step + 1, swing), "Gate overlaps next step");
        }
        require(zazza::stepAt(-0.01, 0.2) == 31, "Negative PPQ broken");
        for (int style = 0; style < 5; ++style) for (unsigned seed = 1; seed < 60; ++seed) for (float density : {0.0f, 0.5f, 1.0f}) {
            const auto idea = zazza::arrange(static_cast<zazza::Style>(style), density, seed);
            const auto again = zazza::arrange(static_cast<zazza::Style>(style), density, seed);
            for (int i = 0; i < 32; ++i) {
                const auto index = static_cast<size_t>(i);
                const auto harmony = idea.pattern[1][index].degree;
                require(!idea.pattern[2][index].on || (!idea.pattern[0][index].on && !idea.pattern[1][index].on), "Melody crowds bass/chord attacks");
                for (int layer : {0, 2}) {
                    const auto relative = idea.pattern[static_cast<size_t>(layer)][index].degree - harmony;
                    require(relative == 0 || relative == 2 || relative == 4 || relative == 6, "Part conflicts with shared harmony");
                }
                if (i < 16) for (int layer = 0; layer < 3; ++layer)
                    require(idea.pattern[static_cast<size_t>(layer)][index].on == idea.pattern[static_cast<size_t>(layer)][index + 16].on, "Motif does not repeat");
            }
            for (size_t layer = 0; layer < 3; ++layer) {
                const auto& sound = idea.sounds[layer];
                require(sound.wave == again.sounds[layer].wave && sound.cutoff == again.sounds[layer].cutoff, "Sound palette not reproducible");
                require(sound.gain > 0 && sound.gain <= 1 && sound.cutoff >= 60 && sound.cutoff <= 16000 && sound.decay >= 0.04f, "Invalid automatic mix");
            }
            require(idea.sounds[0].pan == 0 && idea.sounds[1].pan < 0 && idea.sounds[2].pan > 0, "Automatic placement lost");
        }
        {
            std::vector<float> wave(22050);
            for (size_t i = 0; i < wave.size(); ++i) wave[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.141592653589793 * 440.0 * static_cast<double>(i) / 44100.0));
            std::array<zazza::SampleView, 3> samples {};
            samples[0] = {wave.data(), nullptr, static_cast<int>(wave.size()), 69, 44100.0, true};
            std::array<zazza::LayerSound, 3> sounds {};
            sounds[0].cutoff = 16000; sounds[0].decay = 2;
            for (int note : {69, 81}) {
                zazza::Synth synth; synth.prepare(48000); synth.noteOn(0, note, 1.0f);
                int crossings = 0; float previous = 0;
                for (int i = 0; i < 8192; ++i) {
                    const auto frame = synth.render(sounds, samples);
                    require(std::isfinite(frame[0]) && std::abs(frame[0] - frame[1]) < 1e-6f, "Mono sample playback invalid");
                    if (i >= 512 && frame[0] >= 0 && previous < 0) ++crossings;
                    previous = frame[0];
                }
                const auto measured = crossings * 48000.0 / (8192.0 - 512.0);
                require(std::abs(measured - (note == 69 ? 440.0 : 880.0)) < 8.0, "Sample pitch/rate conversion wrong");
                for (int i = 0; i < 30000; ++i) synth.render(sounds, samples);
                require(synth.render(sounds, samples)[0] == 0.0f, "Sample does not stop at end");
            }
            zazza::Synth empty; empty.prepare(48000); empty.noteOn(0, 60, 1);
            samples[0] = {}; samples[0].enabled = true;
            for (int i = 0; i < 512; ++i) require(empty.render(sounds, samples)[0] == 0, "Empty sample falls back to unwanted synth");
            auto opposite = wave; for (auto& sample : opposite) sample = -sample;
            samples[0] = {wave.data(), opposite.data(), static_cast<int>(wave.size()), 69, 44100.0, true};
            zazza::Synth stereo; stereo.prepare(48000); stereo.noteOn(0, 69, 1);
            for (int i = 0; i < 4096; ++i) {
                const auto audio = stereo.render(sounds, samples);
                require(std::abs(audio[0] + audio[1]) < 1e-6f, "Stereo sample channels collapsed");
            }
        }
        for (double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
            zazza::Synth synth; synth.prepare(rate);
            std::array<zazza::LayerSound, 3> sounds {};
            for (int wave = 0; wave < 4; ++wave) {
                synth.reset(); for (auto& sound : sounds) sound.wave = wave;
                for (int l = 0; l < 3; ++l) for (int note : {36, 60, 64, 67}) synth.noteOn(l, note, 1.0f);
                double energy = 0.0;
                for (int i = 0; i < 8192; ++i) for (auto sample : synth.render(sounds)) {
                    require(std::isfinite(sample) && std::abs(sample) < 4.0f, "Unstable synth"); energy += sample * sample;
                }
                require(energy > 0.01, "Silent synth");
                synth.releaseAll();
                for (int i = 0; i < static_cast<int>(rate * 0.25); ++i) synth.render(sounds);
                for (auto sample : synth.render(sounds)) require(std::abs(sample) < 0.0001f, "Release leaves stuck voice");
                synth.reset(); for (auto sample : synth.render(sounds)) require(sample == 0.0f, "Reset not silent");
            }
        }
        std::cout << "PASS: " << checks << " checks (scales, deterministic styles, swing, loops, synthesis, releases at four sample rates)\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
