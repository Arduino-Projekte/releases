#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiManager.h>

#if !defined(ESP32)
#error "Dieses Sketch ist nur fuer ESP32 gedacht."
#endif

// ============================================================
// ClockClock24 - Minimaler WiFi-/Online-Updater (eine Datei)
// ============================================================
// Aufgabe:
// 1) Per WiFiManager mit WLAN verbinden (oder AP + Captive Portal anbieten)
// 2) update.json vom Server holen
// 3) neuere BIN herunterladen und installieren
//
// Wichtige Voraussetzung:
// - ESP32 Partition Scheme mit OTA-Slots verwenden
//   (z.B. otadata + app0/ota_0 + app1/ota_1)
// - Dieses Sketch ist absichtlich schlank gehalten:
//   keine WebUI, keine Timer, keine Master/Slave-Logik
//
// Hinweise:
// - HTTPS wird hier bewusst mit setInsecure() verwendet, damit keine
//   Zertifikatsdatei im Sketch gepflegt werden muss.
// - Die Manifest-/BIN-Pfade sind aus deinem aktuellen Projekt uebernommen.
// ============================================================

// ---------- Projekt / Firmware ----------
static const char* APP_NAME            = "ClockClock24";
static const char* CURRENT_FW_VERSION  = "V2.26.3.09.0";   // anpassen, wenn du den Updater neu baust

// ---------- WiFiManager / AP ----------
static const char* AP_PREFIX           = "ClockClock24_OTA_";
static const char* AP_PASSWORD         = nullptr;           // optional: "12345678" oder nullptr fuer offen
static const uint32_t WIFI_CONNECT_TIMEOUT_S = 20;
static const uint32_t WIFI_PORTAL_TIMEOUT_S  = 0;           // 0 = kein Timeout

// ---------- Update-Quelle ----------
static const char* MANIFEST_URLS[] = {
  "https://raw.githubusercontent.com/Arduino-Projekte/releases/main/firmware/cc/update.json",
  "https://raw.githubusercontent.com/Arduino-Projekte/releases/main/releases/firmware/cc/update.json"
};

// ---------- Laufzeit ----------
static const uint32_t HTTP_TIMEOUT_MS     = 15000;
static const uint32_t NO_DATA_TIMEOUT_MS  = 15000;
static const size_t   DOWNLOAD_CHUNK_SIZE = 2048;
static const uint32_t REBOOT_DELAY_MS     = 1500;
static const bool     FORCE_INSTALL_SAME_VERSION = false;   // true = auch gleiche Version neu flashen

struct ManifestInfo {
  String manifestUrl;
  String version;
  String bin;
  String url;
  uint32_t size = 0;
};

static String makePortalName() {
  uint64_t mac = ESP.getEfuseMac();
  uint32_t last24 = (uint32_t)(mac & 0xFFFFFFULL);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s%06X", AP_PREFIX, (unsigned int)last24);
  return String(buf);
}

static String makeHostname() {
  uint64_t mac = ESP.getEfuseMac();
  uint32_t last24 = (uint32_t)(mac & 0xFFFFFFULL);
  char buf[40];
  snprintf(buf, sizeof(buf), "clockclock24-ota-%06x", (unsigned int)last24);
  return String(buf);
}

static int versionCompare(const String& aIn, const String& bIn) {
  auto toNums = [](const String& in, int out[8], int& n) {
    n = 0;
    int cur = -1;
    for (size_t i = 0; i < in.length(); ++i) {
      const char c = in[i];
      if (c >= '0' && c <= '9') {
        if (cur < 0) cur = 0;
        cur = cur * 10 + (c - '0');
      } else {
        if (cur >= 0) {
          if (n < 8) out[n++] = cur;
          cur = -1;
        }
      }
    }
    if (cur >= 0 && n < 8) out[n++] = cur;
  };

  int a[8] = {0};
  int b[8] = {0};
  int na = 0, nb = 0;
  toNums(aIn, a, na);
  toNums(bIn, b, nb);

  const int n = (na > nb) ? na : nb;
  for (int i = 0; i < n; ++i) {
    const int av = (i < na) ? a[i] : 0;
    const int bv = (i < nb) ? b[i] : 0;
    if (av < bv) return -1;
    if (av > bv) return 1;
  }
  return 0;
}

static String jsonGetStr(const String& json, const char* key) {
  const String k = String('"') + key + '"';
  int p = json.indexOf(k);
  if (p < 0) return String();

  p = json.indexOf(':', p);
  if (p < 0) return String();
  ++p;

  while (p < (int)json.length() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) {
    ++p;
  }
  if (p >= (int)json.length() || json[p] != '"') return String();
  ++p;

  int e = p;
  while (e < (int)json.length()) {
    if (json[e] == '"' && json[e - 1] != '\\') break;
    ++e;
  }
  if (e >= (int)json.length()) return String();

  return json.substring(p, e);
}

static uint32_t jsonGetU32(const String& json, const char* key) {
  const String k = String('"') + key + '"';
  int p = json.indexOf(k);
  if (p < 0) return 0;

  p = json.indexOf(':', p);
  if (p < 0) return 0;
  ++p;

  while (p < (int)json.length() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) {
    ++p;
  }

  uint32_t v = 0;
  while (p < (int)json.length()) {
    const char c = json[p++];
    if (c < '0' || c > '9') break;
    v = v * 10UL + (uint32_t)(c - '0');
  }
  return v;
}

static bool httpGetText(const String& url, String& out, int& httpCode) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout((uint16_t)HTTP_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    httpCode = -1;
    return false;
  }

  httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  out = http.getString();
  http.end();
  return true;
}

static bool fetchManifest(ManifestInfo& outInfo, String& errMsg) {
  outInfo = ManifestInfo();

  for (size_t i = 0; i < (sizeof(MANIFEST_URLS) / sizeof(MANIFEST_URLS[0])); ++i) {
    String json;
    int code = 0;
    const String url = MANIFEST_URLS[i];

    Serial.printf("[FWU] Manifest pruefen: %s\n", url.c_str());
    if (!httpGetText(url, json, code)) {
      Serial.printf("[FWU] Manifest fehlgeschlagen, HTTP=%d\n", code);
      continue;
    }

    outInfo.manifestUrl = url;
    outInfo.version = jsonGetStr(json, "version");
    outInfo.bin     = jsonGetStr(json, "bin");
    outInfo.url     = jsonGetStr(json, "url");
    outInfo.size    = jsonGetU32(json, "size");

    if (!outInfo.version.length() || (!outInfo.bin.length() && !outInfo.url.length())) {
      errMsg = "Manifest ungueltig (version + bin/url fehlen)";
      return false;
    }

    return true;
  }

  errMsg = "Kein gueltiges Manifest gefunden";
  return false;
}

static String resolveBinUrl(const ManifestInfo& m) {
  if (m.url.length()) return m.url;
  if (!m.manifestUrl.length() || !m.bin.length()) return String();

  const int slash = m.manifestUrl.lastIndexOf('/');
  if (slash < 0) return String();
  return m.manifestUrl.substring(0, slash + 1) + m.bin;
}

static bool installFromUrl(const String& url, String& errMsg) {
  if (!url.length()) {
    errMsg = "BIN-URL ist leer";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout((uint16_t)HTTP_TIMEOUT_MS);

  Serial.printf("[FWU] Download: %s\n", url.c_str());
  if (!http.begin(client, url)) {
    errMsg = "HTTP begin failed";
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    errMsg = String("HTTP ") + String(code);
    http.end();
    return false;
  }

  const int len = http.getSize();
  WiFiClient* stream = http.getStreamPtr();

  Serial.printf("[FWU] Groesse: %d Bytes\n", len);

  if (!Update.begin((len > 0) ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
    errMsg = String("Update.begin failed: ") + Update.errorString();
    http.end();
    return false;
  }

  uint8_t buf[DOWNLOAD_CHUNK_SIZE];
  size_t written = 0;
  int remaining = len;
  uint32_t lastDataMs = millis();
  int lastPct = -1;

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    delay(1);

    const size_t avail = stream->available();
    if (!avail) {
      if ((millis() - lastDataMs) > NO_DATA_TIMEOUT_MS) {
        errMsg = "Timeout: no data";
        Update.abort();
        http.end();
        return false;
      }
      continue;
    }

    const size_t toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
    const int r = stream->readBytes(buf, toRead);
    if (r <= 0) continue;

    lastDataMs = millis();

    const size_t w = Update.write(buf, (size_t)r);
    written += w;
    if (w != (size_t)r) {
      errMsg = String("Write mismatch ") + String((unsigned)w) + "/" + String((unsigned)r);
      Update.abort();
      http.end();
      return false;
    }

    if (remaining > 0) remaining -= r;

    if (len > 0) {
      const int pct = (int)((written * 100ULL) / (unsigned)len);
      if (pct != lastPct) {
        lastPct = pct;
        Serial.printf("[FWU] Fortschritt: %d%%\n", pct);
      }
    }
  }

  if (len > 0 && (int)written != len) {
    errMsg = String("Short write ") + String((unsigned)written) + "/" + String(len);
    Update.abort();
    http.end();
    return false;
  }

  if (!Update.end()) {
    errMsg = String("Update.end failed: ") + Update.errorString();
    http.end();
    return false;
  }

  if (!Update.isFinished()) {
    errMsg = "Update nicht abgeschlossen";
    http.end();
    return false;
  }

  http.end();
  return true;
}

static bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.setHostname(makeHostname().c_str());

  WiFiManager wm;
  wm.setTitle(APP_NAME);
  wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_S);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);

  const String apName = makePortalName();
  Serial.printf("[WIFI] Hostname: %s\n", makeHostname().c_str());
  Serial.printf("[WIFI] Portal-SSID: %s\n", apName.c_str());

  bool ok = false;
  if (AP_PASSWORD && AP_PASSWORD[0]) ok = wm.autoConnect(apName.c_str(), AP_PASSWORD);
  else                               ok = wm.autoConnect(apName.c_str());

  if (!ok) {
    Serial.println("[WIFI] Verbindung fehlgeschlagen oder Portal beendet");
    return false;
  }

  Serial.println("[WIFI] Verbunden");
  Serial.printf("[WIFI] SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

static void checkAndInstallUpdate() {
  ManifestInfo mf;
  String msg;

  if (!fetchManifest(mf, msg)) {
    Serial.printf("[FWU] %s\n", msg.c_str());
    return;
  }

  Serial.printf("[FWU] Aktuell : %s\n", CURRENT_FW_VERSION);
  Serial.printf("[FWU] Remote  : %s\n", mf.version.c_str());
  Serial.printf("[FWU] Manifest: %s\n", mf.manifestUrl.c_str());

  const int cmp = versionCompare(CURRENT_FW_VERSION, mf.version);
  if (!FORCE_INSTALL_SAME_VERSION && cmp >= 0) {
    Serial.println("[FWU] Kein neueres Update verfuegbar");
    return;
  }

  const String binUrl = resolveBinUrl(mf);
  if (!binUrl.length()) {
    Serial.println("[FWU] BIN-URL konnte nicht aufgeloest werden");
    return;
  }

  Serial.printf("[FWU] Installiere: %s\n", binUrl.c_str());
  if (!installFromUrl(binUrl, msg)) {
    Serial.printf("[FWU] FEHLER: %s\n", msg.c_str());
    return;
  }

  Serial.println("[FWU] Update erfolgreich, Neustart...");
  delay(REBOOT_DELAY_MS);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("============================================================");
  Serial.println("ClockClock24 Minimal WiFi OTA Updater");
  Serial.printf("Aktuelle FW-Version: %s\n", CURRENT_FW_VERSION);
  Serial.println("============================================================");

  if (!connectWifi()) {
    Serial.println("[SYS] Kein WLAN, Neustart in 5s...");
    delay(5000);
    ESP.restart();
  }

  checkAndInstallUpdate();
  Serial.println("[SYS] Fertig. Keine weitere Aufgabe.");
}

void loop() {
  delay(1000);
}
