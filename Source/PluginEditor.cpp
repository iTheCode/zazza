#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

namespace {
const juce::Colour neon(0xff26baff), cream(0xfff4e9ce), dark(0xff101820);
const char* layerNames[] {"SUB / BASS", "CHORD / STAB", "TOP / MOTIF"};
void label(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, float size, juce::Colour colour,
           juce::Justification align = juce::Justification::centredLeft) {
    g.setColour(colour); g.setFont(juce::FontOptions(size)); g.drawText(text, area, align);
}
}
ZazzaLookAndFeel::ZazzaLookAndFeel()
    : typeface(juce::Typeface::createSystemTypefaceFor(BinaryData::LuckiestGuyRegular_ttf, BinaryData::LuckiestGuyRegular_ttfSize)) {
    setColour(juce::Slider::thumbColourId, neon); setColour(juce::Slider::trackColourId, neon);
    setColour(juce::Slider::backgroundColourId, neon.withAlpha(0.12f));
    setColour(juce::Slider::textBoxTextColourId, cream);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextButton::textColourOffId, cream); setColour(juce::TextButton::textColourOnId, neon);
    setColour(juce::ComboBox::backgroundColourId, dark); setColour(juce::ComboBox::textColourId, cream);
    setColour(juce::ComboBox::outlineColourId, neon.withAlpha(0.4f)); setColour(juce::ComboBox::arrowColourId, neon);
    setColour(juce::PopupMenu::backgroundColourId, dark); setColour(juce::PopupMenu::textColourId, cream);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, neon.withAlpha(0.2f));
    setColour(juce::ToggleButton::textColourId, cream); setColour(juce::ToggleButton::tickColourId, neon);
    setColour(juce::Label::textColourId, cream);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
}
juce::Font ZazzaLookAndFeel::displayFont(float h) const { return juce::Font(juce::FontOptions(typeface).withHeight(h)); }
juce::Font ZazzaLookAndFeel::getTextButtonFont(juce::TextButton& button, int) {
    return button.getWidth() < 40 ? juce::Font(juce::FontOptions(10.0f)) : displayFont(14.0f);
}
void ZazzaLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) {
    const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(neon.withAlpha(down ? 0.4f : button.getToggleState() ? 0.28f : over ? 0.17f : 0.06f));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(neon.withAlpha(over || button.getToggleState() ? 0.95f : 0.3f));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}
void ZazzaLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float position, float start, float end, juce::Slider&) {
    const auto radius = static_cast<float>(juce::jmin(w, h)) * 0.5f - 5.0f;
    const float cx = static_cast<float>(x) + static_cast<float>(w) * 0.5f, cy = static_cast<float>(y) + static_cast<float>(h) * 0.5f;
    juce::Path track, active;
    track.addCentredArc(cx, cy, radius, radius, 0.0f, start, end, true);
    active.addCentredArc(cx, cy, radius, radius, 0.0f, start, start + (end - start) * position, true);
    g.setColour(neon.withAlpha(0.12f)); g.strokePath(track, juce::PathStrokeType(3.0f));
    g.setColour(neon.withAlpha(0.09f)); g.strokePath(active, juce::PathStrokeType(9.0f));
    g.setColour(neon); g.strokePath(active, juce::PathStrokeType(2.5f));
    const auto angle = start + (end - start) * position;
    g.drawLine(cx, cy, cx + std::sin(angle) * (radius - 5.0f), cy - std::cos(angle) * (radius - 5.0f), 2.0f);
}
void ZazzaEditor::setupKnob(Knob& knob, const juce::String& id, const juce::String& name, bool rotary) {
    knob.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    knob.slider.setColour(juce::Slider::textBoxTextColourId, cream);
    knob.slider.setSliderStyle(rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
    knob.slider.setNumDecimalPlacesToDisplay(id.contains("cutoff") || id.contains("octave") || id == "tempo" || id.contains("degree") ? 0 : 2);
    knob.slider.setName(name); knob.slider.setTooltip(name + ": arrastra o escribe un valor. Doble clic para restablecer.");
    if (auto* p = owner.parameters.getParameter(id)) knob.slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
    knob.label.setText(name, juce::dontSendNotification); knob.label.setFont(juce::FontOptions(11.0f));
    knob.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(knob.slider); addAndMakeVisible(knob.label);
    knob.attachment = std::make_unique<SliderAttachment>(owner.parameters, id, knob.slider);
}
ZazzaEditor::ZazzaEditor(ZazzaProcessor& p)
    : AudioProcessorEditor(&p), owner(p),
      wallpaper(juce::ImageCache::getFromMemory(BinaryData::wallpaper_gif, BinaryData::wallpaper_gifSize)) {
    setLookAndFeel(&theme);
    const char* choiceIds[] {"root", "scale", "style"};
    for (size_t i = 0; i < choices.size(); ++i) {
        auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(owner.parameters.getParameter(choiceIds[i]));
        choices[i].addItemList(parameter->choices, 1); choices[i].setName(parameter->name);
        addAndMakeVisible(choices[i]); choiceAttachments[i] = std::make_unique<ComboAttachment>(owner.parameters, choiceIds[i], choices[i]);
    }
    choices[2].setTooltip("Nueva idea crea sonidos y patrones coordinados del estilo elegido. Variar patron conserva los sonidos y la mezcla.");
    const char* ids[] {"density", "swing", "tempo", "duck", "drive", "master"};
    const char* names[] {"DENSIDAD (GENERAR)", "SWING", "BPM ESCUCHA", "BOMBEO", "DRIVE", "SALIDA dB"};
    for (size_t i = 0; i < globals.size(); ++i) setupKnob(globals[i], ids[i], names[i]);
    for (int l = 0; l < zazza::layers; ++l) {
        auto& controls = layerControls[static_cast<size_t>(l)];
        controls.wave.addItemList({"Seno / Sub", "Saw / Analog", "Pulse / Hollow", "FM / Glass"}, 1);
        controls.wave.setName(juce::String(layerNames[l]) + " sintesis");
        addAndMakeVisible(controls.wave); addAndMakeVisible(controls.enabled);
        controls.source.addItemList({"Sintesis", "Sample"}, 1);
        controls.source.setName("Fuente de " + juce::String(layerNames[l]));
        for (int note = 0; note < 128; ++note) controls.sampleRoot.addItem(juce::MidiMessage::getMidiNoteName(note, true, true, 4), note + 1);
        controls.sampleRoot.setName("Nota raiz del sample de " + juce::String(layerNames[l]));
        controls.sampleRoot.setTooltip("Nota original del sample. C4 = MIDI 60. Ajustala para afinarlo con el patron. No se detecta automaticamente.");
        controls.sampleLabel.setFont(juce::FontOptions(11.0f));
        controls.importSample.setTooltip("Cargar WAV/AIFF mono o estereo: sin limite fijo de segundos. Los archivos grandes se vinculan al preset.");
        controls.removeSample.setTooltip("Volver a sintesis y quitar el sample de esta capa. No borra tu archivo original.");
        controls.editSample.onClick = [this, l] { editSample(l); };
        addAndMakeVisible(controls.editSample);
        controls.importSample.onClick = [this, l] { chooseSample(l); };
        controls.removeSample.onClick = [this, l] { owner.clearSample(l); timerCallback(); };
        addAndMakeVisible(controls.source); addAndMakeVisible(controls.sampleRoot); addAndMakeVisible(controls.sampleLabel);
        addAndMakeVisible(controls.importSample); addAndMakeVisible(controls.removeSample);
        controls.sourceAttachment = std::make_unique<ComboAttachment>(owner.parameters, ZazzaProcessor::layerId(l, "source"), controls.source);
        controls.rootAttachment = std::make_unique<ComboAttachment>(owner.parameters, ZazzaProcessor::layerId(l, "sampleRoot"), controls.sampleRoot);
        controls.waveAttachment = std::make_unique<ComboAttachment>(owner.parameters, ZazzaProcessor::layerId(l, "wave"), controls.wave);
        controls.enabledAttachment = std::make_unique<ButtonAttachment>(owner.parameters, ZazzaProcessor::layerId(l, "enabled"), controls.enabled);
        const char* layerParams[] {"gain", "cutoff", "decay", "pan", "octave"};
        const char* layerLabels[] {"NIVEL", "FILTRO Hz", "CAIDA s", "PAN", "OCTAVA"};
        for (size_t k = 0; k < controls.knobs.size(); ++k) setupKnob(controls.knobs[k], ZazzaProcessor::layerId(l, layerParams[k]), layerLabels[k], true);
        for (int s = 0; s < zazza::steps; ++s) {
            auto& cell = cells[static_cast<size_t>(l)][static_cast<size_t>(s)];
            cell.setTooltip("Clic: activar/desactivar. Shift + clic: seleccionar sin cambiar. Edita grado, velocidad y duracion abajo.");
            cell.onClick = [this, l, s] {
                selectStep(l, s);
                if (!juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()) {
                    const auto id = ZazzaProcessor::stepId(l, s, "on"); owner.setControlValue(id, owner.value(id) > 0.5f ? 0.0f : 1.0f);
                }
                timerCallback();
            };
            addAndMakeVisible(cell);
        }
    }
    sequenceAttachment = std::make_unique<ButtonAttachment>(owner.parameters, "sequencer", sequence);
    sequence.setTooltip("Activado: patrones sincronizados al DAW. Desactivado: toca las tres capas desde tu teclado MIDI; notas originales sin cuantizar.");
    listen.onClick = [this] {
        owner.preview.store(!owner.preview.load());
        if (owner.preview.load()) owner.setControlValue("sequencer", 1.0f);
    };
    listen.setTooltip("Escucha sin iniciar el DAW. Con transporte activo manda el tempo y la posicion del DAW.");
    panic.onClick = [this] { owner.preview.store(false); owner.setControlValue("sequencer", 0.0f); owner.panicRequested.store(true); };
    panic.setTooltip("Detiene la escucha y desactiva el secuenciador. Activa PATRON para seguir al DAW otra vez.");
    generate.onClick = [this] { owner.generateIdea(true); message.setText("Nueva idea: armonia, sonidos y mezcla coordinados. Tus samples se conservan.", juce::dontSendNotification); };
    generate.setTooltip("Genera patrones, timbres internos, filtros, niveles, panorama y swing. Conserva samples, nota raiz, tonalidad, tempo y salida.");
    apply.onClick = [this] { owner.generatePattern(true); message.setText("Patron nuevo; sonidos, samples y mezcla conservados.", juce::dontSendNotification); };
    exportButton.onClick = [this] { chooseFile(0); }; save.onClick = [this] { chooseFile(1); }; load.onClick = [this] { chooseFile(2); };
    for (auto* button : {&listen, &panic, &generate, &apply, &exportButton, &save, &load}) addAndMakeVisible(*button);
    addAndMakeVisible(sequence); addAndMakeVisible(message);
    message.setFont(juce::FontOptions(12.0f)); message.setColour(juce::Label::textColourId, neon);
    message.setText("Elige estilo + escala y pulsa NUEVA IDEA. Puedes importar tus samples por capa.", juce::dontSendNotification);
    selectStep(0, 0); setSize(1120, 894); startTimerHz(24); timerCallback();
}
ZazzaEditor::~ZazzaEditor() { stopTimer(); setLookAndFeel(nullptr); }
void ZazzaEditor::selectStep(int layer, int step) {
    selectedLayer = layer; selectedStep = step;
    const char* ids[] {"degree", "velocity", "gate"};
    const char* names[] {"GRADO (0 = TONICA)", "VELOCIDAD", "DURACION"};
    for (size_t i = 0; i < stepControls.size(); ++i) {
        stepControls[i].attachment.reset(); setupKnob(stepControls[i], ZazzaProcessor::stepId(layer, step, ids[i]), names[i]);
    }
    repaint();
}
void ZazzaEditor::placeKnob(Knob& k, juce::Rectangle<int> area) {
    k.label.setBounds(area.removeFromTop(18)); k.slider.setBounds(area);
}
void ZazzaEditor::resized() {
    listen.setBounds(30, 106, 124, 34); panic.setBounds(164, 106, 62, 34); sequence.setBounds(239, 106, 210, 34);
    choices[0].setBounds(36, 190, 100, 30); choices[1].setBounds(148, 190, 173, 30); choices[2].setBounds(333, 190, 170, 30);
    apply.setBounds(520, 190, 162, 30); generate.setBounds(698, 184, 182, 40); exportButton.setBounds(898, 190, 186, 30);
    for (size_t i = 0; i < globals.size(); ++i) placeKnob(globals[i], {30 + static_cast<int>(i) * 178, 231, 168, 48});
    for (size_t l = 0; l < layerControls.size(); ++l) {
        const auto x = 30 + static_cast<int>(l) * 364;
        auto& c = layerControls[l]; c.enabled.setBounds(x + 283, 307, 56, 25);
        c.wave.setBounds(x + 8, 339, 100, 29);
        c.editSample.setBounds(x + 113, 339, 110, 29);
        c.importSample.setBounds(x + 229, 339, 103, 29);
        c.source.setBounds(x + 8, 376, 108, 27);
        c.sampleRoot.setBounds(x + 170, 376, 68, 27);
        c.removeSample.setBounds(x + 249, 376, 83, 27);
        c.sampleLabel.setBounds(x + 8, 405, 324, 18);
        for (size_t k = 0; k < c.knobs.size(); ++k) placeKnob(c.knobs[k], {x + static_cast<int>(k) * 68, 423, 68, 90});
        for (size_t s = 0; s < cells[l].size(); ++s) cells[l][s].setBounds(124 + static_cast<int>(s) * 30, 584 + static_cast<int>(l) * 42, 27, 34);
    }
    for (size_t i = 0; i < stepControls.size(); ++i) placeKnob(stepControls[i], {278 + static_cast<int>(i) * 174, 765, 160, 57});
    save.setBounds(830, 779, 120, 32); load.setBounds(962, 779, 120, 32);
    message.setBounds(26, 854, 830, 24);
    if (sampleEditor) sampleEditor->setBounds(20, 294, 1080, 540);
}
void ZazzaEditor::timerCallback() {
    listen.setButtonText(owner.preview.load() ? "ESCUCHANDO" : "ESCUCHAR"); listen.setToggleState(owner.preview.load(), juce::dontSendNotification);
    for (int layer = 0; layer < zazza::layers; ++layer) {
        auto& controls = layerControls[static_cast<size_t>(layer)];
        const auto name = owner.sampleName(layer);
        controls.sampleLabel.setText(name.isEmpty() ? "Arrastra WAV / AIFF a esta capa" : name, juce::dontSendNotification);
        controls.sampleLabel.setTooltip(name.isEmpty() ? "Usa preferentemente una nota individual, sin silencio inicial." : name);
        const bool sampleMode = owner.value(ZazzaProcessor::layerId(layer, "source")) > 0.5f;
        controls.wave.setEnabled(!sampleMode); controls.sampleRoot.setEnabled(sampleMode);
        const auto asset = owner.sampleAsset(layer);
        controls.importSample.setButtonText(asset && asset->missing() ? "REUBICAR" : "IMPORTAR");
        controls.sampleLabel.setTooltip(asset && asset->external() ? "Archivo vinculado: " + asset->sourcePath : name);
        controls.removeSample.setEnabled(!name.isEmpty()); controls.editSample.setEnabled(!name.isEmpty());
    }
    const auto p = owner.pattern();
    const auto scale = static_cast<zazza::Scale>(static_cast<int>(owner.value("scale")));
    const auto root = static_cast<int>(owner.value("root"));
    for (int l = 0; l < zazza::layers; ++l) for (int s = 0; s < zazza::steps; ++s) {
        auto& cell = cells[static_cast<size_t>(l)][static_cast<size_t>(s)]; const auto& step = p[static_cast<size_t>(l)][static_cast<size_t>(s)];
        const auto notes = zazza::notesForStep(l, step, root, scale, static_cast<int>(owner.value(ZazzaProcessor::layerId(l, "octave"))));
        const auto name = juce::MidiMessage::getMidiNoteName(notes[0], true, true, 4);
        cell.setButtonText(step.on ? name : "-"); cell.setToggleState(step.on, juce::dontSendNotification);
        cell.setName(juce::String(layerNames[l]) + " paso " + juce::String(s + 1) + " " + name + (step.on ? " activo" : " apagado"));
    }
    repaint();
}
void ZazzaEditor::paint(juce::Graphics& g) {
    g.fillAll(dark);
    g.drawImage(wallpaper, juce::Rectangle<float>(0, -180, 1120, 630), juce::RectanglePlacement::fillDestination);
    g.setGradientFill(juce::ColourGradient(dark.withAlpha(0.94f), 0.0f, 150.0f, dark.withAlpha(0.22f), 1100.0f, 0.0f, false));
    g.fillAll();
    const auto title = [this, &g](const juce::String& text, juce::Rectangle<int> area, float size, juce::Colour colour) {
        g.setFont(theme.displayFont(size)); g.setColour(colour); g.drawText(text, area, juce::Justification::centredLeft);
    };
    title("ZAZZA", {30, 19, 350, 65}, 63.0f, cream);
    label(g, "LESS PROGRAMMING. MORE GROOVE.", {33, 79, 390, 20}, 13.0f, neon);
    label(g, "HOUSE  /  TECH  /  MINIMAL  /  DEEP  /  ROLLIN", {653, 111, 430, 24}, 12.0f, cream, juce::Justification::centredRight);
    const auto panel = [&g](juce::Rectangle<float> area) {
        g.setColour(dark.withAlpha(0.94f)); g.fillRoundedRectangle(area, 12.0f);
        g.setColour(neon.withAlpha(0.2f)); g.drawRoundedRectangle(area, 12.0f, 1.0f);
    };
    panel({20, 160, 1080, 126});
    label(g, "TONALIDAD", {36, 167, 100, 20}, 11.0f, neon); label(g, "ESCALA", {148, 167, 170, 20}, 11.0f, neon);
    label(g, "ESTILO", {333, 167, 170, 20}, 11.0f, neon);
    for (int l = 0; l < zazza::layers; ++l) {
        const auto x = 20 + l * 364; panel({static_cast<float>(x), 298, 352, 228});
        label(g, "RAIZ", {x + 136, 379, 43, 20}, 10.0f, neon);
        title("0" + juce::String(l + 1) + "  " + layerNames[l], {x + 18, 306, 265, 26}, 20.0f, cream);
    }
    panel({20, 538, 1080, 208});
    title("GROOVE LAB", {36, 547, 230, 26}, 21.0f, cream);
    label(g, "2 COMPASES  /  32 PASOS  /  1/16", {366, 551, 290, 20}, 12.0f, neon);
    label(g, "CLIC: ON/OFF   |   SHIFT + CLIC: EDITAR", {760, 551, 323, 20}, 11.0f, cream, juce::Justification::centredRight);
    for (int l = 0; l < zazza::layers; ++l) label(g, l == 0 ? "BASS" : l == 1 ? "CHORD" : "TOP", {37, 586 + l * 42, 80, 28}, 12.0f, neon);
    for (int s = 0; s < zazza::steps; ++s) {
        label(g, juce::String(s + 1), {124 + s * 30, 712, 27, 16}, 10.0f, cream.withAlpha(s % 4 == 0 ? 0.95f : 0.35f), juce::Justification::centred);
        if (s % 4 == 0) { g.setColour(neon.withAlpha(0.18f)); g.drawVerticalLine(120 + s * 30, 583.0f, 705.0f); }
    }
    const auto current = owner.playingStep.load();
    if (current >= 0) { g.setColour(neon); g.fillRoundedRectangle(static_cast<float>(124 + current * 30), 576.0f, 27.0f, 3.0f, 1.5f); }
    g.setColour(cream.withAlpha(0.9f));
    g.drawRoundedRectangle(cells[static_cast<size_t>(selectedLayer)][static_cast<size_t>(selectedStep)].getBounds().toFloat().expanded(1.0f), 5.0f, 1.2f);
    panel({20, 758, 1080, 82});
    title("PASO " + juce::String(selectedStep + 1), {37, 770, 205, 24}, 20.0f, cream);
    label(g, layerNames[selectedLayer], {37, 798, 214, 20}, 12.0f, neon);
    g.setColour(dark.withAlpha(0.9f)); g.fillRect(0, 848, getWidth(), 46);
    const auto status = owner.hostRunning.load() ? "DAW" : owner.preview.load() ? "PREVIEW" : "READY";
    label(g, juce::String(status) + "  /  " + juce::String(owner.currentTempo.load(), 1) + " BPM", {853, 855, 230, 20}, 12.0f, neon, juce::Justification::centredRight);
    g.setColour(neon.withAlpha(0.2f)); g.fillRect(860, 879, 224, 3);
    g.setColour(neon); g.fillRect(860, 879, juce::roundToInt(224.0f * juce::jlimit(0.0f, 1.0f, owner.peak.load())), 3);
}
void ZazzaEditor::chooseFile(int operation) {
    const bool importing = operation == 2;
    chooser = std::make_unique<juce::FileChooser>(importing ? "Abrir preset Zazza" : operation == 0 ? "Exportar MIDI de 2 compases" : "Guardar preset Zazza",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(operation == 0 ? "Zazza.mid" : "Groove.zazza"), operation == 0 ? "*.mid" : "*.zazza");
    const auto flags = importing ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                 : juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync(flags, [safe = juce::Component::SafePointer<ZazzaEditor>(this), operation](const juce::FileChooser& dialog) {
        if (safe == nullptr) return;
        const auto destination = dialog.getResult(); if (destination == juce::File()) return;
        bool ok = false;
        if (operation == 2) {
            juce::MemoryBlock block;
            ok = destination.getSize() <= ZazzaProcessor::maxStateBytes && destination.loadFileAsData(block)
                 && safe->owner.restoreState(block.getData(), static_cast<int>(block.getSize()));
        } else {
            juce::MemoryOutputStream output;
            if (operation == 0) ok = safe->owner.exportMidi().writeTo(output, 1);
            else { juce::MemoryBlock block; safe->owner.getStateInformation(block); ok = output.write(block.getData(), block.getSize()); }
            if (ok) ok = destination.replaceWithData(output.getData(), output.getDataSize());
        }
        safe->message.setText(ok ? (operation == 2 ? "Preset cargado." : "Guardado: " + destination.getFileName()) : "No se pudo completar. Revisa el archivo y los permisos.", juce::dontSendNotification);
    });
}

bool ZazzaEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    return files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;aif;aiff");
}
void ZazzaEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    if (!isInterestedInFileDrag(files)) return;
    if (x < 20 || x >= 1100 || y < 298 || y >= 526) {
        message.setText("Suelta el sample sobre una de las tres capas.", juce::dontSendNotification); return;
    }
    importSampleFile(juce::jlimit(0, 2, (x - 20) / 364), juce::File(files[0]));
}
void ZazzaEditor::importSampleFile(int layer, const juce::File& file) {
    const auto current = owner.sampleAsset(layer);
    const auto result = current && current->missing() ? owner.relinkSample(layer, file) : owner.loadSample(layer, file);
    message.setText(result.wasOk() ? "Sample completo a velocidad original. Elige un slice y escucha antes de generar." : result.getErrorMessage(), juce::dontSendNotification);
    timerCallback();
    if (result.wasOk()) editSample(layer);
}
void ZazzaEditor::editSample(int layer) {
    sampleEditor = std::make_unique<SampleEditor>(owner, layer, [safe = juce::Component::SafePointer<ZazzaEditor>(this)] {
        juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->sampleEditor.reset(); });
    });
    addAndMakeVisible(*sampleEditor); sampleEditor->setBounds(20, 294, 1080, 540); sampleEditor->toFront(true);
}
void ZazzaEditor::chooseSample(int layer) {
    chooser = std::make_unique<juce::FileChooser>("Importar o reubicar audio WAV/AIFF", juce::File(), "*.wav;*.aif;*.aiff");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<ZazzaEditor>(this), layer](const juce::FileChooser& dialog) {
            if (safe != nullptr && dialog.getResult() != juce::File()) safe->importSampleFile(layer, dialog.getResult());
        });
}
