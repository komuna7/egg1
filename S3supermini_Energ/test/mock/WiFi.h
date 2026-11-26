#pragma once
#include <Arduino.h>
#define WL_CONNECTED 3
#define WIFI_STA 1
#define WIFI_AP 2
#define WIFI_AP_STA 3
#define WIFI_OFF 0
#define WIFI_AUTH_OPEN 0
struct IPAddress { String toString()const{return String();} };
class WiFiClient: public StringStream {};
class WiFiClass { public: int status(){return 0;} int scanNetworks(){return 0;} String SSID(int i=0){return String();} int32_t RSSI(int){return 0;}
 int encryptionType(int){return 0;} void scanDelete(){} void setAutoReconnect(bool){} void disconnect(bool=false,bool=false){} void mode(int){}
 void begin(const char*,const char*){} void begin(){} IPAddress localIP(){return {};} IPAddress softAPIP(){return {};} String psk(){return String();}
 bool softAP(const char*){return true;} int softAPgetStationNum(){return 0;} void softAPdisconnect(bool){} };
extern WiFiClass WiFi;
