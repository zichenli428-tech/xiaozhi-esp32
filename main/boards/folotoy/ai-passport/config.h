#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <hal/adc_types.h>

// ============================================================================
// 音频:ES8311 codec,I2S0 全双工(MCLK/BCLK/WS 共用)
// 硬件事实源:ai-passport/components/bsp/include/bsp_pins.h
// ============================================================================
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_6
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_3
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_4   // 录音:codec -> MCU
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_2   // 播放:MCU -> codec

// 功放使能未接 MCU(常通)
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_NC
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_10
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_7
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

// 无板载 LED
#define BUILTIN_LED_GPIO        GPIO_NUM_NC

// ============================================================================
// 按键:上/下/确定 三键共用 GPIO0 / ADC1_CH0 电阻分压(外部 10k 上拉)
//   上   : 0Ω   -> ~0   mV  (窗口 0-150)
//   下   : 1kΩ  -> ~300 mV  (窗口 150-447)
//   确定 : 2.2kΩ-> ~595 mV  (窗口 447-1900)
// ⚠ 禁用内部上拉(约 45kΩ 会把三档挤到 0~154mV 并随温漂重叠)
// ============================================================================
#define BUTTON_ADC_UNIT         ADC_UNIT_1
#define BUTTON_ADC_CHANNEL      ADC_CHANNEL_0   // GPIO0

#define VOLUME_UP_BUTTON_MV_MIN    0
#define VOLUME_UP_BUTTON_MV_MAX    150
#define VOLUME_DOWN_BUTTON_MV_MIN  150
#define VOLUME_DOWN_BUTTON_MV_MAX  447
#define OK_BUTTON_MV_MIN           447
#define OK_BUTTON_MV_MAX           1900

// ============================================================================
// 显示:ST7789P3 240x320,4-line SPI mode0,需反色
// ============================================================================
#define DISPLAY_SPI_SCK_PIN     GPIO_NUM_8
#define DISPLAY_SPI_MOSI_PIN    GPIO_NUM_9
#define DISPLAY_DC_PIN          GPIO_NUM_20
#define DISPLAY_SPI_CS_PIN      GPIO_NUM_1
#define DISPLAY_SPI_CLK_HZ      (40 * 1000 * 1000)

#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY  false

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

#define DISPLAY_BACKLIGHT_PIN            GPIO_NUM_21
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT  false

#endif // _BOARD_CONFIG_H_
