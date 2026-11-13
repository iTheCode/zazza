#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StateMigration.h"
#include <cmath>

juce::String ZazzaProcessor::layerId(int l, const juce::String& name) { return "layer" + juce::String(l) + "_" + name; }
juce::String ZazzaProcessor::stepId(int l, int s, const juce::String& name) { return layerId(l, "step" + juce::String(s) + "_" + name); }

juce::AudioProcessorValueTreeState::ParameterLayout ZazzaProcessor::makeParameters() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto real = [&layout](const juce::String& id, const juce::String& name, float lo, float hi, float initial, float skew = 1.0f) {
        const auto decimals = id.contains("cutoff") ? 0 : id == "tempo" || id == "master" ? 1 : 2;
        const auto attributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [decimals](float value, int) { return juce::String(value, decimals); });
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 1}, name, juce::NormalisableRange<float>(lo, hi, 0.0f, skew), initial, attributes));
    };
    const auto integer = [&layout](const juce::String& id, const juce::String& name, int lo, int hi, int initial) {
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{id, 1}, name, lo, hi, initial));
    };
    const auto choice = [&layout](const juce::String& id, const juce::String& name, const juce::StringArray& choices, int initial) {
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, choices, initial));
    };
    choice("root", "Tonalidad", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0);
    choice("scale", "Escala", {"Menor", "Dorica", "Mayor", "Pentatonica menor"}, 0);
    choice("style", "Estilo", {"House", "Tech house", "Minimal", "Deep house", "Rollin"}, 1);
    real("density", "Densidad al generar", 0.0f, 1.0f, 0.6f);
    real("swing", "Swing", 0.0f, 0.6f, 0.16f);
    real("tempo", "BPM de escucha", 60.0f, 180.0f, 126.0f);
    real("master", "Salida dB", -48.0f, 0.0f, -6.0f);
    real("duck", "Bombeo 4/4", 0.0f, 1.0f, 0.25f);
    real("drive", "Saturacion", 0.0f, 1.0f, 0.1f);
    integer("seed", "Semilla", 1, 999999, 401);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sequencer", 1}, "Secuenciador", true));
    const auto initial = zazza::generate(zazza::Style::techHouse, 0.6f, 401);
    for (int l = 0; l < zazza::layers; ++l) {
        const auto prefix = "Capa " + juce::String(l + 1) + " ";
        real(layerId(l, "gain"), prefix + "Nivel", 0.0f, 1.0f, l == 0 ? 0.8f : 0.5f);
        real(layerId(l, "cutoff"), prefix + "Filtro Hz", 60.0f, 16000.0f, l == 0 ? 800.0f : 2800.0f, 0.3f);
        real(layerId(l, "decay"), prefix + "Caida s", 0.04f, 2.0f, l == 0 ? 0.22f : 0.4f, 0.5f);
        real(layerId(l, "pan"), prefix + "Pan", -1.0f, 1.0f, l == 0 ? 0.0f : l == 1 ? -0.2f : 0.2f);
        choice(layerId(l, "source"), prefix + "Fuente", {"Sintesis", "Sample"}, 0);
        integer(layerId(l, "sampleRoot"), prefix + "Nota raiz del sample", 0, 127, 60);
        integer(layerId(l, "octave"), prefix + "Octava", -2, 2, 0);
        choice(layerId(l, "wave"), prefix + "Sintesis", {"Seno / Sub", "Saw / Analog", "Pulse / Hollow", "FM / Glass"}, l == 0 ? 1 : l == 1 ? 2 : 3);
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{layerId(l, "enabled"), 1}, prefix + "Activa", true));
        for (int s = 0; s < zazza::steps; ++s) {
            const auto& step = initial[static_cast<size_t>(l)][static_cast<size_t>(s)];
            const auto name = prefix + "Paso " + juce::String(s + 1) + " ";
            layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{stepId(l, s, "on"), 1}, name + "Activo", step.on));
            integer(stepId(l, s, "degree"), name + "Grado", -7, 14, step.degree);
            real(stepId(l, s, "velocity"), name + "Velocidad", 0.1f, 1.0f, step.velocity);
            real(stepId(l, s, "gate"), name + "Duracion", 0.1f, 0.98f, step.gate);
        }
    }
    return layout;
}
ZazzaProcessor::ZazzaProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ZazzaState", makeParameters()) {
    const auto param = [this](const juce::String& id) { return parameters.getRawParameterValue(id); };
    root = param("root"); scale = param("scale"); swing = param("swing"); tempo = param("tempo");
    master = param("master"); duck = param("duck"); drive = param("drive"); sequencer = param("sequencer");
    for (int l = 0; l < zazza::layers; ++l) {
        auto& ref = refs[static_cast<size_t>(l)];
        ref.gain = param(layerId(l, "gain")); ref.cutoff = param(layerId(l, "cutoff"));
        ref.decay = param(layerId(l, "decay")); ref.pan = param(layerId(l, "pan"));
        ref.wave = param(layerId(l, "wave")); ref.octave = param(layerId(l, "octave")); ref.enabled = param(layerId(l, "enabled"));
        ref.source = param(layerId(l, "source")); ref.sampleRoot = param(layerId(l, "sampleRoot"));
        for (int s = 0; s < zazza::steps; ++s)
            ref.steps[static_cast<size_t>(s)] = {param(stepId(l, s, "on")), param(stepId(l, s, "degree")), param(stepId(l, s, "velocity")), param(stepId(l, s, "gate"))};
    }
}
void ZazzaProcessor::setControlValue(const juce::String& id, float v) {
    if (auto* p = parameters.getParameter(id)) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
    }
}
float ZazzaProcessor::value(const juce::String& id) const { return parameters.getRawParameterValue(id)->load(); }
zazza::Pattern ZazzaProcessor::pattern() const {
    zazza::Pattern result;
    for (size_t l = 0; l < result.size(); ++l)
        for (size_t s = 0; s < result[l].size(); ++s) {
            const auto& r = refs[l].steps[s];
            result[l][s] = {r.on->load() >= 0.5f, juce::roundToInt(r.degree->load()), r.velocity->load(), r.gate->load()};
        }
    return result;
}
void ZazzaProcessor::generatePattern(bool newSeed) {
    if (newSeed) setControlValue("seed", static_cast<float>(juce::Random::getSystemRandom().nextInt(999999) + 1));
    const auto result = zazza::generate(static_cast<zazza::Style>(static_cast<int>(value("style"))), value("density"), static_cast<uint32_t>(value("seed")));
    for (int l = 0; l < zazza::layers; ++l) for (int s = 0; s < zazza::steps; ++s) {
        const auto& step = result[static_cast<size_t>(l)][static_cast<size_t>(s)];
        setControlValue(stepId(l, s, "on"), step.on ? 1.0f : 0.0f);
        setControlValue(stepId(l, s, "degree"), static_cast<float>(step.degree));
        setControlValue(stepId(l, s, "velocity"), step.velocity);
        setControlValue(stepId(l, s, "gate"), step.gate);
    }
    stateChanged.store(true);
}
void ZazzaProcessor::generateIdea(bool newSeed) {
    if (newSeed) setControlValue("seed", static_cast<float>(juce::Random::getSystemRandom().nextInt(999999) + 1));
    const auto idea = zazza::arrange(static_cast<zazza::Style>(static_cast<int>(value("style"))), value("density"), static_cast<uint32_t>(value("seed")));
    for (int layer = 0; layer < zazza::layers; ++layer) {
        const auto& sound = idea.sounds[static_cast<size_t>(layer)];
        setControlValue(layerId(layer, "wave"), static_cast<float>(sound.wave));
        setControlValue(layerId(layer, "gain"), sound.gain);
        setControlValue(layerId(layer, "cutoff"), sound.cutoff);
        setControlValue(layerId(layer, "decay"), sound.decay);
        setControlValue(layerId(layer, "pan"), sound.pan);
        setControlValue(layerId(layer, "octave"), 0.0f);
    }
    setControlValue("swing", idea.swing); setControlValue("duck", idea.duck); setControlValue("drive", idea.drive);
    generatePattern(false);
}
void ZazzaProcessor::applyStyle(int style) {
    style = juce::jlimit(0, 4, style);
    setControlValue("style", static_cast<float>(style));
    const float bpms[] {124, 128, 125, 120, 130};
    setControlValue("tempo", bpms[style]);
    generateIdea(false);
}
juce::Result ZazzaProcessor::loadSample(int layer, const juce::File& file) {
    if (file.getSize() <= 0 || file.getSize() > static_cast<juce::int64>(zazza::SampleAsset::maxBytes))
        return juce::Result::fail("Usa un WAV/AIFF de hasta 8 MiB.");
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes)) return juce::Result::fail("No se pudo abrir el archivo.");
    return loadSampleData(layer, bytes, file.getFileName());
}
juce::Result ZazzaProcessor::loadSampleData(int layer, const juce::MemoryBlock& bytes, const juce::String& name) {
    if (layer < 0 || layer >= zazza::layers) return juce::Result::fail("Capa invalida.");
    juce::String error;
    auto decoded = zazza::SampleAsset::decode(bytes, name, error);
    if (!decoded) return juce::Result::fail(error);
    std::shared_ptr<const zazza::SampleAsset> retired;
    {
        const juce::ScopedLock lock(getCallbackLock());
        retired = std::move(samples[static_cast<size_t>(layer)]);
        samples[static_cast<size_t>(layer)] = std::move(decoded);
        synth.reset(); stateChanged.store(true);
    }
    // Host notifications can synchronise with the audio thread; never hold its lock here.
    setControlValue(layerId(layer, "source"), 1.0f);
    return juce::Result::ok();
}
void ZazzaProcessor::clearSample(int layer) {
    if (layer < 0 || layer >= zazza::layers) return;
    std::shared_ptr<const zazza::SampleAsset> retired;
    {
        const juce::ScopedLock lock(getCallbackLock());
        retired = std::move(samples[static_cast<size_t>(layer)]);
        synth.reset(); stateChanged.store(true);
    }
    setControlValue(layerId(layer, "source"), 0.0f);
}
juce::String ZazzaProcessor::sampleName(int layer) const {
    const juce::ScopedLock lock(getCallbackLock());
    if (layer < 0 || layer >= zazza::layers || !samples[static_cast<size_t>(layer)]) return {};
    return samples[static_cast<size_t>(layer)]->name;
}
void ZazzaProcessor::prepareToPlay(double sampleRate, int) {
    rate = sampleRate; synth.prepare(rate); generatedMidi.ensureSize(262144);
    soundingStep.fill(-1); previewPpq = 0.0; wasRunning = false; wasPreview = false;
    masterSmoothed = juce::Decibels::decibelsToGain(master->load());
}
void ZazzaProcessor::releaseResources() { synth.reset(); soundingStep.fill(-1); playingStep.store(-1); peak.store(0.0f); }
bool ZazzaProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    return layout.getMainInputChannelSet().isDisabled()
        && (layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() || layout.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
}
void ZazzaProcessor::stopGenerated(int sample) {
    for (int l = 0; l < zazza::layers; ++l) {
        if (soundingStep[static_cast<size_t>(l)] < 0) continue;
        for (int n = 0; n < zazza::noteCount(l); ++n) {
            const auto note = soundingNotes[static_cast<size_t>(l)][static_cast<size_t>(n)];
            synth.noteOff(l, note); generatedMidi.addEvent(juce::MidiMessage::noteOff(l + 1, note), sample);
        }
    }
    soundingStep.fill(-1);
}
void ZazzaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedTryLock sampleGuard(getCallbackLock());
    if (!sampleGuard.isLocked()) {
        buffer.clear(); midi.clear();
        // A skipped callback may contain incoming note-offs. Silence downstream
        // instruments too, rather than leaving passthrough MIDI notes hanging.
        if (buffer.getNumSamples() > 0)
            for (int channel = 1; channel <= 16; ++channel) midi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
        panicRequested.store(true); return;
    }
    buffer.clear(); generatedMidi.clear();
    const int count = buffer.getNumSamples();
    if (count == 0) return;
    const bool sequence = sequencer->load() > 0.5f;
    double bpm = tempo->load(), ppq = previewPpq;
    bool host = false;
    if (auto* playHead = getPlayHead()) if (auto position = playHead->getPosition()) {
        host = position->getIsPlaying();
        if (auto hostTempo = position->getBpm()) if (std::isfinite(*hostTempo)) bpm = juce::jlimit(20.0, 300.0, *hostTempo);
        if (auto beat = position->getPpqPosition()) if (std::isfinite(*beat)) ppq = *beat;
    }
    const bool audition = preview.load() && !host;
    if (audition) { bpm = tempo->load(); ppq = wasPreview ? previewPpq : 0.0; }
    const bool running = sequence && (host || audition);
    const double increment = bpm / (60.0 * rate);
    const bool jump = running && wasRunning && std::abs(ppq - expectedPpq) > increment * 2.0;
    const bool changedState = stateChanged.exchange(false);
    const bool panic = panicRequested.exchange(false);
    const bool reset = changedState || panic;
    if (!running || jump || sequence != wasSequencer || audition != wasPreview || reset) {
        stopGenerated(0);
        if (jump || sequence != wasSequencer || reset || (wasRunning && !running)) synth.releaseAll();
    }
    const auto currentPattern = pattern();
    const int key = static_cast<int>(root->load());
    const auto mode = static_cast<zazza::Scale>(static_cast<int>(scale->load()));
    const double shuffle = swing->load();
    const float outputGain = juce::Decibels::decibelsToGain(master->load());
    const float pump = duck->load(), saturation = drive->load();
    const float smooth = static_cast<float>(1.0 - std::exp(-1.0 / (rate * 0.01)));
    std::array<zazza::LayerSound, zazza::layers> sounds;
    std::array<zazza::SampleView, zazza::layers> sampleViews {};
    std::array<int, zazza::layers> octaves;
    std::array<bool, zazza::layers> enabled;
    for (size_t l = 0; l < sounds.size(); ++l) {
        const auto& r = refs[l]; enabled[l] = r.enabled->load() > 0.5f;
        sounds[l] = {enabled[l] ? r.gain->load() : 0.0f, r.cutoff->load(), r.decay->load(), r.pan->load(), static_cast<int>(r.wave->load())};
        octaves[l] = static_cast<int>(r.octave->load());
        if (r.source->load() > 0.5f) {
            sampleViews[l].enabled = true;
            if (samples[l]) sampleViews[l] = samples[l]->view(static_cast<int>(r.sampleRoot->load()));
        }
    }
    auto input = midi.cbegin();
    float maximum = 0.0f;
    for (int sample = 0; sample < count; ++sample) {
        if (!sequence) {
            while (input != midi.cend() && (*input).samplePosition <= sample) {
                const auto message = (*input).getMessage();
                for (int l = 0; l < zazza::layers; ++l) {
                    if (message.isNoteOn()) synth.noteOn(l, message.getNoteNumber(), message.getFloatVelocity());
                    if (message.isNoteOff()) synth.noteOff(l, message.getNoteNumber());
                }
                if (message.isAllNotesOff() || message.isAllSoundOff()) synth.releaseAll();
                ++input;
            }
        }
        const double beat = ppq + sample * increment;
        double local = std::fmod(beat, zazza::loopBeats); if (local < 0.0) local += zazza::loopBeats;
        if (running) {
            const auto step = zazza::stepAt(beat, shuffle);
            for (int l = 0; l < zazza::layers; ++l) {
                const auto index = static_cast<size_t>(l);
                const auto& event = currentPattern[index][static_cast<size_t>(step)];
                const auto notes = zazza::notesForStep(l, event, key, mode, octaves[index]);
                const bool active = enabled[index] && event.on && local >= zazza::stepBeat(step, shuffle)
                    && local < zazza::stepBeat(step, shuffle) + zazza::stepDuration(event, shuffle);
                const auto desired = active ? step : -1;
                if (desired != soundingStep[index] || (active && notes != soundingNotes[index])) {
                    if (soundingStep[index] >= 0) for (int n = 0; n < zazza::noteCount(l); ++n) {
                        const auto note = soundingNotes[index][static_cast<size_t>(n)];
                        synth.noteOff(l, note); generatedMidi.addEvent(juce::MidiMessage::noteOff(l + 1, note), sample);
                    }
                    if (active) for (int n = 0; n < zazza::noteCount(l); ++n) {
                        const auto note = notes[static_cast<size_t>(n)];
                        synth.noteOn(l, note, event.velocity);
                        generatedMidi.addEvent(juce::MidiMessage::noteOn(l + 1, note, event.velocity), sample);
                    }
                    soundingStep[index] = desired; soundingNotes[index] = notes;
                }
            }
        }
        auto audio = synth.render(sounds, sampleViews);
        masterSmoothed += smooth * (outputGain - masterSmoothed);
        const auto beatPhase = static_cast<float>(beat - std::floor(beat));
        const float pumping = running ? 1.0f - pump * std::exp(-beatPhase * 8.0f) : 1.0f;
        const float amount = 1.0f + saturation * 3.0f;
        for (auto& channel : audio) {
            channel = std::tanh(channel * amount) * masterSmoothed * pumping;
            maximum = std::max(maximum, std::abs(channel));
        }
        if (buffer.getNumChannels() == 1) buffer.setSample(0, sample, (audio[0] + audio[1]) * 0.5f);
        else if (buffer.getNumChannels() >= 2) { buffer.setSample(0, sample, audio[0]); buffer.setSample(1, sample, audio[1]); }
    }
    if (sequence) midi.swapWith(generatedMidi);
    else midi.addEvents(generatedMidi, 0, count, 0);
    expectedPpq = ppq + count * increment;
    previewPpq = expectedPpq; wasRunning = running; wasPreview = audition; wasSequencer = sequence;
    playingStep.store(running ? zazza::stepAt(ppq + (count - 1) * increment, shuffle) : -1);
    peak.store(maximum); hostRunning.store(host); currentTempo.store(bpm);
}
void ZazzaProcessor::getStateInformation(juce::MemoryBlock& block) {
    auto state = parameters.copyState();
    std::array<std::shared_ptr<const zazza::SampleAsset>, zazza::layers> savedSamples;
    {
        const juce::ScopedLock lock(getCallbackLock());
        savedSamples = samples;
    }
    state.setProperty("schemaVersion", 2, nullptr);
    juce::ValueTree sampleTree("SAMPLES");
    for (int layer = 0; layer < zazza::layers; ++layer) if (const auto& sample = savedSamples[static_cast<size_t>(layer)]) {
        juce::ValueTree child("SAMPLE"); child.setProperty("layer", layer, nullptr);
        child.setProperty("name", sample->name, nullptr);
        child.setProperty("data", juce::Base64::toBase64(sample->encoded.getData(), sample->encoded.getSize()), nullptr);
        sampleTree.addChild(child, -1, nullptr);
    }
    state.addChild(sampleTree, -1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, block);
}
bool ZazzaProcessor::restoreState(const void* data, int size) {
    if (data == nullptr || size <= 0 || size > maxStateBytes) return false;
    if (auto xml = getXmlFromBinary(data, size)) if (xml->hasTagName(parameters.state.getType())) {
        auto state = juce::ValueTree::fromXml(*xml);
        const auto version = state.getProperty("schemaVersion", 1).toString();
        if (version != "1" && version != "2") return false;
        std::array<std::shared_ptr<const zazza::SampleAsset>, zazza::layers> decoded;
        const auto sampleTree = state.getChildWithName("SAMPLES");
        if (version == "2") {
            if (!sampleTree.isValid() || sampleTree.getNumChildren() > zazza::layers) return false;
            for (const auto& child : sampleTree) {
                const auto layerText = child.getProperty("layer").toString();
                const int layer = layerText.getIntValue();
                if (!child.hasType("SAMPLE") || layerText != juce::String(layer) || layer < 0 || layer >= zazza::layers || decoded[static_cast<size_t>(layer)]) return false;
                const auto base64 = child.getProperty("data").toString();
                if (base64.length() > static_cast<int>(zazza::SampleAsset::maxBytes * 4 / 3 + 8)) return false;
                juce::MemoryOutputStream bytes;
                if (!juce::Base64::convertFromBase64(bytes, base64)) return false;
                juce::String error;
                decoded[static_cast<size_t>(layer)] = zazza::SampleAsset::decode(bytes.getMemoryBlock(), child.getProperty("name").toString(), error);
                if (!decoded[static_cast<size_t>(layer)]) return false;
            }
            state.removeChild(sampleTree, nullptr);
        }
        juce::StringArray seen;
        for (const auto& child : state) {
            const auto id = child.getProperty("id").toString();
            const auto v = static_cast<float>(child.getProperty("value"));
            const auto* parameter = parameters.getParameter(id);
            if (!child.hasType("PARAM") || !child.hasProperty("value") || parameter == nullptr || seen.contains(id) || !std::isfinite(v)) return false;
            const auto& range = parameter->getNormalisableRange();
            if (v < range.start || v > range.end) return false;
            seen.add(id);
        }
        auto defaults = parameters.copyState();
        for (auto child : defaults) {
            const auto* parameter = parameters.getParameter(child.getProperty("id").toString());
            child.setProperty("value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
        }
        if (!zazza::completeValidatedSchemaOneState(state, defaults)) return false;
        // Only exchange assets/reset voices here. Parameter updates notify the host.
        {
            const juce::ScopedLock lock(getCallbackLock());
            samples.swap(decoded);
            synth.reset(); stateChanged.store(true);
        }
        parameters.replaceState(state);
        stateChanged.store(true);
        return true;
    }
    return false;
}
void ZazzaProcessor::setStateInformation(const void* data, int size) { restoreState(data, size); }
juce::MidiFile ZazzaProcessor::exportMidi() const {
    juce::MidiFile file; file.setTicksPerQuarterNote(960);
    juce::MidiMessageSequence conductor;
    auto bpm = static_cast<double>(value("tempo"));
    if (hostRunning.load()) bpm = currentTempo.load();
    conductor.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / bpm)), 0.0);
    conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    conductor.addEvent(juce::MidiMessage::endOfTrack(), zazza::loopBeats * 960.0);
    file.addTrack(conductor);
    const auto current = pattern();
    const char* names[] {"Zazza - Bass", "Zazza - Chords", "Zazza - Top"};
    for (int l = 0; l < zazza::layers; ++l) {
        juce::MidiMessageSequence track;
        track.addEvent(juce::MidiMessage::textMetaEvent(3, names[l]), 0.0);
        if (value(layerId(l, "enabled")) > 0.5f) for (int s = 0; s < zazza::steps; ++s) {
            const auto& step = current[static_cast<size_t>(l)][static_cast<size_t>(s)]; if (!step.on) continue;
            const auto notes = zazza::notesForStep(l, step, static_cast<int>(value("root")), static_cast<zazza::Scale>(static_cast<int>(value("scale"))), static_cast<int>(value(layerId(l, "octave"))));
            const auto start = zazza::stepBeat(s, value("swing")) * 960.0;
            const auto end = start + zazza::stepDuration(step, value("swing")) * 960.0;
            for (int n = 0; n < zazza::noteCount(l); ++n) {
                track.addEvent(juce::MidiMessage::noteOn(l + 1, notes[static_cast<size_t>(n)], step.velocity), start);
                track.addEvent(juce::MidiMessage::noteOff(l + 1, notes[static_cast<size_t>(n)]), end);
            }
        }
        track.addEvent(juce::MidiMessage::endOfTrack(), zazza::loopBeats * 960.0); track.updateMatchedPairs(); file.addTrack(track);
    }
    return file;
}
juce::AudioProcessorEditor* ZazzaProcessor::createEditor() { return new ZazzaEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ZazzaProcessor(); }
