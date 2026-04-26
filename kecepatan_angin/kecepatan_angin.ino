#include <WiFi.h>
#include "config.h"
#include "firebase_helper.h"

FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig config;

volatile int  pulseCount = 0;
unsigned long lastSecond = 0;

void IRAM_ATTR hitungPulsa() {
  pulseCount++;
}

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
  if (millis() - lastSecond >= INTERVAL_REALTIME) {

    int pulsa  = pulseCount;
    pulseCount = 0;
    lastSecond = millis();

    // v (m/s) = 2π × r × pulsa/t × K
    float speedMS = 2.0 * PI * RADIUS_M * pulsa * K_FAKTOR;
    // (dibagi t tidak perlu karena INTERVAL = 1 detik, pulsa/1 = pulsa)

    Serial.printf("Pulsa: %d | Speed: %.4f m/s\n", pulsa, speedMS);

    sendRealtime(fbdo, speedMS);
  }
}