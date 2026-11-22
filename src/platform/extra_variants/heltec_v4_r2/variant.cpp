#include "configuration.h"

#if defined(HELTEC_V4_R2_ILI9488)
#include <Arduino.h>
#include <SPI.h>

static_assert(LGFX_PIN_CS != HELTEC_EXT_TOUCH_CS && LGFX_PIN_CS != HELTEC_EXT_SD_CS && HELTEC_EXT_TOUCH_CS != HELTEC_EXT_SD_CS,
              "LCD, touch and SD need independent chip selects");
static_assert(LORA_SCK == 9 && LORA_MOSI == 10 && LORA_MISO == 11 && LORA_CS == 8,
              "External SPI must preserve the SX1262 pin assignment");
#if defined(HELTEC_V4_R2_P04)
static_assert(SD_SPI_FREQUENCY >= 400000 && SD_SPI_FREQUENCY <= 4000000, "P04 SD clock exceeds bring-up limits");
#endif

void earlyInitVariant()
{
    // Park every external CS before any clocks; LoRa uses its own SPI3 host.
    for (int pin : {LGFX_PIN_CS, HELTEC_EXT_TOUCH_CS, HELTEC_EXT_SD_CS}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    SPI.begin(LGFX_PIN_SCK, LGFX_PIN_MISO, LGFX_PIN_MOSI, LGFX_PIN_CS);
}
#endif
