#!/usr/bin/env python3
"""A fake MIDI keyboard for testing Soundgineer without hardware.

Creates an ALSA sequencer client with one output port, which PipeWire's
Midi-Bridge shows to JACK clients the way it shows a USB keyboard
("Midi-Bridge:<name>:(capture_0) <port>"). Then it plays what it's told.
Needs only Python and libasound (alsa-lib).

  fake-keyboard.py [--name NAME] [--wait SECONDS] [--hold SECONDS] [EVENT ...]

EVENT is note:<n>[:<velocity>] (held for --hold, then released),
cc:<n>:<value>, bend:<-8192..8191> or pause:<seconds>. With no events it
stays open (a device that's plugged in) until Ctrl-C.
"""
import argparse
import ctypes
import signal
import sys
import time

SND_SEQ_OPEN_OUTPUT = 1
SND_SEQ_PORT_CAP_READ = 1 << 0
SND_SEQ_PORT_CAP_SUBS_READ = 1 << 5
SND_SEQ_PORT_TYPE_MIDI_GENERIC = 1 << 1
SND_SEQ_PORT_TYPE_HARDWARE = 1 << 16
SND_SEQ_PORT_TYPE_APPLICATION = 1 << 20
SND_SEQ_EVENT_NOTEON, SND_SEQ_EVENT_NOTEOFF = 6, 7
SND_SEQ_EVENT_CONTROLLER, SND_SEQ_EVENT_PITCHBEND = 10, 13
SND_SEQ_QUEUE_DIRECT = 253
SND_SEQ_ADDRESS_SUBSCRIBERS = 254
SND_SEQ_ADDRESS_UNKNOWN = 253


class Note(ctypes.Structure):
    _fields_ = [("channel", ctypes.c_ubyte), ("note", ctypes.c_ubyte), ("velocity", ctypes.c_ubyte),
                ("off_velocity", ctypes.c_ubyte), ("duration", ctypes.c_uint)]


class Ctrl(ctypes.Structure):
    _fields_ = [("channel", ctypes.c_ubyte), ("unused", ctypes.c_ubyte * 3),
                ("param", ctypes.c_uint), ("value", ctypes.c_int)]


class Data(ctypes.Union):
    _fields_ = [("note", Note), ("control", Ctrl), ("raw", ctypes.c_ubyte * 12)]


class Event(ctypes.Structure):  # snd_seq_event_t, 28 bytes
    _fields_ = [("type", ctypes.c_ubyte), ("flags", ctypes.c_ubyte), ("tag", ctypes.c_ubyte),
                ("queue", ctypes.c_ubyte), ("time", ctypes.c_ubyte * 8),
                ("source_client", ctypes.c_ubyte), ("source_port", ctypes.c_ubyte),
                ("dest_client", ctypes.c_ubyte), ("dest_port", ctypes.c_ubyte), ("data", Data)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--name", default="Fake Keyboard")
    ap.add_argument("--wait", type=float, default=1.0, help="seconds to wait before playing (to be connected)")
    ap.add_argument("--hold", type=float, default=0.3, help="seconds each note is held")
    ap.add_argument("events", nargs="*")
    args = ap.parse_args()

    alsa = ctypes.CDLL("libasound.so.2")
    seq = ctypes.c_void_p()
    if alsa.snd_seq_open(ctypes.byref(seq), b"default", SND_SEQ_OPEN_OUTPUT, 0) < 0:
        sys.exit("fake-keyboard: can't open the ALSA sequencer")
    alsa.snd_seq_set_client_name(seq, args.name.encode())
    port = alsa.snd_seq_create_simple_port(
        seq, b"Keys", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_HARDWARE | SND_SEQ_PORT_TYPE_APPLICATION)
    if port < 0:
        sys.exit("fake-keyboard: can't create a port")
    print(f"fake-keyboard: '{args.name}' is plugged in (client {alsa.snd_seq_client_id(seq)})", flush=True)

    def send(kind, fill):
        ev = Event()
        ev.type = kind
        ev.queue = SND_SEQ_QUEUE_DIRECT
        ev.source_port = port
        ev.dest_client = SND_SEQ_ADDRESS_SUBSCRIBERS
        ev.dest_port = SND_SEQ_ADDRESS_UNKNOWN
        fill(ev.data)
        alsa.snd_seq_event_output_direct(seq, ctypes.byref(ev))

    def note(kind, n, vel):
        def fill(d):
            d.note.note, d.note.velocity = n, vel
        send(kind, fill)

    def ctrl(kind, param, value):
        def fill(d):
            d.control.param, d.control.value = param, value
        send(kind, fill)

    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        if not args.events:
            signal.pause()
        time.sleep(args.wait)
        for e in args.events:
            kind, *v = e.split(":")
            if kind == "note":
                n, vel = int(v[0]), int(v[1]) if len(v) > 1 else 100
                note(SND_SEQ_EVENT_NOTEON, n, vel)
                time.sleep(args.hold)
                note(SND_SEQ_EVENT_NOTEOFF, n, 0)
            elif kind == "cc":
                ctrl(SND_SEQ_EVENT_CONTROLLER, int(v[0]), int(v[1]))
            elif kind == "bend":
                ctrl(SND_SEQ_EVENT_PITCHBEND, 0, int(v[0]))
            elif kind == "pause":
                time.sleep(float(v[0]))
            else:
                sys.exit(f"fake-keyboard: unknown event {e}")
            print(f"fake-keyboard: sent {e}", flush=True)
        time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        alsa.snd_seq_close(seq)


if __name__ == "__main__":
    main()
