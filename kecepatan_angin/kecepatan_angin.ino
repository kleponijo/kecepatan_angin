#include <WiFi.h>
#include "config.h"
#include "firebase_helper.h"

FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig config;

volatile int  pulseCount = 0;
unsigned long lastSecond = 0;
unsigned long lastHistory  = 0;

// Untuk akumulasi rata-rata selama 10 menit
float totalSpeed    = 0;
int   jumlahSample  = 0;

void IRAM_ATTR hitungPulsa() {
  pulseCount++;
}

  // ── Cek & reconnect WiFi jika putus ─────────────────────────────
  void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi putus! Reconnecting...");
    WiFi.reconnect();

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 10) {
      delay(500);
      retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi reconnected!");
    } else {
      Serial.println("WiFi gagal reconnect.");
      bunyiAlarmError(); // alarm juga kalau WiFi gagal
    }
  }
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(PIN_HALL,   INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  // Koneksi WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    for (int i = 0; i < 2; i++) {
      tone(PIN_BUZZER, 1000); delay(300);
      noTone(PIN_BUZZER);     delay(700);
    }
    Serial.print(".");
    delay(500);
  }

  // Buzzer tanda berhasil
  tone(PIN_BUZZER, 1500); delay(500); noTone(PIN_BUZZER);
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  setupFirebase(fbdo, auth, config);
  attachInterrupt(digitalPinToInterrupt(PIN_HALL), hitungPulsa, FALLING);
  Serial.println("=== Sistem Anemometer Aktif ===");
  }


void loop() {
  checkWiFi();

  // --- REALTIME (tiap 1 detik) ---
  if (millis() - lastSecond >= INTERVAL_REALTIME) {
    int pulsa  = pulseCount;
    pulseCount = 0;
    lastSecond = millis();

 // ✅ Sesuaikan rumus dengan interval 5 detik
    float intervalDetik = INTERVAL_REALTIME / 1000.0; // = 5.0
    float speedMS = 2.0 * PI * RADIUS_M * (pulsa / intervalDetik) * K_FAKTOR;

    Serial.printf("Pulsa: %d | Speed: %.4f m/s\n", pulsa, speedMS);
    sendRealtime(fbdo, speedMS);

    totalSpeed += speedMS;
    jumlahSample++;
  }

  // --- HISTORY (tiap 10 menit) ---
  if (millis() - lastHistory >= INTERVAL_HISTORY) {
    lastHistory = millis();

    if (jumlahSample > 0) {
      float avgSpeed = totalSpeed / jumlahSample;
      Serial.printf("History — Avg: %.4f m/s dari %d sample\n",
                    avgSpeed, jumlahSample);
      sendHistory(fbdo, avgSpeed);
      totalSpeed   = 0;
      jumlahSample = 0;
    }
  }

} // ← kurung tutup loop() paling bawah
