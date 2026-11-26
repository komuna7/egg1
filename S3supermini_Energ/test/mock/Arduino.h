#pragma once
#define ARDUINO 10800
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <string>
#include <algorithm>
using std::min; using std::max;
#define PROGMEM
#define HIGH 1
#define LOW 0
#define INPUT_PULLDOWN 9
#define OUTPUT 3
#define RTC_NOINIT_ATTR
typedef const char *PGM_P;
inline unsigned long millis(){return 0;}
inline void delay(unsigned long){}
inline void pinMode(int,int){}
inline int digitalRead(int){return 0;}
inline void digitalWrite(int,int){}
template<class T,class L,class H> auto constrain(T x,L l,H h)->decltype(x+l){return x<l?l:(x>h?h:x);}
#define radians(d) ((d)*0.017453292519943295)
inline void *ps_malloc(size_t n){return malloc(n);}
inline bool psramFound(){return true;}
inline size_t strlcpy(char*d,const char*s,size_t n){size_t l=strlen(s);if(n){size_t c=l<n-1?l:n-1;memcpy(d,s,c);d[c]=0;}return l;}
class String {
 public:
  std::string s;
  String(){} String(const char*c){if(c)s=c;} String(const std::string&x):s(x){}
  String(char c){s=c;} String(int v){s=std::to_string(v);} String(unsigned v){s=std::to_string(v);}
  String(long v){s=std::to_string(v);} String(unsigned long v){s=std::to_string(v);} String(float v,int d=2){char b[32];snprintf(b,32,"%.*f",d,v);s=b;}
  String(double v,int d=2){char b[32];snprintf(b,32,"%.*f",d,v);s=b;}
  const char*c_str()const{return s.c_str();} unsigned length()const{return s.size();}
  bool isEmpty()const{return s.empty();}
  bool concat(const char*c){s+=c;return true;} bool concat(const char*c,unsigned n){s.append(c,n);return true;} bool reserve(unsigned n){s.reserve(n);return true;}
  String& operator+=(const String&o){s+=o.s;return *this;} String& operator+=(const char*o){s+=o;return *this;} String& operator+=(char c){s+=c;return *this;}
  String& operator+=(int v){s+=std::to_string(v);return *this;} String& operator+=(unsigned v){s+=std::to_string(v);return *this;}
  char operator[](unsigned i)const{return s[i];}
  bool operator==(const String&o)const{return s==o.s;} bool operator==(const char*o)const{return s==o;}
  bool operator!=(const String&o)const{return s!=o.s;} bool operator!=(const char*o)const{return s!=o;}
  bool startsWith(const char*p)const{return s.rfind(p,0)==0;} bool startsWith(const String&p)const{return startsWith(p.c_str());}
  int indexOf(char c,unsigned from=0)const{auto r=s.find(c,from);return r==std::string::npos?-1:(int)r;}
  int lastIndexOf(char c,unsigned from)const{auto r=s.rfind(c,from);return r==std::string::npos?-1:(int)r;}
  String substring(unsigned a)const{return a>=s.size()?String():String(s.substr(a));}
  String substring(unsigned a,unsigned b)const{return a>=s.size()?String():String(s.substr(a,b-a));}
  void trim(){size_t a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos){s.clear();return;}s=s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
  long toInt()const{return atol(s.c_str());}
};
class StringSumHelper: public String { public: using String::String; StringSumHelper(const String&o):String(o){} };
inline StringSumHelper operator+(const String&a,const String&b){return StringSumHelper(a.s+b.s);}
inline StringSumHelper operator+(const String&a,const char*b){return StringSumHelper(a.s+b);}
inline StringSumHelper operator+(const char*a,const String&b){return StringSumHelper(std::string(a)+b.s);}
inline StringSumHelper operator+(const String&a,int b){return StringSumHelper(a.s+std::to_string(b));}
inline StringSumHelper operator+(const String&a,unsigned b){return StringSumHelper(a.s+std::to_string(b));}
inline StringSumHelper operator+(const String&a,uint16_t b){return StringSumHelper(a.s+std::to_string(b));}
inline StringSumHelper operator+(const String&a,char b){return StringSumHelper(a.s+b);}
class Print { public: virtual size_t write(uint8_t)=0; virtual size_t write(const uint8_t*b,size_t n){for(size_t i=0;i<n;i++)write(b[i]);return n;}
  size_t print(const String&x){return write((const uint8_t*)x.c_str(),x.length());} size_t print(const char*x){return write((const uint8_t*)x,strlen(x));}
  size_t println(const char*x){return print(x);} size_t printf(const char*,...){return 0;} virtual ~Print(){} };
class Stream: public Print { public: virtual int available(){return 0;} virtual int read(){return -1;} virtual int peek(){return -1;}
  size_t readBytes(char*b,size_t n){size_t i=0;for(;i<n;i++){int c=read();if(c<0)break;b[i]=c;}return i;}
  size_t readBytes(uint8_t*b,size_t n){return readBytes((char*)b,n);}
  bool findUntil(const char*t,const char*term){size_t ti=0,mi=0,tl=strlen(t),ml=term?strlen(term):0;int c;
    while((c=read())>=0){ if(c==t[ti]){if(++ti==tl)return true;}else ti=(c==t[0])?1:0;
      if(ml){ if(c==term[mi]){if(++mi==ml)return false;}else mi=(c==term[0])?1:0;} } return false;}
  bool find(const char*t){return findUntil(t,nullptr);}
  String readStringUntil(char e){String r;int c;while((c=read())>=0&&c!=e)r+=(char)c;return r;}
  size_t write(uint8_t) override {return 1;} };
class StringStream: public Stream { public: std::string d; size_t p=0; int read() override {return p<d.size()?(uint8_t)d[p++]:-1;}
  int peek() override {return p<d.size()?(uint8_t)d[p]:-1;} int available() override {return d.size()-p;} };
class HWSerial: public Stream { public: void begin(int){} }; extern HWSerial Serial;
struct EspClass { uint64_t getEfuseMac(){return 0;} void restart(){} }; extern EspClass ESP;
class Printable { public: virtual size_t printTo(Print&) const = 0; virtual ~Printable(){} };
inline void configTzTime(const char*,const char*,const char*){}
inline bool getLocalTime(struct tm*,uint32_t=5000){return true;}
