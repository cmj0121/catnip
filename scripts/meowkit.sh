#!/usr/bin/env bash
#
# meowkit.sh - flash-side helper for installing Catnip onto a MeowKit (ESP32-S3).
#
# Subcommands (added across issues #12/#13/#14):
#   flash        put the MeowKit into flash/download status; first-time setup (#12)
#   probe        just verify esptool can talk to the device (#12)
#   backup       read the full flash to a file and verify it (#13)
#   install      backup-first: back up stock, then build & flash Catnip (#13)
#   uninstall    restore stock: re-flash your backup, or official stock (#14)
#   restore-stock  download & flash the official MeowKit firmware (#14)
#   monitor      watch the serial log (the fastest way to see a boot succeed)
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
# Upstream MeowKit stock firmware bins (single-app 16 MB layout).
STOCK_BASE="${STOCK_BASE:-https://raw.githubusercontent.com/mingolucky/meowkit-s3-firmware/main/1.firmware/MeowKit}"

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

# Reset strategies, in the order worth trying. The MeowKit exposes the
# ESP32-S3's native USB-Serial-JTAG, where the classic DTR/RTS dance reaches
# nothing - `usb-reset` is the one that actually pokes the ROM there. Trying
# both costs a few seconds and saves reaching for the buttons.
ESP_RESETS="default-reset usb-reset"

# Run esptool, retrying with each reset strategy until one connects.
esp_run() {
	local r
	for r in $ESP_RESETS; do
		log "connecting (--before $r) ..."
		# --after no-reset: leave the chip in download mode. Resetting out of
		# it between steps would drop us back into the firmware, and the next
		# step (flashing right after a backup) would have nothing to talk to.
		if "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$PORT" --baud "$BAUD" \
			--before "$r" --after no-reset "$@"; then
			return 0
		fi
	done
	# Every strategy failed. Say so plainly: reporting success here would let
	# callers act on a connection that does not exist.
	return 1
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
		0) die "no serial device found. Power the MeowKit on (hold power 1-2s) - the USB port only appears while it is running - or set PORT=..." ;;
		*) die "multiple serial devices found (${found[*]}). Set PORT=... to choose one" ;;
	esac
}

download_mode_help() {
	cat <<'EOF'
If esptool cannot connect, put the MeowKit into ROM download mode by hand.
Two properties of this board make the usual recipe fail:

  * There is no RESET button on the case - RESET is only a pin on the GPIO
    expansion header - so you cannot tap reset while holding BOOT.
  * It has a battery, so unplugging USB does not cut power. The chip keeps
    running and never sees a power-up with BOOT held.

Use the power button to get a real cold start:

  1. Hold power 3-4s until the screen goes dark. The device is now off.
  2. Hold BOOT.
  3. With BOOT still held, press power 1-2s to switch it back on.
  4. Keep holding BOOT for another second or two, then release.

A screen that stays dark is the sign it worked: in download mode the ROM runs
instead of the firmware, so nothing is drawn. The port name can change when
the ROM takes over USB, so re-check it before retrying:

  ls /dev/cu.usbmodem*        (macOS)      ls /dev/ttyACM*  (Linux)

If that still fails, Espressif's browser flasher is the vendor's own route and
drives the reset itself: https://espressif.github.io/esp-launchpad/

Download mode is always reachable, which is what makes this recoverable: the
device cannot be permanently bricked by flashing.
EOF
}

cmd_probe() {
	resolve_esptool
	detect_port
	log "probing $CHIP on $PORT ..."
	if ! esp_run flash-id; then
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
	if ! esp_run read-flash 0x0 "$FLASH_SIZE_BYTES" "$out"; then
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
	# Actually start it, and check that it started. esptool's RTS reset is a
	# no-op on this board's native USB-Serial/JTAG: it prints "Hard resetting"
	# and the chip stays in the ROM bootloader, so a flash that "succeeded"
	# leaves a device that never runs what was just written. The watchdog reset
	# is the one that lands - but not always on the first attempt, because the
	# flasher stub is still winding down. So verify rather than announce: if the
	# bootloader still answers, the app is not running.
	log "starting the new firmware ..."
	if esp_restart; then
		log "running."
	else
		log "flashed, but it is still sitting in the bootloader."
		log "  power-cycle by hand: hold power 3-4s, then press it for 1-2s."
	fi

	log "Catnip installed. To revert to stock: make uninstall"
	log "--- boot log ---"
	serial_read "${LOG_SECONDS:-10}"
}

cmd_restore_stock() {
	resolve_esptool
	detect_port
	command -v curl >/dev/null 2>&1 || die "curl not found (needed to fetch stock firmware)"
	local tmp
	tmp="$(mktemp -d)"
	# shellcheck disable=SC2064
	trap "rm -rf '$tmp'" RETURN
	log "downloading official stock firmware from $STOCK_BASE ..."
	local f
	for f in bootloader.bin partitions.bin firmware.bin; do
		curl -fsSL "$STOCK_BASE/$f" -o "$tmp/$f" || die "download failed: $f"
	done
	log "flashing stock firmware (bootloader + partitions + app) ..."
	if ! "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$PORT" --baud "$BAUD" \
		write_flash --flash_size 16MB \
		0x0 "$tmp/bootloader.bin" \
		0x8000 "$tmp/partitions.bin" \
		0x10000 "$tmp/firmware.bin"; then
		download_mode_help
		die "stock flash failed"
	fi
	log "official stock firmware restored."
}

cmd_uninstall() {
	local latest="$BACKUP_DIR/.latest"
	local bfile=""
	[ -f "$latest" ] && bfile="$(cat "$latest")"
	if [ -n "$bfile" ] && [ -s "$bfile" ]; then
		resolve_esptool
		detect_port
		log "restoring the MeowKit from your backup: $bfile"
		if ! "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$PORT" --baud "$BAUD" \
			write_flash --flash_size 16MB 0x0 "$bfile"; then
			download_mode_help
			die "restore from backup failed"
		fi
		log "restored from backup - Catnip removed, stock is back."
	else
		log "no local backup found; restoring official stock firmware instead."
		cmd_restore_stock
	fi
}

# Restart the chip and check that it actually restarted. esptool's RTS reset is
# a no-op on this board's native USB-Serial/JTAG; the watchdog reset lands, but
# not always on the first attempt while the flasher stub is winding down. A
# bootloader that still answers means the application is not running.
esp_restart() {
	for _ in 1 2 3; do
		sleep 1
		"${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$PORT" \
			--before no-reset --after watchdog-reset chip-id >/dev/null 2>&1 || true
		sleep 2
		if ! "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$PORT" \
			--before no-reset --after no-reset chip-id >/dev/null 2>&1; then
			return 0
		fi
	done
	return 1
}

# Read the serial port for SECONDS, or until interrupted when given 0. Reading
# straight after a flash is the one way the boot log reliably reaches the host
# on this board, which is why install does it rather than leaving it to the
# reader to catch the timing.
serial_read() {
	local secs="$1" py
	for py in python3 /opt/homebrew/Cellar/esptool/*/libexec/bin/python; do
		[ -x "$(command -v "$py" 2>/dev/null || echo "$py")" ] || continue
		"$py" - "$PORT" "$secs" <<-'PY' && return 0
			import os, sys, time, glob
			try:
			    import serial
			except ImportError:
			    sys.exit(9)

			want, secs = sys.argv[1], float(sys.argv[2])
			end = time.time() + secs if secs > 0 else float("inf")

			# Stop when the shell that started this is gone. Without it a
			# killed `make monitor` leaves this holding the serial port, and
			# the next flash fails with the device apparently disconnected -
			# which reads as a hardware fault rather than a stale reader.
			parent = os.getppid()

			def orphaned():
			    return os.getppid() != parent

			def stream():
			    while time.time() < end and not orphaned():
			        ports = [want] if glob.glob(want) else sorted(glob.glob('/dev/cu.usbmodem*'))
			        if not ports:
			            time.sleep(0.2)
			            continue
			        try:
			            s = serial.Serial(ports[0], 115200, timeout=0.2)
			            # Over USB CDC the firmware waits for a host to attach
			            # before printing; opening the port does not say so.
			            s.dtr = True
			        except Exception:
			            time.sleep(0.2)
			            continue
			        try:
			            while time.time() < end and not orphaned():
			                d = s.read(512)
			                if d:
			                    sys.stdout.write(d.decode("utf-8", "replace"))
			                    sys.stdout.flush()
			        except Exception:
			            pass
			        finally:
			            try:
			                s.close()
			            except Exception:
			                pass
			        time.sleep(0.1)

			try:
			    stream()
			except KeyboardInterrupt:
			    pass
		PY
	done
	die "no python with pyserial found; try: pio device monitor -p $PORT -b 115200"
}

# Watch the serial log until interrupted.
cmd_monitor() {
	resolve_esptool
	detect_port
	if [ "${RESET:-0}" = 1 ]; then
		log "restarting the device so the log starts from boot ..."
		esp_restart || log "could not restart it; showing the log as-is."
	fi
	log "watching $PORT (ctrl-c to stop)"
	serial_read 0
}

usage() {
	sed -n '2,23p' "$0"
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
		uninstall) cmd_uninstall "$@" ;;
		restore-stock) cmd_restore_stock "$@" ;;
		monitor) cmd_monitor "$@" ;;
		-h|--help|help) usage 0 ;;
		*) die "unknown subcommand: $cmd (try: flash, probe, backup, install, uninstall, restore-stock, monitor)" ;;
	esac
}

main "$@"
