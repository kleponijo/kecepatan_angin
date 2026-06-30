#pragma once

// ═══════════════════════════════════════════════════════════════
//  cfg_config.h  —  Konfigurasi Global Sistem Anemometer
//
//  ⚠️  WiFi SSID & Password TIDAK hardcode di sini.
//      Disimpan di NVS (flash) oleh wifi_manager.h
//      dan bisa diubah via Captive Portal atau Serial Monitor.
// ═══════════════════════════════════════════════════════════════

// --- Firebase ---
const char* DATABASE_URL = "https://klimatologiot-default-rtdb.asia-southeast1.firebasedatabase.app";
const char* API_KEY      = "AIzaSyAZrk_k4DQ_ijCa6gp67oRklFMKD2dLcbQ";

// --- Firebase Auth ---
const char* FB_EMAIL    = "coba@gmail.com";
const char* FB_PASSWORD = "coba123";

// --- Pin ---
#define PIN_HALL   32
#define PIN_BUZZER 18

// ─────────────────────────────────────────────────────────────
//  Kalkulasi Windweg (pulse-based, tanpa k_faktor)
//
//  Anemometer memiliki 3 magnet per cup (PULSE_PER_ROTATION = 3)
//  Satu km di windweg speedo = 6 putaran penuh (ROTATION_PER_KM = 6)
//  → 1 km = 3 × 6 = 18 pulsa (PULSE_PER_KM)
//
//  Rumus:  windweg_km = jumlah_pulsa / PULSE_PER_KM
//  Contoh: 18 pulsa → 1.0 km | 9 pulsa → 0.5 km | 1 pulsa → 0.0556 km
// ─────────────────────────────────────────────────────────────
const int PULSE_PER_ROTATION = 3;
const int ROTATION_PER_KM = 6;
const int PULSE_PER_KM       = PULSE_PER_ROTATION * ROTATION_PER_KM; // = 18

// --- Interval Default (bisa di-override dari Firebase) ---
const unsigned long  DEFAULT_INTERVAL_REALTIME = 1000UL;      // 1 detik
const unsigned long DEFAULT_INTERVAL_AVERAGE  = 60000UL;   // 1 menit
const unsigned long  DEFAULT_INTERVAL_HISTORY  = 3600000UL;   // 1 jam

// --- Settings Fallback jika Firebase tidak terbaca ---
// (k_faktor & radius_m tidak dipakai di kalkulasi pulse-based,
//  disimpan sebagai fallback untuk kompatibilitas Flutter app)
const int   DEFAULT_MAGNET_COUNT = 3;     // jumlah magnet pada cup anemometer

// --- Device Identity ---
// Ganti ke "esp_percobaan" untuk unit indoor/test
const char* DEVICE_ID = "esp_lapangan"; // ← ganti ke "esp_lapangan" untuk ESP outdoor

// --- WiFi Manager Settings ---
const char*          AP_SSID        = "Anemometer-Setup"; // nama hotspot captive portal
const char*          AP_PASSWORD    = "";                  // kosong = open AP
const int            WIFI_MAX_RETRY = 40;                  // retry sebelum masuk AP mode
const unsigned long  WIFI_RETRY_DELAY = 1500;               // jeda antar retry (ms)

const char* FIRMWARE_VERSION   = "v1.0.9";         // ← ganti tiap mau update

const char* GITHUB_USER        = "kleponijo";   // ← isi username GitHub kamu
const char* GITHUB_REPO        = "kecepatan_angin";  // ← isi nama repo GitHub kamu
const unsigned long OTA_CHECK_INTERVAL = 3600000UL; // cek tiap 1 jam (ms)
