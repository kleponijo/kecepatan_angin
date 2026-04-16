#include <Firebase_ESP_Client.h>
#include <time.h>

void setupFirebase(FirebaseData &fbdo, FirebaseAuth &auth, FirebaseConfig &config) {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase Auth: Ok");
  } else {
    Serial.printf("Firebase Auth Gagal: %s\n", config.signer.signupError.message.c_str());
  }

  fbdo.setResponseSize(1024);
  config.timeout.serverResponse = 10 * 1000; 

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Pakai UTC 0 agar fleksibel di aplikasi
  configTime(0, 0, "pool.ntp.org"); 
}

void sendRealtime(FirebaseData &fbdo, float speed) {
  FirebaseJson json;
  json.set("speed", speed);
  json.set("timestamp", (int)time(NULL)); 

  if (Firebase.RTDB.updateNode(&fbdo, "/anemometer/realtime", &json)) {
    Serial.println("Firebase Realtime: BERHASIL");
  } else {
    Serial.print("Firebase Realtime: GAGAL, Alasan: ");
    Serial.println(fbdo.errorReason());
  }
}

void sendHistory(FirebaseData &fbdo, float avgSpeed) {
  FirebaseJson json;
  json.set("speed", avgSpeed);
  json.set("timesTamp", (int)time(NULL));

   if (Firebase.RTDB.pushJSON(&fbdo, "/anemometer/history", &json)) {
    Serial.println("Firebase History: BERHASIL TERKIRIM");
  } else {
    Serial.print("Firebase History: GAGAL! ");
    Serial.println(fbdo.errorReason());
  }
}