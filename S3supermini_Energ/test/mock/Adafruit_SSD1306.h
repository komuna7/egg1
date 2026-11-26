#pragma once
#include <Arduino.h>
#include <Wire.h>
#define SSD1306_WHITE 1
#define SSD1306_BLACK 0
#define SSD1306_SWITCHCAPVCC 2
class Adafruit_SSD1306: public Print { public: Adafruit_SSD1306(int,int,TwoWire*,int){}
 bool begin(int,uint8_t){return true;} void clearDisplay(){} void display(){} void setTextSize(uint8_t){} void setTextColor(int){}
 void setCursor(int16_t,int16_t){} void getTextBounds(const String&,int16_t,int16_t,int16_t*,int16_t*,uint16_t*,uint16_t*){}
 void drawBitmap(int16_t,int16_t,const uint8_t*,int16_t,int16_t,int){} void fillRect(int16_t,int16_t,int16_t,int16_t,int){}
 void drawRect(int16_t,int16_t,int16_t,int16_t,int){} void drawFastHLine(int16_t,int16_t,int16_t,int){} size_t write(uint8_t) override {return 1;} };
