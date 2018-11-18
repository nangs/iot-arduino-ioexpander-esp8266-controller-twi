# iot-arduino-ioexpander-esp8266-controller-twi

arduino nano as ioexpander controlled by esp8266 through TWI

## browser page

![img](doc/home.png)

## requirements

- esp8266 nodemcu
- arduino nano V3

## features

- imagemap of arduino nano ports
- index.htm, app.js, image.png served by esp8266 ( encoded into flash by [gen-h](esp8266-controller-twi/gen-h) )
- scalable TWI slave devices

## api

| **api** | **description** | **result example** |
|---|---|---|
| `/api/scan` | list of twi slave devices addresses (dec) | `[8,19]` |