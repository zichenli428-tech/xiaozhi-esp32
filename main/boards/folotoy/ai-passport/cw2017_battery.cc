// 移植自 ai-passport components/bsp/src/bsp_battery.c:
// 去掉了 bsp_i2c 依赖,改为接收板型已建好的 i2c_master_bus_handle_t(共享总线,不另建)。
// (去掉了电池 profile 写入部分:开源硬件用户电池各异,用芯片自带 Li-Poly profile 更通用)
#include "cw2017_battery.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Cw2017Battery"

#define CW2017_ADDR     0x63   // 7bit 地址
#define CW_REG_VERSION  0x00   // 版本号,上电应答即代表芯片在位
#define CW_REG_VCELL_H  0x02   // 14bit 电压,V(uV) = raw * 312.5
#define CW_REG_SOC_H    0x04   // 高字节 = 整数百分比;低字节(0x05)= 1/256 %
#define CW_REG_CONFIG   0x08   // 0xF0=睡眠 / 0x30=复位态 / 0x00=正常

esp_err_t Cw2017Battery::Init(i2c_master_bus_handle_t bus) {
    if (!bus || present_) {
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CW2017_ADDR,
        .scl_speed_hz = 100 * 1000,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev_) != ESP_OK) {
        return ESP_FAIL;
    }

    uint8_t reg = CW_REG_VERSION;
    uint8_t ver = 0;
    if (i2c_master_transmit_receive(dev_, &reg, 1, &ver, 1, 100) != ESP_OK) {
        ESP_LOGW(TAG, "CW2017 not responding (0x%02X), battery gauge optional - battery icon disabled",
                 CW2017_ADDR);
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "CW2017 detected, VERSION=0x%02X", ver);

    // 确保处于正常工作模式(非睡眠/复位态),用芯片自带 Li-Poly profile,不写自定义 profile。
    uint8_t w[2] = { CW_REG_CONFIG, 0x00 };
    i2c_master_transmit(dev_, w, 2, 100);
    vTaskDelay(pdMS_TO_TICKS(100));   // 等首次 SOC 计算完成

    present_ = true;
    return ESP_OK;
}

int Cw2017Battery::GetSoc() {
    if (!present_ || !dev_) {
        return -1;
    }
    uint8_t reg = CW_REG_SOC_H;
    uint8_t b[2] = { 0 };
    if (i2c_master_transmit_receive(dev_, &reg, 1, b, 2, 100) != ESP_OK) {
        return -1;
    }
    int soc = b[0];                   // 高字节即整数百分比
    if (soc > 100) {
        return -1;                    // 芯片未就绪时可能读到 0xFF
    }
    return soc;
}

int Cw2017Battery::GetMillivolts() {
    if (!present_ || !dev_) {
        return -1;
    }
    uint8_t reg = CW_REG_VCELL_H;
    uint8_t b[2] = { 0 };
    if (i2c_master_transmit_receive(dev_, &reg, 1, b, 2, 100) != ESP_OK) {
        return -1;
    }
    uint32_t raw = ((uint32_t)b[0] << 8 | b[1]) & 0x3FFF;   // 14bit
    return (int)((raw * 3125) / 10000);                     // raw * 312.5uV → mV
}
