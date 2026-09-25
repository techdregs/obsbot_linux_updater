#include <dev/dev.hpp>
#include <dev/devs.hpp>
#include "dev-upgrade.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct UpgradeState
{
    std::mutex mutex;
    std::condition_variable cv;

    bool finished = false;
    bool success = false;

    int progress = 0;
    DevUpgrade::DevUgErrType error =
    DevUpgrade::DevUgErrNull;

    DevUpgrade *upgrade = nullptr;
};

static const char *errorName(DevUpgrade::DevUgErrType err)
{
    switch (err) {
        case DevUpgrade::DevUgErrNull:
            return "none";

        case DevUpgrade::DevUgErrOccupied:
            return "device occupied";

        case DevUpgrade::DevUgErrPkgOld:
            return "firmware package is too old";

        case DevUpgrade::DevUgErrPkgInvalid:
            return "invalid firmware package";
    }

    return "unknown";
}

static void progressCallback(
    void *param,
    int progress,
    DevUpgrade::DevUgErrType error,
    void *extra)
{
    (void)extra;

    auto *state = static_cast<UpgradeState *>(param);

    const uint32_t upgradeType =
    state->upgrade
    ? state->upgrade->getUpgradeType()
    : DevUpgrade::DevUgTypeButt;

    bool terminal = false;

    {
        std::lock_guard<std::mutex> lock(state->mutex);

        state->progress = progress;
        state->error = error;

        if (progress >= 0) {
            std::cout
            << "\rUpgrade progress: "
            << progress << "%"
            << std::flush;
        } else {
            std::cout
            << "\nSDK status: "
            << progress
            << ", error: "
            << errorName(error)
            << ", transport: "
            << upgradeType
            << '\n';
        }

        /*
         * UVC upgrade-mode transition.
         * This is transient, not terminal.
         */
        if (progress == -2 &&
            error == DevUpgrade::DevUgErrOccupied) {
            return;
            }

            /*
             * The SDK uses -3/null as a terminal failure after its
             * internal transport/reconnect attempts have been exhausted.
             *
             * Raw MTP messages such as "Read: device was disconnected"
             * are separate from this progress callback and should not be
             * treated as terminal by the wrapper.
             */
            if (progress == -3 &&
                error == DevUpgrade::DevUgErrNull) {

                state->finished = true;
            state->success = false;
            terminal = true;
                }

            /*
            * Explicit package errors are terminal.
            */
            if (progress < 0 &&
                error != DevUpgrade::DevUgErrNull &&
                error != DevUpgrade::DevUgErrOccupied) {

                state->finished = true;
            state->success = false;
            terminal = true;
                }

            /*
            * Successful completion.
            */
            if (progress == 100 &&
                error == DevUpgrade::DevUgErrNull) {

                state->finished = true;
            state->success = true;
            terminal = true;
                }
    }

    if (terminal) {
        state->cv.notify_all();
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr
        << "Usage:\n  "
        << argv[0]
        << " <firmware.bin>\n";

        return 2;
    }

    const std::string firmware = argv[1];

    const std::filesystem::path firmwarePath =
    std::filesystem::absolute(firmware);

    const std::string firmwareDir =
    firmwarePath.parent_path().string();

    if (!std::filesystem::is_regular_file(firmwarePath)) {
        std::cerr
        << "Firmware file does not exist: "
        << firmwarePath.string() << '\n';

        return 2;
    }

    /*
     * Initialize SDK discovery and disable network/mDNS discovery.
     * We only want a locally attached USB camera.
     */
    auto &devices = Devices::get();
    devices.setEnableMdnsScan(false);

    std::shared_ptr<Device> camera;

    std::cout << "Looking for OBSBOT camera...\n";

    /*
     * Give the SDK some time to enumerate the locally attached
     * USB camera. mDNS discovery is disabled above, so the normal
     * case should contain exactly one device.
     */
    for (int i = 0; i < 50; ++i) {
        auto deviceList = devices.getDevList();

        if (deviceList.size() == 1) {
            camera = deviceList.front();
            break;
        }

        if (deviceList.size() > 1) {
            std::cerr
            << "Multiple OBSBOT cameras were found.\n"
            << "Disconnect all but the camera to update.\n";

            return 1;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(200));
    }

    if (!camera) {
        std::cerr
        << "No OBSBOT camera was found.\n";

        return 1;
    }

    /*
     * The SDK provides the actual camera serial after discovery.
     * DevUpgrade uses this serial to track the same device across
     * the UVC -> MTP upgrade-mode transition.
     */
    const std::string serial = camera->devSn();

    if (serial.empty()) {
        std::cerr
        << "The detected camera did not report a serial number.\n";

        return 1;
    }

    std::cout
    << "Found: " << camera->devName() << '\n'
    << "Serial: " << camera->devSn() << '\n'
    << "Current firmware: "
    << camera->devVersion() << '\n'
    << "Firmware package: "
    << firmwarePath.string() << '\n'
    << "Firmware directory: "
    << firmwareDir << '\n';

    std::cout
    << "\nWARNING: This will update the firmware on the camera.\n"
    << "Do not disconnect USB or power during the upgrade.\n\n"
    << "Type UPGRADE to continue: "
    << std::flush;

    std::string confirmation;
    std::getline(std::cin, confirmation);

    if (confirmation != "UPGRADE") {
        std::cout << "Upgrade cancelled.\n";
        return 0;
    }

    std::cout << '\n';

    /*
     * IMPORTANT:
     *
     * false = normal upgrade of the locally attached camera.
     *
     * Do not use external_dev=true here. The normal path builds
     * the upgrade package against this camera's DevInfo and calls
     * PkgUg::verifyFirmware() before the transfer is allowed.
     */

    UpgradeState state;

    DevUpgrade upgrade(
        serial,
        DevUpgrade::DevUgTaskUpgrade,
        firmwarePath.string(),
                       false);

    /*
     * Required by the SDK's MTP path.
     * PkgManager scans this directory for firmware packages.
     */
    upgrade.setFwDir(firmwareDir);

    state.upgrade = &upgrade;

    upgrade.setUgProgressCallback(
        progressCallback,
        &state);

    /*
     * Do NOT force setUpgradeType().
     *
     * The SDK defaults to MTP and automatically chooses/falls back
     * to the appropriate UVC upgrade path for devices/protocol
     * versions that require it.
     */

    std::cout
    << "\nStarting firmware upgrade.\n"
    << "Do not disconnect power or USB.\n";

    upgrade.start();

    /*
     * start() launches the SDK worker on its own pthread.
     * Keep both UpgradeState and DevUpgrade alive until the
     * terminal callback arrives.
     */
    {
        std::unique_lock<std::mutex> lock(state.mutex);

        state.cv.wait(lock, [&state] {
            return state.finished;
        });
    }

    std::cout << '\n';

    if (!state.success) {
        std::cerr
        << "Firmware upgrade failed.\n"
        << "SDK error: "
        << errorName(state.error)
        << '\n';

        return 1;
    }

    std::cout
    << "Firmware upgrade completed successfully.\n"
    << "The camera may disconnect/reboot while returning "
    "to normal mode.\n";

    return 0;
}
