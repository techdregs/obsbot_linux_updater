#!/usr/bin/env bash
set -euo pipefail

ROOT="$(
    cd "$(dirname "${BASH_SOURCE[0]}")/.."
    pwd
)"

LIB="$ROOT/sdk/lib/libdev.so.1.0.2"
UPDATER="$ROOT/bin/obsbot-fwupdate"
PATCHER="$ROOT/tools/patch-libdev-mtp-close.py"

CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}"
PATCHDIR="$CACHE_ROOT/obsbot-camera-control/fwupdate-lib"
PATCHED_LIB="$PATCHDIR/libdev.so.1.0.2"

usage()
{
    echo "Usage:"
    echo "  $0 <firmware.bin>"
}

if [[ $# -ne 1 ]]; then
    usage
    exit 2
fi

FIRMWARE="$1"

if [[ ! -f "$FIRMWARE" ]]; then
    echo "ERROR: Firmware file does not exist:" >&2
    echo "  $FIRMWARE" >&2
    exit 1
fi

if [[ ! -r "$FIRMWARE" ]]; then
    echo "ERROR: Firmware file is not readable:" >&2
    echo "  $FIRMWARE" >&2
    exit 1
fi

FIRMWARE="$(
    realpath "$FIRMWARE"
)"

if [[ ! -x "$UPDATER" ]]; then
    echo "ERROR: Firmware updater is not built:" >&2
    echo "  $UPDATER" >&2
    echo >&2
    echo "Build it first with:" >&2
    echo "  cmake -S . -B build" >&2
    echo "  cmake --build build -j" >&2
    exit 1
fi

if [[ ! -f "$LIB" ]]; then
    echo "ERROR: OBSBOT SDK library not found:" >&2
    echo "  $LIB" >&2
    exit 1
fi

if [[ ! -f "$PATCHER" ]]; then
    echo "ERROR: Patch utility not found:" >&2
    echo "  $PATCHER" >&2
    exit 1
fi


echo "Checking USB connection..."

obsbot_count=0

for device in /sys/bus/usb/devices/*; do
    [[ -f "$device/idVendor" ]] || continue

    vendor="$(
        cat "$device/idVendor" 2>/dev/null || true
    )"

    [[ "$vendor" == "3564" ]] || continue

    obsbot_count=$((obsbot_count + 1))

    usb_path="$(basename "$device")"

    product="$(
        cat "$device/product" 2>/dev/null || true
    )"

    echo
    echo "OBSBOT USB device:"
    echo "  USB path: $usb_path"

    if [[ -n "$product" ]]; then
        echo "  Product:  $product"
    fi

    #
    # OBSBOT libdev.so parses Linux USB device paths with:
    #
    #     %u-%255[0-9]
    #
    # A path such as 1-2.2 is consequently interpreted as
    # 1-2, causing upgrade-mode MTP discovery to inspect the
    # upstream hub instead of the camera.
    #
    if [[ "$usb_path" == *.* ]]; then
        cat >&2 <<EOF

ERROR:
The OBSBOT camera is connected through a downstream USB hub:

  $usb_path

The OBSBOT Linux SDK used by this updater cannot correctly
rediscover an upgrade-mode camera at a dotted Linux USB path
such as 1-2.2.

Connect the camera directly to a motherboard/root USB port and
try again.
EOF
        exit 1
    fi

    #
    # Starting DevUpgrade while the camera is already in its
    # temporary MTP upgrade personality is not the normal
    # supported starting state.
    #
    if [[ "$product" == "USB Function Filesystem" ]]; then
        cat >&2 <<EOF

ERROR:
The camera appears to already be in MTP upgrade mode:

  $product

Return the camera to its normal UVC mode before starting a new
firmware update.
EOF
        exit 1
    fi
done

if [[ "$obsbot_count" -eq 0 ]]; then
    cat >&2 <<EOF

ERROR:
No locally attached OBSBOT USB device was found.

Connect the camera and try again.
EOF
    exit 1
fi

if [[ "$obsbot_count" -gt 1 ]]; then
    cat >&2 <<EOF

ERROR:
More than one OBSBOT USB device was found.

Disconnect all but the camera that should be updated.
EOF
    exit 1
fi


echo
echo "Preparing patched OBSBOT SDK..."

mkdir -p "$PATCHDIR"

python3 \
    "$PATCHER" \
    "$LIB" \
    "$PATCHED_LIB"

echo
echo "Checking dynamic library selection..."

resolved="$(
    LD_LIBRARY_PATH="$PATCHDIR" \
        ldd "$UPDATER" |
        grep 'libdev\.so' || true
)"

if [[ -z "$resolved" ]]; then
    echo "ERROR: ldd did not report libdev.so." >&2
    exit 1
fi

echo "  $resolved"

expected="$PATCHDIR/libdev.so.1.0.2"

if [[ "$resolved" != *"$expected"* ]]; then
    cat >&2 <<EOF

ERROR:
The firmware updater is not resolving libdev.so to the patched
copy.

Expected:
  $expected

Actual:
  $resolved

Refusing to start the firmware update.
EOF
    exit 1
fi


echo
echo "Patched SDK verified."
echo
echo "Firmware:"
echo "  $FIRMWARE"
echo
echo "Starting firmware updater..."
echo

exec env \
    LD_LIBRARY_PATH="$PATCHDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$UPDATER" \
    "$FIRMWARE"
SH
