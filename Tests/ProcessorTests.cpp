#include "PluginProcessor.h"
#include "StateMigration.h"
#include "SampleEditor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
#include <thread>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct PlayHead : juce::AudioPlayHead {
    double ppq = 0.0, bpm = 126.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info; info.setPpqPosition(ppq); info.setBpm(bpm); info.setIsPlaying(playing); return info;
    }
};
struct Event { int sample, channel, note; bool on; };
// Probe from another thread: the callback lock is recursive on the notifying thread.
struct CallbackLockProbe : juce::AudioProcessorParameter::Listener {
    ZazzaProcessor& processor;
    juce::AudioProcessorParameter& parameter;
    int changes = 0, gestures = 0;
    bool available = true;
    CallbackLockProbe(ZazzaProcessor& p, juce::AudioProcessorParameter& param) : processor(p), parameter(param) { parameter.addListener(this); }
    ~CallbackLockProbe() override { parameter.removeListener(this); }
    void probe() {
        bool acquired = false;
        std::thread audioThread([&] {
            const juce::ScopedTryLock lock(processor.getCallbackLock());
            acquired = lock.isLocked();
        });
        audioThread.join();
        available &= acquired;
    }
    void parameterValueChanged(int, float) override { ++changes; probe(); }
    void parameterGestureChanged(int, bool) override { ++gestures; probe(); }
    void reset() { changes = gestures = 0; available = true; }
};
juce::MemoryBlock testWave(int frames = 24000) {
    juce::MemoryOutputStream out;
    out.write("RIFF", 4); out.writeInt(36 + frames * 2); out.write("WAVEfmt ", 8);
    out.writeInt(16); out.writeShort(1); out.writeShort(1); out.writeInt(48000);
    out.writeInt(96000); out.writeShort(2); out.writeShort(16); out.write("data", 4); out.writeInt(frames * 2);
    for (int i = 0; i < frames; ++i) out.writeShort(static_cast<short>(12000 * std::sin(2.0 * juce::MathConstants<double>::pi * 261.625565 * i / 48000.0)));
    return out.getMemoryBlock();
}
std::vector<Event> timeline(int blockSize, double rate) {
    ZazzaProcessor p; PlayHead head; p.setPlayHead(&head); p.prepareToPlay(rate, blockSize);
    std::vector<Event> events; double energy = 0.0;
    const int total = static_cast<int>(std::ceil(8.0 * 60.0 / head.bpm * rate));
    for (int start = 0; start < total; start += blockSize) {
        const auto length = std::min(blockSize, total - start);
        juce::AudioBuffer<float> audio(2, length); juce::MidiBuffer midi;
        head.ppq = start * head.bpm / (60.0 * rate); p.processBlock(audio, midi);
        for (const auto event : midi) {
            const auto m = event.getMessage();
            if (m.isNoteOnOrOff()) events.push_back({start + event.samplePosition, m.getChannel(), m.getNoteNumber(), m.isNoteOn()});
            require(event.samplePosition >= 0 && event.samplePosition < length, "MIDI outside block");
        }
        for (int i = 0; i < length; ++i) for (int c = 0; c < 2; ++c) {
            const auto sample = audio.getSample(c, i); require(std::isfinite(sample) && std::abs(sample) <= 1.0f, "Output clipping/non-finite"); energy += sample * sample;
        }
    }
    require(energy > 1.0, "Sequencer silent");
    std::map<std::pair<int, int>, int> held;
    for (const auto& e : events) {
        auto& active = held[{e.channel, e.note}]; active += e.on ? 1 : -1;
        require(active >= 0 && active <= 1, "Unbalanced/overlapping MIDI");
    }
    for (const auto& pair : held) require(pair.second == 0, "Hanging note at loop end");
    auto exported = p.exportMidi(); require(exported.getNumTracks() == 4, "MIDI tracks missing");
    int exportedNotes = 0;
    for (int track = 1; track < 4; ++track) {
        const auto* sequence = exported.getTrack(track);
        require(std::abs(sequence->getEndTime() - 7680.0) < 1e-9, "Export must last exactly two 4/4 bars");
        for (int i = 0; i < sequence->getNumEvents(); ++i) {
            const auto m = sequence->getEventPointer(i)->message; if (!m.isNoteOnOrOff()) continue;
            ++exportedNotes;
            const auto ideal = m.getTimeStamp() / 960.0 * 60.0 / head.bpm * rate;
            bool found = false;
            for (const auto& e : events) if (e.channel == m.getChannel() && e.note == m.getNoteNumber() && e.on == m.isNoteOn() && std::abs(e.sample - ideal) < 1.01) found = true;
            require(found, "Export and realtime MIDI differ");
        }
    }
    require(exportedNotes == static_cast<int>(events.size()), "MIDI export count differs");
    p.setPlayHead(nullptr); return events;
}
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        for (double rate : {44100.0, 48000.0, 96000.0}) {
            const auto a = timeline(64, rate); const auto b = timeline(257, rate);
            require(a.size() == b.size(), "Block-size-dependent MIDI count");
            for (size_t i = 0; i < a.size(); ++i) require(std::abs(a[i].sample - b[i].sample) <= 1 && a[i].note == b[i].note && a[i].channel == b[i].channel && a[i].on == b[i].on, "Block-size-dependent timing");
        }
        ZazzaProcessor p; p.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> audio(2, 512); juce::MidiBuffer midi;
        p.processBlock(audio, midi); require(audio.getMagnitude(0, 512) == 0.0f && midi.isEmpty(), "Autoplay on plugin load");
        p.preview.store(true); p.processBlock(audio, midi); require(!midi.isEmpty(), "Preview not running");
        p.preview.store(false); midi.clear(); p.processBlock(audio, midi);
        require(!midi.isEmpty(), "Stop must send note offs");
        for (const auto e : midi) require(e.getMessage().isNoteOff(), "Stop sends note on");
        PlayHead head; p.setPlayHead(&head); midi.clear(); p.processBlock(audio, midi);
        head.ppq = 4.0; midi.clear(); p.processBlock(audio, midi);
        bool off = false; for (const auto e : midi) off |= e.getMessage().isNoteOff(); require(off, "Seek did not flush notes");
        head.playing = false; midi.clear(); p.processBlock(audio, midi);
        for (int i = 0; i < 30; ++i) { midi.clear(); p.processBlock(audio, midi); }
        require(audio.getMagnitude(0, 512) < 0.0001f, "Transport stop has stuck audio");
        p.setPlayHead(nullptr);
        p.setControlValue("sequencer", 0); midi.clear(); midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 100); p.processBlock(audio, midi);
        require(audio.getMagnitude(0, 100) < 0.0001f && audio.getMagnitude(100, 412) > 0.001f, "MIDI input timing wrong");
        p.setControlValue("root", 5); p.setControlValue("scale", 1); p.applyStyle(4);
        p.setControlValue(ZazzaProcessor::stepId(2, 7, "degree"), -3);
        juce::MemoryBlock state; p.getStateInformation(state);
        ZazzaProcessor restored; require(restored.restoreState(state.getData(), static_cast<int>(state.getSize())), "State rejected");
        for (auto* param : p.getParameters()) {
            const auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param);
            const auto actual = restored.value(withId->paramID), expected = p.value(withId->paramID);
            const auto tolerance = std::max(1e-6f, std::abs(expected) * 1e-6f);
            if (std::abs(actual - expected) > tolerance)
                std::cerr << withId->paramID << ": restored " << actual << ", expected " << expected << '\n';
            require(std::abs(actual - expected) <= tolerance, "Parameter not restored");
        }
        require(!restored.restoreState("bad", 3) && std::abs(restored.value("root") - 5.0f) < 0.01f, "Malformed state accepted");
        auto legacy = p.parameters.copyState();
        auto futureDefaults = legacy.createCopy();
        juce::ValueTree additional("PARAM"); additional.setProperty("id", "future_parameter", nullptr);
        additional.setProperty("value", 0.25f, nullptr); futureDefaults.addChild(additional, -1, nullptr);
        require(zazza::schemaOneRequiredIds().size() == 416, "Schema 1 contract changed");
        require(zazza::completeValidatedSchemaOneState(legacy, futureDefaults), "Old session rejected after additive parameter");
        require(std::abs(static_cast<float>(legacy.getChildWithProperty("id", "future_parameter").getProperty("value")) - 0.25f) < 1e-6f, "New parameter not defaulted");
        require(std::abs(static_cast<float>(legacy.getChildWithProperty("id", "root").getProperty("value")) - 5.0f) < 1e-6f, "Migration changed original value");
        legacy.getChildWithProperty("id", "future_parameter").setProperty("value", 0.75f, nullptr);
        require(zazza::completeValidatedSchemaOneState(legacy, futureDefaults)
                && std::abs(static_cast<float>(legacy.getChildWithProperty("id", "future_parameter").getProperty("value")) - 0.75f) < 1e-6f, "Migration overwrote existing additive value");
        auto incomplete = p.parameters.copyState(); incomplete.removeChild(0, nullptr);
        require(!zazza::completeValidatedSchemaOneState(incomplete, futureDefaults), "Truncated v1 state accepted by migration");
        juce::MemoryBlock broken; juce::AudioProcessor::copyXmlToBinary(*incomplete.createXml(), broken);
        require(!restored.restoreState(broken.getData(), static_cast<int>(broken.getSize())), "Truncated v1 session accepted");
        {
            ZazzaProcessor notifying;
            CallbackLockProbe probe(notifying, *notifying.parameters.getParameter("layer0_source"));
            require(notifying.loadSampleData(0, testWave(), "notification.wav").wasOk(), "Notification fixture import failed");
            require(probe.changes > 0 && probe.gestures == 2 && probe.available, "Sample import notifies with audio callback lock held");
            juce::MemoryBlock saved;
            notifying.getStateInformation(saved);
            probe.reset();
            notifying.clearSample(0);
            require(probe.changes > 0 && probe.gestures == 2 && probe.available, "Sample removal notifies with audio callback lock held");
            probe.reset();
            require(notifying.restoreState(saved.getData(), static_cast<int>(saved.getSize())), "Notification fixture restore failed");
            require(probe.changes > 0 && probe.available, "State restore notifies with audio callback lock held");
        }
        {
            ZazzaProcessor sampled;
            const auto wav = testWave();
            require(sampled.loadSampleData(0, wav, "My C4.wav").wasOk(), "WAV import failed");
            require(sampled.sampleName(0) == "My C4.wav" && sampled.value("layer0_source") > 0.5f, "Sample not selected");
            require(sampled.sampleAsset(0)->audio.getNumSamples() == 24000
                    && sampled.sampleAsset(0)->encoded == wav, "Import trimmed or rewrote the original sample");
            require(std::abs(sampled.value("layer0_samplePlayback") - 1) < 1e-6f && std::abs(sampled.value("layer0_samplePitch") - 0) < 1e-6f
                    && std::abs(sampled.value("layer0_sampleStart") - 0) < 1e-6f && std::abs(sampled.value("layer0_sampleEnd") - 1) < 1e-6f, "Import must default to full original playback");
            const juce::MemoryBlock bad("invalid", 7);
            require(sampled.loadSampleData(0, bad, "bad.wav").failed() && sampled.sampleName(0) == "My C4.wav", "Bad import replaced sample");
            require(sampled.loadSampleData(0, testWave(384001), "too-long.wav").failed(), "Sample longer than 8 seconds accepted");
            const juce::MemoryBlock oversized(zazza::SampleAsset::maxBytes + 1, true);
            require(sampled.loadSampleData(0, oversized, "too-large.wav").failed(), "Oversized sample accepted");
            auto silent = wav; std::memset(static_cast<char*>(silent.getData()) + 44, 0, silent.getSize() - 44);
            require(sampled.loadSampleData(0, silent, "silence.wav").failed(), "Silent sample accepted");
            sampled.setControlValue("root", 7); sampled.setControlValue("scale", 2); sampled.setControlValue("master", -12);
            sampled.setControlValue("layer0_sampleRoot", 60); sampled.generateIdea(false);
            require(sampled.sampleName(0) == "My C4.wav" && sampled.value("layer0_source") > 0.5f
                    && std::abs(sampled.value("layer0_sampleRoot") - 60) < 0.01f
                    && std::abs(sampled.value("root") - 7) < 0.01f && std::abs(sampled.value("master") + 12) < 0.01f, "Idea replaced sample/tuning/output");
            const float cutoff = sampled.value("layer0_cutoff"); sampled.generatePattern(true);
            require(std::abs(sampled.value("layer0_cutoff") - cutoff) < 0.001f, "Pattern-only variation changed sound");
            sampled.setControlValue("sequencer", 0); sampled.setControlValue("layer1_enabled", 0); sampled.setControlValue("layer2_enabled", 0);
            sampled.setControlValue("layer0_sampleStart", 0.25f); sampled.setControlValue("layer0_sampleEnd", 0.75f);
            juce::MemoryBlock saved; sampled.getStateInformation(saved);
            ZazzaProcessor recalled;
            require(recalled.restoreState(saved.getData(), static_cast<int>(saved.getSize())) && recalled.sampleName(0) == "My C4.wav", "Embedded sample not restored");
            require(std::abs(recalled.value("layer0_sampleStart") - 0.25) < 1e-6f && std::abs(recalled.value("layer0_sampleEnd") - 0.75) < 1e-6f
                    && std::abs(recalled.value("layer0_samplePlayback") - 1) < 1e-6f && std::abs(recalled.value("layer0_samplePitch") - 0) < 1e-6f, "Slice settings lost on recall");
            auto previousSampleState = juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize())));
            for (int layer = 0; layer < 3; ++layer) for (const auto* suffix : {"sampleStart", "sampleEnd", "samplePlayback", "samplePitch"})
                previousSampleState.removeChild(previousSampleState.getChildWithProperty("id", ZazzaProcessor::layerId(layer, suffix)), nullptr);
            juce::MemoryBlock previousBytes; juce::AudioProcessor::copyXmlToBinary(*previousSampleState.createXml(), previousBytes);
            ZazzaProcessor previous;
            require(previous.restoreState(previousBytes.getData(), static_cast<int>(previousBytes.getSize())) && previous.sampleAsset(0)
                    && previous.value("layer0_samplePlayback") < 0.5f && previous.value("layer0_samplePitch") > 0.5f
                    && previous.value("layer0_sampleEnd") > 0.99f, "Older embedded-sample presets changed behaviour");
            sampled.prepareToPlay(48000, 512); recalled.prepareToPlay(48000, 512);
            juce::AudioBuffer<float> first(2, 512), second(2, 512); juce::MidiBuffer m1, m2;
            m1.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0); m2 = m1;
            sampled.processBlock(first, m1); recalled.processBlock(second, m2);
            require(first.getMagnitude(0, 512) > 0.001f, "Sample engine silent");
            for (int i = 0; i < 512; ++i) require(std::abs(first.getSample(0, i) - second.getSample(0, i)) < 1e-5f, "Embedded sample audio changed on recall");
            auto corrupted = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
            auto corruptTree = juce::ValueTree::fromXml(*corrupted);
            corruptTree.getChildWithName("SAMPLES").getChild(0).setProperty("data", "invalid base64!", nullptr);
            juce::MemoryBlock corruptBytes; juce::AudioProcessor::copyXmlToBinary(*corruptTree.createXml(), corruptBytes);
            require(!recalled.restoreState(corruptBytes.getData(), static_cast<int>(corruptBytes.getSize())) && recalled.sampleName(0) == "My C4.wav", "Bad embedded sample damaged session");
            auto oldState = p.parameters.copyState(); oldState.setProperty("schemaVersion", 1, nullptr);
            for (int layer = 0; layer < 3; ++layer) for (const auto* suffix : {"source", "sampleRoot", "sampleStart", "sampleEnd", "samplePlayback", "samplePitch"})
                oldState.removeChild(oldState.getChildWithProperty("id", ZazzaProcessor::layerId(layer, suffix)), nullptr);
            juce::MemoryBlock oldBytes; juce::AudioProcessor::copyXmlToBinary(*oldState.createXml(), oldBytes);
            require(recalled.restoreState(oldBytes.getData(), static_cast<int>(oldBytes.getSize())) && recalled.sampleName(0).isEmpty()
                    && recalled.value("layer0_source") < 0.5f, "Original v1 session no longer loads");
            require(std::abs(recalled.value("layer0_samplePlayback") - 0) < 1e-6f && std::abs(recalled.value("layer0_samplePitch") - 1) < 1e-6f,
                    "Legacy sessions must retain gated pitched playback");
            // Audition uses the selected region at original speed, independently of note gates.
            sampled.auditionSample(0);
            m1.clear(); sampled.processBlock(first, m1);
            require(sampled.auditionProgress.load() >= 0.25f && sampled.auditionProgress.load() < 0.28f, "Slice audition starts at wrong position/speed");
            sampled.stopSampleAudition(); m1.clear(); sampled.processBlock(first, m1);
            require(sampled.auditionProgress.load() < 0, "Slice audition failed to stop");
            const juce::TemporaryFile aiff(".aiff");
            std::unique_ptr<juce::OutputStream> stream = aiff.getFile().createOutputStream();
            juce::AiffAudioFormat format;
            auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(16));
            require(writer != nullptr, "AIFF fixture writer failed");
            require(writer->writeFromAudioSampleBuffer(first, 0, first.getNumSamples()), "AIFF fixture write failed"); writer.reset();
            require(sampled.loadSample(1, aiff.getFile()).wasOk(), "Stereo AIFF file import failed");
            sampled.clearSample(0); require(sampled.sampleName(0).isEmpty() && sampled.value("layer0_source") < 0.5f, "Clear sample failed");
            std::atomic<bool> finite { true };
            std::thread playback([&] {
                juce::AudioBuffer<float> output(2, 64); juce::MidiBuffer incoming;
                for (int block = 0; block < 200; ++block) {
                    incoming.clear();
                    if (block % 8 == 0) incoming.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
                    if (block % 8 == 7) incoming.addEvent(juce::MidiMessage::allNotesOff(1), 0);
                    sampled.processBlock(output, incoming);
                    for (int channel = 0; channel < 2; ++channel) for (int i = 0; i < 64; ++i)
                        if (!std::isfinite(output.getSample(channel, i)) || std::abs(output.getSample(channel, i)) > 1.0f) finite.store(false);
                }
            });
            bool imported = true;
            for (int iteration = 0; iteration < 6; ++iteration) {
                imported &= sampled.loadSampleData(0, wav, "live.wav").wasOk();
                juce::MemoryBlock snapshot; sampled.getStateInformation(snapshot);
                sampled.clearSample(0);
            }
            playback.join(); require(imported && finite.load(), "Concurrent sample replacement/capture corrupted playback");
            bool releasedDownstream = false;
            {
                const juce::ScopedLock changingSample(sampled.getCallbackLock());
                std::thread blockedCallback([&] {
                    juce::AudioBuffer<float> output(2, 64); juce::MidiBuffer outgoing;
                    outgoing.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
                    sampled.processBlock(output, outgoing);
                    releasedDownstream = outgoing.getNumEvents() == 16 && output.getMagnitude(0, 64) < 1e-6f;
                    for (const auto event : outgoing) releasedDownstream &= event.getMessage().isAllNotesOff();
                });
                blockedCallback.join();
            }
            require(releasedDownstream, "Contended sample swap can leave downstream MIDI notes hanging");
        }
        {
            ZazzaProcessor chords;
            require(chords.loadSampleData(1, testWave(), "original-chord.wav").wasOk(), "Chord fixture import failed");
            chords.setControlValue("drive", 0); chords.setControlValue("duck", 0);
            for (int layer = 0; layer < 3; ++layer) {
                chords.setControlValue(ZazzaProcessor::layerId(layer, "enabled"), layer == 1 ? 1.0f : 0.0f);
                chords.setControlValue(ZazzaProcessor::layerId(layer, "source"), 1);
                for (int step = 0; step < 32; ++step) chords.setControlValue(ZazzaProcessor::stepId(layer, step, "on"), 0);
            }
            chords.setControlValue(ZazzaProcessor::stepId(1, 0, "on"), 1);
            chords.setControlValue(ZazzaProcessor::stepId(1, 0, "velocity"), juce::MidiMessage::noteOn(1, 60, 0.8f).getFloatVelocity());
            juce::MemoryBlock chordState; chords.getStateInformation(chordState);
            ZazzaProcessor single;
            require(single.restoreState(chordState.getData(), static_cast<int>(chordState.getSize())), "Chord reference restore failed");
            single.setControlValue("sequencer", 0);
            chords.prepareToPlay(48000, 512); single.prepareToPlay(48000, 512); chords.preview.store(true);
            juce::AudioBuffer<float> chordAudio(2, 512), singleAudio(2, 512); juce::MidiBuffer chordMidi, singleMidi;
            singleMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            chords.processBlock(chordAudio, chordMidi); single.processBlock(singleAudio, singleMidi);
            int notes = 0; for (const auto e : chordMidi) if (e.getMessage().isNoteOn()) ++notes;
            require(notes == 4, "Original sample must preserve the chord in MIDI output");
            require(chordAudio.getMagnitude(0, 512) > 0.001f, "Original chord is silent");
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i)
                require(std::abs(chordAudio.getSample(c, i) - singleAudio.getSample(c, i)) < 1e-5f, "Original chord stacks duplicate internal voices");
        }
        {
            auto loud = testWave();
            for (size_t i = 44; i < loud.getSize(); i += 2) {
                auto* bytes = static_cast<unsigned char*>(loud.getData());
                const short value = 30000;
                bytes[i] = static_cast<unsigned char>(value & 255); bytes[i + 1] = static_cast<unsigned char>((value >> 8) & 255);
            }
            ZazzaProcessor imported;
            require(imported.loadSampleData(0, loud, "loud.wav").wasOk(), "Loud sample import failed");
            const auto expected = 30000.0f / 32768.0f;
            juce::MemoryOutputStream extreme;
            extreme.write("RIFF", 4); extreme.writeInt(36 + 256); extreme.write("WAVEfmt ", 8);
            extreme.writeInt(16); extreme.writeShort(3); extreme.writeShort(1); extreme.writeInt(48000);
            extreme.writeInt(192000); extreme.writeShort(4); extreme.writeShort(32); extreme.write("data", 4); extreme.writeInt(256);
            for (int i = 0; i < 64; ++i) extreme.writeFloat(1e30f);
            require(imported.loadSampleData(0, extreme.getMemoryBlock(), "extreme.wav").failed(), "Extreme float amplitude accepted without headroom");
            require(std::abs(imported.sampleAsset(0)->audio.getSample(0, 100) - expected) < 1e-5f, "New import changes the recorded amplitude");
            juce::MemoryBlock saved; imported.getStateInformation(saved);
            ZazzaProcessor recalled;
            require(recalled.restoreState(saved.getData(), static_cast<int>(saved.getSize()))
                    && std::abs(recalled.sampleAsset(0)->audio.getSample(0, 100) - expected) < 1e-5f, "Full-level sample changed on recall");
            auto old = juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize())));
            old.setProperty("schemaVersion", 2, nullptr);
            old.getChildWithName("SAMPLES").getChild(0).removeProperty("legacyGain", nullptr);
            juce::MemoryBlock oldBytes; juce::AudioProcessor::copyXmlToBinary(*old.createXml(), oldBytes);
            require(recalled.restoreState(oldBytes.getData(), static_cast<int>(oldBytes.getSize()))
                    && std::abs(recalled.sampleAsset(0)->audio.getSample(0, 100) - 0.8f) < 1e-5f, "Legacy preset unexpectedly became louder");
            recalled.getStateInformation(saved);
            require(recalled.restoreState(saved.getData(), static_cast<int>(saved.getSize()))
                    && std::abs(recalled.sampleAsset(0)->audio.getSample(0, 100) - 0.8f) < 1e-5f, "Migrated preset loses legacy gain on second recall");
        }
        p.setControlValue("layer1_enabled", 0); require(p.exportMidi().getTrack(2)->getNumEvents() == 2, "Muted layer exported notes");
        juce::MemoryOutputStream midiBytes; require(p.exportMidi().writeTo(midiBytes, 1), "Cannot serialize MIDI");
        juce::MemoryInputStream midiInput(midiBytes.getData(), midiBytes.getDataSize(), false); juce::MidiFile parsed;
        require(parsed.readFrom(midiInput) && parsed.getNumTracks() == 4, "MIDI roundtrip failed");
        auto mono = p.getBusesLayout(); mono.outputBuses.set(0, juce::AudioChannelSet::mono());
        require(p.setBusesLayout(mono), "Mono unsupported"); p.prepareToPlay(48000, 64);
        juce::AudioBuffer<float> monoAudio(1, 64); midi.clear(); p.processBlock(monoAudio, midi);
        if (argc > 1) {
            ZazzaProcessor preview;
            std::unique_ptr<juce::AudioProcessorEditor> editor(preview.createEditor());
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
            juce::MemoryOutputStream png;
            require(juce::PNGImageFormat().writeImageToStream(image, png)
                    && juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]).replaceWithData(png.getData(), png.getDataSize()), "Preview export failed");
        }
        if (argc > 2) {
            ZazzaProcessor preview;
            require(preview.loadSampleData(0, testWave(), "C4 - waveform and slice.wav").wasOk(), "Slice snapshot import failed");
            preview.setControlValue("layer0_sampleStart", 0.25f); preview.setControlValue("layer0_sampleEnd", 0.75f);
            SampleEditor editor(preview, 0, [] {}); editor.setSize(1080, 540);
            const auto image = editor.createComponentSnapshot(editor.getLocalBounds());
            juce::MemoryOutputStream png;
            require(juce::PNGImageFormat().writeImageToStream(image, png)
                    && juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]).replaceWithData(png.getData(), png.getDataSize()), "Slice snapshot export failed");
        }
        std::cout << "PASS: MIDI/export equivalence, variable blocks at 44.1/48/96 kHz, transport, MIDI input, state migration, notifications outside callback lock, WAV/AIFF imports and limits, embedded samples, invalid-state isolation, generated-idea preservation, mute, mono, editor snapshot\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
