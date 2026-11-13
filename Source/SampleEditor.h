#pragma once
#include "PluginProcessor.h"

// Non-destructive sample selection. Owned by the plugin editor, never by the audio thread.
class SampleEditor final : public juce::Component, private juce::Timer {
public:
    SampleEditor(ZazzaProcessor&, int layer, std::function<void()> close);
    ~SampleEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    class Waveform final : public juce::Component {
    public:
        std::shared_ptr<const zazza::SampleAsset> asset;
        float begin = 0, end = 1, playhead = -1;
        double viewStart = 0, span = 1;
        std::function<void(float, float)> select;
        void paint(juce::Graphics&) override;
        void resized() override { rebuild(); }
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void rebuild();
    private:
        juce::Image cache;
        bool draggingStart = true;
        float position(float x) const;
    } wave;
    ZazzaProcessor& owner;
    int layer;
    std::shared_ptr<const zazza::SampleAsset> asset;
    juce::Slider begin, end, zoom, scroll;
    juce::ComboBox root, playback, pitch;
    juce::Label title, info, help, beginLabel, endLabel, rootLabel, zoomLabel;
    juce::TextButton listen {"ESCUCHAR SLICE"}, stop {"PARAR"}, full {"TODO EL AUDIO"}, closeButton {"CERRAR"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rootAttachment, playbackAttachment, pitchAttachment;
    void selection(float, float);
    void timerCallback() override;
    void updateView();
};
