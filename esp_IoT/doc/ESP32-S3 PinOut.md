# ESP32-S3 GPIO Pinout and Wiring

This document shows the GPIO assignments currently used by the project and the GPIOs not assigned by its firmware. The diagram is a **logical ESP32-S3 pin map**, not a board-specific physical header layout: exact header positions and available pins depend on the ESP32-S3 development board and module.

ESP32-S3 GPIO signals use **3.3 V logic**. Do not apply 5 V to a GPIO.

## ESP32-S3 pin map

```text
                       ESP32-S3
             ┌──────────────────────────────────────────┐
 GPIO  0  ···│                                          │··· GPIO 48  FREE*
 GPIO  1  ···│                                          │··· GPIO 47  FREE*
 GPIO  2  ···│                                          │··· GPIO 46  FREE* / STRAP
 GPIO  3  ···│                                          │··· GPIO 45  FREE* / STRAP
 GPIO  4  ───│ IR receiver input                        │··· GPIO 44  FREE*
 GPIO  5  ───│ IR transmitter output                    │··· GPIO 43  FREE*
 GPIO  6  ───│ DHT11 data                               │··· GPIO 42  FREE*
 GPIO  7  ───│ TFT backlight                            │··· GPIO 41  FREE*
 GPIO  8  ───│ TFT reset                                │··· GPIO 40  FREE*
 GPIO  9  ───│ TFT data/command                         │··· GPIO 39  FREE*
 GPIO 10  ───│ TFT chip select                          │··· GPIO 38  RGB LED (WS2812)
 GPIO 11  ───│ TFT SPI MOSI                             │··· GPIO 37  FREE* / FLASH-PSRAM**
 GPIO 12  ───│ TFT SPI clock                            │··· GPIO 36  FREE* / FLASH-PSRAM**
 GPIO 13  ───│ Passive buzzer PWM                       │··· GPIO 35  FREE* / FLASH-PSRAM**
 GPIO 14  ···│                                          │··· GPIO 34  FREE* / FLASH-PSRAM**
 GPIO 15  ···│                                          │··· GPIO 33  FREE* / FLASH-PSRAM**
 GPIO 16  ···│                                          │··· GPIO 32  FREE* / FLASH**
 GPIO 17  ···│                                          │··· GPIO 31  FREE* / FLASH**
 GPIO 18  ───│ I2C SDA (OLED + BMP180)                  │··· GPIO 30  FREE* / FLASH**
 GPIO 19  ───│ I2C SCL (OLED + BMP180)                  │··· GPIO 29  FREE* / FLASH**
 GPIO 20  ···│ FREE*                                    │··· GPIO 28  FREE* / FLASH**
 GPIO 21  ···│ FREE*                                    │··· GPIO 27  FREE* / FLASH**
             │                                          │··· GPIO 26  FREE* / FLASH**
             └──────────────────────────────────────────┘

Legend: ─── used by this project     ··· not assigned by this project
```

**GPIO 22–25 do not exist on the ESP32-S3.**

- `FREE*` means the project firmware does not currently assign that GPIO. It does **not** guarantee the pin is exposed on your board or safe to use with every peripheral.
- `FLASH**` and `FLASH-PSRAM**` are not used by this project's firmware, but may be connected internally to flash or PSRAM in your ESP32-S3 module. Do not use them unless the exact module's datasheet confirms they are available.
- GPIO 0, 3, 45, and 46 are strapping pins. External circuitry can affect boot mode; use caution before connecting loads or pull resistors.
- GPIO 19 and 20 can be used for native USB D-/D+ on ESP32-S3 boards. GPIO 19 is assigned to I2C SCL here, so native USB functionality may conflict if your board uses those pins for USB.
- Board-specific functions and onboard peripherals may reserve additional pins. Check the pinout and module datasheet for your exact board before wiring anything to a pin marked free.

## GPIO assignments in this project

| GPIO | Project use | Connected device / signal |
|---:|---|---|
| 4 | IR receiver input | IR receiver `OUT` / `DATA` |
| 5 | IR transmitter output | IR transmitter `IN` / `SIG` |
| 6 | Single-wire data | DHT11 `DATA` / `OUT` |
| 7 | Backlight control | ST7735 TFT `LED` / `BL` |
| 8 | Display reset | ST7735 TFT `RST` / `RES` |
| 9 | Display data/command select | ST7735 TFT `DC` / `A0` |
| 10 | SPI chip select | ST7735 TFT `CS` |
| 11 | SPI data from ESP32-S3 (MOSI) | ST7735 TFT `SDA` / `MOSI` / `DIN` |
| 12 | SPI clock | ST7735 TFT `SCK` / `SCL` / `CLK` |
| 13 | LEDC PWM output for melody | Passive buzzer `SIG` / `+` |
| 18 | I2C data (SDA) | OLED and BMP180 share this bus |
| 19 | I2C clock (SCL) | OLED and BMP180 share this bus |
| 38 | Addressable RGB LED data | Onboard WS2812 `DIN` |

The TFT pin labelled `SDA` is SPI MOSI, **not** I2C SDA. The TFT's MISO/SDO pin is not connected.

## Pins not assigned by this project's firmware

The following ESP32-S3 GPIO numbers are not assigned in the project source:

- **GPIO 0–3**
- **GPIO 14–17**
- **GPIO 20–21**
- **GPIO 26–37**
- **GPIO 39–48**

GPIO 22–25 are not available on the ESP32-S3. Treat GPIO 26–37 as potentially reserved for module flash/PSRAM, and check the exact board/module documentation before using any unassigned pin.

## Wiring

```text
ESP32-S3                 ST7735 TFT
────────                 ──────────
GPIO 12 ──────────────── SCK / SCL / CLK
GPIO 11 ──────────────── SDA / MOSI / DIN
GPIO 10 ──────────────── CS
GPIO  9 ──────────────── DC / A0
GPIO  8 ──────────────── RST / RES
GPIO  7 ──────────────── LED / BL
3V3     ──────────────── VCC
GND     ──────────────── GND

ESP32-S3                 OLED SSD1306       BMP180
────────                 ────────────       ──────
GPIO 18 ──────────────── SDA ────────────── SDA
GPIO 19 ──────────────── SCL ────────────── SCL
3V3     ──────────────── VCC                VCC / VIN*
GND     ──────────────── GND ────────────── GND

ESP32-S3                 DHT11
────────                 ─────
GPIO  6 ──────────────── DATA / OUT
3V3     ──────────────── VCC
GND     ──────────────── GND

ESP32-S3                 Passive buzzer
────────                 ──────────────
GPIO 13 ──────────────── SIG / +
GND     ──────────────── GND / -

ESP32-S3                 IR receiver      IR transmitter
────────                 ───────────      ──────────────
GPIO  4 ──────────────── OUT / DATA
GPIO  5 ──────────────────────────────── IN / SIG
3V3/GND ──────────────── VCC/GND          VCC/GND*
```

`*` Confirm the module's permitted supply voltage before connecting VCC. The IR transmitter circuit depends on the module; a bare IR LED or a load drawing more current than a GPIO can supply needs a transistor driver. Do not drive it directly from GPIO 5.

## Electrical notes

- Connect the ESP32-S3 and peripheral grounds together.
- Use 3.3 V signals. Never apply 5 V to an ESP32-S3 GPIO.
- The OLED and BMP180 share I2C on GPIO 18/19; their usual addresses are `0x3C` and `0x77`, respectively.
- A bare four-pin DHT11 may need a 4.7–10 kΩ pull-up resistor between DATA and 3V3. Many breakout boards already include one.
- The buzzer is passive and receives PWM from GPIO 13. Use a transistor driver if the buzzer requires more current than a GPIO can safely provide.
- The IR receiver and transmitter are separate circuits; check each module's labels and voltage requirements.

## Source configuration

- TFT ST7735: `drivers/st7735_display/src/st7735_display.c`
- OLED SSD1306: `drivers/oled_display/src/oled_display.c`
- BMP180 I2C pins: `sdkconfig` (`CONFIG_EXAMPLE_I2C_MASTER_SDA` and `CONFIG_EXAMPLE_I2C_MASTER_SCL`)
- DHT11: `main/main.c`
- Buzzer PWM: `drivers/buzzer/src/buzzer.c`
- Infrared transceiver: `drivers/ir_nec/inc/ir_nec_transceiver.h`
- Onboard RGB LED: `drivers/rgb_led/inc/rgb_led.h`
