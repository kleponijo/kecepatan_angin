#pragma once

// --- WiFi ---
const char* WIFI_SSID     = "wifiku";
const char* WIFI_PASSWORD = "kamunanyak";

// --- Firebase ---
const char* DATABASE_URL = "https://klimatologiot-default-rtdb.asia-southeast1.firebasedatabase.app";
const char* API_KEY      = "AIzaSyAZrk_k4DQ_ijCa6gp67oRklFMKD2dLcbQ";

// --- Pin ---
#define PIN_HALL   32
#define PIN_BUZZER 18

// --- Parameter Anemometer ---
const float RADIUS_M = 0.08; // jari-jari lengan anemometer (meter) = 8cm
const float K_FAKTOR = 1.0;  // konstanta kalibrasi (belum kalibrasi lapangan)

// --- Interval ---
const unsigned long INTERVAL_REALTIME = 1000; // 1 detik (ms)
const unsigned long INTERVAL_HISTORY  = 600000; // 10 menit