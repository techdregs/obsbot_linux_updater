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

During the firmware update, the camera may disconnect from USB and reconnect several times. This is expected.

The OBSBOT updater switches the camera between its normal USB video mode and a temporary firmware-update/MTP mode, so you may see messages such as:

```text
USB disconnect
device was disconnected
remove mtp device
find a new mtp device
open mtp device success
```

You may also see the camera temporarily disappear from /dev/video* and then reappear.

Do not physically unplug the camera, disconnect power, or interrupt the updater just because the camera temporarily disappears from the system.

Some low-level messages such as:

```text
Read: timeout get urb
WriteBulkx: device was disconnected
```

may also appear during USB mode transitions and are not necessarily failures by themselves.

The important thing is to let the updater continue unless it reports an explicit error or exits.

If the updater appears completely stuck for an extended period while repeatedly printing only:

```text
Read: device was disconnected
```

that indicates something has gone wrong rather than a normal mode transition.

Notes:
1. Connect the camera directly to a USB root port, not through a downstream hub. The OBSBOT Linux SDK used here cannot correctly rediscover the camera in firmware-update mode when Linux gives it a dotted USB topology path such as 1-2.2. The launcher detects this condition and refuses to proceed.
2. The tool currently patches the x86-64 OBSBOT libdev.so implementation at runtime to work around an MTP reconnect deadlock encountered during firmware updates.
3. The patcher does not use a fixed binary offset. It identifies the expected mtp::Session::Close() → mtp::PipePacketer::Read() call and refuses to continue unless it can identify that call uniquely.
4. The patched library is placed under ~/.cache/obsbot-camera-control/fwupdate-lib/; the original SDK library is never modified.
5. Firmware flashing is inherently risky. The explicit UPGRADE confirmation is intentional.
6. This was only tested on one Obsbot Tiny 2 Lite using the OBSBOT SDK libdev.so.1.0.2, so who knows if it'll work for you.
7. No warranty, support, or guarantee is provided. If you use this, you accept the risk of rendering your camera unusable.
