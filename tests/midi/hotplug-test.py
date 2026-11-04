#!/usr/bin/env python3
"""End-to-end MIDI test: a keyboard plugged in while the engine runs.

Starts a soundgineer-engine of its own (own socket, own state dirs, so a
running engine and its session are left alone) on the real PipeWire, plugs
in fake-keyboard.py, and checks that:
  1. the engine connects the new keyboard by itself and reports it (`midi`),
  2. its notes, mod wheel and pitch bend reach the synth (voices, midiEvents),
  3. unplugging it removes it from the list,
  4. notes sent while the power is off are dropped, not played at power-on.

  tests/midi/hotplug-test.py <path to soundgineer-engine>

Needs PipeWire with pipewire-jack and alsa-lib. Not part of ctest: CI has no
sound server. It makes no sound you'd hear unless your output is on, and then
only two short notes.
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
KEYBOARD = "btw test keys"


def main():
    engine_bin = sys.argv[1] if len(sys.argv) > 1 else "soundgineer-engine"
    work = tempfile.mkdtemp(prefix="sg-midi-")
    sock_path = os.path.join(work, "sg.sock")
    env = dict(os.environ, XDG_DATA_HOME=os.path.join(work, "data"), XDG_STATE_HOME=os.path.join(work, "state"))
    engine = subprocess.Popen([engine_bin, "--socket", sock_path], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    keyboard = None
    try:
        conn = connect(sock_path, engine)
        conn.send({"type": "hello", "client": "midi-test", "protocol": 1})
        conn.send({"type": "subscribe", "status": True})
        conn.send({"type": "getState"})
        state = conn.wait(lambda m: m["type"] == "state", 5, "the engine's state")
        if not state["powered"]:
            fail(f"the engine didn't start its audio: {state['audio']}")
        print(f"engine up; MIDI inputs before: {state.get('midi')}")
        if any(KEYBOARD in i for i in state.get("midi", [])):
            fail("the test keyboard is connected before it's plugged in")
        before = conn.wait(lambda m: m["type"] == "status", 5, "a status message")["midiEvents"]

        # 1. Plug in a keyboard: it plays after 1.5 s, once it's been connected.
        keyboard = subprocess.Popen([sys.executable, "-I", os.path.join(HERE, "fake-keyboard.py"), "--name", KEYBOARD,
                                     "--wait", "1.5", "--hold", "0.4",
                                     "note:60:100", "cc:1:64", "bend:4000", "note:64:90", "pause:5"],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        midi = conn.wait(lambda m: m["type"] == "midi" and any(KEYBOARD in i for i in m["inputs"]), 5,
                         "the engine to connect the new keyboard")
        print(f"1. connected by itself: {midi['inputs']}")

        # 2. Its notes play: voices go up, and every message is counted.
        voices = conn.wait(lambda m: m["type"] == "status" and m["voices"] > 0, 5, "a voice from a key")
        print(f"2. a key played a voice (voices={voices['voices']}, peak {voices['peakL']})")
        got = conn.wait(lambda m: m["type"] == "status" and m["midiEvents"] - before >= 6, 6,
                        "all 6 MIDI messages (2 notes on and off, mod wheel, bend)")
        print(f"   {got['midiEvents'] - before} MIDI messages counted")

        # 3. Unplug it.
        keyboard.terminate()
        keyboard.wait(5)
        gone = conn.wait(lambda m: m["type"] == "midi" and not any(KEYBOARD in i for i in m["inputs"]), 5,
                         "the keyboard to leave the list when unplugged")
        print(f"3. unplugged; MIDI inputs now: {gone['inputs']}")
        conn.wait(lambda m: m["type"] == "status" and m["voices"] == 0, 8, "the voices to finish")

        # 4. Power off, play, power on: nothing may sound.
        conn.send({"type": "power", "on": False})
        conn.wait(lambda m: m["type"] == "state" and not m["powered"], 5, "power off")
        for n in (60, 64, 67):
            conn.send({"type": "noteOn", "note": n, "velocity": 1})
        time.sleep(0.3)
        conn.send({"type": "power", "on": True})
        conn.wait(lambda m: m["type"] == "state" and m["powered"], 5, "power on")
        loud = [m["voices"] for m in conn.collect(1.0) if m["type"] == "status"]
        if not loud:
            fail("no status after power on")
        if max(loud) > 0:
            fail(f"notes played while off sounded at power-on (voices {max(loud)})")
        print("4. notes played while off were dropped")
        print("\nMIDI test passed")
    finally:
        if keyboard and keyboard.poll() is None:
            keyboard.kill()
        engine.terminate()
        try:
            out, _ = engine.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            engine.kill()
            out, _ = engine.communicate()
        print("\n-- engine log --\n" + out.strip())
        shutil.rmtree(work, ignore_errors=True)


class Conn:
    def __init__(self, s):
        self.s, self.buf = s, b""

    def send(self, msg):
        self.s.sendall((json.dumps(msg) + "\n").encode())

    def messages(self, timeout):
        self.s.settimeout(timeout)
        try:
            chunk = self.s.recv(65536)
        except socket.timeout:
            return []
        if not chunk:
            fail("the engine closed the connection")
        self.buf += chunk
        *lines, self.buf = self.buf.split(b"\n")
        return [json.loads(l) for l in lines if l.strip()]

    def wait(self, test, timeout, what):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for m in self.messages(max(0.05, deadline - time.time())):
                if m.get("type") == "error":
                    fail(f"engine error: {m.get('message')}")
                if test(m):
                    return m
        fail(f"timed out waiting for {what}")

    def collect(self, seconds):
        out, deadline = [], time.time() + seconds
        while time.time() < deadline:
            out += self.messages(max(0.05, deadline - time.time()))
        return out


def connect(path, engine):
    for _ in range(100):
        if engine.poll() is not None:
            fail(f"the engine exited: {engine.stdout.read()}")
        try:
            s = socket.socket(socket.AF_UNIX)
            s.connect(path)
            return Conn(s)
        except OSError:
            time.sleep(0.1)
    fail("couldn't connect to the engine")


def fail(message):
    print(f"\nMIDI test failed: {message}", file=sys.stderr, flush=True)
    raise SystemExit(1)


if __name__ == "__main__":
    main()
