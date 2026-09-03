#!/usr/bin/env bash
#
# meowkit.sh - flash-side helper for installing Catnip onto a MeowKit (ESP32-S3).
#
# Subcommands (added across issues #12/#13/#14):
#   flash        put the MeowKit into flash/download status; first-time setup (#12)
#   probe        just verify esptool can talk to the device (#12)
#   backup       read the full flash to a file and verify it (#13)
#   install      backup-first: back up stock, then build & flash Catnip (#13)
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
FLASH_SIZE_BYTES=16777216 # 16 MB
BACKUP_DIR="${BACKUP_DIR:-backup}"
FIRMWARE_DIR="${FIRMWARE_DIR:-firmware}"
PIO_ENV="${PIO_ENV:-meowkit}"

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

# Portable file size in bytes (BSD stat on macOS, GNU stat on Linux).
file_size() {
	if stat -f%z "$1" >/dev/null 2>&1; then
		stat -f%z "$1"
	else
		stat -c%s "$1"
	fi
}

resolve_pio() {
	if [ -n "${PIO:-}" ]; then
		# shellcheck disable=SC2206
		PIO_CMD=($PIO)
	elif command -v pio >/dev/null 2>&1; then
		PIO_CMD=(pio)
	elif command -v platformio >/dev/null 2>&1; then
		PIO_CMD=(platformio)
	elif python3 -c 'import platformio' >/dev/null 2>&1; then
		PIO_CMD=(python3 -m platformio)
	else
		die "PlatformIO not found. Install it: pip install platformio (https://platformio.org)"
	fi
}

cmd_backup() {
	resolve_esptool
	detect_port
	mkdir -p "$BACKUP_DIR"
	local out="${BACKUP:-$BACKUP_DIR/meowkit-stock-$(date +%Y%m%d-%H%M%S).bin}"
	log "backing up the full 16 MB flash to $out (reads the whole chip) ..."
	if ! "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$PORT" --baud "$BAUD" \
		read_flash 0x0 "$FLASH_SIZE_BYTES" "$out"; then
		download_mode_help
		die "backup read failed - refusing to go further"
	fi
	local sz
	sz="$(file_size "$out")"
	[ "$sz" = "$FLASH_SIZE_BYTES" ] ||
		die "backup is $sz bytes, expected $FLASH_SIZE_BYTES; NOT trusting it"
	printf '%s\n' "$out" >"$BACKUP_DIR/.latest"
	log "backup verified ($sz bytes) and recorded as latest."
}

cmd_install() {
	# L1 safety: never write Catnip without a verified stock backup in hand.
	local latest="$BACKUP_DIR/.latest"
	local have_backup=0
	if [ -f "$latest" ]; then
		local bfile
		bfile="$(cat "$latest")"
		[ -n "$bfile" ] && [ -s "$bfile" ] && have_backup=1
	fi
	if [ "$have_backup" = 1 ]; then
		log "existing stock backup found ($(cat "$latest")); keeping stock image."
	else
		cmd_backup
	fi

	resolve_pio
	detect_port
	log "building and flashing Catnip via PlatformIO (env: $PIO_ENV) ..."
	if ! "${PIO_CMD[@]}" run -d "$FIRMWARE_DIR" -e "$PIO_ENV" \
		-t upload --upload-port "$PORT"; then
		download_mode_help
		die "flash failed - the device is still recoverable via 'make uninstall'"
	fi
	log "Catnip installed. To revert to stock: make uninstall"
}

usage() {
	sed -n '2,22p' "$0"
	exit "${1:-0}"
}

main() {
	local cmd="${1:-}"
	[ -n "$cmd" ] || usage 1
	shift || true
	case "$cmd" in
		flash) cmd_flash "$@" ;;
		probe) cmd_probe "$@" ;;
		backup) cmd_backup "$@" ;;
		install) cmd_install "$@" ;;
		-h|--help|help) usage 0 ;;
		*) die "unknown subcommand: $cmd (try: flash, probe, backup, install)" ;;
	esac
}

main "$@"
