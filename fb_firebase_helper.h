#pragma once

// ═══════════════════════════════════════════════════════════════
//  fb_firebase_helper.h  —  Firebase RTDB: Setup, Realtime, History
//
//  Perbaikan:
//  - Auto token refresh saat token expired/revoked
//  - Auto-reboot ESP jika gagal terus > MAX_FAIL_BEFORE_REBOOT
// ═══════════════════════════════════════════════════════════════

#include <Firebase_ESP_Client.h>
#include <time.h>
#include "cfg_config.h"

// ── Konstanta error handling ──────────────────────────────────────
// 600 gagal × 1 detik = 10 menit terus gagal → auto reboot
const int MAX_FAIL_BEFORE_REBOOT = 600;

// ── State internal ────────────────────────────────────────────────
static int           _consecutiveFail    = 0;
static unsigned long _lastReinitAttempt  = 0;
const  unsigned long REINIT_COOLDOWN     = 30000; // coba reinit tiap 30 detik

// ── Alarm error (tetap seperti semula) ───────────────────────────
void bunyiAlarmError() {
  for (int i = 0; i < 10; i++) {
    tone(PIN_BUZZER, 2000); delay(100);
    noTone(PIN_BUZZER);     delay(100);
  }
}

// ── Tunggu NTP sinkron ────────────────────────────────────────────
void waitNTP() {
  Serial.print("[Firebase] Sinkronisasi NTP");
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = 0;
  int retry  = 0;
  while (now < 100000 && retry < 40) {
    delay(500);
    time(&now);
    Serial.print(".");
    retry++;
  }

  if (now < 100000) Serial.println("\n[Firebase] NTP GAGAL!");
  else Serial.printf("\n[Firebase] NTP OK! Unix: %lu\n", (unsigned long)now);
}

// ── Setup Firebase ────────────────────────────────────────────────
void setupFirebase(FirebaseData &fbdo, FirebaseAuth &auth, FirebaseConfig &config) {
  waitNTP();

  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email     = FB_EMAIL;
  auth.user.password  = FB_PASSWORD;

  fbdo.setResponseSize(4096);
  config.timeout.serverResponse = 15 * 1000;

  // Monitor status token di Serial
  config.token_status_callback = [](TokenInfo info) {
    if (info.status == token_status_error)
      Serial.printf("[Firebase] Token error: %s\n", info.error.message.c_str());
    else if (info.status == token_status_ready)
      Serial.println("[Firebase] Token ready/refreshed.");
  };

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("[Firebase] Menunggu ready");
  int retry = 0;
  while (!Firebase.ready() && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (Firebase.ready()) {
    Serial.println("\n[Firebase] READY!");
    _consecutiveFail = 0;
  } else {
    Serial.println("\n[Firebase] GAGAL READY");
    bunyiAlarmError();
  }
}

// ── Reinit token (dipanggil saat token expired) ───────────────────
static void _tryReinitFirebase(FirebaseConfig &config) {
  unsigned long now = millis();
  if (now - _lastReinitAttempt < REINIT_COOLDOWN) return;
  _lastReinitAttempt = now;

  Serial.println("[Firebase] Token bermasalah → mencoba refresh...");
  Firebase.refreshToken(&config);

  int retry = 0;
  while (!Firebase.ready() && retry < 10) {
    delay(500);
    retry++;
  }

  if (Firebase.ready()) {
    Serial.println("[Firebase] Token refresh berhasil!");
    _consecutiveFail = 0;
  } else {
    Serial.println("[Firebase] Token refresh gagal, akan coba lagi nanti.");
  }
}

// ── Kirim Realtime ────────────────────────────────────────────────
void sendRealtime(FirebaseData &fbdo, float speedMS, FirebaseConfig &config) {
  static int ok = 0, fail = 0;

  FirebaseJson json;
  json.set("speed",     speedMS);
  json.set("timestamp", (int)time(NULL));

  if (Firebase.RTDB.updateNode(&fbdo, "/anemometer/realtime", &json)) {
    ok++;
    _consecutiveFail = 0;
    Serial.printf("[Firebase] Realtime OK (%d ok / %d fail)\n", ok, fail);

  } else {
    fail++;
    _consecutiveFail++;
    String reason = fbdo.errorReason();
    Serial.printf("[Firebase] Realtime GAGAL (%d ok / %d fail) — %s\n",
                  ok, fail, reason.c_str());
    bunyiAlarmError();

    // Deteksi token expired → coba refresh
    if (reason.indexOf("token") >= 0   ||
        reason.indexOf("expired") >= 0 ||
        reason.indexOf("revoked") >= 0 ||
        reason.indexOf("not ready") >= 0) {
      _tryReinitFirebase(config);
    }

    // Auto-reboot jika gagal terlalu lama
    if (_consecutiveFail >= MAX_FAIL_BEFORE_REBOOT) {
      Serial.printf("[Firebase] Gagal %d kali berturut-turut → AUTO REBOOT!\n",
                    _consecutiveFail);
      delay(1000);
      ESP.restart();
    }
  }
}

// ── Kirim History ─────────────────────────────────────────────────
void sendHistory(FirebaseData &fbdo, float avgSpeedMS, FirebaseConfig &config) {
  FirebaseJson json;
  json.set("speed",     avgSpeedMS);
  json.set("timestamp", (int)time(NULL));

  if (Firebase.RTDB.pushJSON(&fbdo, "/anemometer/history", &json)) {
    Serial.println("[Firebase] History OK");
  } else {
    String reason = fbdo.errorReason();
    Serial.printf("[Firebase] History GAGAL — %s\n", reason.c_str());
    bunyiAlarmError();

    if (reason.indexOf("token") >= 0   ||
        reason.indexOf("expired") >= 0 ||
        reason.indexOf("not ready") >= 0) {
      _tryReinitFirebase(config);
    }
  }
}
