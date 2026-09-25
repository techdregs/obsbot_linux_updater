# obsbot_linux_updater
 A firmware updater for Obsbot webcams for Linux
 
*** I offer this without support or guarantees. Brick your camera at your own risk. ***

install https://github.com/aaronsb/obsbot-camera-control

Place files in /src/cli and copy in the /tools folder as illustrated below.
Replace CMakeLists.txt with the version included here.

```text
obsbot-camera-control/
├── src/
│   └── cli/
│       ├── obsbot_fwupdate.cpp
│       └── dev-upgrade.hpp
├── tools/
│   ├── obsbot-fwupdate.sh
│   └── patch-libdev-mtp-close.py
└── CMakeLists.txt
```

Open a cli in the parent folder and build:

```text
cd obsbot-camera-control

cmake -S . -B build
cmake --build build -j
```

Updating the firmware, run:

```text
./tools/obsbot-fwupdate.sh /path/to/firmware.bin
```

The updater will automatically detect the connected OBSBOT camera and its serial number.

Before flashing, it will display the detected camera, current firmware version, and firmware package. You must explicitly type:

```text
UPGRADE
```
to begin the update.

Notes:
1. Connect the camera directly to a USB root port, not through a downstream hub. The OBSBOT Linux SDK used here cannot correctly rediscover the camera in firmware-update mode when Linux gives it a dotted USB topology path such as 1-2.2. The launcher detects this condition and refuses to proceed.
2. The tool currently patches the x86-64 OBSBOT libdev.so implementation at runtime to work around an MTP reconnect deadlock encountered during firmware updates.
3. The patcher does not use a fixed binary offset. It identifies the expected mtp::Session::Close() → mtp::PipePacketer::Read() call and refuses to continue unless it can identify that call uniquely.
4. The patched library is placed under ~/.cache/obsbot-camera-control/fwupdate-lib/; the original SDK library is never modified.
5. Firmware flashing is inherently risky. The explicit UPGRADE confirmation is intentional.
6. This was only tested on one Obsbot Tiny 2 Lite using the OBSBOT SDK libdev.so.1.0.2, so who knows if it'll work for you.
7. No warranty, support, or guarantee is provided. If you use this, you accept the risk of rendering your camera unusable.
