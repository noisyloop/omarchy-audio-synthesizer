// soundgineer-engine: headless synth daemon.
// Phase 1 stub. It proves the JUCE audio/MIDI stack links; the realtime
// engine, socket server and presets land in Phase 4.

#include "sg/version.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <cstring>

namespace {

int listDevices() {
    const juce::ScopedJuceInitialiser_GUI juceInit;  // message manager only, no windows
    juce::AudioDeviceManager manager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    manager.createAudioDeviceTypes(types);
    for (auto* type : types) {
        type->scanForDevices();
        std::printf("%s\n", type->getTypeName().toRawUTF8());
        for (const auto& name : type->getDeviceNames())
            std::printf("  out: %s\n", name.toRawUTF8());
    }
    for (const auto& midi : juce::MidiInput::getAvailableDevices())
        std::printf("midi in: %s\n", midi.name.toRawUTF8());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("soundgineer-engine %s (protocol %d, %s)\n", sg::versionString(),
                    sg::kProtocolVersion, juce::SystemStats::getJUCEVersion().toRawUTF8());
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--list-devices") == 0)
        return listDevices();
    if (argc > 1 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        std::printf("Usage: soundgineer-engine [--version | --list-devices]\n");
        return 0;
    }
    std::fprintf(stderr, "soundgineer-engine: realtime engine not implemented yet\n");
    return 2;
}
