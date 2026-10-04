#ifndef RGB_LED_H_
#define RGB_LED_H_

#include <stdint.h>

// Data pin connected to the onboard addressable LED.
#define RGB_PIN 38

// Prototipos de funciones
void init_led_color(void);
void set_led_color(uint32_t r, uint32_t g, uint32_t b);

#endif /* RGB_LED_H_ */
