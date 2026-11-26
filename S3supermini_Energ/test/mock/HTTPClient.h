#pragma once
#include <WiFi.h>
#define HTTP_CODE_OK 200
extern std::string testPayload;
enum followRedirects_t { HTTPC_STRICT_FOLLOW_REDIRECTS };
class HTTPClient { public: bool begin(WiFiClient&,const String&){return true;} void useHTTP10(bool){} void setConnectTimeout(int){} void setTimeout(int){}
 void setFollowRedirects(followRedirects_t){} void setUserAgent(const char*){} void addHeader(const char*,const char*){} int GET(){return 200;}
 void end(){} WiFiClient& getStream(){static WiFiClient c;c.d=testPayload;c.p=0;return c;} };
