// CellWise CW2017 电量计驱动:I2C 0x63,与 ES8311 共用 I2C0(SDA=10/SCL=7)。
// 芯片自带 Li-Poly profile,直接读 SOC%,无需外部分压电阻与查表。
// 可选外设:初始化失败(芯片不应答)时 Present()==false,上层降级为不显示电池。
#pragma once

#include <esp_err.h>
#include <driver/i2c_master.h>

class Cw2017Battery {
public:
    // 在既有 I2C master bus 上挂载 CW2017(不新建总线)。
    // 芯片不应答时返回 ESP_ERR_NOT_FOUND。
    esp_err_t Init(i2c_master_bus_handle_t bus);

    bool Present() const { return present_; }

    // 剩余电量百分比 0..100;读取失败返回 -1。
    int GetSoc();

    // 电池电压 mV;读取失败返回 -1。
    int GetMillivolts();

private:
    bool present_ = false;
    i2c_master_dev_handle_t dev_ = nullptr;
};
