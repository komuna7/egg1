#include "energ.cpp"  // run.sh kopē .ino uz šejieni
#include <cassert>
#include <iostream>
std::string testPayload; HWSerial Serial; EspClass ESP; WiFiClass WiFi; TwoWire Wire; MDNSClass MDNS;
int fails=0;
#define CHECK(x) do{ if(!(x)){std::cout<<"FAIL line "<<__LINE__<<": "#x<<"\n";fails++;} }while(0)
int main(){
  // Fuel: home in Riga centre, max 15 min @50 km/h.
  homeLat=56.9496f; homeLon=24.1134f; maxMinutes=15; averageSpeed=50; roundTrip=false;
  testPayload = R"({"updated":"2026-11-26T10:00:00+00:00","stations":[
{"b":"Virši","a":"Rīga, Brīvības iela 1","lat":56.955,"lon":24.115,"p":{"95":1.599,"dd":1.519,"lpg":0.799}},
{"b":"Circle K","a":"Rīga, Krasta iela 101","lat":56.93,"lon":24.15,"p":{"95":1.559,"98":1.659}},
{"b":"Neste","a":"Ogre, Rīgas iela 23","lat":56.82,"lon":24.60,"p":{"95":1.499,"dd":1.449}},
{"b":"Viada","a":"Rīga, Lāčplēša iela 2","lat":56.952,"lon":24.120,"p":{"95":1.559,"lpg":0.799}}
]})";
  CHECK(fetchFuel());
  CHECK(stationCount==4);
  CHECK(fuelFeedUpdated=="2026-11-26T10:00:00+00:00");
  // 95: Circle K & Viada same price 1.559 in reach; Viada is closer. Neste (Ogre) cheaper but too far.
  CHECK(bestPick[FUEL_95].index>=0 && String(stations[bestPick[FUEL_95].index].brand)=="Viada");
  CHECK(bestPick[FUEL_DIESEL].index>=0 && String(stations[bestPick[FUEL_DIESEL].index].brand)=="Virši");
  CHECK(bestPick[FUEL_98].index>=0 && String(stations[bestPick[FUEL_98].index].brand)=="Circle K");
  CHECK(bestPick[FUEL_LPG].index>=0);
  std::cout<<"95 best "<<stations[bestPick[FUEL_95].index].brand<<" "<<bestPick[FUEL_95].minutes<<" min\n";
  maxMinutes=60; computePicks();
  CHECK(String(stations[bestPick[FUEL_95].index].brand)=="Neste");
  std::cout<<"95 best@60 "<<stations[bestPick[FUEL_95].index].brand<<" "<<bestPick[FUEL_95].minutes<<" min, "<<bestPick[FUEL_95].km<<" km\n";
  homeLat=57.54f; homeLon=25.42f; maxMinutes=5; computePicks(); homeLat=56.9496f; homeLon=24.1134f;
  CHECK(bestPick[FUEL_95].index<0 && nearestPick[FUEL_95].index>=0); std::cout<<"nearest from Valmiera: "<<stations[nearestPick[FUEL_95].index].brand<<" "<<nearestPick[FUEL_95].minutes<<" min\n";
  // Broken feed must keep previous data.
  testPayload = R"({"updated":"x","stations":[{"b":"A","lat":56.9,"lon":24.1,"p":{"95":1.2}},{"b":)";
  CHECK(!fetchFuel()); CHECK(stationCount==4);
  // Empty list and missing "updated".
  testPayload = R"({"stations":[ ]})"; CHECK(fetchFuel()); CHECK(stationCount==0);
  testPayload = R"({"stations":[{"b":"A","a":"x","lat":56.95,"lon":24.11,"p":{"lpg":0.75}}]})"; maxMinutes=15; CHECK(fetchFuel()); CHECK(stationCount==1);
  CHECK(bestPick[FUEL_LPG].index==0); CHECK(bestPick[FUEL_95].index<0 && nearestPick[FUEL_95].index<0);
  // Electricity: 15-min slots starting now-ish.
  time_t now=time(nullptr); time_t start=localMidnight(now);
  std::string e=R"({"success":true,"data":{"ee":[{"timestamp":1,"price":9}],"lv":[)";
  for(int i=0;i<192;i++){ if(i) e+=","; e+="{\"timestamp\":"+std::to_string(start+i*900)+",\"price\":"+std::to_string(50+i%96)+".5}"; }
  e+="],\"lt\":[]}}"; testPayload=e;
  CHECK(fetchElectricity()); CHECK(priceCount==192);
  int slot=currentPriceSlot(now); CHECK(slot>=0 && priceTimes[slot]<=now && now<priceTimes[slot]+900);
  int last=lastSlotWithin24h(slot); CHECK(last-slot==95 || last==191);
  showVat=false; CHECK(fabsf(displayCents(100)-10)<0.001); showVat=true; CHECK(fabsf(displayCents(100)-12.1)<0.001); CHECK(displayCents(-20)==-2);
  CHECK(currentPriceSlot(start-10)==-1); CHECK(currentPriceSlot(start+192*900+5)==-1);
  // sections parsing
  parseSections("0:5,1:30,0:7,1:999"); CHECK(!sections[0].enabled && sections[1].seconds==30 && sections[3].seconds==600);
  CHECK(sectionsText()=="0:5,1:30,0:7,1:600");
  String a,b; splitTwoLines("RIGA, BRIVIBAS GATVE 201A KREISA PUSE",21,a,b); std::cout<<"["<<a.c_str()<<"]["<<b.c_str()<<"]\n";
  CHECK(oledText("Rīga, Lāčplēša iela",30)=="RIGA, LACPLESA IELA");
  std::cout<<(fails?"FAILED ":"ALL OK ")<<fails<<"\n"; return fails!=0;
}
