#!/bin/sh
# Programmas loģikas pārbaude datorā (bez ESP32): plūsmas lasīšana, stacijas izvēle, cenu intervāli.
# Vajag g++ un git. ArduinoJson tiek lejupielādēts mapē test/.cache.
set -e
cd "$(dirname "$0")"
[ -d .cache/ArduinoJson ] || git clone -q --depth 1 --branch v7.4.3 https://github.com/bblanchon/ArduinoJson .cache/ArduinoJson
cp ../S3supermini_Energ.ino .cache/energ.cpp
g++ -std=gnu++2b -g -fsanitize=address,undefined -Wall -Wno-unused-parameter \
  -I mock -I .cache -I .cache/ArduinoJson/src -I .. test_main.cpp -o .cache/test
TZ=EET-2EEST,M3.5.0/3,M10.5.0/4 .cache/test
