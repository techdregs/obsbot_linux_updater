#pragma once

#include <cstdint>
#include <functional>
#include <string>

class DevUpgradePrivate;

class DevUpgrade
{
public:
    enum DevUgTaskType : unsigned int {
        DevUgTaskUpgrade = 0,
        DevUgTaskLog     = 1
    };

    enum DevUgType : unsigned int {
        DevUgTypeUvcFirst = 0,
        DevUgTypeUvc      = 1,
        DevUgTypeMtp      = 2,
        DevUgTypeButt     = 3
    };

    enum DevUgErrType : unsigned int {
        DevUgErrNull       = 0,
        DevUgErrOccupied   = 1,
        DevUgErrPkgOld     = 2,
        DevUgErrPkgInvalid = 3
    };

    using UpgradeProgressCallback =
    std::function<void(void *, int, DevUgErrType, void *)>;

    DevUpgrade(const std::string &dev_sn,
               DevUgTaskType task_type,
               const std::string &fw_path,
               bool external_dev);

    ~DevUpgrade();

    void start();

    void setTaskType(DevUgTaskType task_type);
    void setFwDir(const std::string &fw_dir);
    void setUgPatchDir(const std::string &patch_dir);
    void setLogDir(const std::string &log_dir);

    void setUgProgressCallback(UpgradeProgressCallback cb,
                               void *param);

    void setUpgradeType(DevUgType type);
    uint32_t getUpgradeType();

private:
    DevUpgradePrivate *d_ptr;
};
