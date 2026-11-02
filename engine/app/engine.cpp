#include "engine.h"

#include <jack/midiport.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <cstring>
#include <random>

namespace sg {

Engine::Engine() {
    for (int m = 0; m < 4; m++) {
        char id[32];
        std::snprintf(id, sizeof id, "macro%d.value", m + 1);
        macroIdx_[m] = paramIndex(id);
    }
    ensureProcessor(48000);
}

Engine::~Engine() {
    stop();
    collectGarbage();
    delete audioSample_;
}

void Engine::ensureProcessor(double sr) {
    if (proc_ && proc_->sampleRate() == sr) return;
    std::random_device rd;
    proc_ = std::make_unique<Processor>(sr, rd());
    syncPatch(*proc_, patch_, bank_);
    proc_->setSample(audioSample_);
    blockPos_ = kBlockSize;
}

bool Engine::start(bool dummyAudio, std::string& error) {
    if (running_) return true;
    bank_.prebuild();
    if (dummyAudio) {
        sampleRate_ = 48000;
        ensureProcessor(sampleRate_);
        dummyRun_ = true;
        dummy_ = std::thread([this] { dummyLoop(); });
        running_ = true;
        return true;
    }
    jack_status_t status{};
    client_ = jack_client_open("Soundgineer", JackNoStartServer, &status);
    if (!client_) {
        error = "no PipeWire/JACK server reachable (install pipewire-jack)";
        return false;
    }
    sampleRate_ = jack_get_sample_rate(client_);
    ensureProcessor(sampleRate_);
    outL_ = jack_port_register(client_, "out_L", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    outR_ = jack_port_register(client_, "out_R", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    midiIn_ = jack_port_register(client_, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    if (!outL_ || !outR_ || !midiIn_) {
        error = "could not register JACK ports";
        jack_client_close(client_);
        client_ = nullptr;
        return false;
    }
    jack_set_process_callback(client_, &Engine::jackProcess, this);
    if (jack_activate(client_) != 0) {
        error = "could not activate JACK client";
        jack_client_close(client_);
        client_ = nullptr;
        return false;
    }
    // Auto-connect: stereo out to the default output (what other apps use),
    // MIDI in from every hardware MIDI source (PipeWire bridges ALSA MIDI).
    connectToDefaultOutput();
    if (const char** ports = jack_get_ports(client_, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput | JackPortIsPhysical)) {
        for (const char** p = ports; *p; ++p) jack_connect(client_, *p, jack_port_name(midiIn_));
        jack_free(ports);
    }
    running_ = true;
    return true;
}

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) b++;
    return s.substr(b);
}

// PipeWire's default sink as {node.name, description}. pipewire-jack names
// ports "<description>:playback_FL", so both are needed. Control thread only.
static std::pair<std::string, std::string> defaultSink() {
    std::string name, desc;
    if (FILE* p = ::popen("LC_ALL=C pactl get-default-sink 2>/dev/null", "r")) {
        char buf[512];
        if (std::fgets(buf, sizeof buf, p)) name = trim(buf);
        ::pclose(p);
    }
    if (name.empty()) return {};
    if (FILE* p = ::popen("LC_ALL=C pactl list sinks 2>/dev/null", "r")) {
        char buf[1024];
        bool inSink = false;
        while (std::fgets(buf, sizeof buf, p)) {
            const std::string line = trim(buf);
            if (line.rfind("Name: ", 0) == 0) inSink = line.substr(6) == name;
            else if (inSink && line.rfind("Description: ", 0) == 0) { desc = line.substr(13); inSink = false; }
        }
        ::pclose(p);
    }
    return {name, desc};
}

void Engine::connectToDefaultOutput() {
    if (!client_) return;
    // Drop existing output links, then link to the default sink's ports.
    jack_port_disconnect(client_, outL_);
    jack_port_disconnect(client_, outR_);
    std::vector<std::string> targets;
    const auto [sink, desc] = defaultSink();
    const char** ports = jack_get_ports(client_, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
    if (ports && !sink.empty()) {
        const std::string prefix = sink + ":";
        const std::string descPrefix = desc.empty() ? std::string("\x01") : desc + ":";
        const int size = jack_port_name_size();
        std::vector<char> a1(static_cast<std::size_t>(size)), a2(static_cast<std::size_t>(size));
        for (const char** p = ports; *p; ++p) {
            jack_port_t* port = jack_port_by_name(client_, *p);
            if (!port) continue;
            char* aliases[2] = {a1.data(), a2.data()};
            const int n = jack_port_get_aliases(port, aliases);
            const std::string pn(*p);
            if (pn.find(":playback_") == std::string::npos) continue;
            bool match = pn.rfind(prefix, 0) == 0 || pn.rfind(descPrefix, 0) == 0;
            for (int i = 0; i < n && !match; i++) match = std::string(aliases[i]).rfind(prefix, 0) == 0;
            if (match) targets.emplace_back(pn);
        }
    }
    if (targets.empty() && ports) {  // fallback: first physical playback ports
        if (const char** phys = jack_get_ports(client_, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput | JackPortIsPhysical)) {
            for (const char** p = phys; *p && targets.size() < 2; ++p) targets.emplace_back(*p);
            jack_free(phys);
        }
    }
    if (ports) jack_free(ports);
    std::sort(targets.begin(), targets.end());  // ..._FL before ..._FR
    if (!targets.empty()) jack_connect(client_, jack_port_name(outL_), targets[0].c_str());
    if (targets.size() > 1) jack_connect(client_, jack_port_name(outR_), targets[1].c_str());
    else if (!targets.empty()) jack_connect(client_, jack_port_name(outR_), targets[0].c_str());
    std::fprintf(stderr, "soundgineer-engine: output -> %s%s\n", targets.empty() ? "(nothing found)" : (targets[0] + (targets.size() > 1 ? " + " + targets[1] : "")).c_str(),
                 sink.empty() ? " (default sink unknown, used first hardware output)" : "");
}

void Engine::stop() {
    if (!running_) return;
    if (client_) {
        jack_deactivate(client_);
        jack_client_close(client_);
        client_ = nullptr;
    }
    if (dummyRun_) {
        dummyRun_ = false;
        if (dummy_.joinable()) dummy_.join();
    }
    running_ = false;
    // Audio is stopped: apply whatever is still queued and silence voices.
    if (proc_) {
        drainCommands();
        proc_->allNotesOff();
    }
}

void Engine::dummyLoop() {
    using clock = std::chrono::steady_clock;
    float l[kBlockSize], r[kBlockSize];
    auto next = clock::now();
    const auto period = std::chrono::nanoseconds(static_cast<long long>(1e9 * kBlockSize / sampleRate_));
    while (dummyRun_) {
        processAudio(l, r, kBlockSize, nullptr);
        next += period;
        std::this_thread::sleep_until(next);
    }
}

int Engine::jackProcess(jack_nframes_t n, void* self) {
    auto* e = static_cast<Engine*>(self);
    auto* l = static_cast<float*>(jack_port_get_buffer(e->outL_, n));
    auto* r = static_cast<float*>(jack_port_get_buffer(e->outR_, n));
    e->processAudio(l, r, n, jack_port_get_buffer(e->midiIn_, n));
    return 0;
}

void Engine::processAudio(float* outL, float* outR, unsigned n, void* midiBuf) {
    // Fixed 128-frame core blocks (the reference render quantum), adapted to
    // the host buffer through blockPos_. MIDI events apply at the next block.
    const std::uint32_t midiCount = midiBuf ? jack_midi_get_event_count(midiBuf) : 0;
    std::uint32_t midiNext = 0;
    for (unsigned i = 0; i < n; i++) {
        if (blockPos_ >= kBlockSize) {
            while (midiNext < midiCount) {
                jack_midi_event_t ev;
                if (jack_midi_event_get(&ev, midiBuf, midiNext) != 0) { midiNext++; continue; }
                if (ev.time > i) break;
                handleMidi(ev.buffer, ev.size);
                midiNext++;
            }
            drainCommands();
            proc_->process(blockL_, blockR_);
            if (proc_->telemetryReady()) {
                telemetry_.push(proc_->telemetry());
                proc_->clearTelemetryReady();
            }
            blockPos_ = 0;
        }
        outL[i] = blockL_[blockPos_];
        outR[i] = blockR_[blockPos_];
        blockPos_++;
    }
    for (; midiNext < midiCount; midiNext++) {  // late events: apply for the next block
        jack_midi_event_t ev;
        if (jack_midi_event_get(&ev, midiBuf, midiNext) == 0) handleMidi(ev.buffer, ev.size);
    }
}

void Engine::handleMidi(const unsigned char* d, std::size_t size) noexcept {
    if (size < 2) return;
    const int status = d[0] & 0xF0;
    const int a = d[1] & 0x7F;
    const int b = size > 2 ? (d[2] & 0x7F) : 0;
    switch (status) {
    case 0x90:
        if (b > 0) proc_->noteOn(a, b / 127.0);
        else proc_->noteOff(a);
        break;
    case 0x80: proc_->noteOff(a); break;
    case 0xB0:
        if (a == 1) proc_->modWheel(b / 127.0);
        else if (a == 64) proc_->sustain(b >= 64);
        else if (a >= 20 && a <= 23) {
            const int idx = macroIdx_[a - 20];
            proc_->setParam(idx, b / 127.0);
            audioEvents_.push({idx, static_cast<float>(b / 127.0)});
        } else if (a == 123 || a == 120) proc_->allNotesOff();
        break;
    case 0xE0: {
        const int v = (b << 7) | a;
        proc_->pitchBend(std::max(-1.0, (v - 8192) / 8192.0));
        break;
    }
    case 0xD0: proc_->aftertouch(a / 127.0); break;
    default: break;
    }
}

void Engine::drainCommands() noexcept {
    Command c;
    while (commands_.pop(c)) {
        switch (c.type) {
        case Command::Param: proc_->setParam(c.a, c.x); break;
        case Command::NoteOn: proc_->noteOn(c.a, c.x); break;
        case Command::NoteOff: proc_->noteOff(c.a); break;
        case Command::Sustain: proc_->sustain(c.flag); break;
        case Command::PitchBend: proc_->pitchBend(c.x); break;
        case Command::ModWheel: proc_->modWheel(c.x); break;
        case Command::Aftertouch: proc_->aftertouch(c.x); break;
        case Command::ModSlot:
            proc_->setModSlot(c.a, c.flag ? std::optional<ModSlotState>(c.slot) : std::nullopt);
            break;
        case Command::Lfo: {
            const auto* shape = static_cast<const LfoShape*>(c.ptr);
            proc_->setLfoShape(c.a, *shape);
            garbage_.push({0, shape});
            break;
        }
        case Command::Wavetable: proc_->setWavetable(c.a, static_cast<const WavetableData*>(c.ptr)); break;
        case Command::FxOrder: proc_->setFxOrder(c.order, 8); break;
        case Command::AllNotesOff: proc_->allNotesOff(); break;
        case Command::SampleSet: {
            const Sample* old = audioSample_;
            audioSample_ = static_cast<const Sample*>(c.ptr);
            proc_->setSample(audioSample_);
            if (old) garbage_.push({1, old});
            break;
        }
        }
    }
}

bool Engine::push(const Command& c) {
    if (!proc_) return false;
    if (!running_) {
        // No audio thread: apply directly so the patch stays in sync.
        if (!commands_.push(c)) return false;
        drainCommands();
        collectGarbage();
        return true;
    }
    for (int tries = 0; tries < 200; tries++) {
        if (commands_.push(c)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void Engine::collectGarbage() {
    Garbage g;
    while (garbage_.pop(g)) {
        if (g.kind == 0) delete static_cast<const LfoShape*>(g.ptr);
        else delete static_cast<const Sample*>(g.ptr);
    }
}

void Engine::setParam(int index, double value) {
    if (index < 0 || index >= kNumParams) return;
    value = value < 0 ? 0 : value > 1 ? 1 : value;
    patch_.values[index] = static_cast<float>(value);
    Command c; c.type = Command::Param; c.a = index; c.x = value;
    push(c);
    // Reference engine.ts: changing an oscillator's table param re-sends its table.
    for (int o = 0; o < 3; o++) {
        static const int wt[3] = {paramIndex("osc1.wavetable"), paramIndex("osc2.wavetable"), paramIndex("osc3.wavetable")};
        if (index == wt[o]) setWavetable(o, bank_.tableFor(patch_.values[index], o));
    }
}

void Engine::setWavetable(int osc, const WavetableData* t) {
    Command c; c.type = Command::Wavetable; c.a = osc; c.ptr = t;
    push(c);
}

void Engine::setModSlot(int slot, const std::optional<ModSlotState>& s) {
    if (slot < 0 || slot >= kMaxModSlots) return;
    patch_.slots[slot] = s;
    Command c; c.type = Command::ModSlot; c.a = slot; c.flag = s.has_value(); if (s) c.slot = *s;
    push(c);
}

void Engine::setLfoShape(int lfo, const LfoShape& shape) {
    if (lfo < 0 || lfo >= 8) return;
    patch_.shapes[lfo] = shape.count ? shape : defaultLfoShape();
    Command c; c.type = Command::Lfo; c.a = lfo; c.ptr = new LfoShape(patch_.shapes[lfo]);
    if (!push(c)) delete static_cast<const LfoShape*>(c.ptr);
}

void Engine::setFxOrder(const int* order, int count) {
    if (count != 8) return;
    std::copy(order, order + 8, patch_.fxOrder);
    Command c; c.type = Command::FxOrder; std::copy(order, order + 8, c.order);
    push(c);
}

void Engine::setSample(std::unique_ptr<Sample> sample) {
    Command c; c.type = Command::SampleSet; c.ptr = sample.release();
    if (!push(c)) delete static_cast<const Sample*>(c.ptr);
}

void Engine::loadPatch(const Patch& p) {
    patch_ = p;
    if (!proc_) return;
    struct Sink {
        Engine& e;
        void allNotesOff() { Command c; c.type = Command::AllNotesOff; e.push(c); }
        void setParam(int i, double v) { Command c; c.type = Command::Param; c.a = i; c.x = v; e.push(c); }
        void setModSlot(int s, const std::optional<ModSlotState>& st) {
            Command c; c.type = Command::ModSlot; c.a = s; c.flag = st.has_value(); if (st) c.slot = *st; e.push(c);
        }
        void setLfoShape(int l, const LfoShape& sh) {
            Command c; c.type = Command::Lfo; c.a = l; c.ptr = new LfoShape(sh);
            if (!e.push(c)) delete static_cast<const LfoShape*>(c.ptr);
        }
        void setFxOrder(const int* o, int) { Command c; c.type = Command::FxOrder; std::copy(o, o + 8, c.order); e.push(c); }
        void setWavetable(int o, const WavetableData* t) { e.setWavetable(o, t); }
    } sink{*this};
    syncPatch(sink, patch_, bank_);
}

void Engine::noteOn(int note, double vel) { Command c; c.type = Command::NoteOn; c.a = note; c.x = vel; push(c); }
void Engine::noteOff(int note) { Command c; c.type = Command::NoteOff; c.a = note; push(c); }
void Engine::sustain(bool down) { Command c; c.type = Command::Sustain; c.flag = down; push(c); }
void Engine::pitchBend(double v) { Command c; c.type = Command::PitchBend; c.x = v; push(c); }
void Engine::modWheel(double v) { Command c; c.type = Command::ModWheel; c.x = v; push(c); }
void Engine::aftertouch(double v) { Command c; c.type = Command::Aftertouch; c.x = v; push(c); }
void Engine::allNotesOff() { Command c; c.type = Command::AllNotesOff; push(c); }

bool Engine::takeTelemetry(Processor::Telemetry& out) {
    bool got = false;
    while (telemetry_.pop(out)) got = true;
    return got;
}

std::vector<AudioEvent> Engine::syncAudioEvents() {
    std::vector<AudioEvent> out;
    AudioEvent e;
    while (audioEvents_.pop(e)) {
        if (e.param >= 0 && e.param < kNumParams) patch_.values[e.param] = e.value;
        out.push_back(e);
    }
    return out;
}

} // namespace sg
