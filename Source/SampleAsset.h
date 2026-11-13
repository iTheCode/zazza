#pragma once
#include "MusicEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace zazza {
struct SampleAsset {
    static constexpr size_t maxEmbeddedBytes = 8 * 1024 * 1024;
    juce::AudioBuffer<float> audio;
    juce::MemoryBlock encoded;
    juce::String name;
    double rate = 48000.0;
    bool legacyGain = false;
    juce::String sourcePath, sourceHash;
    bool missing() const { return audio.getNumSamples() == 0; }
    bool external() const { return sourcePath.isNotEmpty(); }
    // Lexical check never touches the filesystem; test both platform syntaxes.
    static bool hasLocalPathSyntax(const juce::String&, bool windows);
    static bool canAutoRestorePath(const juce::String&);
    SampleView view(int rootNote) const {
        if (missing()) { SampleView empty; empty.enabled = true; return empty; }
        return {audio.getReadPointer(0), audio.getNumChannels() > 1 ? audio.getReadPointer(1) : nullptr,
                audio.getNumSamples(), rootNote, rate, true};
    }
    static std::shared_ptr<const SampleAsset> fromFile(const juce::File&, juce::String& error, bool legacyGain = false,
                                                       const juce::String& expectedHash = {});
    static std::shared_ptr<const SampleAsset> decode(const juce::MemoryBlock&, const juce::String& name, juce::String& error, bool legacyGain = false);
};
}
