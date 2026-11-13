#include "SampleAsset.h"
#include <cmath>

namespace zazza {
std::shared_ptr<const SampleAsset> SampleAsset::decode(const juce::MemoryBlock& bytes, const juce::String& name, juce::String& error) {
    const auto fail = [&error](const juce::String& message) -> std::shared_ptr<const SampleAsset> { error = message; return {}; };
    if (bytes.getSize() == 0 || bytes.getSize() > maxBytes) return fail("Usa un WAV/AIFF de hasta 8 MiB.");
    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    formats.registerFormat(new juce::AiffAudioFormat(), false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(bytes, false)));
    if (!reader) return fail("No se pudo leer este WAV/AIFF.");
    if (!std::isfinite(reader->sampleRate) || reader->sampleRate < 8000.0 || reader->sampleRate > 192000.0
        || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples < 64
        || static_cast<double>(reader->lengthInSamples) > reader->sampleRate * 8.0)
        return fail("Usa un sample mono/estereo de hasta 8 segundos, entre 8 y 192 kHz.");
    auto sample = std::make_shared<SampleAsset>();
    sample->audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&sample->audio, 0, sample->audio.getNumSamples(), 0, true, true)) return fail("El archivo de audio esta incompleto.");
    float peak = 0.0f;
    for (int channel = 0; channel < sample->audio.getNumChannels(); ++channel)
        for (int i = 0; i < sample->audio.getNumSamples(); ++i) {
            const auto value = sample->audio.getSample(channel, i);
            if (!std::isfinite(value)) return fail("El sample contiene valores de audio invalidos.");
            peak = std::max(peak, std::abs(value));
        }
    if (peak < 1e-6f) return fail("El sample no contiene audio audible.");
    // Leave quiet recordings unchanged; reserve headroom for polyphonic samples.
    if (peak > 0.8f) sample->audio.applyGain(0.8f / peak);
    sample->encoded = bytes;
    sample->name = name.substring(0, 256);
    sample->rate = reader->sampleRate;
    return sample;
}
}
