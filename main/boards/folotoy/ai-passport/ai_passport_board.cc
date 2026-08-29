#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "cw2017_battery.h"

#include <esp_log.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_wifi.h>

#define TAG "AiPassportBoard"

class AiPassportBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    LcdDisplay* display_;
    PowerSaveTimer* power_save_timer_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    // 三键共用 ADC1_CH0,button_adc 驱动按 button_index 区分,内部共享同一个 ADC unit
    AdcButton ok_button_;
    AdcButton up_button_;
    AdcButton down_button_;

    // CW2017 电量计:与 ES8311 共用 I2C0,可选外设(不在时电池图标自动隐藏)
    Cw2017Battery battery_;

    void InitializeCodecI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));

        // i2c_master_probe() 只检查 ACK,悬浮/弱上拉的 SDA 也可能误 ACK,故校验芯片 ID。
        if (!IsEs8311Present()) {
            while (true) {
                ESP_LOGE(TAG, "ES8311 not detected, please check if you have installed the correct firmware");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
    }

    // 读 ES8311 芯片 ID 寄存器(0xFD/0xFE 应为 0x83/0x11)
    bool IsEs8311Present() {
        i2c_master_dev_handle_t dev = nullptr;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x18,
            .scl_speed_hz = 100 * 1000,
        };
        if (i2c_master_bus_add_device(codec_i2c_bus_, &dev_cfg, &dev) != ESP_OK) {
            return false;
        }

        uint8_t reg = 0xFD;
        uint8_t id1 = 0, id2 = 0;
        esp_err_t err1 = i2c_master_transmit_receive(dev, &reg, 1, &id1, 1, 100);
        reg = 0xFE;
        esp_err_t err2 = i2c_master_transmit_receive(dev, &reg, 1, &id2, 1, 100);
        i2c_master_bus_rm_device(dev);

        ESP_LOGI(TAG, "ES8311 chip id: err=(%s,%s) id=0x%02X 0x%02X",
            esp_err_to_name(err1), esp_err_to_name(err2), id1, id2);
        return err1 == ESP_OK && err2 == ESP_OK && id1 == 0x83 && id2 == 0x11;
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void ChangeVol(int delta) {
        auto codec = GetAudioCodec();
        int volume = codec->output_volume() + delta;
        if (volume > 100) {
            volume = 100;
        }
        if (volume < 0) {
            volume = 0;
        }
        codec->SetOutputVolume(volume);
        ESP_LOGI(TAG, "Volume: %d", volume);
    }

    void InitializeButtons() {
        // 上键:音量+;长按最大
        up_button_.OnClick([this]() { ChangeVol(10); });
        up_button_.OnLongPress([this]() { GetAudioCodec()->SetOutputVolume(100); });

        // 下键:音量-;长按静音
        down_button_.OnClick([this]() { ChangeVol(-10); });
        down_button_.OnLongPress([this]() { GetAudioCodec()->SetOutputVolume(0); });

        // 确定键:启动阶段进入配网,其他时候切换对话状态
        ok_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        ok_button_.OnPressDown([this]() {
            if (power_save_timer_) {
                power_save_timer_->WakeUp();
            }
        });
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = DISPLAY_SPI_CLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;   // RST 未接 MCU,走 SWRESET 软复位
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));

        esp_lcd_panel_reset(panel_);
        esp_lcd_panel_init(panel_);
        esp_lcd_panel_invert_color(panel_, true);   // 本屏出厂即需反色
        esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new SpiLcdDisplay(panel_io_, panel_,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializePowerSaveTimer() {
        // 空闲 60s 进入省电:灭屏降亮 + WiFi modem-sleep;任意按键唤醒
        power_save_timer_ = new PowerSaveTimer(-1, 60, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling modem-sleep mode");
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
            esp_wifi_set_ps(WIFI_PS_NONE);
        });
        power_save_timer_->SetEnabled(true);
    }

public:
    AiPassportBoard()
        : ok_button_({.adc_handle = nullptr, .unit_id = BUTTON_ADC_UNIT, .adc_channel = BUTTON_ADC_CHANNEL, .button_index = 2, .min = OK_BUTTON_MV_MIN, .max = OK_BUTTON_MV_MAX}),
          up_button_({.adc_handle = nullptr, .unit_id = BUTTON_ADC_UNIT, .adc_channel = BUTTON_ADC_CHANNEL, .button_index = 0, .min = VOLUME_UP_BUTTON_MV_MIN, .max = VOLUME_UP_BUTTON_MV_MAX}),
          down_button_({.adc_handle = nullptr, .unit_id = BUTTON_ADC_UNIT, .adc_channel = BUTTON_ADC_CHANNEL, .button_index = 1, .min = VOLUME_DOWN_BUTTON_MV_MIN, .max = VOLUME_DOWN_BUTTON_MV_MAX}) {
        InitializeCodecI2c();
        battery_.Init(codec_i2c_bus_);   // 可选外设:失败仅降级(不显示电池图标)
        InitializeSpi();
        InitializeSt7789Display();
        InitializePowerSaveTimer();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();

        // ⚠ 注意:本板绝不可烧写 ESP_EFUSE_VDD_SPI_AS_GPIO(xmini/surfer 等 C3 板会烧),
        // 那会永久破坏 USB-Serial/JTAG(GPIO18/19)控制台与烧录通道。
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_,
            I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT, 5000);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        // 本板无充电检测引脚(未证实),充电状态未知;电池在线即视为放电中
        charging = false;
        discharging = battery_.Present();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = battery_.GetSoc();
        return level >= 0;   // CW2017 不在/读失败 → 返回 false,框架隐藏电池图标
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(AiPassportBoard);
