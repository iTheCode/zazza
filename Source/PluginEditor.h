#pragma once
#include "PluginProcessor.h"
#include "SampleEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ZazzaLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    ZazzaLookAndFeel();
    juce::Font displayFont(float height) const;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
private:
    juce::Typeface::Ptr typeface;
};
class ZazzaEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit ZazzaEditor(ZazzaProcessor&);
    ~ZazzaEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };
    struct LayerControls {
        juce::ComboBox wave;
        juce::ComboBox source, sampleRoot;
        juce::TextButton importSample { "IMPORTAR" }, removeSample { "QUITAR" }, editSample { "EDITAR SLICE" };
        juce::Label sampleLabel;
        juce::ToggleButton enabled { "ON" };
        std::unique_ptr<ComboAttachment> waveAttachment;
        std::unique_ptr<ComboAttachment> sourceAttachment, rootAttachment;
        std::unique_ptr<ButtonAttachment> enabledAttachment;
        std::array<Knob, 5> knobs;
    };
    ZazzaProcessor& owner;
    ZazzaLookAndFeel theme;
    juce::Image wallpaper;
    juce::TooltipWindow tooltip {this, 500};
    std::array<juce::ComboBox, 3> choices;
    std::array<std::unique_ptr<ComboAttachment>, 3> choiceAttachments;
    std::array<Knob, 6> globals;
    std::array<LayerControls, 3> layerControls;
    std::array<std::array<juce::TextButton, 32>, 3> cells;
    std::array<Knob, 3> stepControls;
    juce::ToggleButton sequence { "PATRON / MIDI LIVE" };
    std::unique_ptr<ButtonAttachment> sequenceAttachment;
    juce::TextButton listen { "ESCUCHAR" }, panic { "STOP" }, generate { "NUEVA IDEA" }, apply { "VARIAR PATRON" },
        exportButton { "EXPORTAR MIDI" }, save { "GUARDAR" }, load { "ABRIR" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<SampleEditor> sampleEditor;
    void editSample(int layer);
    juce::Label message;
    int selectedLayer = 0, selectedStep = 0;
    void timerCallback() override;
    void setupKnob(Knob&, const juce::String&, const juce::String&, bool rotary = false);
    static void placeKnob(Knob&, juce::Rectangle<int>);
    void selectStep(int, int);
    void chooseFile(int operation);
    void chooseSample(int layer);
    void importSampleFile(int layer, const juce::File&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZazzaEditor)
};
