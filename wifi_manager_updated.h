#pragma once

// ═══════════════════════════════════════════════════════════════
//  wifi_manager.h  —  WiFi Manager + Captive Portal + Serial CMD
//
//  Endpoint yang tersedia saat AP mode:
//  GET  /              → halaman HTML captive portal (browser)
//  POST /save          → form-urlencoded dari browser / Flutter
//  POST /api/setwifi   → JSON dari Flutter (alternatif)
//  GET  /api/status    → status ESP dalam JSON
//
//  Serial Commands (baud 115200):
//  ┌─────────────────────────────────────────────────────────┐
//  │ SETWIFI:<SSID>,<PASSWORD>  → ganti WiFi, restart auto   │
//  │ GETWIFI                    → lihat SSID tersimpan        │
//  │ CLEARWIFI                  → hapus kredensial            │
//  │ RESTART                    → restart ESP                 │
//  └─────────────────────────────────────────────────────────┘
// ═══════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>   // ← tambahan untuk endpoint JSON Flutter
#include "cfg_config.h"

// ── Objek internal ────────────────────────────────────────────────
static Preferences  _prefs;
static WebServer    _server(80);
static DNSServer    _dns;
static bool         _apModeActive = false;
static String       _serialBuffer = "";

// ── Buzzer helpers ────────────────────────────────────────────────
static void _beepConnecting() {
  tone(PIN_BUZZER, 1000); delay(300);
  noTone(PIN_BUZZER);     delay(700);
}
static void _beepSuccess() {
  tone(PIN_BUZZER, 1500); delay(500);
  noTone(PIN_BUZZER);
}
static void _beepError() {
  for (int i = 0; i < 6; i++) {
    tone(PIN_BUZZER, 2000); delay(100);
    noTone(PIN_BUZZER);     delay(100);
  }
}

// ════════════════════════════════════════════════════════════════
//  NVS  —  Simpan & Baca SSID/Password ke flash ESP32
// ════════════════════════════════════════════════════════════════
static void _saveCredentials(const String& ssid, const String& pass) {
  _prefs.begin("wifi", false);
  _prefs.putString("ssid", ssid);
  _prefs.putString("pass", pass);
  _prefs.end();
  Serial.println("[WiFiMgr] Kredensial disimpan ke NVS.");
}

static bool _loadCredentials(String& ssid, String& pass) {
  _prefs.begin("wifi", true);
  ssid = _prefs.getString("ssid", "");
  pass = _prefs.getString("pass", "");
  _prefs.end();
  return ssid.length() > 0;
}

// ════════════════════════════════════════════════════════════════
//  HTML Captive Portal
// ════════════════════════════════════════════════════════════════
static const char PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Anemometer — WiFi Setup</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Segoe UI', sans-serif;
      background: #0f172a;
      color: #e2e8f0;
      display: flex;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
      padding: 16px;
    }
    .card {
      background: #1e293b;
      border: 1px solid #334155;
      border-radius: 16px;
      padding: 32px 28px;
      width: 100%;
      max-width: 400px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.5);
    }
    .icon { font-size: 40px; text-align: center; margin-bottom: 8px; }
    h1 { text-align: center; font-size: 20px; font-weight: 700; color: #38bdf8; margin-bottom: 4px; }
    p.sub { text-align: center; font-size: 13px; color: #94a3b8; margin-bottom: 24px; }
    label { display: block; font-size: 12px; font-weight: 600; color: #94a3b8; text-transform: uppercase; letter-spacing: .05em; margin-bottom: 6px; }
    input[type=text], input[type=password] {
      width: 100%; padding: 12px 14px; background: #0f172a;
      border: 1px solid #334155; border-radius: 8px; color: #e2e8f0;
      font-size: 15px; margin-bottom: 16px; outline: none; transition: border-color .2s;
    }
    input:focus { border-color: #38bdf8; }
    .row { display: flex; align-items: center; gap: 8px; margin-bottom: 24px; }
    .row input[type=checkbox] { width: 16px; height: 16px; accent-color: #38bdf8; }
    .row label { margin: 0; font-size: 13px; text-transform: none; letter-spacing: 0; }
    button { width: 100%; padding: 13px; background: #38bdf8; color: #0f172a; font-size: 15px; font-weight: 700; border: none; border-radius: 8px; cursor: pointer; transition: background .2s; }
    button:hover { background: #7dd3fc; }
    .msg { margin-top: 16px; padding: 10px 14px; border-radius: 8px; font-size: 13px; text-align: center; }
    .msg.ok  { background: #064e3b; color: #6ee7b7; }
    .msg.err { background: #450a0a; color: #fca5a5; }
  </style>
</head>
<body>
<div class="card">
  <div class="icon">📡</div>
  <h1>Anemometer Setup</h1>
  <p class="sub">Masukkan kredensial WiFi untuk perangkat</p>
  <form id="wifiForm">
    <label for="ssid">Nama WiFi (SSID)</label>
    <input type="text" id="ssid" name="ssid" placeholder="Nama jaringan WiFi" required>
    <label for="pass">Password</label>
    <input type="password" id="pass" name="pass" placeholder="Password WiFi">
    <div class="row">
      <input type="checkbox" id="showPass">
      <label for="showPass">Tampilkan password</label>
    </div>
    <button type="submit" id="btn">Simpan &amp; Sambungkan</button>
  </form>
  <div id="msg"></div>
</div>
<script>
  document.getElementById('showPass').addEventListener('change', function() {
    document.getElementById('pass').type = this.checked ? 'text' : 'password';
  });
  document.getElementById('wifiForm').addEventListener('submit', async function(e) {
    e.preventDefault();
    const btn = document.getElementById('btn');
    const msg = document.getElementById('msg');
    btn.disabled = true; btn.textContent = 'Menyimpan...';
    msg.className = 'msg'; msg.textContent = '';
    const body = new URLSearchParams({
      ssid: document.getElementById('ssid').value,
      pass: document.getElementById('pass').value
    });
    try {
      const r = await fetch('/save', { method: 'POST', body });
      const t = await r.text();
      if (r.ok) { msg.className = 'msg ok'; msg.textContent = t; btn.textContent = 'Tersimpan! ESP akan restart...'; }
      else { throw new Error(t); }
    } catch(err) {
      msg.className = 'msg err'; msg.textContent = 'Gagal: ' + err.message;
      btn.disabled = false; btn.textContent = 'Simpan & Sambungkan';
    }
  });
</script>
</body>
</html>
)rawhtml";

// ════════════════════════════════════════════════════════════════
//  Helper: kirim respons JSON
// ════════════════════════════════════════════════════════════════
static void _sendJson(int code, const String& status, const String& message) {
  StaticJsonDocument<128> doc;
  doc["status"]  = status;
  doc["message"] = message;
  String body;
  serializeJson(doc, body);
  _server.sendHeader("Access-Control-Allow-Origin", "*"); // izinkan Flutter akses
  _server.send(code, "application/json", body);
}

// ════════════════════════════════════════════════════════════════
//  AP Mode + Captive Portal + API Endpoints
// ════════════════════════════════════════════════════════════════
static void _startCaptivePortal() {
  _apModeActive = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, (strlen(AP_PASSWORD) > 0) ? AP_PASSWORD : nullptr);

  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("[WiFiMgr] AP aktif. SSID: %s | IP: %s\n", AP_SSID, apIP.toString().c_str());
  Serial.println("[WiFiMgr] Buka browser → http://" + apIP.toString());

  _dns.start(53, "*", apIP);

  // ── Route: halaman HTML (browser) ──────────────────────────────
  _server.on("/", HTTP_GET, []() {
    _server.send(200, "text/html", PORTAL_HTML);
  });

  // ── Route: captive portal redirect ─────────────────────────────
  _server.on("/generate_204",   HTTP_GET, []() { _server.sendHeader("Location", "/"); _server.send(302); });
  _server.on("/hotspot-detect", HTTP_GET, []() { _server.sendHeader("Location", "/"); _server.send(302); });
  _server.on("/fwlink",         HTTP_GET, []() { _server.sendHeader("Location", "/"); _server.send(302); });

  // ── Route: POST /save — form-urlencoded (browser & Flutter dio) ─
  _server.on("/save", HTTP_POST, []() {
    if (!_server.hasArg("ssid") || _server.arg("ssid").isEmpty()) {
      _sendJson(400, "error", "SSID tidak boleh kosong");
      return;
    }
    String newSSID = _server.arg("ssid");
    String newPass = _server.hasArg("pass") ? _server.arg("pass") : "";
    _saveCredentials(newSSID, newPass);

    // Kirim respons dulu sebelum restart
    _sendJson(200, "ok",
      "WiFi berhasil disimpan! ESP akan restart dan konek ke \"" + newSSID + "\"");
    Serial.printf("[WiFiMgr] Kredensial dari POST /save: SSID=%s\n", newSSID.c_str());
    delay(1500);
    ESP.restart();
  });

  // ── Route: POST /api/setwifi — JSON body (Flutter alternatif) ───
  // Body contoh: {"ssid":"NamaWiFi","password":"secret123"}
  _server.on("/api/setwifi", HTTP_POST, []() {
    if (!_server.hasArg("plain") || _server.arg("plain").isEmpty()) {
      _sendJson(400, "error", "Body JSON kosong");
      return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
      _sendJson(400, "error", String("JSON tidak valid: ") + err.c_str());
      return;
    }

    const char* newSSID = doc["ssid"] | "";
    const char* newPass = doc["password"] | "";

    if (strlen(newSSID) == 0) {
      _sendJson(400, "error", "Field 'ssid' tidak boleh kosong");
      return;
    }

    _saveCredentials(String(newSSID), String(newPass));
    _sendJson(200, "ok",
      String("WiFi berhasil disimpan! ESP akan restart dan konek ke \"") + newSSID + "\"");
    Serial.printf("[WiFiMgr] Kredensial dari POST /api/setwifi: SSID=%s\n", newSSID);
    delay(1500);
    ESP.restart();
  });

  // ── Route: GET /api/status — cek status ESP (untuk Flutter ping) ─
  _server.on("/api/status", HTTP_GET, []() {
    StaticJsonDocument<128> doc;
    doc["device"]  = "anemometer";
    doc["mode"]    = "ap";
    doc["ap_ssid"] = AP_SSID;
    String body;
    serializeJson(doc, body);
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", body);
  });

  // ── CORS preflight (OPTIONS) untuk semua route ──────────────────
  _server.on("/save",         HTTP_OPTIONS, []() { _server.sendHeader("Access-Control-Allow-Origin", "*"); _server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS"); _server.sendHeader("Access-Control-Allow-Headers", "Content-Type"); _server.send(204); });
  _server.on("/api/setwifi",  HTTP_OPTIONS, []() { _server.sendHeader("Access-Control-Allow-Origin", "*"); _server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS"); _server.sendHeader("Access-Control-Allow-Headers", "Content-Type"); _server.send(204); });
  _server.on("/api/status",   HTTP_OPTIONS, []() { _server.sendHeader("Access-Control-Allow-Origin", "*"); _server.send(204); });

  _server.onNotFound([]() { _server.sendHeader("Location", "/"); _server.send(302); });

  _server.begin();
  Serial.println("[WiFiMgr] Web server & API aktif.");
}

// ════════════════════════════════════════════════════════════════
//  Coba konek WiFi — retry N kali, lalu AP mode
// ════════════════════════════════════════════════════════════════
static bool _tryConnect(const String& ssid, const String& pass) {
  Serial.printf("[WiFiMgr] Konek ke \"%s\"...\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  for (int i = 0; i < WIFI_MAX_RETRY; i++) {
    if (WiFi.status() == WL_CONNECTED) return true;
    _beepConnecting();
    Serial.printf("[WiFiMgr] Retry %d/%d\n", i + 1, WIFI_MAX_RETRY);
    delay(WIFI_RETRY_DELAY);
  }
  return false;
}

// ════════════════════════════════════════════════════════════════
//  Serial Monitor Commands
// ════════════════════════════════════════════════════════════════
static void _printSerialHelp() {
  Serial.println("╔══════════════════════════════════════════════╗");
  Serial.println("║       Serial WiFi Manager — Commands         ║");
  Serial.println("╠══════════════════════════════════════════════╣");
  Serial.println("║ SETWIFI:<SSID>,<PASSWORD>  → Set WiFi baru   ║");
  Serial.println("║ GETWIFI                    → Lihat SSID       ║");
  Serial.println("║ CLEARWIFI                  → Hapus tersimpan  ║");
  Serial.println("║ RESTART                    → Restart ESP      ║");
  Serial.println("╚══════════════════════════════════════════════╝");
}

static void _handleSerialCommand(const String& cmd) {
  String trimmed = cmd;
  trimmed.trim();
  String upper = trimmed;
  upper.toUpperCase();

  if (upper == "GETWIFI") {
    String ssid, pass;
    if (_loadCredentials(ssid, pass))
      Serial.printf("[WiFiMgr] SSID tersimpan: \"%s\" | Password: ***\n", ssid.c_str());
    else
      Serial.println("[WiFiMgr] Belum ada kredensial tersimpan.");
    return;
  }
  if (upper == "CLEARWIFI") {
    _prefs.begin("wifi", false); _prefs.clear(); _prefs.end();
    Serial.println("[WiFiMgr] Kredensial dihapus. Restart untuk masuk AP mode.");
    return;
  }
  if (upper == "RESTART") {
    Serial.println("[WiFiMgr] Restarting...");
    delay(500); ESP.restart();
    return;
  }
  if (upper.startsWith("SETWIFI:")) {
    String payload  = trimmed.substring(8);
    int    commaIdx = payload.indexOf(',');
    String newSSID  = (commaIdx == -1) ? payload : payload.substring(0, commaIdx);
    String newPass  = (commaIdx == -1) ? ""       : payload.substring(commaIdx + 1);
    newSSID.trim();
    if (newSSID.isEmpty()) {
      Serial.println("[WiFiMgr] ERROR: SSID tidak boleh kosong!");
      return;
    }
    Serial.printf("[WiFiMgr] SSID: \"%s\" | Pass: \"%s\"\n", newSSID.c_str(), newPass.c_str());
    _saveCredentials(newSSID, newPass);
    Serial.println("[WiFiMgr] Tersimpan! Restarting...");
    delay(1000); ESP.restart();
    return;
  }
  if (trimmed.length() > 0) {
    Serial.printf("[WiFiMgr] Tidak dikenal: \"%s\"\n", trimmed.c_str());
    _printSerialHelp();
  }
}

// ════════════════════════════════════════════════════════════════
//  PUBLIC API
// ════════════════════════════════════════════════════════════════

void wifiManagerBegin() {
  Serial.println("\n[WiFiMgr] ══ WiFi Manager Start ══");
  _printSerialHelp();

  String ssid, pass;
  if (!_loadCredentials(ssid, pass)) {
    Serial.println("[WiFiMgr] Tidak ada kredensial → AP mode");
    _beepError();
    _startCaptivePortal();
    return;
  }

  if (_tryConnect(ssid, pass)) {
    _beepSuccess();
    Serial.println("[WiFiMgr] WiFi Connected! IP: " + WiFi.localIP().toString());
    Serial.printf( "[WiFiMgr] SSID: %s | RSSI: %d dBm\n", WiFi.SSID().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[WiFiMgr] Gagal → AP / Captive Portal");
    _beepError();
    _startCaptivePortal();
  }
}

void wifiManagerLoop() {
  if (_apModeActive) {
    _dns.processNextRequest();
    _server.handleClient();
  }
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (_serialBuffer.length() > 0) {
        _handleSerialCommand(_serialBuffer);
        _serialBuffer = "";
      }
    } else {
      _serialBuffer += c;
    }
  }
  if (!_apModeActive && WiFi.status() != WL_CONNECTED) {
    static unsigned long _lastReconnectMs  = 0;
    static int           _reconnectFails   = 0;
    const  unsigned long RECONNECT_COOLDOWN = 30000UL;
    const  int           MAX_RECONNECT_FAIL = 10;

    if (millis() - _lastReconnectMs >= RECONNECT_COOLDOWN) {
      _lastReconnectMs = millis();
      Serial.printf("[WiFiMgr] WiFi putus! Reconnect attempt %d/%d\n",
                    _reconnectFails + 1, MAX_RECONNECT_FAIL);

      String ssid, pass;
      if (_loadCredentials(ssid, pass) && _tryConnect(ssid, pass)) {
        _beepSuccess();
        _reconnectFails = 0;
        Serial.println("[WiFiMgr] Reconnect OK! IP: " + WiFi.localIP().toString());
      } else {
        _reconnectFails++;
        _beepError();
        Serial.printf("[WiFiMgr] Reconnect gagal (%d/%d)\n",
                      _reconnectFails, MAX_RECONNECT_FAIL);

        if (_reconnectFails >= MAX_RECONNECT_FAIL) {
          Serial.println("[WiFiMgr] Terlalu banyak gagal → AUTO RESTART!");
          delay(500);
          ESP.restart(); // setelah 5 menit gagal terus, restart bersih
        }
      }
    }
  }
}

bool wifiIsConnected() {
  return !_apModeActive && (WiFi.status() == WL_CONNECTED);
}
