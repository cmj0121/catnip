#!/usr/bin/env bash
#
# meowkit.sh - flash-side helper for installing Catnip onto a MeowKit (ESP32-S3).
#
# Subcommands (added across issues #12/#13/#14):
#   flash        put the MeowKit into flash/download status; first-time setup (#12)
#   probe        just verify esptool can talk to the device (#12)
#
# Safety model (see the "Install" story): the ESP32-S3 ROM download mode is
# always reachable over USB, so a flash can always be redone. Never burn eFuses
# that disable it.
#
# Config via environment:
#   PORT       serial port (auto-detected if exactly one MeowKit is present)
#   BAUD       upload baud (default 921600)
#   CHIP       target chip (default esp32s3)
#   ESPTOOL    esptool command (auto: `esptool.py`, else `python3 -m esptool`)
set -euo pipefail

CHIP="${CHIP:-esp32s3}"
BAUD="${BAUD:-921600}"

log()  { printf '[meowkit] %s\n' "$*"; }
die()  { printf '[meowkit] error: %s\n' "$*" >&2; exit 1; }

# Resolve an esptool invocation into the ESPTOOL array.
resolve_esptool() {
	if [ -n "${ESPTOOL:-}" ]; then
		# shellcheck disable=SC2206
		ESPTOOL_CMD=($ESPTOOL)
	elif command -v esptool.py >/dev/null 2>&1; then
		ESPTOOL_CMD=(esptool.py)
	elif command -v esptool >/dev/null 2>&1; then
		ESPTOOL_CMD=(esptool)
	elif python3 -c 'import esptool' >/dev/null 2>&1; then
		ESPTOOL_CMD=(python3 -m esptool)
	else
		die "esptool not found. Install it: pip install esptool (or: pipx install esptool)"
	fi
}

# Auto-detect the serial port unless PORT is set.
detect_port() {
	if [ -n "${PORT:-}" ]; then
		[ -e "$PORT" ] || die "PORT=$PORT does not exist"
		return
	fi
	local candidates=()
	case "$(uname -s)" in
		Darwin) candidates=(/dev/cu.usbmodem* /dev/cu.usbserial*) ;;
		*)      candidates=(/dev/ttyACM* /dev/ttyUSB*) ;;
	esac
	local found=()
	local c
	for c in "${candidates[@]}"; do
		[ -e "$c" ] && found+=("$c")
	done
	case "${#found[@]}" in
		1) PORT="${found[0]}"; log "auto-detected PORT=$PORT" ;;
		0) die "no serial device found. Connect the MeowKit and enter download mode (hold BOOT, tap RESET), then set PORT=..." ;;
		*) die "multiple serial devices found (${found[*]}). Set PORT=... to choose one" ;;
	esac
}

download_mode_help() {
	cat <<'EOF'
If esptool cannot connect, put the MeowKit into ROM download mode by hand:
  1. Hold the BOOT button.
  2. Tap RESET (keep holding BOOT).
  3. Release BOOT.
Then re-run the command. This mode is always available and is the ultimate
recovery path - the device cannot be permanently bricked by flashing.
EOF
}

cmd_probe() {
	resolve_esptool
	detect_port
	log "probing $CHIP on $PORT ..."
	if ! "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$PORT" flash_id; then
		echo
		download_mode_help
		die "could not talk to the device on $PORT"
	fi
	log "device is reachable and flash-ready."
}

cmd_flash() {
	# "make the MeowKit be in flash status" - first-time setup + connectivity check.
	log "first-time setup: putting the MeowKit into flash/download status"
	case "$(uname -s)" in
		Linux)
			log "Linux: your user must be able to open the serial port."
			log "  if permission is denied: sudo usermod -aG dialout \"\$USER\" (re-login)" ;;
		Darwin)
			log "macOS: the USB-serial device appears as /dev/cu.usbmodem*." ;;
	esac
	cmd_probe
	log "ready. Next: 'make install' to install Catnip (it backs up stock first)."
}

usage() {
	sed -n '2,20p' "$0"
	exit "${1:-0}"
}

main() {
	local cmd="${1:-}"
	[ -n "$cmd" ] || usage 1
	shift || true
	case "$cmd" in
		flash) cmd_flash "$@" ;;
		probe) cmd_probe "$@" ;;
		-h|--help|help) usage 0 ;;
		*) die "unknown subcommand: $cmd (try: flash, probe)" ;;
	esac
}

main "$@"
