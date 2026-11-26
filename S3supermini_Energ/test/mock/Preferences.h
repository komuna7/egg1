#pragma once
#include <Arduino.h>
class Preferences { public: bool begin(const char*,bool){return true;} void end(){}
 String getString(const char*,const String& d=String()){return d;} size_t putString(const char*,const String&){return 0;}
 float getFloat(const char*,float d){return d;} size_t putFloat(const char*,float){return 0;}
 uint16_t getUShort(const char*,uint16_t d){return d;} size_t putUShort(const char*,uint16_t){return 0;}
 uint8_t getUChar(const char*,uint8_t d){return d;} size_t putUChar(const char*,uint8_t){return 0;}
 bool getBool(const char*,bool d){return d;} size_t putBool(const char*,bool){return 0;}
 int32_t getInt(const char*,int32_t d){return d;} size_t putInt(const char*,int32_t){return 0;}
 bool remove(const char*){return true;} bool isKey(const char*){return false;} };
