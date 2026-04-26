#pragma once
#include <Firebase_ESP_Client.h>
#include <time.h>
#include "config.h"

void setupFirebase(FirebaseData &fbdo, FirebaseAuth &auth, FirebaseConfig &config) {
  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase Auth: OK");
  } else {
    Serial.printf("Firebase Auth Gagal: %s\n", config.signer.signupError.message.c_str());
  }

  fbdo.setResponseSize(1024);
  config.timeout.serverResponse = 10 * 1000;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // UTC 0, konversi waktu dilakukan di sisi aplikasi
  configTime(0, 0, "pool.ntp.org");
}

void sendRealtime(FirebaseData &fbdo, float speedMS) {
  FirebaseJson json;
  json.set("speed",     speedMS);        // m/s
  json.set("timestamp", (int)time(NULL)); // UTC unix timestamp

  if (Firebase.RTDB.updateNode(&fbdo, "/anemometer/realtime", &json)) {
    Serial.println("Firebase: BERHASIL");
  } else {
    Serial.print("Firebase GAGAL: ");
    Serial.println(fbdo.errorReason());
  }
}