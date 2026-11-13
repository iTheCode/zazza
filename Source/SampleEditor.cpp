#include "SampleEditor.h"
#include <cmath>

namespace {
const juce::Colour blue(0xff26baff), background(0xff101820), cream(0xfff4e9ce);
}
float SampleEditor::Waveform::position(float x) const {
    return static_cast<float>(juce::jlimit(0.0, 1.0, viewStart + span * x / juce::jmax(1, getWidth())));
}
void SampleEditor::Waveform::rebuild() {
    if (getWidth() <= 0 || getHeight() <= 0) return;
    cache = juce::Image(juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g(cache);
    if (!asset || asset->missing()) return;
    const auto& audio = asset->audio;
    const int channels = audio.getNumChannels(), length = audio.getNumSamples();
    for (int c = 0; c < channels; ++c) {
        const float height = static_cast<float>(getHeight()) / static_cast<float>(channels);
        const float centre = (static_cast<float>(c) + 0.5f) * height;
        g.setColour(blue.withAlpha(0.15f)); g.drawHorizontalLine(static_cast<int>(centre), 0, static_cast<float>(getWidth()));
        g.setColour(blue);
        for (int x = 0; x < getWidth(); ++x) {
            const int first = juce::jlimit(0, length - 1, static_cast<int>(position(static_cast<float>(x)) * static_cast<double>(length)));
            const int last = juce::jlimit(first + 1, length, static_cast<int>(position(static_cast<float>(x + 1)) * static_cast<double>(length)));
            const auto range = audio.findMinMax(c, first, last - first);
            g.drawVerticalLine(x, centre - range.getEnd() * height * 0.45f, centre - range.getStart() * height * 0.45f + 1);
        }
    }
}
void SampleEditor::Waveform::paint(juce::Graphics& g) {
    g.fillAll(background.brighter(0.1f));
    g.drawImageAt(cache, 0, 0);
    const auto x = [this](float fraction) { return static_cast<float>((fraction - viewStart) / span * getWidth()); };
    g.setColour(blue.withAlpha(0.18f));
    g.fillRect(juce::Rectangle<float>(x(begin), 0, std::max(0.0f, x(end) - x(begin)), static_cast<float>(getHeight())));
    g.setColour(cream);
    g.drawVerticalLine(juce::roundToInt(x(begin)), 0, static_cast<float>(getHeight()));
    g.drawVerticalLine(juce::roundToInt(x(end)) - 1, 0, static_cast<float>(getHeight()));
    g.setFont(juce::FontOptions(12));
    g.drawText("INICIO", juce::jlimit(0, getWidth() - 60, juce::roundToInt(x(begin)) + 4), 3, 60, 18, juce::Justification::left);
    g.drawText("FIN", juce::jlimit(0, getWidth() - 35, juce::roundToInt(x(end)) - 35), getHeight() - 22, 35, 18, juce::Justification::left);
    if (playhead >= 0) { g.setColour(juce::Colours::white); g.drawVerticalLine(juce::roundToInt(x(playhead)), 0, static_cast<float>(getHeight())); }
}
void SampleEditor::Waveform::mouseDown(const juce::MouseEvent& e) {
    const auto p = position(e.position.x);
    draggingStart = e.mods.isShiftDown() ? false : std::abs(p - begin) <= std::abs(p - end);
    mouseDrag(e);
}
void SampleEditor::Waveform::mouseDrag(const juce::MouseEvent& e) {
    if (!asset || asset->missing() || !select) return;
    const float minimum = 2.0f / static_cast<float>(asset->audio.getNumSamples());
    const auto p = position(e.position.x);
    if (draggingStart) select(juce::jlimit(0.0f, std::max(0.0f, end - minimum), p), end);
    else select(begin, juce::jlimit(std::min(1.0f, begin + minimum), 1.0f, p));
}
SampleEditor::SampleEditor(ZazzaProcessor& p, int l, std::function<void()> close) : owner(p), layer(l) {
    setOpaque(true);
    for (auto* label : {&title, &info, &help, &beginLabel, &endLabel, &rootLabel, &zoomLabel}) {
        label->setColour(juce::Label::textColourId, cream); addAndMakeVisible(*label);
    }
    title.setFont(juce::FontOptions(20));
    beginLabel.setText("INICIO (s)", juce::dontSendNotification); endLabel.setText("FIN (s)", juce::dontSendNotification);
    rootLabel.setText("NOTA RAIZ", juce::dontSendNotification); zoomLabel.setText("ZOOM / DESPLAZAR", juce::dontSendNotification);
    help.setText("Arrastra el limite mas cercano; Shift + arrastrar mueve FIN. Completo ignora gate/caida. Original conserva velocidad y tono.", juce::dontSendNotification);
    for (auto* slider : {&begin, &end, &zoom, &scroll}) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal); slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 22);
        addAndMakeVisible(*slider);
    }
    begin.setName("Inicio del slice en segundos"); end.setName("Fin del slice en segundos");
    zoom.setName("Zoom de forma de onda"); scroll.setName("Desplazamiento de forma de onda");
    wave.setName("Forma de onda: arrastra inicio o fin del slice");
    begin.setNumDecimalPlacesToDisplay(4); end.setNumDecimalPlacesToDisplay(4);
    zoom.setRange(1, 32, 1); zoom.setValue(1); scroll.setRange(0, 1, 0); scroll.setNumDecimalPlacesToDisplay(7);
    zoom.onValueChange = [this] {
        const auto span = 1.0 / zoom.getValue();
        const auto centre = (static_cast<double>(wave.begin) + wave.end) * 0.5;
        scroll.setValue(span < 1 ? juce::jlimit(0.0, 1.0, (centre - span * 0.5) / (1.0 - span)) : 0, juce::dontSendNotification);
        updateView();
    }; scroll.onValueChange = [this] { updateView(); };
    begin.onValueChange = [this] { if (asset && !asset->missing()) selection(static_cast<float>(begin.getValue() * asset->rate / asset->audio.getNumSamples()), wave.end); };
    end.onValueChange = [this] { if (asset && !asset->missing()) selection(wave.begin, static_cast<float>(end.getValue() * asset->rate / asset->audio.getNumSamples())); };
    for (int note = 0; note < 128; ++note) root.addItem(juce::MidiMessage::getMidiNoteName(note, true, true, 4), note + 1);
    root.setTooltip("Nota original de la muestra. C4 = MIDI 60. Afinado cambia velocidad y duracion; Original no transpone.");
    playback.addItemList({"Gate / caida", "Completo"}, 1); pitch.addItemList({"Original", "Afinado"}, 1);
    for (auto* combo : {&root, &playback, &pitch}) addAndMakeVisible(*combo);
    using Attachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    rootAttachment = std::make_unique<Attachment>(owner.parameters, ZazzaProcessor::layerId(layer, "sampleRoot"), root);
    playbackAttachment = std::make_unique<Attachment>(owner.parameters, ZazzaProcessor::layerId(layer, "samplePlayback"), playback);
    pitchAttachment = std::make_unique<Attachment>(owner.parameters, ZazzaProcessor::layerId(layer, "samplePitch"), pitch);
    listen.onClick = [this] { owner.auditionSample(layer); };
    listen.setTooltip("Escucha la seleccion a velocidad original, sin filtro, caida ni bombeo. Respeta SALIDA.");
    stop.onClick = [this] { owner.stopSampleAudition(); };
    full.onClick = [this] { selection(0, 1); zoom.setValue(1); scroll.setValue(0); };
    closeButton.onClick = std::move(close);
    for (auto* button : {&listen, &stop, &full, &closeButton}) addAndMakeVisible(*button);
    wave.select = [this](float a, float b) { selection(a, b); };
    addAndMakeVisible(wave); timerCallback(); startTimerHz(20);
}
SampleEditor::~SampleEditor() { stopTimer(); owner.stopSampleAudition(); }
void SampleEditor::selection(float a, float b) {
    if (!asset || asset->missing()) return;
    const float minimum = 2.0f / static_cast<float>(asset->audio.getNumSamples());
    a = juce::jlimit(0.0f, 1.0f - minimum, a);
    b = juce::jlimit(a + minimum, 1.0f, b);
    owner.stopSampleAudition();
    owner.setControlValue(ZazzaProcessor::layerId(layer, "sampleStart"), a);
    owner.setControlValue(ZazzaProcessor::layerId(layer, "sampleEnd"), b);
    timerCallback();
}
void SampleEditor::updateView() {
    wave.span = 1.0 / zoom.getValue(); wave.viewStart = scroll.getValue() * (1.0 - wave.span);
    wave.rebuild(); wave.repaint();
}
void SampleEditor::timerCallback() {
    const auto current = owner.sampleAsset(layer);
    if (current != asset) {
        asset = current; wave.asset = asset;
        title.setText(asset ? asset->name : "Sin sample", juce::dontSendNotification);
        if (asset && !asset->missing()) {
            const double duration = asset->audio.getNumSamples() / asset->rate;
            zoom.setRange(1, std::max(32.0, duration * 100), 0); zoom.setSkewFactor(0.25);
            zoom.setValue(1, juce::dontSendNotification); scroll.setValue(0, juce::dontSendNotification);
            wave.span = 1; wave.viewStart = 0;
            begin.setRange(0, duration, 0); end.setRange(0, duration, 0);
        }
        wave.rebuild();
    }
    wave.begin = owner.value(ZazzaProcessor::layerId(layer, "sampleStart"));
    wave.end = owner.value(ZazzaProcessor::layerId(layer, "sampleEnd"));
    wave.playhead = owner.auditionProgress.load();
    if (asset && !asset->missing()) {
        const double duration = asset->audio.getNumSamples() / asset->rate;
        begin.setValue(wave.begin * duration, juce::dontSendNotification); end.setValue(wave.end * duration, juce::dontSendNotification);
        info.setText("Archivo: " + juce::String(duration, 3) + " s   |   Slice: " + juce::String((wave.end - wave.begin) * duration, 3)
                     + " s   |   " + juce::String(asset->rate / 1000, 1) + " kHz   |   " + juce::String(asset->audio.getNumChannels()) + " canales" + (asset->external() ? "   |   Vinculado al archivo" : "   |   Audio incrustado"), juce::dontSendNotification);
    }
    const bool available = asset && !asset->missing();
    if (!available) info.setText("Archivo no disponible o modificado. Cierra y pulsa REUBICAR para seleccionar el original.", juce::dontSendNotification);
    listen.setEnabled(available); full.setEnabled(available); begin.setEnabled(available); end.setEnabled(available);
    zoom.setEnabled(available); scroll.setEnabled(available);
    wave.repaint();
}
void SampleEditor::paint(juce::Graphics& g) {
    g.fillAll(background); g.setColour(blue); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 10, 1);
}
void SampleEditor::resized() {
    const int width = getWidth();
    title.setBounds(20, 10, width - 150, 28); closeButton.setBounds(width - 120, 10, 100, 28);
    info.setBounds(20, 44, width - 40, 24); wave.setBounds(20, 76, width - 40, 220);
    beginLabel.setBounds(20, 304, 110, 22); begin.setBounds(120, 304, 370, 28);
    endLabel.setBounds(520, 304, 80, 22); end.setBounds(600, 304, width - 620, 28);
    zoomLabel.setBounds(20, 342, 160, 22); zoom.setBounds(180, 340, 300, 28); scroll.setBounds(520, 340, width - 540, 28);
    rootLabel.setBounds(20, 385, 100, 24); root.setBounds(125, 385, 100, 28);
    playback.setBounds(245, 385, 150, 28); pitch.setBounds(415, 385, 125, 28);
    listen.setBounds(560, 385, 160, 30); stop.setBounds(730, 385, 95, 30); full.setBounds(840, 385, width - 860, 30);
    help.setBounds(20, 435, width - 40, 45);
}
