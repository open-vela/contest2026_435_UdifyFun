#!/bin/bash
# flash_ap.sh - BES2800BP AP-only flash (daily iteration)
#
# Usage:
#   ./flash_ap.sh                  # default /dev/ttyUSB0
#   ./flash_ap.sh /dev/ttyUSB1     # explicit port
#
# Run from a directory containing: dldtool, programmer1700_dual.bin,
# nuttx_ap.bin. After "Wait for SYNC" appears, press the board RESET
# button once. For the full 7-image recovery flash see SKILL.md.
set -e

PORT="${1:-/dev/ttyUSB0}"

for f in dldtool programmer1700_dual.bin nuttx_ap.bin; do
  [ -f "$f" ] || { echo "missing: $f (run from the flash folder)"; exit 1; }
done

chmod +x dldtool 2>/dev/null || true

echo "flashing nuttx_ap.bin via $PORT (press RESET when SYNC waits)..."
./dldtool "$PORT" --reboot programmer1700_dual.bin \
  --set-dual-chip 1 -M nuttx_ap.bin --pgm-rate 2000000

echo "done - board reboots automatically."
