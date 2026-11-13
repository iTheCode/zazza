#pragma once
#include "MusicEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace zazza {
struct SampleAsset {
    static constexpr size_t maxBytes = 8 * 1024 * 1024;
    juce::AudioBuffer<float> audio;
    juce::MemoryBlock encoded;
    juce::String name;
    double rate = 48000.0;
    bool legacyGain = false;
    SampleView view(int rootNote) const {
        return {audio.getReadPointer(0), audio.getNumChannels() > 1 ? audio.getReadPointer(1) : nullptr,
                audio.getNumSamples(), rootNote, rate, true};
    }
    static std::shared_ptr<const SampleAsset> decode(const juce::MemoryBlock&, const juce::String& name, juce::String& error, bool legacyGain = false);
};
}
