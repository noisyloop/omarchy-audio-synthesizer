#pragma once

// Realtime host around sg::Processor: JACK (pipewire-jack) audio + MIDI,
// a command queue from the control thread, and a telemetry queue back.

#include "spsc.h"

#include "sg/patch.h"
#include "sg/processor.h"

#include <jack/jack.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sg {

struct Command {
    enum Type { Param, NoteOn, NoteOff, Sustain, PitchBend, ModWheel, Aftertouch, ModSlot, Lfo, Wavetable, FxOrder, AllNotesOff, SampleSet };
    Type type = Param;
    int a = 0;            // index / note / slot / lfo / osc
    double x = 0;         // value / velocity
    bool flag = false;    // sustain down, slot present
    ModSlotState slot{};
    const void* ptr = nullptr;  // LfoShape* (owned, returned via garbage) / WavetableData* / Sample*
    int order[8] = {};
};

/** Audio -> control notifications (MIDI CC macro moves). */
struct AudioEvent {
    int param = -1;
    float value = 0;
};

class Engine {
public:
    Engine();  // creates a 48 kHz processor so the patch works before audio starts
    ~Engine();

    /** Start audio. With dummyAudio, a timer thread stands in for JACK (testing). */
    bool start(bool dummyAudio, std::string& error);
    void stop();
    /** Link the outputs to PipeWire's current default sink (control thread). */
    void connectToDefaultOutput();
    /** MIDI hotplug (control thread): connects MIDI sources that appeared
     *  since the last call and refreshes midiInputs(). Cheap when nothing
     *  changed; returns true when the list of connected inputs changed. */
    bool syncMidi();
    /** The MIDI sources connected to the engine, as readable names. */
    const std::vector<std::string>& midiInputs() const noexcept { return midiInputs_; }
    /** MIDI messages received since start (wraps); for activity lights. */
    std::uint32_t midiEvents() const noexcept { return midiEvents_.load(std::memory_order_relaxed); }
    bool running() const noexcept { return running_; }
    double sampleRate() const noexcept { return sampleRate_; }

    // control thread API: mirrors the patch and queues commands
    Patch& patch() noexcept { return patch_; }
    WavetableBank& bank() noexcept { return bank_; }
    void setParam(int index, double value);
    void setModSlot(int slot, const std::optional<ModSlotState>& s);
    void setLfoShape(int lfo, const LfoShape& shape);
    void setFxOrder(const int* order, int count);
    void setSample(std::unique_ptr<Sample> sample);
    void loadPatch(const Patch& p);
    void noteOn(int note, double vel);
    void noteOff(int note);
    void sustain(bool down);
    void pitchBend(double v);
    void modWheel(double v);
    void aftertouch(double v);
    void allNotesOff();

    /** Latest telemetry since the last call, if any. */
    bool takeTelemetry(Processor::Telemetry& out);
    /** Drain audio->control events (macro CCs) into the patch mirror; returns changed params. */
    std::vector<AudioEvent> syncAudioEvents();
    /** Free objects the audio thread has finished with. */
    void collectGarbage();

    // Sink interface used by syncPatch()
    void setWavetable(int osc, const WavetableData* t);

private:
    static int jackProcess(jack_nframes_t n, void* self);
    static void jackPortRegistered(jack_port_id_t, int, void* self);
    static void jackPortConnected(jack_port_id_t, jack_port_id_t, int, void* self);
    void processAudio(float* outL, float* outR, unsigned n, void* midiBuf);
    void drainCommands() noexcept;
    void handleMidi(const unsigned char* d, std::size_t size) noexcept;
    bool push(const Command& c);
    void ensureProcessor(double sr);
    void dummyLoop();

    Patch patch_;
    WavetableBank bank_;
    std::unique_ptr<Processor> proc_;
    Spsc<Command> commands_{8192};
    struct Garbage { int kind; const void* ptr; };  // 0 LfoShape, 1 Sample
    Spsc<Garbage> garbage_{1024};
    Spsc<AudioEvent> audioEvents_{1024};
    Spsc<Processor::Telemetry> telemetry_{4};
    const Sample* audioSample_ = nullptr;  // owned; freed via garbage when replaced

    jack_client_t* client_ = nullptr;
    jack_port_t* outL_ = nullptr;
    jack_port_t* outR_ = nullptr;
    jack_port_t* midiIn_ = nullptr;
    // Set by JACK's notification thread when ports come, go or (dis)connect;
    // syncMidi() does the work on the control thread (JACK forbids
    // connecting from inside its callbacks).
    std::atomic<bool> midiGraphChanged_{true};
    std::atomic<std::uint32_t> midiEvents_{0};
    std::vector<std::string> midiInputs_;
    std::thread dummy_;
    std::atomic<bool> dummyRun_{false};
    bool running_ = false;
    double sampleRate_ = 48000;

    float blockL_[kBlockSize] = {}, blockR_[kBlockSize] = {};
    int blockPos_ = kBlockSize;
    int macroIdx_[4];
};

} // namespace sg
