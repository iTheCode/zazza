#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MusicEngine.h"
#include "SampleAsset.h"

class ZazzaProcessor final : public juce::AudioProcessor {
public:
    ZazzaProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Zazza"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.25; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Custom groove"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    bool restoreState(const void*, int);
    void generatePattern(bool newSeed);
    void generateIdea(bool newSeed);
    void applyStyle(int style);
    juce::Result loadSample(int layer, const juce::File&);
    juce::Result loadSampleData(int layer, const juce::MemoryBlock&, const juce::String& name);
    void clearSample(int layer);
    juce::String sampleName(int layer) const;
    static constexpr int maxStateBytes = 40 * 1024 * 1024;
    void setControlValue(const juce::String&, float);
    float value(const juce::String&) const;
    zazza::Pattern pattern() const;
    juce::MidiFile exportMidi() const;
    static juce::String layerId(int, const juce::String&);
    static juce::String stepId(int, int, const juce::String&);
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<bool> preview { false };
    std::atomic<int> playingStep { -1 };
    std::atomic<float> peak { 0.0f };
    std::atomic<double> currentTempo { 126.0 };
    std::atomic<bool> hostRunning { false };
    std::atomic<bool> panicRequested { false };
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    struct StepRefs { std::atomic<float> *on, *degree, *velocity, *gate; };
    struct LayerRefs {
        std::atomic<float> *gain, *cutoff, *decay, *pan, *wave, *octave, *enabled, *source, *sampleRoot;
        std::array<StepRefs, zazza::steps> steps;
    };
    std::array<LayerRefs, zazza::layers> refs;
    std::atomic<float> *root, *scale, *swing, *tempo, *master, *duck, *drive, *sequencer;
    zazza::Synth synth;
    // Mutated only under the callback lock, after decoding outside it. The audio
    // thread uses raw views while holding that lock; it never destroys assets.
    std::array<std::shared_ptr<const zazza::SampleAsset>, zazza::layers> samples;
    std::array<int, 3> soundingStep {{-1, -1, -1}};
    std::array<std::array<int, 4>, 3> soundingNotes {};
    juce::MidiBuffer generatedMidi;
    double rate = 48000.0, previewPpq = 0.0, expectedPpq = 0.0;
    bool wasRunning = false, wasPreview = false, wasSequencer = true;
    float masterSmoothed = 0.7f;
    std::atomic<bool> stateChanged { false };
    void stopGenerated(int sample);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZazzaProcessor)
};
