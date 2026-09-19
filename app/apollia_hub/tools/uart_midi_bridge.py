#!/usr/bin/env python3
"""uart_midi_bridge.py - turn the Apollia hub's UART-MIDI stream into a
real system MIDI port on the host.

The board (apollia_hub app - the "siren's vortex") writes raw
standard-MIDI bytes to its console UART; the EVB's USB-serial bridge
lands that as a serial port here. This script forwards the bytes to a
virtual MIDI output that any DAW / softsynth (GarageBand, FluidSynth,
MIDI-OX...) can consume - the siren's song, heard on your computer.

Usage:
    python3 uart_midi_bridge.py [port] [baud]

    port is auto-detected when omitted:
      macOS   /dev/cu.usbserial-*  /dev/cu.wchusbserial-*  /dev/cu.usbmodem-*
      Linux   /dev/ttyUSB*         /dev/ttyACM*
    baud defaults to 921600 (Vela console rate); try 115200 if you see
    nothing at all.

Dependencies:
    pip3 install pyserial python-rtmidi

macOS notes:
    - Use /dev/cu.* devices, never /dev/tty.* (the tty variants block
      waiting for carrier detect).
    - Verify the virtual port with the built-in "Audio MIDI Setup"
      app (Spotlight -> Audio MIDI Setup): "Apollia UART-MIDI" shows
      up as a destination. GarageBand: create a Software Instrument
      track and play.
    - CH343 needs the WCH serial driver on older macOS; recent macOS
      enumerates it out of the box.

Notes:
    - The console shares the wire, so occasional NSH/syslog bytes are
      ignored as unparseable noise. Avoid typing in NSH while playing.
"""

import glob
import sys

try:
    import serial
    import rtmidi
except ImportError as exc:
    sys.exit(f"missing dependency: {exc.name} - run: pip3 install pyserial python-rtmidi")

BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 921600

# data byte counts per status nibble (channel voice messages)
FIXED_LEN = {0x80: 3, 0x90: 3, 0xA0: 3, 0xB0: 3, 0xC0: 2, 0xD0: 2, 0xE0: 3}


def autodetect_port() -> str:
    """Pick the board's serial port; cu.* first (macOS)."""
    candidates = (
        sorted(glob.glob("/dev/cu.usbserial*"))
        + sorted(glob.glob("/dev/cu.wchusbserial*"))
        + sorted(glob.glob("/dev/cu.usbmodem*"))
        + sorted(glob.glob("/dev/ttyUSB*"))
        + sorted(glob.glob("/dev/ttyACM*"))
    )

    if not candidates:
        sys.exit("no serial port found - plug the board in, or pass the "
                 "port explicitly (macOS: ls /dev/cu.*)")
    if len(candidates) > 1:
        print("multiple candidates, pass one explicitly:")
        for c in candidates:
            print(f"  python3 {sys.argv[0]} {c}")
        sys.exit(1)
    return candidates[0]


def main() -> None:
    port = sys.argv[1] if len(sys.argv) > 1 else autodetect_port()
    ser = serial.Serial(port, BAUD, timeout=0.02)
    midi_out = rtmidi.MidiOut()
    midi_out.open_virtual_port("Apollia UART-MIDI")
    print(f"bridging {port}@{BAUD} -> MIDI port 'Apollia UART-MIDI'")
    print("play the piano on the board; Ctrl-C to quit")

    running_status = None
    pending = bytearray()
    forwarded = 0

    while True:
        chunk = ser.read(256)
        if not chunk:
            continue

        for byte in chunk:
            if byte >= 0xF8:          # realtime, pass through unframed
                midi_out.send_message([byte])
                continue

            if byte >= 0x80:          # status byte starts a new message
                running_status = byte
                pending = bytearray([byte])
                continue

            if not pending:
                if running_status is None:
                    continue          # stray data byte, drop
                pending = bytearray([running_status])

            pending.append(byte)

            need = FIXED_LEN.get(pending[0] & 0xF0)
            if need and len(pending) >= need:
                midi_out.send_message(list(pending[:need]))
                forwarded += 1
                pending = bytearray()

                if forwarded % 16 == 0:
                    print(f"\rforwarded {forwarded} messages", end="",
                          flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nbye")
