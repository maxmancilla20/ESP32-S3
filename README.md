# ESP32-S3 — IoT Protocol Experiments

Personal sandbox for IoT connectivity protocols on the ESP32-S3, built with
ESP-IDF. Starting point: a CoAP server adapted from the ESP-IDF example,
used as a base to explore constrained-device messaging before expanding
into MQTT and Matter.

## Current Implementation: CoAP Server
- Based on the ESP-IDF `libcoap` CoAP server example (RFC 7252)
- Supports DTLS via Pre-Shared Key (PSK) or PKI
- Responds to client GET/PUT requests on `/Espressif` and
  `/.well-known/core`
- Optional OSCORE (RFC 8613) support

## Build & Flash
```bash
idf.py menuconfig   # configure Wi-Fi SSID/password, PSK, CoAP options
idf.py build
idf.py -p PORT flash monitor
```

## Roadmap
- [ ] MQTT client/broker integration
- [ ] Matter device commissioning
- [ ] Combined protocol demo (CoAP + MQTT bridge)

## Hardware
ESP32-S3 (also compatible with ESP32, C2, C3, C6, H2, S2 targets)
