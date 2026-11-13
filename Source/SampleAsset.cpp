#include "SampleAsset.h"
#include <juce_cryptography/juce_cryptography.h>
#include <cmath>
#include <limits>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace zazza {
namespace {
void registerFormats(juce::AudioFormatManager& formats) {
    formats.registerFormat(new juce::WavAudioFormat(), true);
    formats.registerFormat(new juce::AiffAudioFormat(), false);
}
std::shared_ptr<SampleAsset> readAudio(juce::AudioFormatReader& reader, const juce::String& name, juce::String& error, bool legacyGain) {
    const auto fail = [&error](const juce::String& message) -> std::shared_ptr<SampleAsset> { error = message; return {}; };
    if (!std::isfinite(reader.sampleRate) || reader.sampleRate < 8000.0 || reader.sampleRate > 192000.0
        || reader.numChannels < 1 || reader.numChannels > 2 || reader.lengthInSamples < 64
        || reader.lengthInSamples > std::numeric_limits<int>::max())
        return fail("Usa WAV/AIFF mono o estereo entre 8 y 192 kHz, compatible con el motor de audio.");
    // No duration limit. Bound allocations by the machine's RAM, not seconds.
    const auto decodedBytes = static_cast<juce::uint64>(reader.lengthInSamples) * reader.numChannels * sizeof(float);
    const auto ram = static_cast<juce::uint64>(juce::SystemStats::getMemorySizeInMegabytes()) * 1024 * 1024;
    if (ram > 0 && decodedBytes > ram / 4)
        return fail("El audio decodificado supera el presupuesto de memoria por sample (25% de la RAM). Usa menos canales o resolucion.");
    try {
        auto sample = std::make_shared<SampleAsset>();
        sample->audio.setSize(static_cast<int>(reader.numChannels), static_cast<int>(reader.lengthInSamples));
        if (!reader.read(&sample->audio, 0, sample->audio.getNumSamples(), 0, true, true)) return fail("El archivo de audio esta incompleto.");
        float peak = 0.0f;
        for (int channel = 0; channel < sample->audio.getNumChannels(); ++channel)
            for (int i = 0; i < sample->audio.getNumSamples(); ++i) {
                const auto value = sample->audio.getSample(channel, i);
                if (!std::isfinite(value) || std::abs(value) > 16.0f) return fail("El sample contiene valores de audio invalidos o fuera de rango.");
                peak = std::max(peak, std::abs(value));
            }
        if (peak < 1e-6f) return fail("El sample no contiene audio audible.");
        sample->legacyGain = legacyGain;
        if (legacyGain && peak > 0.8f) sample->audio.applyGain(0.8f / peak);
        sample->name = name.substring(0, 256);
        sample->rate = reader.sampleRate;
        return sample;
    } catch (const std::bad_alloc&) {
        return fail("No hay memoria disponible para cargar este sample completo.");
    }
}
}
bool SampleAsset::hasLocalPathSyntax(const juce::String& path, bool windows) {
    const auto normalized = path.replaceCharacter('\\', '/');
    // Reject UNC, mixed-slash UNC and Windows device/extended namespaces before
    // constructing a File or querying its attributes. Hashing is already I/O.
    if (normalized.startsWith("//")) return false;
    if (windows)
        return normalized.length() >= 3 && juce::String("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz").containsChar(normalized[0])
            && normalized[1] == ':' && normalized[2] == '/';
    return path.startsWithChar('/');
}
bool SampleAsset::canAutoRestorePath(const juce::String& path) {
   #if JUCE_WINDOWS
    if (!hasLocalPathSyntax(path, true)) return false;
    // Check only a drive root, never a path supplied by the preset. Mapped
    // network drives must also be selected explicitly with REUBICAR.
    const auto root = path.substring(0, 2) + "\\";
    const auto type = GetDriveTypeW(root.toWideCharPointer());
    return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_CDROM || type == DRIVE_RAMDISK;
   #else
    return hasLocalPathSyntax(path, false);
   #endif
}
std::shared_ptr<const SampleAsset> SampleAsset::decode(const juce::MemoryBlock& bytes, const juce::String& name, juce::String& error, bool legacyGain) {
    if (bytes.getSize() == 0 || bytes.getSize() > maxEmbeddedBytes) { error = "Audio incrustado invalido. Usa Importar para archivos grandes."; return {}; }
    juce::AudioFormatManager formats; registerFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(bytes, false)));
    if (!reader) { error = "No se pudo leer este WAV/AIFF."; return {}; }
    auto sample = readAudio(*reader, name, error, legacyGain);
    if (sample) sample->encoded = bytes;
    return sample;
}
std::shared_ptr<const SampleAsset> SampleAsset::fromFile(const juce::File& file, juce::String& error, bool legacyGain, const juce::String& expectedHash) {
    if (!file.existsAsFile() || file.getSize() <= 0) { error = "No se encuentra el archivo de audio: " + file.getFullPathName(); return {}; }
    auto stream = file.createInputStream();
    if (!stream) { error = "No se pudo abrir el archivo de audio."; return {}; }
    const auto hash = juce::SHA256(*stream).toHexString();
    if (expectedHash.isNotEmpty() && hash != expectedHash) { error = "El archivo no coincide con el sample guardado. Selecciona el original o quitalo para cargar otro sonido."; return {}; }
    if (file.getSize() <= static_cast<juce::int64>(maxEmbeddedBytes)) {
        juce::MemoryBlock bytes;
        if (!file.loadFileAsData(bytes) || juce::SHA256(bytes).toHexString() != hash) { error = "El archivo cambio mientras se leia."; return {}; }
        return decode(bytes, file.getFileName(), error, legacyGain);
    }
    juce::AudioFormatManager formats; registerFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader) { error = "No se pudo leer este WAV/AIFF."; return {}; }
    auto sample = readAudio(*reader, file.getFileName(), error, legacyGain);
    if (!sample) return {};
    if (juce::SHA256(file).toHexString() != hash) { error = "El archivo cambio mientras se leia."; return {}; }
    sample->sourcePath = file.getFullPathName(); sample->sourceHash = hash;
    return sample;
}
}
