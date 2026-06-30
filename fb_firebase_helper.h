#pragma once

// ═══════════════════════════════════════════════════════════════
//  fb_firebase_helper.h  —  Firebase RTDB: Setup, Realtime, Average, History
//
//  Tiga jalur pengiriman data (independent):
//  ┌─────────────┬────────────────────────────────┬───────────┐
//  │ Fungsi      │ Path Firebase                  │ Mode      │
//  ├─────────────┼────────────────────────────────┼───────────┤
//  │ sendRealtime│ /…/realtime                    │ update    │
//  │ sendAverage │ /…/average                     │ update    │
//  │ sendHistory │ /…/history                     │ push      │
//  └─────────────┴────────────────────────────────┴───────────┘
//
//  Kalkulasi: windweg_km = pulseCount / PULSE_PER_KM (= 18)
//
//  Perbaikan v2:
//  - Tambah intervalAverage ke SensorSettings
//  - Fix sendRealtime: pakai parameter windwegKm & pulseCount
//    (sebelumnya salah pakai 'speed' & 'pulseRealtime' yang tidak ada)
//  - Tambah sendAverage (baru)
//  - Update sendHistory: pakai total/avg/max windweg berbasis pulse
//  - Tambah sanity check intervalAverage vs intervalHistory
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
const  unsigned long REINIT_COOLDOWN     = 30000UL; // coba reinit tiap 30 detik

// ════════════════════════════════════════════════════════════════
//  SensorSettings — nilai dari Firebase atau default cfg_config.h
// ════════════════════════════════════════════════════════════════
struct SensorSettings {
  unsigned long intervalRealtime = DEFAULT_INTERVAL_REALTIME;
  unsigned long intervalAverage  = DEFAULT_INTERVAL_AVERAGE; // ← interval rata-rata (baru)
  unsigned long intervalHistory  = DEFAULT_INTERVAL_HISTORY;
  int magnetCount = DEFAULT_MAGNET_COUNT;
};

// ── Device path prefix ────────────────────────────────────────
static String _basePath() {
  return String("/anemometer/") + DEVICE_ID;
}

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
    Serial.printf("\n[Firebase] READY! Device ID: %s\n", DEVICE_ID);
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

// ════════════════════════════════════════════════════════════════
//  fetchSettings — baca settings dari Firebase
//
//  Path Firebase (set dari Flutter app):
//    /anemometer/settings/interval_realtime_ms  (int, min 1000 ms)
//    /anemometer/settings/interval_average_ms   (int, min 10000 ms)
//    /anemometer/settings/interval_history_ms   (int, min 30000 ms)
//    /anemometer/settings/magnet_count          (int, 1–4)
//
//  Jika node belum ada atau gagal baca → pakai nilai default
//  dari cfg_config.h (tidak crash).
// ════════════════════════════════════════════════════════════════
SensorSettings fetchSettings(FirebaseData &fbdo) {
  SensorSettings s; // mulai dari default

  // --- interval_realtime_ms ---
  if (Firebase.RTDB.getInt(&fbdo, "/anemometer/settings/interval_realtime_ms")) {
    long val = fbdo.intData();
    if (val >= 1000) {
      s.intervalRealtime = (unsigned long)val;
      Serial.printf("[Settings] interval_realtime = %lu ms\n", s.intervalRealtime);
    }
  } else {
    Serial.printf("[Settings] interval_realtime gagal dibaca, pakai default %lu ms\n",
                  s.intervalRealtime);
  }

    // --- interval_average_ms (baru) ---
  if (Firebase.RTDB.getInt(&fbdo, "/anemometer/settings/interval_average_ms")) {
    long val = fbdo.intData();
    if (val >= 10000) { // minimal 10 detik
      s.intervalAverage = (unsigned long)val;
      Serial.printf("[Settings] interval_average = %lu ms\n", s.intervalAverage);
    }
  } else {
    Serial.printf("[Settings] interval_average gagal dibaca, pakai default %lu ms\n",
                  s.intervalAverage);
  }


  // --- interval_history_ms ---
  if (Firebase.RTDB.getInt(&fbdo, "/anemometer/settings/interval_history_ms")) {
    long val = fbdo.intData();
    if (val >= 30000) {
      s.intervalHistory = (unsigned long)val;
      Serial.printf("[Settings] interval_history  = %lu ms\n", s.intervalHistory);
    }
  } else {
    Serial.printf("[Settings] interval_history gagal dibaca, pakai default %lu ms\n",
                  s.intervalHistory);
  }

  // --- magnet_count ---
  if (Firebase.RTDB.getInt(&fbdo, "/anemometer/settings/magnet_count")) {
    int val = fbdo.intData();
    if (val == 1 || val == 2 || val == 3) {
      s.magnetCount = val;
      Serial.printf("[Settings] magnet_count = %d\n", s.magnetCount);
    }
  } else {
    Serial.printf("[Settings] magnet_count gagal dibaca, pakai default %d\n",
                  s.magnetCount);
  }

  // --- Sanity check ---
  // intervalAverage tidak boleh lebih besar dari intervalHistory,
  // karena rata-rata harus bisa terkumpul sebelum history dikirim.
  if (s.intervalAverage > s.intervalHistory) {
    Serial.printf("[Settings] PERINGATAN: interval_average (%lu ms) > interval_history (%lu ms). "
                  "interval_average disesuaikan.\n",
                  s.intervalAverage, s.intervalHistory);
    s.intervalAverage = s.intervalHistory;
  }

  return s;
}

// ════════════════════════════════════════════════════════════════
//  sendLog — push satu baris log ke Firebase
//
//  Path: /anemometer/{DEVICE_ID}/logs/{pushKey}
// ════════════════════════════════════════════════════════════════
void sendLog(FirebaseData &fbdo, const String &msg) {
  String path = _basePath() + "/logs";

  FirebaseJson json;
  json.set("msg",       msg);
  json.set("timestamp", (int)time(NULL));

  if (Firebase.RTDB.pushJSON(&fbdo, path, &json)) {
    Serial.printf("[Log] OK: %s\n", msg.c_str());
  } else {
    Serial.printf("[Log] GAGAL kirim: %s\n", msg.c_str());
  }
}

// ════════════════════════════════════════════════════════════════
//  sendRealtime — update node realtime per intervalRealtime
//
//  Path: /anemometer/{DEVICE_ID}/realtime  (updateNode / overwrite)
//  Fields:
//    windweg_km    → jarak angin selama interval ini (pulseCount / 18)
//    pulse_count   → jumlah pulsa selama interval ini
//    pulses_per_km → konstanta = 18
//    interval_ms   → durasi interval realtime
//    timestamp     → unix time WIB
//
//  FIX: sebelumnya pakai variabel 'speed' & 'pulseRealtime' yang
//       tidak ada di scope fungsi → sekarang pakai parameter windwegKm
//       & pulseCount yang dikirim dari loop().
// ════════════════════════════════════════════════════════════════
void sendRealtime(FirebaseData &fbdo,
                  float windwegKm,
                  int   pulseCount,
                  const SensorSettings &settings,
                  FirebaseConfig &config) {

  static int ok = 0, fail = 0;
  String path = _basePath() + "/realtime";

  FirebaseJson json;
  json.set("windweg_km",    windwegKm);
  json.set("pulse_count",   pulseCount);
  json.set("pulses_per_km", PULSE_PER_KM);
  json.set("interval_ms",   (int)settings.intervalRealtime);
  json.set("timestamp",     (int)time(NULL));

  if (Firebase.RTDB.updateNode(&fbdo, path, &json)) {
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

    if (reason.indexOf("token")    >= 0 ||
        reason.indexOf("expired")  >= 0 ||
        reason.indexOf("revoked")  >= 0 ||
        reason.indexOf("not ready") >= 0) {
      _tryReinitFirebase(config);
    }

    if (_consecutiveFail >= MAX_FAIL_BEFORE_REBOOT) {
      Serial.printf("[Firebase] Gagal %d kali berturut-turut → AUTO REBOOT!\n",
                    _consecutiveFail);
      delay(1000);
      ESP.restart();
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  sendAverage — update node rata-rata per intervalAverage (default 1 menit)
//
//  Path: /anemometer/{DEVICE_ID}/average  (updateNode / overwrite)
//  Fields:
//    windweg_km    → jarak angin selama interval rata-rata ini
//    pulse_count   → jumlah pulsa selama interval rata-rata ini
//    sample_number → urutan sample sejak history terakhir (1, 2, 3, …)
//    pulses_per_km → konstanta = 18
//    interval_ms   → durasi interval rata-rata
//    timestamp     → unix time WIB
//
//  Node /average selalu ditimpa (bukan push), menampilkan nilai
//  rata-rata terkini. Untuk rekap per-periode, lihat /history.
// ════════════════════════════════════════════════════════════════

void sendAverage(FirebaseData &fbdo,
                 float windwegKm,
                 int   pulseCount,
                 int   sampleNumber,
                 const SensorSettings &settings,
                 FirebaseConfig &config) {

  static int ok = 0, fail = 0;
  String path = _basePath() + "/average";

  FirebaseJson json;
  json.set("windweg_km",    windwegKm);
  json.set("pulse_count",   pulseCount);
  json.set("sample_number", sampleNumber);
  json.set("pulses_per_km", PULSE_PER_KM);
  json.set("interval_ms",   (int)settings.intervalAverage);
  json.set("timestamp",     (int)time(NULL));

  if (Firebase.RTDB.updateNode(&fbdo, path, &json)) {
    ok++;
    Serial.printf("[Firebase] Average OK #%d (%.4f km) (%d ok / %d fail)\n",
                  sampleNumber, windwegKm, ok, fail);
  } else {
    fail++;
    String reason = fbdo.errorReason();
    Serial.printf("[Firebase] Average GAGAL (%d ok / %d fail) — %s\n",
                  ok, fail, reason.c_str());

    if (reason.indexOf("token")     >= 0 ||
        reason.indexOf("expired")   >= 0 ||
        reason.indexOf("revoked")   >= 0 ||
        reason.indexOf("not ready") >= 0) {
      _tryReinitFirebase(config);
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  sendHistory — push rekap per intervalHistory (default 1 jam)
//
//  Path: /anemometer/{DEVICE_ID}/history/{pushKey}
//  Fields:
//    total_windweg_km  → total jarak angin periode ini (pulseHistory / 18)
//    avg_windweg_km    → rata-rata per avg-interval (totalAvg / sampleCount)
//    max_windweg_km    → nilai avg-interval tertinggi periode ini
//    total_pulse       → total pulsa mentah periode ini
//    sample_count      → berapa kali avg-interval terjadi
//    pulses_per_km     → konstanta = 18
//    interval_avg_ms   → durasi interval rata-rata
//    interval_hist_ms  → durasi interval history
//    timestamp         → unix time WIB
// ════════════════════════════════════════════════════════════════
void sendHistory(FirebaseData &fbdo,
                 float totalWindwegKm,
                 float avgWindwegKm,
                 float maxWindwegKm,
                 int   totalPulse,
                 int   sampleCount,
                 const SensorSettings &settings,
                 FirebaseConfig &config) {

  String path = _basePath() + "/history";

  FirebaseJson json;
  json.set("total_windweg_km", totalWindwegKm);
  json.set("avg_windweg_km",   avgWindwegKm);
  json.set("max_windweg_km",   maxWindwegKm);
  json.set("total_pulse",      totalPulse);
  json.set("sample_count",     sampleCount);
  json.set("pulses_per_km",    PULSE_PER_KM);
  json.set("interval_avg_ms",  (int)settings.intervalAverage);
  json.set("interval_hist_ms", (int)settings.intervalHistory);
  json.set("timestamp",        (int)time(NULL));

  if (Firebase.RTDB.pushJSON(&fbdo, path, &json)) {
    Serial.printf("[Firebase] History OK — total=%.4f km | avg=%.4f km | max=%.4f km | sample=%d\n",
                  totalWindwegKm, avgWindwegKm, maxWindwegKm, sampleCount);
  } else {
    String reason = fbdo.errorReason();
    Serial.printf("[Firebase] History GAGAL — %s\n", reason.c_str());
    bunyiAlarmError();

    if (reason.indexOf("token")     >= 0 ||
        reason.indexOf("expired")   >= 0 ||
        reason.indexOf("not ready") >= 0) {
      _tryReinitFirebase(config);
    }
  }
}
  
// ════════════════════════════════════════════════════════════════
//  checkRemoteCommand — cek perintah restart dari Firebase
//
//  Path: /anemometer/{DEVICE_ID}/command/restart
//    true  → ESP restart, lalu node di-reset ke false
//    false → tidak ada aksi
// ════════════════════════════════════════════════════════════════
void checkRemoteCommand(FirebaseData &fbdo) {
  String path = _basePath() + "/command/restart";

  if (!Firebase.RTDB.getBool(&fbdo, path)) {
    // Node belum ada = normal, tidak perlu log
    return;
  }

  if (fbdo.boolData() == true) {
    Serial.println("[CMD] Remote restart diterima dari Firebase!");
    sendLog(fbdo, "CMD: remote restart dipicu dari app");

    // Reset node dulu sebelum restart
    Firebase.RTDB.setBool(&fbdo, path, false);
    delay(500);
    ESP.restart();
  }
}


