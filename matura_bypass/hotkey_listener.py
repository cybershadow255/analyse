#!/usr/bin/env python3
import evdev
from evdev import ecodes
import subprocess
import os

# Configuration
HOTKEY = [ecodes.KEY_LEFTCTRL, ecodes.KEY_LEFTSHIFT, ecodes.KEY_K]
KEYLOGGER_PATTERN = "keylog|logkeys|input-utils"
HOTKEY_FILE = "/tmp/keylogger_paused"
LOG_FILE = "/tmp/bypass.log"

def log(msg):
    with open(LOG_FILE, "a") as f:
        f.write(msg + "\n")

def toggle_keylogger():
    if os.path.exists(HOTKEY_FILE):
        # Resume
        try:
            with open(HOTKEY_FILE, "r") as f:
                pids = f.read().strip().split()
            for pid in pids:
                subprocess.run(["kill", "-CONT", pid])
            os.remove(HOTKEY_FILE)
            log("Keylogger RESUMED")
        except Exception as e:
            log(f"Error resuming: {e}")
    else:
        # Pause
        try:
            result = subprocess.run(["pgrep", "-f", "-i", KEYLOGGER_PATTERN], capture_output=True, text=True)
            pids = result.stdout.strip()
            if pids:
                for pid in pids.split():
                    subprocess.run(["kill", "-STOP", pid])
                with open(HOTKEY_FILE, "w") as f:
                    f.write(pids)
                log(f"Keylogger PAUSED (PIDs: {pids})")
        except Exception as e:
            log(f"Error pausing: {e}")

def main():
    log("Starting Hotkey Listener...")

    # Find keyboard device
    devices = [evdev.InputDevice(path) for path in evdev.list_devices()]
    kbd = None
    for device in devices:
        if "keyboard" in device.name.lower():
            kbd = device
            break

    if not kbd:
        log("No keyboard found!")
        return

    log(f"Listening on {kbd.name} ({kbd.path})")

    pressed_keys = set()

    for event in kbd.read_loop():
        if event.type == ecodes.EV_KEY:
            key_event = evdev.categorize(event)
            if key_event.keystate == key_event.key_down:
                pressed_keys.add(key_event.scancode)
                # Check for hotkey
                if all(k in pressed_keys for k in HOTKEY):
                    toggle_keylogger()
            elif key_event.keystate == key_event.key_up:
                if key_event.scancode in pressed_keys:
                    pressed_keys.remove(key_event.scancode)

if __name__ == "__main__":
    main()
