#pragma once
#include <Firebase_ESP_Client.h>
#include <time.h>
#include "config.h"

// ── Alarm error — bunyi tit-tit-tit cepat ───────────────────────
void bunyiAlarmError() {
  for (int i = 0; i < 10; i++) {
    tone(PIN_BUZZER, 2000); delay(100);
    noTone(PIN_BUZZER);     delay(100);
  }
}

// ── Tunggu NTP sinkron dulu sebelum lanjut ──────────────────────
void waitNTP() {
  Serial.print("Sinkronisasi NTP");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  time_t now = 0;
  int retry  = 0;
  while (now < 100000 && retry < 40) {
    delay(500);
    time(&now);
    Serial.print(".");
    retry++;
  }

  if (now < 100000) {
    Serial.println("\nNTP GAGAL!");
  } else {
    Serial.println("\nNTP OK! Waktu: " + String(now));
  }
}

void setupFirebase(FirebaseData &fbdo, FirebaseAuth &auth, FirebaseConfig &config) {
// ✅ NTP HARUS sinkron SEBELUM Firebase init
  waitNTP();

  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;

   // ✅ Login pakai akun yang sudah ada, bukan buat user baru
  auth.user.email    = "coba@gmail.com";  // email akun Firebase kamu
  auth.user.password = "coba123";      // password akun itu

  fbdo.setResponseSize(4096);
  config.timeout.serverResponse = 15 * 1000;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // ✅ Tunggu Firebase benar-benar ready
  Serial.print("Menunggu Firebase ready");
  int retry = 0;
  while (!Firebase.ready() && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (Firebase.ready()) {
    Serial.println("\nFirebase: READY!");
  } else {
    Serial.println("\nFirebase: GAGAL READY");
    bunyiAlarmError();
  }

}


// ── Kirim Realtime ───────────────────────────────────────────────
void sendRealtime(FirebaseData &fbdo, float speedMS) {
  static int berhasil = 0;
  static int gagal    = 0;

  FirebaseJson json;
  json.set("speed",     speedMS);        // m/s
  json.set("timestamp", (int)time(NULL)); // UTC unix timestamp

  if (Firebase.RTDB.updateNode(&fbdo, "/anemometer/realtime", &json)) {
    berhasil++;
    Serial.printf("Realtime OK ✅ | Berhasil: %d | Gagal: %d\n",
                  berhasil, gagal);
  } else {
    gagal++;
    Serial.printf("Realtime GAGAL ❌ | Berhasil: %d | Gagal: %d | %s\n",
                  berhasil, gagal, fbdo.errorReason().c_str());
    Serial.println(fbdo.errorReason());
    bunyiAlarmError(); // ✅ alarm kalau gagal
  }
}

// ── Kirim History ────────────────────────────────────────────────
void sendHistory(FirebaseData &fbdo, float avgSpeedMS) {
  FirebaseJson json;
  json.set("speed",     avgSpeedMS);
  json.set("timestamp", (int)time(NULL));

  if (Firebase.RTDB.pushJSON(&fbdo, "/anemometer/history", &json)) {
    Serial.println("History: OK");
  } else {
    Serial.print("History GAGAL: ");
    Serial.println(fbdo.errorReason());
    bunyiAlarmError(); // ✅ alarm kalau gagal
  }
}