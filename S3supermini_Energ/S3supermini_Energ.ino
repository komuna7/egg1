// S3supermini_Energ: elektrības biržas cena un lētākā degviela tuvumā.
// Atvasināts no S3supermini_VcSar5 (Wi-Fi iestatīšana, sensors, QR, tīmekļa lapa).
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <qrcode.h>
#include <WebServer.h>
#include <Wire.h>
#include <math.h>
#include <time.h>
#include "EnergWebUi.h"
#include "EnergSetupUi.h"

constexpr int SDA_PIN = 8;
constexpr int SCL_PIN = 9;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr char LOCAL_TIMEZONE[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr char HOST_NAME[] = "energ";

// Nord Pool dienas tirgus cenas (EUR/MWh) no Elering publiskā API; "lv" ir Latvijas zona.
constexpr char ELECTRICITY_URL[] = "https://dashboard.elering.ee/api/nps/price";
// Degvielas staciju saraksts (sk. tools/fuel_feed.py). Maina tīmekļa lapā.
constexpr char DEFAULT_FUEL_URL[] =
    "https://raw.githubusercontent.com/komuna7/egg1/main/S3supermini_Energ/data/fuel.json";

constexpr uint32_t ELECTRICITY_REFRESH_MS = 30UL * 60UL * 1000UL;
constexpr uint32_t ELECTRICITY_RETRY_MS = 3UL * 60UL * 1000UL;
constexpr uint32_t FUEL_REFRESH_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t FUEL_RETRY_MS = 5UL * 60UL * 1000UL;
constexpr int DAILY_RESTART_HOUR = 3;
constexpr uint32_t DAILY_RESTART_MIN_UPTIME_MS = 70UL * 60UL * 1000UL;

// Ceļš pa autoceļiem ir garāks par taisni; 1,3 ir tipiska attiecība Latvijā.
constexpr float ROAD_FACTOR = 1.3f;
constexpr float VAT_RATE = 0.21f;

constexpr int LIST_BUTTON_PIN = 4;
constexpr uint8_t TOUCH_ACTIVE_LEVEL = HIGH;
constexpr int TOUCH_POWER_PIN = 5;
constexpr int TOUCH_WARNING_SECONDS = 3;
constexpr uint32_t TOUCH_CALIBRATION_MS = 1000;
constexpr int TOUCH_CALIBRATION_ATTEMPTS = 3;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 20;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Preferences preferences;
WebServer webServer(80);

// ---------------------------------------------------------------- iestatījumi

enum Section : uint8_t { SEC_ELECTRICITY, SEC_LPG, SEC_PETROL, SEC_DIESEL, SECTION_COUNT };
const char *const SECTION_IDS[SECTION_COUNT] = {"elec", "lpg", "petrol", "diesel"};
const char *const SECTION_NAMES[SECTION_COUNT] = {"Elektrība", "Gāze (LPG)", "Benzīns",
                                                   "Dīzeļdegviela"};
struct SectionConfig {
  bool enabled;
  uint16_t seconds;
};
SectionConfig sections[SECTION_COUNT] = {{true, 10}, {true, 8}, {true, 8}, {true, 8}};

enum Fuel : uint8_t { FUEL_LPG, FUEL_95, FUEL_98, FUEL_DIESEL, FUEL_COUNT };
const char *const FUEL_KEYS[FUEL_COUNT] = {"lpg", "95", "98", "dd"};

float homeLat = 56.9496f;  // Rīga, Brīvības piemineklis
float homeLon = 24.1134f;
uint16_t maxMinutes = 15;
uint16_t averageSpeed = 50;  // km/h
bool roundTrip = false;
uint8_t petrolKind = FUEL_95;
bool showVat = true;
String fuelUrl = DEFAULT_FUEL_URL;

uint32_t restartRequestedAt = 0;
uint32_t factoryResetRequestedAt = 0;
bool fuelRefreshRequested = false;
bool electricityRefreshRequested = false;

// ---------------------------------------------------------------- Wi-Fi iestatīšana

enum class SetupLink : uint8_t { idle, connecting, connected, failed };
constexpr uint32_t SETUP_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t SETUP_RETRY_MS = 30000;
constexpr uint32_t SETUP_AUTO_FINISH_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t SETUP_CLOSE_DELAY_MS = 1500;
constexpr uint32_t READY_SCREEN_MS = 8000;
constexpr size_t SETUP_MAX_NETWORKS = 16;
DNSServer dnsServer;
bool webStarted = false;
bool setupActive = false;
bool setupConnectRequested = false;
bool setupAutomaticAttempt = false;
uint32_t setupFinishAt = 0;
uint32_t setupConnectStarted = 0;
SetupLink setupLink = SetupLink::idle;
String setupSsid;
String setupPassword;
String setupNetworks = "[]";
String deviceName;

// ---------------------------------------------------------------- elektrība

constexpr size_t MAX_PRICE_SLOTS = 3 * 96;  // 15 min solis, šodiena + rītdiena + rezerve
time_t priceTimes[MAX_PRICE_SLOTS];
float priceValues[MAX_PRICE_SLOTS];  // EUR/MWh bez PVN
size_t priceCount = 0;
time_t electricityUpdated = 0;
int electricityStatus = 0;
uint32_t nextElectricityAt = 0;

// ---------------------------------------------------------------- degviela

struct Station {
  float lat;
  float lon;
  float price[FUEL_COUNT];  // NAN, ja nav zināma
  char brand[16];
  char address[56];
};
constexpr size_t MAX_STATIONS = 1500;
// Divi buferi: jauno sarakstu lasa otrā, lai kļūda lejupielādē nesabojātu veco.
Station *stations = nullptr;
Station *stationsIncoming = nullptr;
size_t stationCapacity = 0;
size_t stationCount = 0;
String fuelFeedUpdated;
time_t fuelFetched = 0;
int fuelStatus = 0;
uint32_t nextFuelAt = 0;

struct FuelPick {
  int16_t index = -1;
  float minutes = 0;
  float km = 0;
};
FuelPick bestPick[FUEL_COUNT];     // lētākā sasniedzamā, vienādas cenas gadījumā tuvākā
FuelPick nearestPick[FUEL_COUNT];  // tuvākā ar šo degvielu (ja robežās nav nevienas)

// ---------------------------------------------------------------- ekrāns

enum class AppState : uint8_t { starting, connectingWifi, syncing, ready, error };
AppState appState = AppState::starting;
String statusDetail;
uint8_t currentSection = SEC_ELECTRICITY;
uint32_t sectionStartedAt = 0;

void centeredText(const String &text, int16_t y, uint8_t size = 1) {
  int16_t x1, y1;
  uint16_t width, height;
  display.setTextSize(size);
  display.getTextBounds(text, 0, y, &x1, &y1, &width, &height);
  display.setCursor((SCREEN_WIDTH - width) / 2, y);
  display.print(text);
}

void rightText(const String &text, int16_t y) {
  display.setTextSize(1);
  display.setCursor(SCREEN_WIDTH - static_cast<int>(text.length()) * 6, y);
  display.print(text);
}

// Zibens ikona virsrakstam.
constexpr uint8_t BOLT_WIDTH = 10;
constexpr uint8_t BOLT_HEIGHT = 15;
const uint8_t BOLT_ICON[] PROGMEM = {0x03, 0xC0, 0x07, 0x80, 0x07, 0x00, 0x0E, 0x00,
                                     0x1E, 0x00, 0x1C, 0x00, 0x3F, 0xC0, 0x7F, 0x80,
                                     0x07, 0x00, 0x0E, 0x00, 0x0C, 0x00, 0x18, 0x00,
                                     0x30, 0x00, 0x20, 0x00, 0x40, 0x00};

void drawBrandHeader() {
  constexpr int16_t brandWidth = BOLT_WIDTH + 5 + 5 * 12;
  const int16_t x = (SCREEN_WIDTH - brandWidth) / 2;
  display.drawBitmap(x, 1, BOLT_ICON, BOLT_WIDTH, BOLT_HEIGHT, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(x + BOLT_WIDTH + 5, 2);
  display.print("Energ");
  display.setTextSize(1);
}

void drawStatus() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawBrandHeader();
  switch (appState) {
    case AppState::starting:
      centeredText("STARTE...", 27);
      break;
    case AppState::connectingWifi:
      centeredText("SLEDZ WI-FI", 21);
      centeredText(statusDetail, 37);
      break;
    case AppState::syncing:
      centeredText("IELADE DATUS", 21);
      centeredText(statusDetail, 37);
      break;
    case AppState::ready:
      break;
    case AppState::error:
      centeredText("KLUDA", 21, 2);
      centeredText(statusDetail, 47);
      break;
  }
  display.display();
}

// OLED fonts rāda tikai ASCII, tāpēc garumzīmes nomaina ar pamatburtu.
String oledText(const String &text, size_t maxLength) {
  String output;
  for (size_t index = 0; index < text.length() && output.length() < maxLength; ++index) {
    const uint8_t value = text[index];
    if (value < 0x80) {
      if (value >= 0x20 && value != 0x7f) output += static_cast<char>(toupper(value));
      continue;
    }
    if (index + 1 >= text.length()) break;
    const uint16_t code = (value << 8) | static_cast<uint8_t>(text[++index]);
    char mapped = 0;
    switch (code & 0xFFFE) {
      case 0xC480: mapped = 'A'; break;
      case 0xC48C: mapped = 'C'; break;
      case 0xC492: mapped = 'E'; break;
      case 0xC4A2: mapped = 'G'; break;
      case 0xC4AA: mapped = 'I'; break;
      case 0xC4B6: mapped = 'K'; break;
      case 0xC5AA: mapped = 'U'; break;
      case 0xC5A0: mapped = 'S'; break;
    }
    if (code == 0xC4BB || code == 0xC4BC) mapped = 'L';
    if (code == 0xC585 || code == 0xC586) mapped = 'N';
    if (code == 0xC5BD || code == 0xC5BE) mapped = 'Z';
    if (mapped) output += mapped;
  }
  output.trim();
  return output;
}

// Garu adresi sadala divās rindās pa vārdiem.
void splitTwoLines(const String &text, size_t width, String &first, String &second) {
  if (text.length() <= width) {
    first = text;
    second = "";
    return;
  }
  int cut = text.lastIndexOf(' ', width);
  if (cut <= 0) cut = width;
  first = text.substring(0, cut);
  second = text.substring(cut);
  second.trim();
  if (second.length() > width) second = second.substring(0, width);
}

// ---------------------------------------------------------------- iestatījumu glabāšana

String sectionsText() {
  String text;
  for (int i = 0; i < SECTION_COUNT; ++i) {
    if (i) text += ',';
    text += sections[i].enabled ? '1' : '0';
    text += ':';
    text += sections[i].seconds;
  }
  return text;
}

void parseSections(const String &text) {
  int position = 0;
  for (int i = 0; i < SECTION_COUNT && position < static_cast<int>(text.length()); ++i) {
    int comma = text.indexOf(',', position);
    if (comma < 0) comma = text.length();
    const String part = text.substring(position, comma);
    const int colon = part.indexOf(':');
    if (colon > 0) {
      sections[i].enabled = part.substring(0, colon).toInt() != 0;
      sections[i].seconds = constrain(part.substring(colon + 1).toInt(), 2, 600);
    }
    position = comma + 1;
  }
}

void loadSettings() {
  parseSections(preferences.getString("sections", sectionsText()));
  homeLat = preferences.getFloat("lat", homeLat);
  homeLon = preferences.getFloat("lon", homeLon);
  maxMinutes = constrain(preferences.getUShort("max_min", maxMinutes), 1, 240);
  averageSpeed = constrain(preferences.getUShort("speed", averageSpeed), 5, 130);
  roundTrip = preferences.getBool("round_trip", roundTrip);
  petrolKind = preferences.getUChar("petrol", petrolKind) == FUEL_98 ? FUEL_98 : FUEL_95;
  showVat = preferences.getBool("vat", showVat);
  fuelUrl = preferences.getString("fuel_url", DEFAULT_FUEL_URL);
  if (!fuelUrl.startsWith("http")) fuelUrl = DEFAULT_FUEL_URL;
}

void saveSettings() {
  preferences.putString("sections", sectionsText());
  preferences.putFloat("lat", homeLat);
  preferences.putFloat("lon", homeLon);
  preferences.putUShort("max_min", maxMinutes);
  preferences.putUShort("speed", averageSpeed);
  preferences.putBool("round_trip", roundTrip);
  preferences.putUChar("petrol", petrolKind);
  preferences.putBool("vat", showVat);
  preferences.putString("fuel_url", fuelUrl);
}

// ---------------------------------------------------------------- Wi-Fi

bool waitForWifi(uint32_t timeoutMs) {
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

const char *wifiSsidKey(int slot) { return slot == 1 ? "primary_ssid" : "fallback_ssid"; }
const char *wifiPassKey(int slot) { return slot == 1 ? "primary_pass" : "fallback_pass"; }

constexpr uint32_t WIFI_CHOICE_MS = 5000;
constexpr int32_t RSSI_NOT_SEEN = -1000;

void scanWifiSlots(const String ssids[3], int32_t rssi[3]) {
  rssi[1] = rssi[2] = RSSI_NOT_SEEN;
  const int found = WiFi.scanNetworks();
  for (int i = 0; i < found; ++i) {
    for (int slot = 1; slot <= 2; ++slot) {
      if (WiFi.SSID(i) == ssids[slot] && WiFi.RSSI(i) > rssi[slot]) rssi[slot] = WiFi.RSSI(i);
    }
  }
  WiFi.scanDelete();
}

void drawWifiChoice(int chosen, bool automatic, const String ssids[3], const int32_t rssi[3],
                    uint32_t remainingMs) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  centeredText(automatic ? "WI-FI: AUTO (SIGNALS)" : "WI-FI: TAVA IZVELE", 2);
  for (int slot = 1; slot <= 2; ++slot) {
    const int16_t rowY = 4 + slot * 14;
    if (slot == chosen) {
      display.fillRect(0, rowY - 2, SCREEN_WIDTH, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    }
    display.setTextSize(1);
    display.setCursor(2, rowY);
    display.print(String(slot) + " " + ssids[slot].substring(0, 14));
    const String level = rssi[slot] == RSSI_NOT_SEEN ? String("--") : String(rssi[slot]);
    display.setCursor(SCREEN_WIDTH - 2 - level.length() * 6, rowY);
    display.print(level);
    display.setTextColor(SSD1306_WHITE);
  }
  centeredText("POGA: MAINIT  " + String((remainingMs + 999) / 1000) + "s", 54);
  display.display();
}

bool touchPressed() { return digitalRead(LIST_BUTTON_PIN) == TOUCH_ACTIVE_LEVEL; }

int chooseWifiSlot(int chosen, const String ssids[3], const int32_t rssi[3]) {
  uint32_t deadline = millis() + WIFI_CHOICE_MS;
  bool automatic = true;
  bool wasPressed = touchPressed();
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    const bool pressed = touchPressed();
    if (pressed && !wasPressed) {
      chosen = chosen == 1 ? 2 : 1;
      automatic = false;
      deadline = millis() + WIFI_CHOICE_MS;
    }
    wasPressed = pressed;
    drawWifiChoice(chosen, automatic, ssids, rssi, deadline - millis());
    delay(BUTTON_DEBOUNCE_MS);
  }
  return chosen;
}

// Neveiksmīgs mēģinājums paliek "connecting" stāvoklī; to notīra tikai radio restarts.
void stopWifiAttempt() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  delay(300);
  WiFi.mode(WIFI_STA);
  delay(100);
}

bool connectWifiSlot(int slot, const String ssids[3]) {
  if (ssids[slot].isEmpty()) return false;
  statusDetail = ssids[slot];
  drawStatus();
  stopWifiAttempt();
  Serial.printf("Wi-Fi %d: connecting to %s\n", slot, ssids[slot].c_str());
  WiFi.begin(ssids[slot].c_str(), preferences.getString(wifiPassKey(slot), "").c_str());
  if (waitForWifi(12000)) {
    Serial.printf("Wi-Fi %d: connected, %s\n", slot, WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.printf("Wi-Fi %d: failed (status %d)\n", slot, WiFi.status());
  stopWifiAttempt();
  return false;
}

void drawReadyScreen();
bool runSetupMode(bool forced);

bool finishWifiConnection(bool connected) {
  if (!connected) return false;
  WiFi.setAutoReconnect(true);
  if (MDNS.begin(HOST_NAME)) MDNS.addService("http", "tcp", 80);
  drawReadyScreen();
  delay(READY_SCREEN_MS);
  return true;
}

bool configureWifi() {
  WiFi.mode(WIFI_STA);
  if (preferences.getBool("force_setup", false)) {
    preferences.remove("force_setup");
    return finishWifiConnection(runSetupMode(true));
  }
  const String ssids[3] = {"", preferences.getString(wifiSsidKey(1), ""),
                           preferences.getString(wifiSsidKey(2), "")};
  int chosen = preferences.getInt("wifi_choice", 1) == 2 ? 2 : 1;
  if (ssids[chosen].isEmpty()) chosen = chosen == 1 ? 2 : 1;
  if (!ssids[1].isEmpty() && !ssids[2].isEmpty()) {
    appState = AppState::connectingWifi;
    statusDetail = "Skenee tiklus...";
    drawStatus();
    int32_t rssi[3];
    scanWifiSlots(ssids, rssi);
    if (rssi[1] != RSSI_NOT_SEEN || rssi[2] != RSSI_NOT_SEEN) chosen = rssi[1] >= rssi[2] ? 1 : 2;
    chosen = chooseWifiSlot(chosen, ssids, rssi);
    preferences.putInt("wifi_choice", chosen);
  }
  appState = AppState::connectingWifi;
  const int other = chosen == 1 ? 2 : 1;
  bool connected = connectWifiSlot(chosen, ssids) || connectWifiSlot(other, ssids);
  if (!connected) connected = runSetupMode(false);
  return finishWifiConnection(connected);
}

bool synchronizeClock() {
  appState = AppState::syncing;
  statusDetail = "Laiks (NTP)";
  drawStatus();
  configTzTime(LOCAL_TIMEZONE, "time.cloudflare.com", "pool.ntp.org");
  struct tm localTime;
  for (uint8_t attempt = 0; attempt < 20; ++attempt) {
    if (getLocalTime(&localTime, 500)) return true;
    delay(250);
  }
  statusDetail = "NTP neizdevas";
  return false;
}

// ---------------------------------------------------------------- elektrības cenas

float displayCents(float eurPerMwh) {
  const float cents = eurPerMwh / 10.0f;  // EUR/MWh -> c/kWh
  return showVat && cents > 0 ? cents * (1.0f + VAT_RATE) : cents;
}

void isoUtc(time_t value, char *output, size_t size) {
  struct tm utc;
  gmtime_r(&value, &utc);
  strftime(output, size, "%Y-%m-%dT%H:%M:%S.000Z", &utc);
}

time_t localMidnight(time_t now) {
  struct tm local;
  localtime_r(&now, &local);
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  return mktime(&local);
}

bool fetchElectricity() {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  const time_t start = localMidnight(now);
  char from[32], to[32];
  isoUtc(start, from, sizeof(from));
  isoUtc(start + 2 * 86400, to, sizeof(to));
  const String url = String(ELECTRICITY_URL) + "?start=" + from + "&end=" + to;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15);
  HTTPClient http;
  http.useHTTP10(true);  // bez "chunked", lai JSON var lasīt tieši no plūsmas
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    electricityStatus = -1;
    return false;
  }
  http.setUserAgent("S3supermini-Energ/1.0");
  http.addHeader("Accept", "application/json");
  electricityStatus = http.GET();
  if (electricityStatus != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  JsonDocument filter;
  filter["data"]["lv"][0]["timestamp"] = true;
  filter["data"]["lv"][0]["price"] = true;
  JsonDocument doc;
  const DeserializationError error =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (error) {
    electricityStatus = -3;
    return false;
  }
  JsonArrayConst list = doc["data"]["lv"];
  size_t count = 0;
  for (JsonObjectConst entry : list) {
    if (count >= MAX_PRICE_SLOTS) break;
    if (!entry["timestamp"].is<uint32_t>() || !entry["price"].is<float>()) continue;
    priceTimes[count] = static_cast<time_t>(entry["timestamp"].as<uint32_t>());
    priceValues[count] = entry["price"].as<float>();
    ++count;
  }
  if (!count) {
    electricityStatus = -4;
    return false;
  }
  priceCount = count;
  electricityUpdated = now;
  Serial.printf("Electricity: %u slots\n", static_cast<unsigned>(count));
  return true;
}

// Indekss intervālam, kurā ir "now"; -1, ja datu nav.
int currentPriceSlot(time_t now) {
  int found = -1;
  for (size_t i = 0; i < priceCount; ++i) {
    if (priceTimes[i] <= now) found = i;
    else break;
  }
  if (found < 0) return -1;
  const time_t step = found + 1 < static_cast<int>(priceCount)
                          ? priceTimes[found + 1] - priceTimes[found]
                          : (found > 0 ? priceTimes[found] - priceTimes[found - 1] : 3600);
  return now < priceTimes[found] + step ? found : -1;
}

// Pēdējais intervāls, kas sākas nākamo 24 h laikā.
int lastSlotWithin24h(int first) {
  int last = first;
  for (size_t i = first; i < priceCount; ++i) {
    if (priceTimes[i] - priceTimes[first] < 86400) last = i;
  }
  return last;
}

void refreshElectricity() {
  const bool ok = fetchElectricity();
  nextElectricityAt = millis() + (ok ? ELECTRICITY_REFRESH_MS : ELECTRICITY_RETRY_MS);
}

// ---------------------------------------------------------------- degvielas cenas

float distanceKm(float lat1, float lon1, float lat2, float lon2) {
  constexpr float R = 6371.0f;
  const float dLat = radians(lat2 - lat1);
  const float dLon = radians(lon2 - lon1);
  const float a = sinf(dLat / 2) * sinf(dLat / 2) +
                  cosf(radians(lat1)) * cosf(radians(lat2)) * sinf(dLon / 2) * sinf(dLon / 2);
  return 2 * R * atan2f(sqrtf(a), sqrtf(1 - a));
}

float travelMinutes(float km) {
  return km * ROAD_FACTOR / averageSpeed * 60.0f * (roundTrip ? 2 : 1);
}

// Attālums (km taisnē), ko var sasniegt maksimālajā laikā; kartes aplim.
float reachKm() {
  return static_cast<float>(maxMinutes) / (roundTrip ? 2 : 1) / 60.0f * averageSpeed / ROAD_FACTOR;
}

void computePicks() {
  for (int fuel = 0; fuel < FUEL_COUNT; ++fuel) bestPick[fuel] = nearestPick[fuel] = FuelPick();
  for (size_t i = 0; i < stationCount; ++i) {
    const Station &station = stations[i];
    const float km = distanceKm(homeLat, homeLon, station.lat, station.lon);
    const float minutes = travelMinutes(km);
    for (int fuel = 0; fuel < FUEL_COUNT; ++fuel) {
      const float price = station.price[fuel];
      if (isnan(price)) continue;
      FuelPick &nearest = nearestPick[fuel];
      if (nearest.index < 0 || minutes < nearest.minutes) nearest = {static_cast<int16_t>(i), minutes, km};
      if (minutes > maxMinutes) continue;
      FuelPick &best = bestPick[fuel];
      const float bestPrice = best.index < 0 ? INFINITY : stations[best.index].price[fuel];
      const bool cheaper = price < bestPrice - 0.0005f;
      const bool sameButCloser = fabsf(price - bestPrice) <= 0.0005f && minutes < best.minutes;
      if (cheaper || sameButCloser) best = {static_cast<int16_t>(i), minutes, km};
    }
  }
}

void readStationText(JsonVariantConst value, char *output, size_t size) {
  strlcpy(output, value.is<const char *>() ? value.as<const char *>() : "", size);
}

bool allocateStations() {
  if (stations) return true;
  stationCapacity = MAX_STATIONS;
  stations = static_cast<Station *>(ps_malloc(stationCapacity * sizeof(Station)));
  stationsIncoming = static_cast<Station *>(ps_malloc(stationCapacity * sizeof(Station)));
  if (!stations || !stationsIncoming) {
    free(stations);
    free(stationsIncoming);
    stationCapacity = 200;  // bez PSRAM
    stations = static_cast<Station *>(malloc(stationCapacity * sizeof(Station)));
    stationsIncoming = static_cast<Station *>(malloc(stationCapacity * sizeof(Station)));
  }
  if (!stations || !stationsIncoming) {
    free(stations);
    free(stationsIncoming);
    stations = stationsIncoming = nullptr;
    stationCapacity = 0;
    return false;
  }
  return true;
}

int peekNonSpace(Stream &stream) {
  const uint32_t startedAt = millis();
  while (millis() - startedAt < 5000) {
    const int c = stream.peek();
    if (c < 0) {
      delay(1);
    } else if (isspace(c)) {
      stream.read();
    } else {
      return c;
    }
  }
  return -1;
}

// Saraksts var būt simtiem staciju, tāpēc to lasa pa vienai no plūsmas.
// Formāts: {"updated":"...","stations":[{"b":"..","a":"..","lat":..,"lon":..,"p":{"lpg":..}}]}
bool fetchFuel() {
  if (!allocateStations()) {
    fuelStatus = -2;
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15);
  HTTPClient http;
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  if (!http.begin(client, fuelUrl)) {
    fuelStatus = -1;
    return false;
  }
  http.setUserAgent("S3supermini-Energ/1.0");
  fuelStatus = http.GET();
  if (fuelStatus != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  Stream &stream = http.getStream();
  String updated;
  bool atStations = false;
  if (stream.findUntil("\"updated\"", "\"stations\"")) {
    if (stream.find("\"")) updated = stream.readStringUntil('"');
  } else {
    atStations = true;
  }
  if ((!atStations && !stream.find("\"stations\"")) || !stream.find("[")) {
    http.end();
    fuelStatus = -3;
    return false;
  }
  size_t count = 0;
  bool ok = true;
  if (peekNonSpace(stream) != ']') {
    do {
      JsonDocument doc;
      if (deserializeJson(doc, stream)) {
        ok = false;
        break;
      }
      if (count >= stationCapacity) continue;
      if (!doc["lat"].is<float>() || !doc["lon"].is<float>()) continue;
      Station &station = stationsIncoming[count];
      station.lat = doc["lat"];
      station.lon = doc["lon"];
      readStationText(doc["b"], station.brand, sizeof(station.brand));
      readStationText(doc["a"], station.address, sizeof(station.address));
      bool any = false;
      for (int fuel = 0; fuel < FUEL_COUNT; ++fuel) {
        JsonVariantConst value = doc["p"][FUEL_KEYS[fuel]];
        station.price[fuel] = value.is<float>() && value.as<float>() > 0 ? value.as<float>() : NAN;
        any = any || !isnan(station.price[fuel]);
      }
      if (any) ++count;
    } while (stream.findUntil(",", "]"));
  }
  http.end();
  if (!ok) {
    fuelStatus = -4;
    return false;
  }
  Station *previous = stations;
  stations = stationsIncoming;
  stationsIncoming = previous;
  stationCount = count;
  fuelFeedUpdated = updated;
  fuelFetched = time(nullptr);
  computePicks();
  Serial.printf("Fuel: %u stations, feed %s\n", static_cast<unsigned>(count), updated.c_str());
  return true;
}

void refreshFuel() {
  const bool ok = fetchFuel();
  nextFuelAt = millis() + (ok ? FUEL_REFRESH_MS : FUEL_RETRY_MS);
}

bool anyFuelSectionEnabled() {
  return sections[SEC_LPG].enabled || sections[SEC_PETROL].enabled || sections[SEC_DIESEL].enabled;
}

// ---------------------------------------------------------------- ekrāna sadaļas

String formatPrice(float value, uint8_t decimals) {
  char text[12];
  snprintf(text, sizeof(text), "%.*f", decimals, value);
  return text;
}

String clockText(time_t value) {
  struct tm local;
  localtime_r(&value, &local);
  char text[8];
  strftime(text, sizeof(text), "%H:%M", &local);
  return text;
}

void drawHeader(const String &title, const String &right) {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(title);
  rightText(right, 0);
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
}

void drawElectricity() {
  const time_t now = time(nullptr);
  drawHeader("ELEKTRIBA LV", clockText(now));
  const int slot = currentPriceSlot(now);
  if (slot < 0) {
    centeredText("NAV DATU", 26, 2);
    centeredText(electricityStatus ? String("HTTP ") + electricityStatus : String("GAIDA..."), 50);
    return;
  }
  const float cents = displayCents(priceValues[slot]);
  const float magnitude = fabsf(cents);
  // Platums: līdz 4 zīmēm lielajā fontā, lai blakus ietilpst mērvienība.
  const uint8_t decimals = cents < 0 ? (magnitude >= 10 ? 0 : 1)
                                     : (magnitude >= 100 ? 0 : magnitude >= 10 ? 1 : 2);
  const String big = formatPrice(cents, decimals);
  display.setTextSize(3);
  display.setCursor(0, 13);
  display.print(big);
  const int unitX = big.length() * 18 + 3;
  display.setTextSize(1);
  display.setCursor(unitX, 14);
  display.print("c/kWh");
  display.setCursor(unitX, 25);
  display.print(showVat ? "ar PVN" : "bez PVN");

  // Lētākais intervāls nākamajās 24 h.
  const int last = lastSlotWithin24h(slot);
  int cheapest = slot;
  float low = priceValues[slot], high = priceValues[slot];
  for (int i = slot; i <= last; ++i) {
    if (priceValues[i] < priceValues[cheapest]) cheapest = i;
    low = min(low, priceValues[i]);
    high = max(high, priceValues[i]);
  }
  display.setCursor(0, 39);
  display.print("LETAKAIS " + clockText(priceTimes[cheapest]) + " " +
                formatPrice(displayCents(priceValues[cheapest]), 1));

  // Stabiņi: no pašreizējā intervāla līdz +24 h.
  constexpr int chartTop = 49, chartHeight = 15;
  const int bars = last - slot + 1;
  const float base = min(0.0f, low);
  const float span = max(high - base, 1.0f);
  for (int i = 0; i < bars; ++i) {
    const int x0 = i * SCREEN_WIDTH / bars;
    const int x1 = (i + 1) * SCREEN_WIDTH / bars;
    const int height = 1 + static_cast<int>((priceValues[slot + i] - base) / span * (chartHeight - 1));
    display.fillRect(x0, chartTop + chartHeight - height, max(1, x1 - x0 - (x1 - x0 > 2 ? 1 : 0)),
                     height, SSD1306_WHITE);
  }
}

uint8_t sectionFuel(uint8_t section) {
  if (section == SEC_LPG) return FUEL_LPG;
  if (section == SEC_DIESEL) return FUEL_DIESEL;
  return petrolKind;
}

const char *fuelTitle(uint8_t fuel) {
  switch (fuel) {
    case FUEL_LPG: return "AUTOGAZE";
    case FUEL_95: return "BENZINS 95";
    case FUEL_98: return "BENZINS 98";
    default: return "DIZELDEGVIELA";
  }
}

void drawFuel(uint8_t fuel) {
  const FuelPick &best = bestPick[fuel];
  const bool inside = best.index >= 0;
  const FuelPick &pick = inside ? best : nearestPick[fuel];
  drawHeader(fuelTitle(fuel), inside ? String("<") + maxMinutes + "MIN" : String("TUVAKA"));
  if (pick.index < 0) {
    centeredText(stationCount ? "NAV CENU" : "NAV DATU", 26, 2);
    if (!stationCount && fuelStatus) centeredText(String("HTTP ") + fuelStatus, 50);
    return;
  }
  const Station &station = stations[pick.index];
  const String big = formatPrice(station.price[fuel], 3);
  display.setTextSize(3);
  display.setCursor(0, 13);
  display.print(big);
  const int unitX = big.length() * 18 + 3;
  display.setTextSize(1);
  display.setCursor(unitX, 14);
  display.print("EUR");
  display.setCursor(unitX, 25);
  display.print("/L");

  const String minutes = String(static_cast<int>(pick.minutes + 0.5f)) + "MIN" + (inside ? "" : "!");
  display.setCursor(0, 39);
  display.print(oledText(station.brand, 20 - minutes.length()));
  rightText(minutes, 39);
  String first, second;
  splitTwoLines(oledText(station.address, 42), 21, first, second);
  display.setCursor(0, 48);
  display.print(first);
  display.setCursor(0, 57);
  display.print(second);
}

int nextEnabledSection(int from) {
  for (int step = 1; step <= SECTION_COUNT; ++step) {
    const int candidate = (from + step) % SECTION_COUNT;
    if (sections[candidate].enabled) return candidate;
  }
  return -1;
}

void advanceSection() {
  const int next = nextEnabledSection(currentSection);
  if (next >= 0) currentSection = next;
  sectionStartedAt = millis();
}

void drawSections() {
  if (!sections[currentSection].enabled) advanceSection();
  else if (millis() - sectionStartedAt >= sections[currentSection].seconds * 1000UL) advanceSection();
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  if (!sections[currentSection].enabled) {
    drawBrandHeader();
    centeredText("VISAS SADALAS", 30);
    centeredText("IZSLEGTAS", 42);
  } else if (currentSection == SEC_ELECTRICITY) {
    drawElectricity();
  } else {
    drawFuel(sectionFuel(currentSection));
  }
  display.display();
}

// Īss pieskāriens: nākamā sadaļa uzreiz.
void handleTouchTap() {
  static bool rawPrevious = false;
  static bool stableState = false;
  static uint32_t changedAt = 0;
  const bool rawState = touchPressed();
  const uint32_t now = millis();
  if (rawState != rawPrevious) {
    rawPrevious = rawState;
    changedAt = now;
  }
  if (rawState == stableState || now - changedAt < BUTTON_DEBOUNCE_MS) return;
  stableState = rawState;
  if (stableState) advanceSection();
}

// ---------------------------------------------------------------- tīmekļa API

void sendJson(const JsonDocument &doc, int status = 200) {
  String body;
  serializeJson(doc, body);
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(status, "application/json; charset=utf-8", body);
}

void sendApiError(int status, const char *message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(doc, status);
}

// JSON prasība liek pārlūkiem veikt preflight pieprasījumu, ko šis serveris noraida.
bool readJsonBody(JsonDocument &doc) {
  if (!webServer.header("Content-Type").startsWith("application/json")) {
    sendApiError(415, "Nepareizs pieprasījuma formāts.");
    return false;
  }
  if (deserializeJson(doc, webServer.arg("plain"))) {
    sendApiError(400, "Neizdevās nolasīt datus.");
    return false;
  }
  return true;
}

bool rejectDuringSetup() {
  if (!setupActive) return false;
  sendApiError(409, "Nav pieejams iestatīšanas laikā.");
  return true;
}

void addPrices(JsonObject output, const Station &station) {
  for (int fuel = 0; fuel < FUEL_COUNT; ++fuel) {
    if (!isnan(station.price[fuel])) output[FUEL_KEYS[fuel]] = station.price[fuel];
  }
}

void addPick(JsonObject output, const char *key, const FuelPick &pick, uint8_t fuel) {
  if (pick.index < 0) return;
  const Station &station = stations[pick.index];
  JsonObject item = output[key].to<JsonObject>();
  item["brand"] = station.brand;
  item["address"] = station.address;
  item["lat"] = station.lat;
  item["lon"] = station.lon;
  item["price"] = station.price[fuel];
  item["minutes"] = roundf(pick.minutes * 10) / 10;
  item["km"] = roundf(pick.km * 10) / 10;
}

void handleApiState() {
  if (rejectDuringSetup()) return;
  JsonDocument doc;
  doc["device"] = deviceName;
  doc["state"] = appState == AppState::ready ? "ready" : appState == AppState::error ? "error" : "starting";
  doc["detail"] = statusDetail;
  JsonArray list = doc["sections"].to<JsonArray>();
  for (int i = 0; i < SECTION_COUNT; ++i) {
    JsonObject item = list.add<JsonObject>();
    item["id"] = SECTION_IDS[i];
    item["name"] = SECTION_NAMES[i];
    item["on"] = sections[i].enabled;
    item["sec"] = sections[i].seconds;
  }
  doc["current"] = SECTION_IDS[currentSection];
  JsonObject place = doc["place"].to<JsonObject>();
  place["lat"] = homeLat;
  place["lon"] = homeLon;
  place["maxMin"] = maxMinutes;
  place["speed"] = averageSpeed;
  place["roundTrip"] = roundTrip;
  place["roadFactor"] = ROAD_FACTOR;
  place["reachKm"] = reachKm();
  doc["petrol"] = FUEL_KEYS[petrolKind];
  doc["vat"] = showVat;
  doc["fuelUrl"] = fuelUrl;
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid1"] = preferences.getString(wifiSsidKey(1), "");
  wifi["ssid2"] = preferences.getString(wifiSsidKey(2), "");
  wifi["connected"] = WiFi.SSID();
  wifi["ip"] = WiFi.localIP().toString();

  const time_t now = time(nullptr);
  JsonObject elec = doc["elec"].to<JsonObject>();
  elec["status"] = electricityStatus;
  elec["updated"] = static_cast<long long>(electricityUpdated);
  const int slot = currentPriceSlot(now);
  if (slot >= 0) {
    elec["now"] = displayCents(priceValues[slot]);
    elec["slotStart"] = static_cast<long long>(priceTimes[slot]);
    const int last = lastSlotWithin24h(slot);
    JsonArray points = elec["points"].to<JsonArray>();
    JsonArray times = elec["times"].to<JsonArray>();
    for (int i = slot; i <= last; ++i) {
      points.add(roundf(displayCents(priceValues[i]) * 100) / 100);
      times.add(static_cast<long long>(priceTimes[i]));
    }
  }
  JsonObject fuel = doc["fuel"].to<JsonObject>();
  fuel["status"] = fuelStatus;
  fuel["feedUpdated"] = fuelFeedUpdated;
  fuel["fetched"] = static_cast<long long>(fuelFetched);
  fuel["stations"] = stationCount;
  JsonObject picks = fuel["picks"].to<JsonObject>();
  for (int kind = 0; kind < FUEL_COUNT; ++kind) {
    JsonObject item = picks[FUEL_KEYS[kind]].to<JsonObject>();
    addPick(item, "best", bestPick[kind], kind);
    addPick(item, "nearest", nearestPick[kind], kind);
  }
  sendJson(doc);
}

// Stacijas kartei: tikai tās, kas ir līdz divkāršam sasniedzamajam attālumam.
void handleApiStations() {
  if (rejectDuringSetup()) return;
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  const float limit = max(reachKm() * 2, 3.0f);
  for (size_t i = 0; i < stationCount && list.size() < 250; ++i) {
    const Station &station = stations[i];
    const float km = distanceKm(homeLat, homeLon, station.lat, station.lon);
    if (km > limit) continue;
    JsonObject item = list.add<JsonObject>();
    item["b"] = station.brand;
    item["a"] = station.address;
    item["lat"] = station.lat;
    item["lon"] = station.lon;
    item["min"] = roundf(travelMinutes(km));
    addPrices(item["p"].to<JsonObject>(), station);
  }
  sendJson(doc);
}

void handleApiSettings() {
  if (rejectDuringSetup()) return;
  JsonDocument doc;
  if (!readJsonBody(doc)) return;
  // Vispirms pārbauda, lai kļūdas gadījumā nekas netiktu mainīts pa pusei.
  JsonObjectConst checkPlace = doc["place"];
  if (!checkPlace.isNull()) {
    const float lat = checkPlace["lat"] | homeLat;
    const float lon = checkPlace["lon"] | homeLon;
    if (lat < 55.0f || lat > 58.5f || lon < 20.5f || lon > 28.5f) {
      sendApiError(400, "Punktam jābūt Latvijā.");
      return;
    }
  }
  const String url = doc["fuelUrl"] | fuelUrl.c_str();
  if (!url.startsWith("https://") && !url.startsWith("http://")) {
    sendApiError(400, "Datu adresei jāsākas ar https://");
    return;
  }
  JsonArrayConst list = doc["sections"];
  for (JsonObjectConst item : list) {
    const String id = item["id"] | "";
    for (int i = 0; i < SECTION_COUNT; ++i) {
      if (id != SECTION_IDS[i]) continue;
      sections[i].enabled = item["on"] | sections[i].enabled;
      sections[i].seconds = constrain(item["sec"] | static_cast<int>(sections[i].seconds), 2, 600);
    }
  }
  JsonObjectConst place = doc["place"];
  if (!place.isNull()) {
    homeLat = place["lat"] | homeLat;
    homeLon = place["lon"] | homeLon;
    maxMinutes = constrain(place["maxMin"] | static_cast<int>(maxMinutes), 1, 240);
    averageSpeed = constrain(place["speed"] | static_cast<int>(averageSpeed), 5, 130);
    roundTrip = place["roundTrip"] | roundTrip;
  }
  const String petrol = doc["petrol"] | FUEL_KEYS[petrolKind];
  petrolKind = petrol == "98" ? FUEL_98 : FUEL_95;
  showVat = doc["vat"] | showVat;
  if (url != fuelUrl) {
    fuelUrl = url;
    fuelRefreshRequested = true;
  }
  saveSettings();
  computePicks();
  webServer.send(204);
}

void handleApiRefresh() {
  if (rejectDuringSetup()) return;
  JsonDocument doc;
  if (!readJsonBody(doc)) return;
  fuelRefreshRequested = true;
  electricityRefreshRequested = true;
  webServer.send(202);
}

void handleApiFactoryReset() {
  if (rejectDuringSetup()) return;
  JsonDocument doc;
  if (!readJsonBody(doc)) return;
  if (String(doc["confirm"] | "") != "RESET") {
    sendApiError(400, "Atiestatīšana nav apstiprināta.");
    return;
  }
  webServer.send(204);
  factoryResetRequestedAt = max<uint32_t>(1, millis());
}

// Dzēš visu NVS, jo Wi-Fi draiveris glabā savu paroles kopiju.
void performFactoryReset() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  centeredText("ATIESTATA...", 28);
  display.display();
  webServer.stop();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  preferences.end();
  nvs_flash_deinit();
  nvs_flash_erase();
  ESP.restart();
}

void handleApiWifi() {
  if (rejectDuringSetup()) return;
  JsonDocument doc;
  if (!readJsonBody(doc)) return;
  const int slot = doc["slot"] | 0;
  const String ssid = doc["ssid"] | "";
  const String password = doc["password"] | "";
  if (slot != 1 && slot != 2) {
    sendApiError(400, "Nepareizs tīkls.");
    return;
  }
  if (ssid.length() > 32 || password.length() > 63) {
    sendApiError(400, "Tīkla nosaukums vai parole ir par garu.");
    return;
  }
  const String savedSsid = preferences.getString(wifiSsidKey(slot), "");
  if (ssid.isEmpty() && slot == 2) {
    preferences.remove(wifiSsidKey(2));
    preferences.remove(wifiPassKey(2));
    preferences.putInt("wifi_choice", 1);
  } else if (ssid.isEmpty()) {
    sendApiError(400, "Tīkla 1 nosaukums nedrīkst būt tukšs.");
    return;
  } else if (password.isEmpty() && ssid != savedSsid) {
    sendApiError(400, "Jaunam tīklam ievadi paroli.");
    return;
  } else {
    preferences.putString(wifiSsidKey(slot), ssid);
    if (!password.isEmpty()) preferences.putString(wifiPassKey(slot), password);
  }
  webServer.send(204);
  restartRequestedAt = max<uint32_t>(1, millis());
}

void handleApiSetupStart() {
  if (rejectDuringSetup()) return;
  JsonDocument doc;
  if (!readJsonBody(doc)) return;
  preferences.putBool("force_setup", true);
  webServer.send(204);
  restartRequestedAt = max<uint32_t>(1, millis());
}

void scanSetupNetworks();

void handleSetupNetworks() {
  if (!setupActive) {
    sendApiError(409, "Iestatīšana nav aktīva.");
    return;
  }
  if (webServer.arg("fresh") == "1") scanSetupNetworks();
  webServer.send(200, "application/json; charset=utf-8", setupNetworks);
}

void handleSetupStatus() {
  if (!setupActive) {
    sendApiError(409, "Iestatīšana nav aktīva.");
    return;
  }
  static const char *const names[] = {"idle", "connecting", "connected", "failed"};
  JsonDocument doc;
  doc["link"] = names[static_cast<uint8_t>(setupLink)];
  doc["device"] = deviceName;
  sendJson(doc);
}

void handleSetupWifi() {
  if (!setupActive) {
    sendApiError(409, "Iestatīšana nav aktīva.");
    return;
  }
  JsonDocument doc;
  if (!readJsonBody(doc)) return;
  const String ssid = doc["ssid"] | "";
  String password = doc["password"] | "";
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) {
    sendApiError(400, "Pārbaudi tīkla nosaukumu un paroli.");
    return;
  }
  if (password.isEmpty()) {
    for (int slot = 1; slot <= 2; ++slot) {
      if (preferences.getString(wifiSsidKey(slot), "") == ssid) {
        password = preferences.getString(wifiPassKey(slot), "");
      }
    }
  }
  setupSsid = ssid;
  setupPassword = password;
  setupAutomaticAttempt = false;
  setupLink = SetupLink::connecting;
  setupConnectRequested = true;
  webServer.send(202);
}

void handleSetupFinish() {
  if (!setupActive || setupLink != SetupLink::connected) {
    sendApiError(409, "Vispirms pieslēdz Wi-Fi.");
    return;
  }
  JsonDocument doc;
  if (!readJsonBody(doc)) return;
  setupFinishAt = max<uint32_t>(1, millis());
  webServer.send(204);
}

void startConfigurationWeb() {
  if (webStarted) return;
  webStarted = true;
  static const char *collectedHeaders[] = {"Content-Type"};
  webServer.collectHeaders(collectedHeaders, 1);
  webServer.on("/", HTTP_GET, []() {
    webServer.send_P(200, "text/html; charset=utf-8", setupActive ? ENERG_SETUP_UI : ENERG_WEB_UI);
  });
  webServer.on("/api/state", HTTP_GET, handleApiState);
  webServer.on("/api/stations", HTTP_GET, handleApiStations);
  webServer.on("/api/settings", HTTP_POST, handleApiSettings);
  webServer.on("/api/refresh", HTTP_POST, handleApiRefresh);
  webServer.on("/api/wifi", HTTP_POST, handleApiWifi);
  webServer.on("/api/factory-reset", HTTP_POST, handleApiFactoryReset);
  webServer.on("/api/setup/start", HTTP_POST, handleApiSetupStart);
  webServer.on("/api/setup/networks", HTTP_GET, handleSetupNetworks);
  webServer.on("/api/setup/status", HTTP_GET, handleSetupStatus);
  webServer.on("/api/setup/wifi", HTTP_POST, handleSetupWifi);
  webServer.on("/api/setup/finish", HTTP_POST, handleSetupFinish);
  webServer.onNotFound([]() {
    if (!setupActive) {
      webServer.send(404, "text/plain", "Not found");
      return;
    }
    webServer.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    webServer.send(302, "text/plain", "");
  });
  webServer.begin();
}

// ---------------------------------------------------------------- sensora kalibrēšana

void drawTouchCalibration(const char *message, int secondsLeft) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawBrandHeader();
  if (secondsLeft > 0) {
    centeredText(message, 21);
    centeredText(String(secondsLeft), 36, 3);
  } else {
    centeredText("KALIBRE...", 36);
  }
  display.display();
}

void calibrateTouchSensor() {
  for (int attempt = 1; attempt <= TOUCH_CALIBRATION_ATTEMPTS; ++attempt) {
    digitalWrite(TOUCH_POWER_PIN, LOW);
    const char *message = attempt == 1 ? "NEPIESKARIES SENSORAM" : "NONEM PIRKSTU";
    for (int seconds = TOUCH_WARNING_SECONDS; seconds > 0; --seconds) {
      drawTouchCalibration(message, seconds);
      delay(1000);
    }
    digitalWrite(TOUCH_POWER_PIN, HIGH);
    drawTouchCalibration(message, 0);
    delay(TOUCH_CALIBRATION_MS);
    if (!touchPressed()) return;
  }
  Serial.println("Touch: output still active after calibration");
}

// ---------------------------------------------------------------- QR un iestatīšanas ekrāni

constexpr uint32_t RESTART_HOLD_MS = 15000;
constexpr uint32_t RESTART_HINT_MS = 3000;
constexpr int QR_MODULES = 29;
constexpr int QR_SCALE = 2;
constexpr int QR_PANEL_X = 56;
constexpr int QR_PANEL_SIZE = 64;
constexpr int RESTART_BAR_X = 122;
constexpr int RESTART_BAR_WIDTH = 6;
uint32_t qrRows[QR_MODULES];
int qrSize = 0;
String qrUrl;

void captureQrModules(esp_qrcode_handle_t qrcode) {
  qrSize = esp_qrcode_get_size(qrcode);
  if (qrSize > QR_MODULES) {
    qrSize = 0;
    return;
  }
  for (int y = 0; y < qrSize; ++y) {
    qrRows[y] = 0;
    for (int x = 0; x < qrSize; ++x) {
      if (esp_qrcode_get_module(qrcode, x, y)) qrRows[y] |= 1UL << x;
    }
  }
}

void setQrText(const String &text) {
  if (text == qrUrl) return;
  qrUrl = text;
  qrSize = 0;
  if (text.isEmpty()) return;
  esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT();
  config.display_func = captureQrModules;
  config.max_qrcode_version = 3;
  config.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  if (esp_qrcode_generate(&config, text.c_str()) != ESP_OK) qrSize = 0;
}

String configUrl() {
  return WiFi.status() == WL_CONNECTED ? "http://" + WiFi.localIP().toString() + "/" : "";
}

void drawQrPanel(int panelX) {
  if (qrSize == 0) {
    display.setTextSize(1);
    display.setCursor(panelX + 23, 20);
    display.print("NAV");
    display.setCursor(panelX + 17, 34);
    display.print("WI-FI");
    return;
  }
  display.fillRect(panelX, 0, QR_PANEL_SIZE, QR_PANEL_SIZE, SSD1306_WHITE);
  const int offset = (QR_PANEL_SIZE - qrSize * QR_SCALE) / 2;
  for (int y = 0; y < qrSize; ++y) {
    for (int x = 0; x < qrSize; ++x) {
      if (qrRows[y] & (1UL << x)) {
        display.fillRect(panelX + offset + x * QR_SCALE, offset + y * QR_SCALE, QR_SCALE, QR_SCALE,
                         SSD1306_BLACK);
      }
    }
  }
}

void leftColumnText(const String &text, int16_t y, uint8_t size) {
  display.setTextSize(size);
  display.setCursor((QR_PANEL_X - 2 - static_cast<int>(text.length()) * 6 * size) / 2, y);
  display.print(text);
}

void drawRestartScreen(uint32_t held) {
  const uint32_t span = RESTART_HOLD_MS - RESTART_HINT_MS;
  const uint32_t left = RESTART_HOLD_MS - min(held, RESTART_HOLD_MS);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  leftColumnText("RESTARTS", 2, 1);
  leftColumnText(String((left + 999) / 1000), 17, 3);
  leftColumnText("ATLAID =", 46, 1);
  leftColumnText("ATCELT", 56, 1);
  setQrText(configUrl());
  drawQrPanel(QR_PANEL_X);
  display.drawRect(RESTART_BAR_X, 0, RESTART_BAR_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  const int filled = (min(left, span) * (SCREEN_HEIGHT - 2)) / span;
  display.fillRect(RESTART_BAR_X + 1, SCREEN_HEIGHT - 1 - filled, RESTART_BAR_WIDTH - 2, filled,
                   SSD1306_WHITE);
  display.display();
}

void setupColumnText(const String &text, int16_t y) {
  display.setCursor(max(0, (SCREEN_WIDTH - QR_PANEL_SIZE - static_cast<int>(text.length()) * 6) / 2), y);
  display.print(text);
}

void setupColumnLargeText(const String &text, int16_t y) {
  display.setTextSize(2);
  display.setCursor(max(0, (SCREEN_WIDTH - QR_PANEL_SIZE - static_cast<int>(text.length()) * 12) / 2), y);
  display.print(text);
  display.setTextSize(1);
}

void drawReadyScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  setupColumnText("GATAVS", 8);
  setupColumnText("ENERG", 30);
  setupColumnText(".LOCAL", 40);
  setQrText(configUrl());
  drawQrPanel(SCREEN_WIDTH - QR_PANEL_SIZE);
  display.display();
}

void drawSetupScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  switch (setupLink) {
    case SetupLink::connecting:
      setupColumnText("SLEDZAS", 22);
      setupColumnText("WI-FI...", 34);
      break;
    case SetupLink::connected:
      setupColumnText("SPIED", 22);
      setupColumnText("PABEIGT", 34);
      break;
    case SetupLink::failed:
      setupColumnText("NEIZDEVAS", 16);
      setupColumnText("PARBAUDI", 32);
      setupColumnText("PAROLI", 42);
      break;
    case SetupLink::idle:
      setupColumnText("NOSKENE", 2);
      setupColumnText("QR KODU", 12);
      setupColumnLargeText(deviceName.substring(0, 5), 28);
      setupColumnLargeText(deviceName.substring(6), 47);
      break;
  }
  setQrText("WIFI:T:nopass;S:" + deviceName + ";;");
  drawQrPanel(SCREEN_WIDTH - QR_PANEL_SIZE);
  display.display();
}

void scanSetupNetworks() {
  const int found = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  bool used[64] = {};
  const int count = min(found, 64);
  const String saved1 = preferences.getString(wifiSsidKey(1), "");
  const String saved2 = preferences.getString(wifiSsidKey(2), "");
  while (list.size() < SETUP_MAX_NETWORKS) {
    int best = -1;
    for (int i = 0; i < count; ++i) {
      if (!used[i] && !WiFi.SSID(i).isEmpty() && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
    }
    if (best < 0) break;
    const String ssid = WiFi.SSID(best);
    for (int i = 0; i < count; ++i) used[i] = used[i] || WiFi.SSID(i) == ssid;
    JsonObject network = list.add<JsonObject>();
    network["ssid"] = ssid;
    network["rssi"] = WiFi.RSSI(best);
    network["open"] = WiFi.encryptionType(best) == WIFI_AUTH_OPEN;
    network["saved"] = ssid == saved1 || ssid == saved2;
  }
  WiFi.scanDelete();
  setupNetworks = "";
  serializeJson(doc, setupNetworks);
}

void loadDeviceIdentity() {
  const uint64_t mac = ESP.getEfuseMac();
  char name[16];
  snprintf(name, sizeof(name), "Energ-%02X%02X", static_cast<uint8_t>(mac >> 32),
           static_cast<uint8_t>(mac >> 40));
  deviceName = name;
}

void restartSetupStation() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false);
  delay(200);
  WiFi.mode(WIFI_AP);
  delay(50);
  WiFi.mode(WIFI_AP_STA);
  delay(50);
}

void beginSetupConnection() {
  setupConnectRequested = false;
  restartSetupStation();
  setupLink = SetupLink::connecting;
  setupConnectStarted = millis();
  WiFi.begin(setupSsid.c_str(), setupPassword.isEmpty() ? nullptr : setupPassword.c_str());
}

void rememberSetupNetwork() {
  int slot = 0;
  for (int candidate = 2; candidate >= 1; --candidate) {
    if (preferences.getString(wifiSsidKey(candidate), "") == setupSsid) slot = candidate;
  }
  if (!slot) {
    if (preferences.getString(wifiSsidKey(1), "").isEmpty()) slot = 1;
    else if (preferences.getString(wifiSsidKey(2), "").isEmpty()) slot = 2;
    else slot = preferences.getInt("wifi_choice", 1) == 2 ? 2 : 1;
  }
  preferences.putString(wifiSsidKey(slot), setupSsid);
  if (!setupPassword.isEmpty() || !preferences.isKey(wifiPassKey(slot))) {
    preferences.putString(wifiPassKey(slot), setupPassword);
  }
  preferences.putInt("wifi_choice", slot);
}

void updateSetupConnection() {
  if (setupLink != SetupLink::connecting || setupConnectRequested) return;
  if (WiFi.status() == WL_CONNECTED) {
    setupLink = SetupLink::connected;
    if (!setupAutomaticAttempt) rememberSetupNetwork();
  } else if (millis() - setupConnectStarted >= SETUP_CONNECT_TIMEOUT_MS) {
    restartSetupStation();
    setupLink = setupAutomaticAttempt ? SetupLink::idle : SetupLink::failed;
  }
}

void retrySavedNetworks(uint32_t &lastRetry, int &nextSlot) {
  if (setupLink == SetupLink::connecting || setupLink == SetupLink::connected) return;
  if (WiFi.softAPgetStationNum() > 0 || millis() - lastRetry < SETUP_RETRY_MS) return;
  lastRetry = millis();
  for (int tries = 0; tries < 2; ++tries) {
    const int slot = nextSlot;
    nextSlot = nextSlot == 1 ? 2 : 1;
    const String ssid = preferences.getString(wifiSsidKey(slot), "");
    if (ssid.isEmpty()) continue;
    setupSsid = ssid;
    setupPassword = preferences.getString(wifiPassKey(slot), "");
    setupAutomaticAttempt = true;
    beginSetupConnection();
    return;
  }
}

bool runSetupMode(bool forced) {
  if (WiFi.status() != WL_CONNECTED) stopWifiAttempt();
  scanSetupNetworks();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(deviceName.c_str());
  delay(200);
  dnsServer.start(53, "*", WiFi.softAPIP());
  setupActive = true;
  setupLink = SetupLink::idle;
  setupFinishAt = 0;
  startConfigurationWeb();
  Serial.printf("Setup: access point %s at %s\n", deviceName.c_str(),
                WiFi.softAPIP().toString().c_str());
  uint32_t lastRetry = millis(), lastDraw = 0, connectedAt = 0;
  int nextSlot = 1;
  while (true) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    if (restartRequestedAt && millis() - restartRequestedAt > 700) ESP.restart();
    if (setupConnectRequested) beginSetupConnection();
    updateSetupConnection();
    if (!forced) retrySavedNetworks(lastRetry, nextSlot);
    if (setupLink == SetupLink::connected) {
      if (!connectedAt) connectedAt = max<uint32_t>(1, millis());
      if (setupAutomaticAttempt || millis() - connectedAt >= SETUP_AUTO_FINISH_MS) break;
    } else {
      connectedAt = 0;
    }
    if (setupFinishAt && millis() - setupFinishAt >= SETUP_CLOSE_DELAY_MS) break;
    if (millis() - lastDraw >= 250) {
      lastDraw = millis();
      drawSetupScreen();
    }
    delay(5);
  }
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  setupActive = false;
  setupPassword = "";
  return WiFi.status() == WL_CONNECTED;
}

// Atgriež true, kamēr restarta atskaite aizņem ekrānu.
bool handleRestartHold() {
  static uint32_t pressedSince = 0;
  static bool holding = false;
  static bool seenReleased = false;
  if (!touchPressed()) {
    seenReleased = true;
    holding = false;
    return false;
  }
  if (!seenReleased) return false;
  const uint32_t now = millis();
  if (!holding) {
    holding = true;
    pressedSince = now;
  }
  const uint32_t held = now - pressedSince;
  if (held < RESTART_HINT_MS) return false;
  if (held >= RESTART_HOLD_MS) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    centeredText("RESTARTS...", 28);
    display.display();
    delay(500);
    ESP.restart();
  }
  drawRestartScreen(held);
  return true;
}

// Nakts restarts notīra atmiņas fragmentāciju pēc daudzām HTTPS sesijām.
void restartNightlyIfDue() {
  static uint32_t lastCheckAt = 0;
  if (millis() - lastCheckAt < 60000UL) return;
  lastCheckAt = millis();
  struct tm now;
  if (millis() < DAILY_RESTART_MIN_UPTIME_MS || !getLocalTime(&now, 0)) return;
  if (now.tm_hour != DAILY_RESTART_HOUR) return;
  ESP.restart();
}

// ---------------------------------------------------------------- setup / loop

void setup() {
  Serial.begin(115200);
  pinMode(LIST_BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(TOUCH_POWER_PIN, OUTPUT);
  digitalWrite(TOUCH_POWER_PIN, LOW);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    while (true) delay(1000);
  }
  calibrateTouchSensor();
  preferences.begin("energ", false);
  loadSettings();
  loadDeviceIdentity();
  if (!configureWifi()) {
    appState = AppState::error;
    statusDetail = "Wi-Fi neizdevas";
    drawStatus();
    return;
  }
  startConfigurationWeb();
  if (!synchronizeClock()) {
    appState = AppState::error;
    drawStatus();
    return;
  }
  appState = AppState::syncing;
  statusDetail = "Elektribas cenas";
  drawStatus();
  refreshElectricity();
  statusDetail = "Degvielas cenas";
  drawStatus();
  refreshFuel();
  appState = AppState::ready;
  sectionStartedAt = millis();
  if (!sections[currentSection].enabled) advanceSection();
}

bool due(uint32_t at) { return static_cast<int32_t>(millis() - at) >= 0; }

void loop() {
  webServer.handleClient();
  if (factoryResetRequestedAt && millis() - factoryResetRequestedAt > 700) performFactoryReset();
  if (restartRequestedAt && millis() - restartRequestedAt > 700) ESP.restart();
  if (appState != AppState::ready) {
    handleRestartHold();
    delay(20);
    return;
  }
  restartNightlyIfDue();
  if (electricityRefreshRequested || (sections[SEC_ELECTRICITY].enabled && due(nextElectricityAt))) {
    electricityRefreshRequested = false;
    refreshElectricity();
  }
  if (fuelRefreshRequested || (anyFuelSectionEnabled() && due(nextFuelAt))) {
    fuelRefreshRequested = false;
    refreshFuel();
  }
  if (handleRestartHold()) {
    delay(20);
    return;
  }
  handleTouchTap();
  drawSections();
  delay(20);
}
