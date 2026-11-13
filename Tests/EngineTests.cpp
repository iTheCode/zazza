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
        {
            // A constant two-second recording exposes early decay/gate cuts and exact slice boundaries.
            std::vector<float> data(96000, 0.5f);
            std::array<zazza::SampleView, 3> views {};
            views[0] = {data.data(), nullptr, static_cast<int>(data.size()), 60, 48000, true};
            views[0].oneShot = true; views[0].pitched = false;
            std::array<zazza::LayerSound, 3> sounds {};
            sounds[0].cutoff = 16000; sounds[0].decay = 0.04f;
            zazza::Synth full; full.prepare(48000); full.noteOn(0, 84, 1);
            for (int i = 0; i < 95900; ++i) {
                if (i == 100) full.noteOff(0, 84);
                const auto audio = full.render(sounds, views);
                if (i > 48000) require(audio[0] > 0.02f, "Original one-shot truncated by pitch, gate or decay");
            }
            for (int i = 0; i < 101; ++i) full.render(sounds, views);
            require(full.render(sounds, views)[0] == 0, "One-shot exceeded original duration");
            views[0].start = 0.5f; views[0].end = 0.75f;
            std::fill(data.begin(), data.begin() + 48000, -0.5f);
            std::fill(data.begin() + 72000, data.end(), -0.5f);
            zazza::Synth slice; slice.prepare(48000); slice.noteOn(0, 36, 1);
            for (int i = 0; i < 23900; ++i) {
                const auto audio = slice.render(sounds, views);
                if (i > 1000) require(audio[0] > 0.02f, "Slice played outside selected region");
            }
            for (int i = 0; i < 101; ++i) slice.render(sounds, views);
            require(slice.render(sounds, views)[0] == 0, "Slice crossed end marker");
            views[0].start = 0; views[0].end = 1;
            zazza::Synth stopped; stopped.prepare(48000); stopped.noteOn(0, 60, 1);
            for (int i = 0; i < 2000; ++i) stopped.render(sounds, views);
            stopped.releaseAll();
            for (int i = 0; i < 12000; ++i) stopped.render(sounds, views);
            require(std::abs(stopped.render(sounds, views)[0]) < 0.00001f, "Panic cannot stop one-shot sample");
            views[0].oneShot = false;
            zazza::Synth gated; gated.prepare(48000); gated.noteOn(0, 60, 1);
            for (int i = 0; i < 2000; ++i) gated.render(sounds, views);
            gated.noteOff(0, 60);
            for (int i = 0; i < 12000; ++i) gated.render(sounds, views);
            require(std::abs(gated.render(sounds, views)[0]) < 0.00001f, "Gate mode ignores note off");
            views[0].start = 0.8f; views[0].end = 0.2f;
            zazza::Synth invalid; invalid.prepare(48000); invalid.noteOn(0, 60, 1);
            require(invalid.render(sounds, views)[0] == 0, "Reversed automated slice must be silent");
        }
        for (double rate : {44100.0, 48000.0, 96000.0}) {
            std::vector<float> data(static_cast<size_t>(rate * 2), 0.5f);
            std::array<zazza::SampleView, 3> views {};
            views[0] = {data.data(), nullptr, static_cast<int>(data.size()), 60, rate, true};
            views[0].oneShot = true; views[0].pitched = false;
            std::array<zazza::LayerSound, 3> sounds {};
            sounds[0].cutoff = 16000;
            zazza::Synth dense; dense.prepare(rate);
            float previous = 0;
            const float mix = 0.5f * sounds[0].gain * 0.28f * std::sqrt(0.5f);
            for (int trigger = 0; trigger < 18; ++trigger) {
                dense.noteOn(0, 40 + trigger, 0.1f + trigger * 0.04f);
                const auto first = dense.render(sounds, views)[0];
                if (trigger >= 12) require(std::abs(first - previous) < 0.001f, "Voice stealing introduces a hard audio discontinuity");
                for (int i = 1; i < static_cast<int>(rate * 0.02); ++i) previous = dense.render(sounds, views)[0];
                if (trigger >= 12) {
                    float expected = 0;
                    for (int n = trigger - 11; n <= trigger; ++n) expected += 0.1f + n * 0.04f;
                    require(std::abs(previous - mix * expected) < 0.0001f, "Full one-shot pool must retain the newest twelve notes");
                }
            }
            // Several MIDI note-ons can arrive before a single render sample.
            for (int n = 0; n < 4; ++n) dense.noteOn(0, 80 + n, 0.9f);
            for (int i = 0; i < static_cast<int>(rate * 0.02); ++i) previous = dense.render(sounds, views)[0];
            float expected = 4 * 0.9f;
            for (int n = 10; n < 18; ++n) expected += 0.1f + n * 0.04f;
            require(std::abs(previous - mix * expected) < 0.0001f, "Same-frame note-ons steal newly allocated voices");
            for (int n = 0; n < 32; ++n) {
                dense.noteOn(0, 60 + n, 0.9f);
                const auto next = dense.render(sounds, views)[0];
                require(std::isfinite(next) && std::abs(next - previous) < 0.01f, "Exhausted retirement pool cuts sounding audio");
                previous = next;
            }
            dense.releaseAll();
            for (int i = 0; i < static_cast<int>(rate * 0.3); ++i) dense.render(sounds, views);
            require(std::abs(dense.render(sounds, views)[0]) < 0.00001f, "Retired voices survive panic");
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
