// ═══════════════════════════════════════════════════════════════
//  kecepatan_angin.ino  —  Sistem Anemometer + WiFi Manager
// ═══════════════════════════════════════════════════════════════

#include "cfg_config.h"
#include "wifi_manager_updated.h"
#include "fb_firebase_helper.h"

FirebaseData   fbdo;
FirebaseAuth   fbAuth;
FirebaseConfig fbConfig;

volatile int  pulseCount  = 0;
unsigned long lastSecond  = 0;
unsigned long lastHistory = 0;

float totalSpeed   = 0;
int   jumlahSample = 0;

void IRAM_ATTR hitungPulsa() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_HALL,   INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║    Sistem Anemometer Klimatologi IoT     ║");
  Serial.println("╚══════════════════════════════════════════╝");

  wifiManagerBegin();

  if (wifiIsConnected()) {
    setupFirebase(fbdo, fbAuth, fbConfig);
  } else {
    Serial.println("[Main] Mode AP aktif — Firebase dilewati.");
    Serial.printf( "[Main] Sambungkan HP ke hotspot \"%s\"\n", AP_SSID);
    Serial.println("[Main] lalu buka browser/app untuk isi WiFi baru.");
  }

  attachInterrupt(digitalPinToInterrupt(PIN_HALL), hitungPulsa, FALLING);
  Serial.println("[Main] Setup selesai. Sistem aktif.\n");
}

void loop() {
  wifiManagerLoop();

  if (!wifiIsConnected()) return;

  // ── REALTIME ─────────────────────────────────────────────────
  if (millis() - lastSecond >= INTERVAL_REALTIME) {
    int pulsa  = pulseCount;
    pulseCount = 0;
    lastSecond = millis();

    float intervalDetik = INTERVAL_REALTIME / 1000.0;
    float speedMS = 2.0 * PI * RADIUS_M * (pulsa / intervalDetik) * K_FAKTOR;

    Serial.printf("[Main] Pulsa: %d | Speed: %.4f m/s\n", pulsa, speedMS);
    sendRealtime(fbdo, speedMS, fbConfig); // ← tambah fbConfig

    totalSpeed += speedMS;
    jumlahSample++;
  }

  // ── HISTORY ───────────────────────────────────────────────────
  if (millis() - lastHistory >= INTERVAL_HISTORY) {
    lastHistory = millis();

    if (jumlahSample > 0) {
      float avgSpeed = totalSpeed / jumlahSample;
      Serial.printf("[Main] History — Avg: %.4f m/s dari %d sample\n",
                    avgSpeed, jumlahSample);
      sendHistory(fbdo, avgSpeed, fbConfig); // ← tambah fbConfig
      totalSpeed   = 0;
      jumlahSample = 0;
    }
  }
}
