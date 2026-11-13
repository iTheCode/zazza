#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace zazza {
// Frozen contract of Zazza 1.0.0. Do not derive this list from the current
// parameter count or future layer/step counts: old sessions must stay readable.
inline juce::StringArray schemaOneRequiredIds() {
    juce::StringArray result {"root", "scale", "style", "density", "swing", "tempo", "master", "duck", "drive", "seed", "sequencer"};
    for (int layer = 0; layer < 3; ++layer) {
        const auto prefix = "layer" + juce::String(layer) + "_";
        for (const auto* name : {"gain", "cutoff", "decay", "pan", "wave", "octave", "enabled"}) result.add(prefix + name);
        for (int step = 0; step < 32; ++step)
            for (const auto* name : {"on", "degree", "velocity", "gate"}) result.add(prefix + "step" + juce::String(step) + "_" + name);
    }
    return result;
}

// Call only after checking types, known IDs, finite values, ranges and duplicates.
// Preserve all v1 fields; fill additive parameters from factory defaults rather
// than the previously loaded session. Incomplete v1 states remain invalid.
inline bool completeValidatedSchemaOneState(juce::ValueTree& state, const juce::ValueTree& defaults) {
    for (const auto& id : schemaOneRequiredIds())
        if (!state.getChildWithProperty("id", id).isValid()) return false;
    for (const auto& parameter : defaults)
        if (!state.getChildWithProperty("id", parameter.getProperty("id")).isValid())
            state.addChild(parameter.createCopy(), -1, nullptr);
    return true;
}
}
