/*
 * ================================================================
 *  Arduino UNO R4 WiFi — Pianta Monitor v3.0
 *  DHT22 + Soil V2.0 + OLED 0.96" + 4 pulsanti + LED + Web UI
 *  Setup Wi‑Fi iniziale via Chrome con salvataggio in EEPROM
 *  Silvio Chessari — 2026
 *  IbleoCare_JA
 * ================================================================
 *
 *  PRIMO AVVIO
 *  1) L'Arduino crea la rete Wi‑Fi: PiantaSetup
 *  2) Password rete setup: pianta123
 *  3) Sul display compare il QR della pagina: http://192.168.4.1
 *  4) Con il telefono:
 *     - collegati a PiantaSetup
 *     - scansiona il QR oppure apri http://192.168.4.1 in Chrome
 *     - inserisci SSID e password del Wi‑Fi di casa
 *  5) L'Arduino salva tutto in EEPROM e si riavvia
 *  6) Dopo il riavvio si collega da solo al Wi‑Fi di casa
 *
 *  RESET WI‑FI
 *  - In questa versione il reset Wi‑Fi avviene dalla pagina web /resetwifi.
 *    La logica TTP223 e' stata rimossa completamente.
 *
 *  OLED + 4 BOTTONI
 *  - BTN1 (D8): pagina Chrome / QR setup
 *  - BTN2 (D9): IP + nome Wi‑Fi + password
 *  - BTN3 (D10): stato terreno
 *  - BTN4 (D11): interfaccia tecnica
 *  - Ogni pressione accende il display per 30 secondi, poi lo spegne.
 *
 *  LIBRERIE (Library Manager)
 *    - DHT sensor library + Adafruit Unified Sensor
 *    - Adafruit SSD1306 + Adafruit GFX Library
 *    - WiFiS3
 *    - EEPROM
 * ================================================================
 */

#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiS3.h>
#include <EEPROM.h>
#include <PubSubClient.h>

#define PIN_DHT22       4
#define PIN_SOIL        A0
#define PIN_BTN_1       8
#define PIN_BTN_2       9
#define PIN_BTN_3       10
#define PIN_BTN_4       11
#define PIN_LED_VERDE   5
#define PIN_LED_GIALLO  6
#define PIN_LED_ROSSO   7

#define DHT_TYPE          DHT22
#define READ_INTERVAL_MS  2000UL

#define SOIL_DRY      543
#define SOIL_WET      290
#define SOIL_SAMPLES  9
#define SOIL_MEDIAN   4

#define OLED_WIDTH        128
#define OLED_HEIGHT        64
#define OLED_ADDR        0x3C
#define OLED_TIMEOUT_MS 30000UL
#define BUTTON_DEBOUNCE_MS  60UL
#define BUTTON_ACTIVE_LEVEL LOW

#define SOGLIA_VERDE   66
#define SOGLIA_GIALLO  33

#define WIFI_CONNECT_TIMEOUT_MS 12000UL
#define INTERNET_CHECK_MS        60000UL

#define STIMA_INTERVALLO_MS 10000UL
#define STIMA_FINESTRA      6

const char* SETUP_AP_SSID = "PiantaSetup";
const char* SETUP_AP_PASS = "pianta123";
const char* SETUP_URL     = "http://192.168.4.1";
const char* PING_TARGET   = "8.8.8.8";
const char* HTTP_TEST_HOST = "example.com";
const uint16_t HTTP_TEST_PORT = 80;
const uint16_t WEB_SERVER_PORT_LAN = 80;
const uint16_t WEB_SERVER_PORT_WAN = 8080;
const char* ROUTER_FORWARD_HINT = "TCP 8080 -> 192.168.1.156:8080";

// ========================= MQTT =========================
const char*    MQTT_BROKER           = "192.168.1.151";
const uint16_t MQTT_PORT             = 1883;
const uint8_t  MQTT_NODE_ID          = 1;        // ID numerico progressivo del nodo
const uint32_t MQTT_PUBLISH_INTERVAL = 2000UL;   // pubblica ogni 2 secondi
char           MQTT_TOPIC_DATA[24];               // costruito in setup(): "pianta/nodoN/data"

struct TipoPianta {
  const char* emoji;
  const char* nome;
  const char* descrizione;
  const char* esempi;
  const char* frequenza;
  int         soglia_min;
  float       perdita_base;
  float       k_temp;
};

const TipoPianta TIPI[] = {
  {"🌵", "Piante Grasse",   "Terreno quasi secco tra annaffiature.",         "Cactus, aloe, echeveria, agave",            "Rara (3-4 settimane)", 15, 0.118f, 0.004f},
  {"🪴", "Piante da Interno", "Terreno leggermente umido, mai fradicio.",    "Pothos, ficus, monstera, spatifillo",      "Moderata (5-7 giorni)", 35, 0.542f, 0.012f},
  {"🌳", "Piante da Esterno", "Terreno drenato con cicli regolari.",         "Rose, gerani, lavanda, ibisco",             "Regolare (2-3 giorni)", 30, 1.458f, 0.025f},
  {"🌿", "Piante Aromatiche", "Terreno umido ma ben drenato.",               "Basilico, menta, prezzemolo, rosmarino",    "Frequente (2-4 giorni)", 40, 0.833f, 0.018f},
  {"🌸", "Piante Delicate",  "Terreno sempre umido, sensibili alla siccita'.", "Felce, orchidea, calathea, begonia",      "Costante (1-2 giorni)", 55, 1.250f, 0.032f}
};
#define NUM_TIPI 5

struct DeviceConfig {
  uint32_t magic;
  uint8_t  version;
  uint8_t  wifiConfigured;
  uint8_t  plantConfigured;
  uint8_t  tipoPianta;
  uint8_t  cloudConfigured;
  char     ssid[33];
  char     pass[65];
  char     cloudApi[129];
};

const uint32_t CFG_MAGIC   = 0x504C4E54UL; // "PLNT"
const uint8_t  CFG_VERSION = 2;

// QR fisso della pagina di setup: http://192.168.4.1
// Matrice 25x25, senza bordo. Disegnata sul display con quiet zone software.
const char* SETUP_QR[25] = {
  "1111111011011101001111111",
  "1000001001010110001000001",
  "1011101000101010001011101",
  "1011101010110000101011101",
  "1011101011101011001011101",
  "1000001010000000001000001",
  "1111111010101010101111111",
  "0000000010011101000000000",
  "1000101110001100111111001",
  "1100010001001111010111010",
  "0110111101111001110101100",
  "1111100001010010011010110",
  "0010011011100111001001111",
  "1101110001101001000110010",
  "0000111101011111101101100",
  "0001010100110101111001110",
  "1111011010001101111111100",
  "0000000011000111100010100",
  "1111111010000000101011000",
  "1000001001101011100011111",
  "1011101010010110111110101",
  "1011101001001000101001111",
  "1011101001011110101000010",
  "1000001000010100010111110",
  "1111111010101100011000111"
};

WiFiServer   serverLan(WEB_SERVER_PORT_LAN);
WiFiServer   serverWan(WEB_SERVER_PORT_WAN);
WiFiClient   mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
DHT dht(PIN_DHT22, DHT_TYPE);
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

DeviceConfig cfg;

unsigned long lastRead          = 0;
unsigned long oledOnTime        = 0;
unsigned long lastStimaTime     = 0;
unsigned long pendingRebootAt   = 0;
unsigned long lastInternetCheck  = 0;
unsigned long lastMqttPublish    = 0;
unsigned long lastMqttReconnect  = 0;

bool oledActive      = false;
bool configurato     = false;
bool setupModeWiFi   = false;
bool wifiConnesso    = false;
bool lastNaN         = true;
bool internetOk      = false;

bool          wifiConnecting    = false;
uint8_t       wifiAttempts      = 0;
unsigned long wifiConnectStart  = 0;

enum OledPage {
  OLED_PAGE_CHROME = 0,
  OLED_PAGE_WIFI_INFO,
  OLED_PAGE_SOIL_STATUS,
  OLED_PAGE_TECH
};

OledPage oledPage = OLED_PAGE_SOIL_STATUS;

const uint8_t BUTTON_PINS[4] = {PIN_BTN_1, PIN_BTN_2, PIN_BTN_3, PIN_BTN_4};
const char* BUTTON_NAMES[4] = {"BTN1", "BTN2", "BTN3", "BTN4"};
bool buttonRawState[4] = {false, false, false, false};
bool buttonStableState[4] = {false, false, false, false};
unsigned long buttonDebounceAt[4] = {0, 0, 0, 0};

int   tipoPiantaAttuale = 1;
int   ledStato          = 0;
int   lastPct           = 0;
int   lastSoilRaw       = 0;
int   soilAutoMax       = 0;
int   soilAutoMin       = 1023;
float lastTemp          = NAN;
float lastHum           = NAN;
char  ipStr[20]         = "192.168.4.1";
char  lastNetInfo[32]    = "non verificato";
int   lastPingMs         = -1;

int   stimaStorico[STIMA_FINESTRA];
int   stimaCount          = 0;
float perditaOraReal      = 0.0f;
float perditaOraEffettiva = 0.0f;
float oreMancantiStima    = -1.0f;
bool  stimaCalibrata      = false;

// Prototipi UI non bloccante
void handleButtons(unsigned long now);
void serviceUiDuringWait();
void cooperativeDelay(unsigned long ms);
bool mqttConnect();
void mqttPublish();

// ========================= Config EEPROM =========================
void configDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic           = CFG_MAGIC;
  cfg.version         = CFG_VERSION;
  cfg.wifiConfigured  = 0;
  cfg.plantConfigured = 0;
  cfg.tipoPianta      = 1;
  cfg.cloudConfigured = 0;
}

void saveConfig() {
  EEPROM.put(0, cfg);
}

void loadConfig() {
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC || cfg.version != CFG_VERSION) {
    configDefaults();
    saveConfig();
  }
  tipoPiantaAttuale = (cfg.tipoPianta < NUM_TIPI) ? cfg.tipoPianta : 1;
  configurato       = (cfg.plantConfigured == 1);
}

void clearWifiConfigOnly() {
  cfg.wifiConfigured  = 0;
  cfg.cloudConfigured = 0;
  memset(cfg.ssid, 0, sizeof(cfg.ssid));
  memset(cfg.pass, 0, sizeof(cfg.pass));
  memset(cfg.cloudApi, 0, sizeof(cfg.cloudApi));
  saveConfig();
}

void clearAllConfig() {
  configDefaults();
  saveConfig();
  tipoPiantaAttuale = 1;
  configurato       = false;
}

void savePlantConfig() {
  cfg.tipoPianta      = (uint8_t)tipoPiantaAttuale;
  cfg.plantConfigured = configurato ? 1 : 0;
  saveConfig();
}

void saveWifiCredentials(const String& ssid, const String& pass, const String& cloudApi) {
  memset(cfg.ssid, 0, sizeof(cfg.ssid));
  memset(cfg.pass, 0, sizeof(cfg.pass));
  memset(cfg.cloudApi, 0, sizeof(cfg.cloudApi));
  ssid.toCharArray(cfg.ssid, sizeof(cfg.ssid));
  pass.toCharArray(cfg.pass, sizeof(cfg.pass));
  cloudApi.toCharArray(cfg.cloudApi, sizeof(cfg.cloudApi));
  cfg.wifiConfigured  = (cfg.ssid[0] != '\0') ? 1 : 0;
  cfg.cloudConfigured = (cfg.cloudApi[0] != '\0') ? 1 : 0;
  saveConfig();
}

bool hasCloudApiSaved() {
  return cfg.cloudConfigured == 1 && cfg.cloudApi[0] != '\0';
}

// ========================= Utility =========================
const char* soilLabel(int pct) {
  if (pct < 25) return "SECCO";
  if (pct < 50) return "ASCIUTTO";
  if (pct < 75) return "UMIDO";
  return "SATURO";
}

int soilPercent(int raw) {
  return constrain((int)map(raw, SOIL_DRY, SOIL_WET, 0, 100), 0, 100);
}

void aggiornaLED(int pct) {
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_GIALLO, LOW);
  digitalWrite(PIN_LED_ROSSO, LOW);

  if (pct >= SOGLIA_VERDE) {
    digitalWrite(PIN_LED_VERDE, HIGH);
    ledStato = 0;
  } else if (pct >= SOGLIA_GIALLO) {
    digitalWrite(PIN_LED_GIALLO, HIGH);
    ledStato = 1;
  } else {
    digitalWrite(PIN_LED_ROSSO, HIGH);
    ledStato = 2;
  }
}

void resetStima() {
  stimaCount = 0;
  perditaOraReal = 0.0f;
  oreMancantiStima = -1.0f;
  stimaCalibrata = false;
  lastStimaTime = 0;
}

void aggiornaStimaAnnaffiatura(int pctAttuale) {
  if (stimaCount < STIMA_FINESTRA) {
    stimaStorico[stimaCount++] = pctAttuale;
  } else {
    for (int i = 0; i < STIMA_FINESTRA - 1; i++) stimaStorico[i] = stimaStorico[i + 1];
    stimaStorico[STIMA_FINESTRA - 1] = pctAttuale;
  }

  if (stimaCount >= 2) {
    float delta = (float)(stimaStorico[0] - stimaStorico[stimaCount - 1]);
    float oreF  = (stimaCount - 1) * (STIMA_INTERVALLO_MS / 1000.0f / 3600.0f);
    if (delta > 0.0f && oreF > 0.0f) {
      perditaOraReal = delta / oreF;
      stimaCalibrata = true;
    }
  }

  float tmpCorr = 1.0f;
  if (!lastNaN && lastTemp > 20.0f) tmpCorr = 1.0f + TIPI[tipoPiantaAttuale].k_temp * (lastTemp - 20.0f);

  float teorica = TIPI[tipoPiantaAttuale].perdita_base * tmpCorr;
  float perdita = teorica;

  if (stimaCalibrata && perditaOraReal > 0.01f) {
    float peso = min(1.0f, (float)(stimaCount - 1) / 3.0f);
    perdita = teorica * (1.0f - peso) + perditaOraReal * peso;
  }

  perditaOraEffettiva = perdita;
  float percSopra = (float)(pctAttuale - TIPI[tipoPiantaAttuale].soglia_min);
  if (percSopra <= 0) oreMancantiStima = 0.0f;
  else if (perdita > 0.001f) oreMancantiStima = percSopra / perdita;
  else oreMancantiStima = -1.0f;
}

void formatTempo(float ore, char* buf, int len) {
  if (ore < 0) snprintf(buf, len, "calcolo...");
  else if (ore == 0) snprintf(buf, len, "ANNAFFIA ORA!");
  else if (ore < 1.0f) snprintf(buf, len, "tra %d min", (int)(ore * 60));
  else if (ore < 48.0f) {
    int h = (int)ore;
    int m = (int)((ore - h) * 60.0f);
    if (m > 0) snprintf(buf, len, "tra %dh %dm", h, m);
    else snprintf(buf, len, "tra %d ore", h);
  } else {
    int g = (int)(ore / 24.0f);
    int h = ((int)ore) % 24;
    if (h > 0) snprintf(buf, len, "tra %dg %dh", g, h);
    else snprintf(buf, len, "tra %d giorni", g);
  }
}

String urlDecode(const String& src) {
  String out;
  out.reserve(src.length());
  for (unsigned int i = 0; i < src.length(); i++) {
    char c = src[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < src.length()) {
      char h1 = src[i + 1];
      char h2 = src[i + 2];
      auto hexVal = [](char x) -> int {
        if (x >= '0' && x <= '9') return x - '0';
        if (x >= 'A' && x <= 'F') return x - 'A' + 10;
        if (x >= 'a' && x <= 'f') return x - 'a' + 10;
        return -1;
      };
      int a = hexVal(h1), b = hexVal(h2);
      if (a >= 0 && b >= 0) {
        out += char((a << 4) | b);
        i += 2;
      } else {
        out += c;
      }
    } else {
      out += c;
    }
  }
  return out;
}

String requestPath(const String& req) {
  int firstLineEnd = req.indexOf("\r\n");
  String firstLine = (firstLineEnd >= 0) ? req.substring(0, firstLineEnd) : req;
  if (!firstLine.startsWith("GET ")) return "/";
  int sp2 = firstLine.indexOf(' ', 4);
  if (sp2 < 0) return "/";
  return firstLine.substring(4, sp2);
}

String pathOnly(const String& url) {
  int q = url.indexOf('?');
  return (q >= 0) ? url.substring(0, q) : url;
}

String queryParam(const String& url, const String& key) {
  int q = url.indexOf('?');
  if (q < 0) return "";
  String qs = url.substring(q + 1);
  int start = 0;
  while (start < (int)qs.length()) {
    int amp = qs.indexOf('&', start);
    if (amp < 0) amp = qs.length();
    String part = qs.substring(start, amp);
    int eq = part.indexOf('=');
    if (eq > 0) {
      String k = part.substring(0, eq);
      if (k == key) return urlDecode(part.substring(eq + 1));
    }
    start = amp + 1;
  }
  return "";
}

void updateIpString(IPAddress ip) {
  snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void startWebServers() {
  serverLan.begin();
  serverWan.begin();
}

bool ipIsValid(IPAddress ip) {
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

bool waitForValidWiFiIP(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      if (ipIsValid(ip)) return true;
    }
    serviceUiDuringWait();
    delay(5);
  }
  return false;
}

bool isButtonPressedNow(uint8_t index) {
  return digitalRead(BUTTON_PINS[index]) == BUTTON_ACTIVE_LEVEL;
}

void drawPageHeader(uint8_t pageNum, const __FlashStringHelper* title) {
  oled.fillRect(0, 0, OLED_WIDTH, 12, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setTextSize(1);
  oled.setCursor(2, 2);
  oled.print(pageNum);
  oled.print(F(". "));
  oled.print(title);
  oled.setTextColor(SSD1306_WHITE);
}

void drawBottomHint(const __FlashStringHelper* hint) {
  oled.fillRect(0, 54, OLED_WIDTH, 10, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setTextSize(1);
  oled.setCursor(2, 56);
  oled.print(hint);
  oled.setTextColor(SSD1306_WHITE);
}

void printCenteredLine(int y, const char* text, uint8_t textSize) {
  int16_t x1, y1;
  uint16_t w, h;
  oled.setTextSize(textSize);
  oled.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int x = (OLED_WIDTH - (int)w) / 2;
  if (x < 0) x = 0;
  oled.setCursor(x, y);
  oled.print(text);
}

void printCenteredFlashLine(int y, const __FlashStringHelper* text, uint8_t textSize) {
  String tmp(text);
  printCenteredLine(y, tmp.c_str(), textSize);
}

// ========================= OLED =========================

// oled.display() su UNO R4 WiFi scrive solo page 0: il puntatore GDDRAM
// resetta a ogni STOP I2C. Workaround: PAGE mode con chunk da 16 byte (17 byte
// per transazione), ri-specificando page+colonna prima di ogni chunk.
void oledDisplay() {
  uint8_t *buf = oled.getBuffer();
  for (uint8_t page = 0; page < 8; page++) {
    uint16_t base = (uint16_t)page * 128;
    for (uint8_t chunk = 0; chunk < 8; chunk++) {
      uint8_t col = chunk * 16;

      Wire.beginTransmission(OLED_ADDR);
      Wire.write(0x00);
      Wire.write(0xB0 | page);
      Wire.write(col & 0x0F);
      Wire.write(0x10 | (col >> 4));
      uint8_t e1 = Wire.endTransmission();

      Wire.beginTransmission(OLED_ADDR);
      Wire.write(0x40);
      for (uint8_t i = 0; i < 16; i++) Wire.write(buf[base + col + i]);
      uint8_t e2 = Wire.endTransmission();

      if (e1 != 0 || e2 != 0) {
        Serial.print(F("[OLED] p=")); Serial.print(page);
        Serial.print(F(" c=")); Serial.print(col);
        Serial.print(F(" e1=")); Serial.print(e1);
        Serial.print(F(" e2=")); Serial.println(e2);
      }
    }
  }
}

void drawSetupQR(int ox, int oy, int scale, int quietZone) {
  int totalModules = 25 + quietZone * 2;
  int size = totalModules * scale;
  oled.fillRect(ox, oy, size, size, SSD1306_WHITE);
  for (int y = 0; y < 25; y++) {
    for (int x = 0; x < 25; x++) {
      if (SETUP_QR[y][x] == '1') {
        oled.fillRect(ox + (x + quietZone) * scale,
                      oy + (y + quietZone) * scale,
                      scale, scale, SSD1306_BLACK);
      }
    }
  }
}

void printTrimmedValue(int x, int y, const char* value, uint8_t maxChars) {
  char buf[32];
  size_t len = strlen(value);
  if (len <= maxChars) {
    snprintf(buf, sizeof(buf), "%s", value);
  } else {
    uint8_t keep = (maxChars > 3) ? maxChars - 3 : maxChars;
    snprintf(buf, sizeof(buf), "%.*s...", keep, value);
  }
  oled.setCursor(x, y);
  oled.print(buf);
}

void getMacString(char* buf, size_t len) {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

void drawConnectingScreen() {
  oled.clearDisplay();
  drawPageHeader(1, F("CONNESSIONE"));
  oled.setTextSize(1);
  oled.setCursor(0, 18); oled.print(F("Sto cercando il WiFi"));
  oled.setCursor(0, 30); oled.print(F("SSID:"));
  printTrimmedValue(34, 30, cfg.ssid[0] ? cfg.ssid : "(vuoto)", 15);
  oled.setCursor(0, 42); oled.print(F("Attendi qualche sec"));
  drawBottomHint(F("BTN2 rete"));
  oledDisplay();
}

void drawChromePage() {
  oled.clearDisplay();

  if (setupModeWiFi) {
    // QR 54x54 allineato al bordo destro, centrato in altezza
    // scale=2, quietZone=1 -> (25+2)*2 = 54px
    drawSetupQR(74, 5, 2, 1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0,  4); oled.print(F("1. WiFi:"));
    oled.setCursor(0, 14); oled.print(F("PiantaSetup"));
    oled.setCursor(0, 24); oled.print(F("pianta123"));
    oled.setCursor(0, 36); oled.print(F("2. Scansiona"));
    oled.setCursor(0, 46); oled.print(F("il QR ->"));
    oledDisplay();
    return;
  }

  if (!wifiConnesso) {
    drawConnectingScreen();
    return;
  }

  // Connesso: AP spento, mostra IP dashboard
  drawPageHeader(1, F("DASHBOARD WEB"));
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 16); oled.print(F("Apri Chrome:"));
  oled.setCursor(0, 28); oled.print(F("http://"));
  oled.setCursor(42, 28); oled.print(ipStr);
  oled.setCursor(0, 40); oled.print(F("porta 80"));
  oled.setCursor(0, 52); oled.print(F("(porta 8080 x WAN)"));
  oledDisplay();
}

void drawWifiInfoPage() {
  oled.clearDisplay();
  char macBuf[18];
  getMacString(macBuf, sizeof(macBuf));

  if (setupModeWiFi) {
    drawPageHeader(2, F("RETE SETUP AP"));
    oled.setTextSize(1);
    oled.setCursor(0, 14); oled.print(F("SSID: PiantaSetup"));
    oled.setCursor(0, 24); oled.print(F("PASS: pianta123"));
    oled.setCursor(0, 34); oled.print(F("IP:   192.168.4.1"));
    oled.setCursor(0, 44); oled.print(F("MAC:"));
    // MAC in 2 righe per leggibilita (xx:xx:xx / xx:xx:xx)
    oled.setCursor(0, 54); oled.print(macBuf);
    oledDisplay();
    return;
  }

  drawPageHeader(2, F("RETE DI CASA"));
  oled.setTextSize(1);

  // SSID troncato a 16 chars (128px - "SSID:"30px = 98px / 6 = 16)
  oled.setCursor(0, 14); oled.print(F("SSID: "));
  printTrimmedValue(36, 14, cfg.ssid[0] ? cfg.ssid : "--", 15);

  // Password
  oled.setCursor(0, 24); oled.print(F("PASS: "));
  printTrimmedValue(36, 24, cfg.pass[0] ? cfg.pass : "--", 15);

  // IP (come pingarlo sulla LAN)
  oled.setCursor(0, 34); oled.print(F("IP:   "));
  oled.print(ipStr);

  // MAC (come appare nel pannello del router / tabella ARP)
  oled.setCursor(0, 44); oled.print(F("MAC:  "));
  // Stampa solo ultimi 3 ottetti per brevita: xx:xx:xx
  oled.print(&macBuf[9]);

  // Stato ping
  oled.setCursor(0, 54);
  if (internetOk) {
    oled.print(F("Ping 8.8.8.8: "));
    oled.print(lastPingMs);
    oled.print(F("ms"));
  } else if (wifiConnesso) {
    oled.print(F("WiFi OK  Internet KO"));
  } else {
    oled.print(F("WiFi non connesso"));
  }

  oledDisplay();
}

void drawSoilStatusPage() {
  oled.clearDisplay();

  bool needsWater = (lastPct <= TIPI[tipoPiantaAttuale].soglia_min);
  bool checkSoon  = (!needsWater && lastPct < TIPI[tipoPiantaAttuale].soglia_min + 12);

  // Titolo
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(F("TERRENO"));

  // Percentuale grande
  oled.setTextSize(3);
  char pctBuf[6];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", lastPct);
  printCenteredLine(14, pctBuf, 3);

  // Barra umidita
  int barW = map(lastPct, 0, 100, 0, OLED_WIDTH - 4);
  oled.drawRect(0, 40, OLED_WIDTH, 8, SSD1306_WHITE);
  if (barW > 0) oled.fillRect(2, 42, barW, 4, SSD1306_WHITE);

  // Messaggio stato - invertito se urgente
  oled.setTextSize(1);
  if (needsWater) {
    oled.fillRect(0, 52, OLED_WIDTH, 12, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    printCenteredFlashLine(54, F(">>> INNAFFIA! <<<"), 1);
    oled.setTextColor(SSD1306_WHITE);
  } else if (checkSoon) {
    printCenteredFlashLine(53, F("Controlla presto"), 1);
  } else {
    printCenteredFlashLine(53, F("Pianta OK  :)"), 1);
  }

  oledDisplay();
}

void drawTechPage() {
  oled.clearDisplay();
  drawPageHeader(4, F("SENSORI"));

  oled.setTextSize(1);

  // Suolo
  oled.setCursor(0, 15);
  oled.print(F("Suolo:  "));
  oled.print(lastPct);
  oled.print(F("% ("));
  oled.print(soilLabel(lastPct));
  oled.print(F(")"));

  // Temperatura
  oled.setCursor(0, 27);
  oled.print(F("Temp:   "));
  if (lastNaN) {
    oled.print(F("--"));
  } else {
    oled.print(lastTemp, 1);
    oled.print(F(" C"));
  }

  // Umidita aria
  oled.setCursor(0, 39);
  oled.print(F("Umid.:  "));
  if (lastNaN) {
    oled.print(F("--"));
  } else {
    oled.print(lastHum, 0);
    oled.print(F(" %"));
  }

  // IP / stato WiFi
  oled.setCursor(0, 51);
  if (wifiConnesso) {
    oled.print(ipStr);
  } else {
    oled.print(F("WiFi non connesso"));
  }

  oledDisplay();
}

void drawOLED() {
  switch (oledPage) {
    case OLED_PAGE_CHROME:
      drawChromePage();
      break;
    case OLED_PAGE_WIFI_INFO:
      drawWifiInfoPage();
      break;
    case OLED_PAGE_TECH:
      drawTechPage();
      break;
    case OLED_PAGE_SOIL_STATUS:
    default:
      drawSoilStatusPage();
      break;
  }
}

void oledWake() {
  // ssd1306_command(DISPLAYON) fallisce silenziosamente su UNO R4 via I2C.
  // begin() e' piu lento ma reinizializza correttamente il controller ogni volta.
  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.setTextColor(SSD1306_WHITE);
  oledActive = true;
  drawOLED();
  oledOnTime = millis(); // dopo drawOLED: evita underflow se now era stale
}

void oledSleep() {
  oled.ssd1306_command(SSD1306_DISPLAYOFF);
  oledActive = false;
}

void showOledPage(OledPage page) {
  oledPage = page;
  if (!oledActive) oledWake();
  else {
    oledOnTime = millis();
    drawOLED();
  }
}

void onButtonPressed(uint8_t index) {
  switch (index) {
    case 0: showOledPage(OLED_PAGE_CHROME); break;
    case 1: showOledPage(OLED_PAGE_WIFI_INFO); break;
    case 2: showOledPage(OLED_PAGE_SOIL_STATUS); break;
    case 3: showOledPage(OLED_PAGE_TECH); break;
  }
}

bool anyButtonHeld() {
  for (uint8_t i = 0; i < 4; i++) {
    if (isButtonPressedNow(i)) return true;
  }
  return false;
}

void handleButtons(unsigned long now) {
  for (uint8_t i = 0; i < 4; i++) {
    bool rawPressed = isButtonPressedNow(i);

    if (rawPressed != buttonRawState[i]) {
      buttonRawState[i] = rawPressed;
      buttonDebounceAt[i] = now;
    }

    if ((now - buttonDebounceAt[i]) >= BUTTON_DEBOUNCE_MS && buttonStableState[i] != buttonRawState[i]) {
      buttonStableState[i] = buttonRawState[i];
      if (buttonStableState[i]) {
        Serial.print(F("[BTN] "));
        Serial.println(BUTTON_NAMES[i]);
        onButtonPressed(i);
      }
    }
  }
}

void serviceUiDuringWait() {
  unsigned long now = millis();
  handleButtons(now);
  now = millis(); // refresh: oledWake() in handleButtons puo avanzare millis
  if (oledActive && anyButtonHeld()) oledOnTime = now;
  if (oledActive && (now - oledOnTime >= OLED_TIMEOUT_MS) && !anyButtonHeld()) oledSleep();
}

void cooperativeDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    serviceUiDuringWait();
    delay(5);
  }
}

// ========================= Wi‑Fi =========================
void resetInternetStatus() {
  internetOk = false;
  lastPingMs = -1;
  strncpy(lastNetInfo, "non verificato", sizeof(lastNetInfo));
  lastNetInfo[sizeof(lastNetInfo) - 1] = '\0';
}

bool tryHttpInternetCheck() {
  WiFiClient probe;
  probe.setTimeout(2500);

  if (!probe.connect(HTTP_TEST_HOST, HTTP_TEST_PORT)) {
    return false;
  }

  probe.print(F("HEAD / HTTP/1.1\r\nHost: "));
  probe.print(HTTP_TEST_HOST);
  probe.print(F("\r\nConnection: close\r\n\r\n"));

  unsigned long t0 = millis();
  while (probe.connected() && !probe.available() && millis() - t0 < 3000UL) {
    serviceUiDuringWait();
    delay(5);
  }

  bool ok = probe.available() > 0;
  probe.stop();
  return ok;
}

void checkInternetReachability() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnesso = false;
    resetInternetStatus();
    strncpy(lastNetInfo, "WiFi disconnesso", sizeof(lastNetInfo));
    lastNetInfo[sizeof(lastNetInfo) - 1] = '\0';
    return;
  }

  int pingResult = WiFi.ping(PING_TARGET);
  lastPingMs = pingResult;
  lastInternetCheck = millis();

  Serial.print(F("[NET] Ping "));
  Serial.print(PING_TARGET);
  Serial.print(F(": "));

  if (pingResult >= 0) {
    internetOk = true;
    strncpy(lastNetInfo, "Ping OK", sizeof(lastNetInfo));
    lastNetInfo[sizeof(lastNetInfo) - 1] = '\0';
    Serial.print(lastPingMs);
    Serial.println(F(" ms -> INTERNET OK"));
    return;
  }

  Serial.print(F("errore "));
  Serial.print(lastPingMs);

  bool httpOk = tryHttpInternetCheck();
  if (httpOk) {
    internetOk = true;
    strncpy(lastNetInfo, "HTTP fallback OK", sizeof(lastNetInfo));
    lastNetInfo[sizeof(lastNetInfo) - 1] = '\0';
    Serial.print(F(" -> ping KO, ma HTTP verso "));
    Serial.print(HTTP_TEST_HOST);
    Serial.println(F(" OK -> INTERNET OK"));
  } else {
    internetOk = false;
    strncpy(lastNetInfo, "Ping KO / HTTP KO", sizeof(lastNetInfo));
    lastNetInfo[sizeof(lastNetInfo) - 1] = '\0';
    Serial.print(F(" -> HTTP verso "));
    Serial.print(HTTP_TEST_HOST);
    Serial.println(F(" KO -> INTERNET KO"));
  }
}

void startSetupAP() {
  WiFi.disconnect();
  cooperativeDelay(50);
  WiFi.beginAP(SETUP_AP_SSID, SETUP_AP_PASS);
  cooperativeDelay(200);
  setupModeWiFi = true;
  wifiConnesso  = false;
  resetInternetStatus();
  strncpy(ipStr, "192.168.4.1", sizeof(ipStr));
  ipStr[sizeof(ipStr) - 1] = '\0';
  startWebServers();
  showOledPage(OLED_PAGE_CHROME);
  Serial.println(F("[WiFi] Modalita setup AP attiva: PiantaSetup / pianta123"));
  Serial.println(F("[WiFi] Apri http://192.168.4.1"));
  Serial.print(F("[WiFi] Porta esterna inoltrata: "));
  Serial.println(WEB_SERVER_PORT_WAN);
}

// Avvia la connessione WiFi in modo non-bloccante; loop() completa la gestione.
void beginWifiConnect() {
  if (!cfg.wifiConfigured || cfg.ssid[0] == '\0') { startSetupAP(); return; }
  wifiAttempts++;
  setupModeWiFi    = false;
  wifiConnesso     = false;
  WiFi.disconnect();
  delay(50);
  WiFi.begin(cfg.ssid, cfg.pass);
  wifiConnectStart = millis();
  wifiConnecting   = true;
  Serial.print(F("[WiFi] Tentativo ")); Serial.print(wifiAttempts);
  Serial.print(F(" SSID: ")); Serial.println(cfg.ssid);
}

// ========================= MQTT =========================
bool mqttConnect() {
  if (!wifiConnesso) return false;
  mqttWifiClient.setConnectionTimeout(1500); // max 1.5s: evita freeze di 5s
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setKeepAlive(60);
  char clientId[20];
  snprintf(clientId, sizeof(clientId), "pianta-nodo%d", MQTT_NODE_ID);
  if (mqttClient.connect(clientId)) {
    Serial.print(F("[MQTT] Connesso al broker "));
    Serial.println(MQTT_BROKER);
    return true;
  }
  Serial.print(F("[MQTT] Connessione fallita, rc="));
  Serial.println(mqttClient.state());
  return false;
}

void mqttPublish() {
  if (!mqttClient.connected()) return; // Se non è connesso, si ferma qui
  
  char payload[80];
  snprintf(payload, sizeof(payload),
    "{\"id\":%d,\"temp\":%.1f,\"hum\":%.1f,\"soil\":%d}",
    (int)MQTT_NODE_ID,
    lastNaN ? 0.0f : lastTemp,
    lastNaN ? 0.0f : lastHum,
    lastPct
  );
  
  // --- RIGHE AGGIUNTE PER IL DEBUG ---
  Serial.print("[MQTT] Pubblico dati: ");
  Serial.println(payload);
  // -----------------------------------
  
  mqttClient.publish(MQTT_TOPIC_DATA, payload);
}

// ========================= Sensori =========================
void aggiornaSensori() {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  int samples[SOIL_SAMPLES];
  for (int i = 0; i < SOIL_SAMPLES; i++) {
    samples[i] = analogRead(PIN_SOIL);
    cooperativeDelay(2);
  }

  for (int i = 1; i < SOIL_SAMPLES; i++) {
    int key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }

  int soilRaw = samples[SOIL_MEDIAN];
  lastSoilRaw = soilRaw;
  if (soilRaw > soilAutoMax) soilAutoMax = soilRaw;
  if (soilRaw < soilAutoMin) soilAutoMin = soilRaw;

  lastPct = soilPercent(soilRaw);
  aggiornaLED(lastPct);

  if (isnan(temp) || isnan(hum)) {
    lastNaN = true;
  } else {
    lastNaN = false;
    lastTemp = temp;
    lastHum  = hum;
  }
}

// ========================= HTML =========================
void sendHeader(WiFiClient& c, const __FlashStringHelper* type) {
  c.println(F("HTTP/1.1 200 OK"));
  c.print(F("Content-Type: "));
  c.println(type);
  c.println(F("Connection: close"));
  c.println();
}

void sendBaseCss(WiFiClient& c) {
  c.println(F("*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#0d1117;color:#e6edf3;padding:18px}"
              ".wrap{max-width:760px;margin:0 auto}.card{background:#161b22;border:1px solid #30363d;border-radius:16px;padding:16px;margin-bottom:14px}"
              "h1{margin:0 0 8px 0;font-size:1.6rem}p{opacity:.9;line-height:1.45}"
              ".muted{color:#9da7b3}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.full{grid-column:1/-1}"
              ".val{font-size:2rem;font-weight:800}.big{font-size:1.2rem;font-weight:700}.ok{color:#3fb950}.warn{color:#f1e05a}.bad{color:#ff6b6b}"
              "input,select,button{width:100%;padding:13px;border-radius:12px;border:1px solid #30363d;background:#0d1117;color:#e6edf3;font-size:1rem}"
              "button{background:#238636;border-color:#238636;font-weight:700;cursor:pointer}.btn2{background:#1f6feb;border-color:#1f6feb}.btn3{background:#21262d;border-color:#30363d}"
              "label{display:block;font-size:.9rem;margin:0 0 8px 2px;color:#c9d1d9}.row{display:flex;gap:10px;flex-wrap:wrap}.row a,.row button{flex:1;text-decoration:none;text-align:center}"
              ".bar{height:10px;background:#0d1117;border-radius:999px;border:1px solid #30363d;overflow:hidden}.fill{height:100%;border-radius:999px;background:#3fb950}"
              "@media (max-width:700px){.grid{grid-template-columns:1fr}}"));
}

void sendSetupPortal(WiFiClient& c) {
  sendHeader(c, F("text/html; charset=utf-8"));
  c.println(F("<!DOCTYPE html><html lang='it'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Setup WiFi</title><style>"));
  sendBaseCss(c);
  c.println(F("</style></head><body><div class='wrap'>"));
  c.println(F("<div class='card'><h1>Configura il Wi‑Fi</h1><p>Inserisci il Wi‑Fi di casa. In questa versione l'Arduino prova a ricevere l'IP dal router via DHCP, lo mostra sull'OLED e rende la dashboard raggiungibile dagli altri dispositivi sulla stessa rete. In piu ascolta anche sulla porta WAN inoltrata 8080.</p></div>"));
  c.println(F("<div class='card'><form action='/savewifi' method='get'>"));
  c.println(F("<label for='ssid'>Nome rete Wi‑Fi (SSID)</label><input id='ssid' name='ssid' maxlength='32' placeholder='Es. Casa_2.4G' required>"));
  c.println(F("<div style='height:12px'></div><label for='pass'>Password Wi‑Fi</label><input id='pass' name='pass' type='password' maxlength='64' placeholder='Password del router'>"));
  c.println(F("<div style='height:12px'></div><label for='cloud'>Arduino Cloud API key (opzionale, per uso futuro)</label><input id='cloud' name='cloud' maxlength='128' placeholder='Lascia vuoto per il test LAN di oggi'>"));
  c.println(F("<div style='height:16px'></div><button type='submit'>Salva e riavvia</button></form></div>"));
  c.println(F("<div class='card'><div class='big'>Rete di configurazione attuale</div><p class='muted'>SSID: <b>PiantaSetup</b><br>Password: <b>pianta123</b><br>URL: <b>http://192.168.4.1</b><br>Usa un Wi‑Fi 2.4 GHz.</p></div>"));
  c.println(F("</div></body></html>"));
}

void sendWifiSaved(WiFiClient& c, const String& ssid) {
  sendHeader(c, F("text/html; charset=utf-8"));
  c.println(F("<!DOCTYPE html><html lang='it'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>WiFi salvato</title><style>"));
  sendBaseCss(c);
  c.println(F("</style></head><body><div class='wrap'>"));
  c.println(F("<div class='card'><h1>Wi‑Fi salvato</h1><p>L'Arduino ha salvato le credenziali e si sta riavviando.</p>"));
  c.print(F("<p class='muted'>Rete: <b>"));
  c.print(ssid);
  c.println(F("</b></p><p class='muted'>Tra pochi secondi ricollegati allo stesso Wi‑Fi di casa. Poi apri in Chrome l'IP che l'Arduino mostrerà sull'OLED. In LAN usa http://IP_LOCALE/ oppure http://IP_LOCALE:8080/ se vuoi testare la porta inoltrata.</p>"));
  c.print(F("<p class='muted'>Cloud key salvata: <b>"));
  c.print(hasCloudApiSaved() ? F("SI") : F("NO"));
  c.println(F("</b></p></div>"));
  c.println(F("</div></body></html>"));
}

void sendResetPage(WiFiClient& c) {
  sendHeader(c, F("text/html; charset=utf-8"));
  c.println(F("<!DOCTYPE html><html lang='it'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Reset WiFi</title><style>"));
  sendBaseCss(c);
  c.println(F("</style></head><body><div class='wrap'><div class='card'><h1>Wi‑Fi azzerato</h1><p>L'Arduino si riavvia in modalita setup.</p></div></div></body></html>"));
}

void sendWizard(WiFiClient& c) {
  sendHeader(c, F("text/html; charset=utf-8"));
  c.println(F("<!DOCTYPE html><html lang='it'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Tipo pianta</title><style>"));
  sendBaseCss(c);
  c.println(F(".opt{cursor:pointer;transition:.15s;border:1px solid #30363d}.opt:hover,.opt.sel{border-color:#1f6feb;box-shadow:0 0 0 1px #1f6feb inset}.tag{display:inline-block;padding:4px 9px;border-radius:999px;background:#0d1117;border:1px solid #30363d;font-size:.8rem;margin-top:7px}"
              "</style></head><body><div class='wrap'>"));
  c.println(F("<div class='card'><h1>Seleziona la pianta</h1><p>Scegli il profilo piu adatto. Questo influenza la soglia minima e la stima della prossima annaffiatura.</p></div>"));
  c.println(F("<div class='grid' style='grid-template-columns:1fr'>"));
  for (int i = 0; i < NUM_TIPI; i++) {
    c.print(F("<div class='card opt' id='o")); c.print(i); c.print(F("' onclick='sel(")); c.print(i); c.println(F(")'>"));
    c.print(F("<div class='big'>")); c.print(TIPI[i].emoji); c.print(F(" ")); c.print(TIPI[i].nome); c.println(F("</div>"));
    c.print(F("<p>")); c.print(TIPI[i].descrizione); c.println(F("</p>"));
    c.print(F("<p class='muted'>")); c.print(TIPI[i].esempi); c.println(F("</p>"));
    c.print(F("<div class='tag'>")); c.print(TIPI[i].frequenza); c.println(F("</div></div>"));
  }
  c.println(F("</div><div class='card'><button id='go' class='btn2' disabled onclick='saveTipo()'>Conferma</button></div>"));
  c.println(F("<script>let s=-1;function sel(i){if(s>=0)document.getElementById('o'+s).classList.remove('sel');s=i;document.getElementById('o'+i).classList.add('sel');document.getElementById('go').disabled=false;}function saveTipo(){location='/setup?tipo='+s;}</script>"));
  c.println(F("</div></body></html>"));
}

void sendDashboard(WiFiClient& c) {
  sendHeader(c, F("text/html; charset=utf-8"));
  c.println(F("<!DOCTYPE html><html lang='it'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>IbleoCare_JA</title><style>"));
  sendBaseCss(c);
  c.println(F("</style></head><body><div class='wrap'>"));
  c.println(F("<div class='card'><h1>IbleoCare_JA</h1><p class='muted'>Dashboard live. Il dispositivo verifica anche l'uscita Internet reale con un ping a 8.8.8.8. Il router puo inoltrare la porta WAN 8080 verso questa board.</p></div>"));
  c.println(F("<div class='grid'>"
              "<div class='card'><div class='muted'>Umidita terreno</div><div class='val' id='soil'>--</div><div id='soilLabel' class='muted'>--</div><div class='bar'><div class='fill' id='soilFill' style='width:0%'></div></div></div>"
              "<div class='card'><div class='muted'>Prossima annaffiatura</div><div class='big' id='ann'>--</div><div class='muted' id='perdita'></div><div class='muted' id='cal'></div></div>"
              "<div class='card'><div class='muted'>Temperatura aria</div><div class='val' id='temp'>--</div></div>"
              "<div class='card'><div class='muted'>Umidita aria</div><div class='val' id='hum'>--</div></div>"
              "<div class='card full'><div class='muted'>Pianta configurata</div><div class='big' id='tipoNome'>--</div><div class='muted' id='tipoEmoji'></div></div>"
              "<div class='card full'><div class='muted'>Rete</div><div class='big'>"));
  c.print(ipStr);
  c.println(F("</div>"));
  c.print(F("<div class='muted'>SSID: "));
  c.print(cfg.ssid);
  c.println(F("</div>"));
  c.println(F("<div class='muted'>Internet: <span id='netState'>--</span></div>\n<div class='muted'>Diagnosi rete: <span id='netInfo'>--</span></div>"));
  c.println(F("<div class='muted'>Ping 8.8.8.8: <span id='pingMs'>--</span></div>"));
  c.print(F("<div class='muted'>Accesso LAN: <b>http://"));
  c.print(ipStr);
  c.println(F("/</b></div>"));
  c.print(F("<div class='muted'>Porta inoltrata da testare in LAN: <b>http://"));
  c.print(ipStr);
  c.print(F(":"));
  c.print(WEB_SERVER_PORT_WAN);
  c.println(F("/</b></div>"));
  c.print(F("<div class='muted'>Regola router: <b>"));
  c.print(ROUTER_FORWARD_HINT);
  c.println(F("</b></div>"));
  c.print(F("<div class='muted'>Accesso esterno previsto: <b>http://TUO_IP_PUBBLICO:"));
  c.print(WEB_SERVER_PORT_WAN);
  c.println(F("/</b> e <b>/data</b> per il JSON</div>"));
  c.print(F("<div class='muted'>Arduino Cloud key salvata: "));
  c.print(hasCloudApiSaved() ? F("SI") : F("NO"));
  c.println(F("</div><div class='row' style='margin-top:14px'><a class='btn3' href='/wizard'>Configura pianta</a><a class='btn3' href='/resetwifi'>Reimposta Wi‑Fi</a></div></div></div>"));
  c.println(F("<script>"));
  c.println(F("const EM=['🌵','🪴','🌳','🌿','🌸'];"));
  c.println(F("const NM=['Piante Grasse','Piante da Interno','Piante da Esterno','Piante Aromatiche','Piante Delicate'];"));
  c.println(F("function upd(d){"));
  c.println(F("  document.getElementById('soil').textContent=d.soil+'%';"));
  c.println(F("  document.getElementById('soilLabel').textContent=d.label;"));
  c.println(F("  document.getElementById('soilFill').style.width=d.soil+'%';"));
  c.println(F("  document.getElementById('soilFill').style.background=(d.soil>=66)?'#3fb950':(d.soil>=33)?'#f1e05a':'#ff6b6b';"));
  c.println(F("  document.getElementById('ann').textContent=d.ann;"));
  c.println(F("  document.getElementById('perdita').textContent=(d.perdita>0)?('Perdita stimata: '+d.perdita.toFixed(3)+'%/h'):'';"));
  c.println(F("  document.getElementById('cal').textContent=d.calibrata?'Stima calibrata':'Stima teorica in calibrazione';"));
  c.println(F("  document.getElementById('temp').textContent=d.nan?'--':(d.temp.toFixed(1)+' C');"));
  c.println(F("  document.getElementById('hum').textContent=d.nan?'--':(d.hum.toFixed(1)+' %');"));
  c.println(F("  document.getElementById('tipoNome').textContent=NM[d.tipo]||'--';"));
  c.println(F("  document.getElementById('tipoEmoji').textContent=EM[d.tipo]||'🪴';"));
  c.println(F("  document.getElementById('netState').textContent=d.wifi?(d.internet?'Internet OK':'WiFi OK, Internet assente'):'WiFi disconnesso';"));
  c.println(F("  document.getElementById('netInfo').textContent=d.netInfo||'--';"));
  c.println(F("  document.getElementById('pingMs').textContent=(d.pingMs>=0)?(d.pingMs+' ms'):'--';"));
  c.println(F("}"));
  c.println(F("function tick(){fetch('/data').then(r=>r.json()).then(upd).catch(()=>{});}"));
  c.println(F("tick();setInterval(tick,1000);"));
  c.println(F("</script>"));
  c.println(F("</div></body></html>"));
}

void sendData(WiFiClient& c) {
  char buf[22];
  formatTempo(oreMancantiStima, buf, sizeof(buf));
  sendHeader(c, F("application/json"));
  c.print(F("{\"soil\":")); c.print(lastPct);
  c.print(F(",\"label\":\"")); c.print(soilLabel(lastPct)); c.print(F("\""));
  c.print(F(",\"temp\":")); c.print(lastNaN ? 0.0f : lastTemp, 1);
  c.print(F(",\"hum\":")); c.print(lastNaN ? 0.0f : lastHum, 1);
  c.print(F(",\"nan\":")); c.print(lastNaN ? F("true") : F("false"));
  c.print(F(",\"ann\":\"")); c.print(buf); c.print(F("\""));
  c.print(F(",\"perdita\":")); c.print(perditaOraEffettiva, 4);
  c.print(F(",\"calibrata\":")); c.print(stimaCalibrata ? F("true") : F("false"));
  c.print(F(",\"led\":")); c.print(ledStato);
  c.print(F(",\"tipo\":")); c.print(tipoPiantaAttuale);
  c.print(F(",\"wifi\":")); c.print(wifiConnesso ? F("true") : F("false"));
  c.print(F(",\"internet\":")); c.print(internetOk ? F("true") : F("false"));
  c.print(F(",\"pingMs\":")); c.print(lastPingMs);
  c.print(F(",\"wanPort\":")); c.print(WEB_SERVER_PORT_WAN);
  c.print(F(",\"netInfo\":\"")); c.print(lastNetInfo); c.print(F("\""));
  c.print(F("}"));
}

void sendRedirect(WiFiClient& c, const char* location) {
  c.println(F("HTTP/1.1 302 Found"));
  c.print(F("Location: "));
  c.println(location);
  c.println(F("Connection: close"));
  c.println();
}

// ========================= Router =========================
void handleClient(WiFiClient& client) {
  String req;
  unsigned long t0 = millis();
  while (client.connected() && millis() - t0 < 2000) {
    serviceUiDuringWait();
    while (client.available()) {
      char ch = client.read();
      req += ch;
      if (req.endsWith("\r\n\r\n")) break;
    }
    if (req.endsWith("\r\n\r\n")) break;
    delay(1);
  }

  String url  = requestPath(req);
  String path = pathOnly(url);

  if (path == "/resetwifi") {
    clearWifiConfigOnly();
    sendResetPage(client);
    pendingRebootAt = millis() + 1500UL;
    client.stop();
    return;
  }

  if (path == "/savewifi") {
    String ssid = queryParam(url, "ssid");
    String pass = queryParam(url, "pass");
    String cloud = queryParam(url, "cloud");
    ssid.trim();
    cloud.trim();
    if (ssid.length() == 0) {
      sendSetupPortal(client);
      client.stop();
      return;
    }
    saveWifiCredentials(ssid, pass, cloud);
    sendWifiSaved(client, ssid);
    pendingRebootAt = millis() + 1800UL;
    client.stop();
    return;
  }

  if (setupModeWiFi) {
    sendSetupPortal(client);
    client.stop();
    return;
  }

  if (path == "/setup") {
    String tipo = queryParam(url, "tipo");
    if (tipo.length() > 0) {
      int t = tipo.toInt();
      if (t >= 0 && t < NUM_TIPI) {
        tipoPiantaAttuale = t;
        configurato = true;
        savePlantConfig();
        resetStima();
        aggiornaStimaAnnaffiatura(lastPct);
      }
    }
    sendRedirect(client, "/");
    client.stop();
    return;
  }

  if (path == "/wizard") {
    sendWizard(client);
    client.stop();
    return;
  }

  if (path == "/data") {
    sendData(client);
    client.stop();
    return;
  }

  sendDashboard(client);
  client.stop();
}

// ========================= Setup / Loop =========================
void setup() {
  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart < 500UL)) {}
  // Forza ADC a 10-bit per compatibilità con calibrazione SOIL_DRY/SOIL_WET (543/290)
  // misurata su 10-bit. Su UNO R4 il default è 14-bit; senza questo, il sensore
  // leggerebbe sempre valori >> SOIL_DRY e soilPercent() tornerebbe sempre 0%.
  analogReadResolution(10);
  snprintf(MQTT_TOPIC_DATA, sizeof(MQTT_TOPIC_DATA), "pianta/nodo%d/data", MQTT_NODE_ID);
  delay(50);

  pinMode(PIN_BTN_1, INPUT_PULLUP);
  pinMode(PIN_BTN_2, INPUT_PULLUP);
  pinMode(PIN_BTN_3, INPUT_PULLUP);
  pinMode(PIN_BTN_4, INPUT_PULLUP);
  delay(20);
  for (uint8_t i = 0; i < 4; i++) {
    bool initial = isButtonPressedNow(i);
    buttonRawState[i] = initial;
    buttonStableState[i] = initial;
    buttonDebounceAt[i] = millis();
  }

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_GIALLO, OUTPUT);
  pinMode(PIN_LED_ROSSO, OUTPUT);
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_GIALLO, LOW);
  digitalWrite(PIN_LED_ROSSO, LOW);

  loadConfig();

  Wire.begin();
  if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(8, 10); oled.print(F("IbleoCare_JA"));
    oled.setCursor(0, 28); oled.print(F("Avvio in corso..."));
    oledDisplay();
    oledActive = true;
    oledOnTime = millis(); // avvia il timer da adesso (non da boot)
  }

  dht.begin();
  cooperativeDelay(400);
  for (int i = 0; i < 6; i++) {
    analogRead(PIN_SOIL);
    cooperativeDelay(40);
  }
  aggiornaSensori();

  if (cfg.wifiConfigured) {
    beginWifiConnect();   // non-bloccante: loop() gestisce la connessione
    // display rimane acceso: loop() aggiornera' subito con i dati sensori
  } else {
    startSetupAP();
  }

  Serial.println(F("=== Pianta Monitor v3.0 pronto ==="));
}

void loop() {
  unsigned long now = millis();

  if (pendingRebootAt && now >= pendingRebootAt) {
    NVIC_SystemReset();
  }

  handleButtons(now);
  now = millis(); // refresh: oledWake() in handleButtons puo avanzare millis
  if (oledActive && anyButtonHeld()) oledOnTime = now; // tasto tenuto: resetta timer
  if (oledActive && (now - oledOnTime >= OLED_TIMEOUT_MS) && !anyButtonHeld()) oledSleep();

  WiFiClient clientLan = serverLan.available();
  if (clientLan) handleClient(clientLan);

  WiFiClient clientWan = serverWan.available();
  if (clientWan) handleClient(clientWan);

  if (!setupModeWiFi) {
    if (wifiConnecting) {
      if (WiFi.status() == WL_CONNECTED && ipIsValid(WiFi.localIP())) {
        // Connessione riuscita
        wifiConnecting = false;
        wifiAttempts   = 0;
        wifiConnesso   = true;
        updateIpString(WiFi.localIP());
        startWebServers();
        resetStima();
        aggiornaStimaAnnaffiatura(lastPct);
        resetInternetStatus();
        strncpy(lastNetInfo, "WiFi connesso", sizeof(lastNetInfo));
        lastNetInfo[sizeof(lastNetInfo) - 1] = '\0';
        lastInternetCheck = millis();
        oledPage = OLED_PAGE_SOIL_STATUS;
        lastMqttReconnect = millis() - 50000UL; // primo tentativo MQTT fra ~10s
        Serial.print(F("[WiFi] Connesso. IP: ")); Serial.println(ipStr);
        Serial.print(F("[WiFi] Dashboard: http://")); Serial.println(ipStr);
      } else if (now - wifiConnectStart >= WIFI_CONNECT_TIMEOUT_MS) {
        // Timeout: riprova o vai in AP
        wifiConnecting = false;
        Serial.println(F("[WiFi] Nessun IP ricevuto."));
        if (wifiAttempts < 2) {
          beginWifiConnect();
        } else {
          wifiAttempts = 0;
          Serial.println(F("[WiFi] Connessione fallita, modo setup."));
          startSetupAP();
        }
      }
    } else if (wifiConnesso && WiFi.status() != WL_CONNECTED) {
      // Connessione caduta
      Serial.println(F("[WiFi] Connessione persa, riconnessione..."));
      wifiConnesso = false;
      resetInternetStatus();
      beginWifiConnect();
    } else if (wifiConnesso && (now - lastInternetCheck >= INTERNET_CHECK_MS)) {
      checkInternetReachability();
    }
  }

  // ---- MQTT: loop sempre, riconnetti se perso, pubblica ogni 5s ----
  if (wifiConnesso && !setupModeWiFi) {
    mqttClient.loop();
    if (!mqttClient.connected() && (now - lastMqttReconnect >= 60000UL)) {
      lastMqttReconnect = now;
      mqttConnect();
    }
    if (mqttClient.connected() && (now - lastMqttPublish >= MQTT_PUBLISH_INTERVAL)) {
      lastMqttPublish = now;
      mqttPublish();
    }
  }

  if (now - lastRead < READ_INTERVAL_MS) return;
  lastRead = now;

  aggiornaSensori();

  if (now - lastStimaTime >= STIMA_INTERVALLO_MS) {
    lastStimaTime = now;
    aggiornaStimaAnnaffiatura(lastPct);
  }

  if (!lastNaN) {
    Serial.print(F("[DHT22] "));
    Serial.print(lastTemp, 1);
    Serial.print(F("C  "));
    Serial.print(lastHum, 1);
    Serial.println(F("%"));
  }
  Serial.print(F("[SOIL] raw="));
  Serial.print(lastSoilRaw);
  Serial.print(F(" pct="));
  Serial.print(lastPct);
  Serial.print(F("%  "));
  Serial.println(soilLabel(lastPct));

  if (oledActive) drawOLED();
}
