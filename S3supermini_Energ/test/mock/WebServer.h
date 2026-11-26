#pragma once
#include <Arduino.h>
#include <functional>
#define HTTP_GET 1
#define HTTP_POST 2
class WebServer { public: WebServer(int){} void on(const char*,int,std::function<void()>){} void onNotFound(std::function<void()>){} void begin(){} void stop(){}
 void handleClient(){} void collectHeaders(const char**,size_t){} String header(const char*){return String();} String arg(const char*){return String();}
 void send(int,const char* =nullptr,const String& =String()){} void send_P(int,const char*,const char*){} void sendHeader(const char*,const String&){} };
