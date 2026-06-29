// ═══════════════════════════════════════════════════════════════
//  kecepatan_angin.ino  —  Sistem Anemometer + WiFi Manager
// ═══════════════════════════════════════════════════════════════

#include "cfg_config.h"
#include "wifi_manager_updated.h"
#include "fb_firebase_helper.h"
#include "ota_github.h"

FirebaseData   fbdo;
FirebaseAuth   fbAuth;
FirebaseConfig fbConfig;

// ── Settings dari Firebase ───────────────
SensorSettings gSettings;
unsigned long  lastSettingsSync   = 0;
const unsigned long SETTINGS_SYNC = 300000UL;

volatile int pulseRealtime = 0;
volatile int pulseAvg      = 0;
volatile int pulseHistory  = 0;
time_t        lastHistoryEpoch = 0;

void IRAM_ATTR hitungPulsa() {
  pulseRealtime++;
  pulseAvg++;
  pulseHistory++;
}

// ── Timer untuk tiap interval ─────────────────────────────────
unsigned long lastRealtime = 0;
unsigned long lastAvg      = 0;
unsigned long lastHistory  = 0;

// ── Akumulator history ────────────────────────────────────────
// Diisi oleh avg interval, direset saat history terkirim.
// Menyimpan informasi untuk field avg & max di /history.
float totalAvgWindweg = 0.0f; // total windweg dari semua avg-interval
float maxAvgWindweg   = 0.0f; // nilai avg-interval tertinggi
int   avgSampleCount  = 0;    // berapa kali avg-interval telah terjadi

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
    gSettings = fetchSettings(fbdo);

    String bootMsg = "Boot OK | FW=" + String(FIRMWARE_VERSION)
                   + " | RT=" + String(gSettings.intervalRealtime / 1000) + "s"
                   + " | AVG=" + String(gSettings.intervalAverage / 1000) + "s"
                   + " | HIST=" + String(gSettings.intervalHistory / 60000) + "m";
    sendLog(fbdo, bootMsg);

    checkAndUpdateOTA(fbdo); 
  } else {
    Serial.println("[Main] Mode AP aktif — Firebase dilewati.");
    Serial.printf( "[Main] Sambungkan HP ke hotspot \"%s\"\n", AP_SSID);
  }

  attachInterrupt(digitalPinToInterrupt(PIN_HALL), hitungPulsa, FALLING);
  lastRealtime = lastAvg = lastHistory = millis();

  Serial.println("[Main] Setup selesai. Sistem aktif.\n");
  Serial.printf( "[Main] PULSE_PER_KM = %d (%.4f km/pulsa)\n\n",
                 PULSE_PER_KM, 1.0f / PULSE_PER_KM);
}

void loop() {
  wifiManagerLoop();

  static unsigned long lastOtaCheck = 0;
  static unsigned long lastCmdCheck = 0;
  const  unsigned long CMD_CHECK_INTERVAL = 5000UL; // cek tiap 5 detik

if (wifiJustReconnected() && wifiIsConnected()) {
    Serial.println("[Main] WiFi reconnect dari AP mode → reinit Firebase...");
    setupFirebase(fbdo, fbAuth, fbConfig);
    gSettings = fetchSettings(fbdo);
    checkAndUpdateOTA(fbdo);
    lastOtaCheck     = millis();
    lastSettingsSync = millis();
    sendLog(fbdo, "WiFi reconnect, Firebase reinit OK | FW=" + String(FIRMWARE_VERSION));
    return;
  }

  // ── OTA check ─────────────────────────────────────────────────
  if (wifiIsConnected() && millis() - lastOtaCheck >= OTA_CHECK_INTERVAL) {
    lastOtaCheck = millis();
    checkAndUpdateOTA(fbdo);
  }

  // ── Remote command check ──────────────────────────────────────
  if (wifiIsConnected() && millis() - lastCmdCheck >= CMD_CHECK_INTERVAL) {
  lastCmdCheck = millis();
  checkRemoteCommand(fbdo);
  }

  // ── Sync settings dari Firebase tiap 5 menit ───────────────
  if (wifiIsConnected() && millis() - lastSettingsSync >= SETTINGS_SYNC) {
    lastSettingsSync = millis();
    gSettings = fetchSettings(fbdo);
  }

  // Tidak ada yang dikirim kalau WiFi putus —
  // pulsa tetap akumulasi di ketiga counter.
  if (!wifiIsConnected()) return;

  // Snapshot waktu sekali per iterasi loop untuk konsistensi
  unsigned long now = millis();

  // ════════════════════════════════════════════════════════════
  //  REALTIME
  //  Windweg interval ini = pulseRealtime / PULSE_PER_KM
  //  Counter direset setelah setiap interval.
  //  Dikirim ke /realtime (updateNode → selalu ditimpa).
  // ════════════════════════════════════════════════════════════
  if (now - lastRealtime >= gSettings.intervalRealtime) {
    lastRealtime = now;

    noInterrupts();
    int p = pulseRealtime;
    pulseRealtime = 0;
    interrupts();

    float windwegKm = (float)p / PULSE_PER_KM;

    Serial.printf("[Realtime] Pulsa: %d | Windweg: %.4f km | Interval: %lu ms\n",
                  p, windwegKm, gSettings.intervalRealtime);

    sendRealtime(fbdo, windwegKm, p, gSettings, fbConfig);
  }

  // ════════════════════════════════════════════════════════════
  //  AVERAGE  (default: setiap 1 menit)
  //  Windweg interval ini = pulseAvg / PULSE_PER_KM
  //  Counter direset setelah setiap interval.
  //
  //  Nilai ini juga diakumulasi ke totalAvgWindweg & maxAvgWindweg
  //  untuk dipakai saat history terkirim.
  //
  //  Dikirim ke /average (updateNode → selalu ditimpa).
  //  sample_number menunjukkan urutan sample sejak history terakhir.
  // ════════════════════════════════════════════════════════════
  if (now - lastAvg >= gSettings.intervalAverage) {
    lastAvg = now;

    noInterrupts();
    int p    = pulseAvg;
    pulseAvg = 0;
    interrupts();

    float windwegKm = (float)p / PULSE_PER_KM;

    // Akumulasi untuk history
    totalAvgWindweg += windwegKm;
    avgSampleCount++;
    if (windwegKm > maxAvgWindweg) maxAvgWindweg = windwegKm;

    Serial.printf("[Avg] Pulsa: %d | Windweg: %.4f km | Sample: %d\n",
                  p, windwegKm, avgSampleCount);

    sendAverage(fbdo, windwegKm, p, avgSampleCount, gSettings, fbConfig);
  }

// ════════════════════════════════════════════════════════════════
//  HISTORY  (default: setiap 1 jam, berbasis waktu kalender)
//  Trigger saat epoch sekarang sudah melewati kelipatan interval
//  dihitung dari epoch 0 — sehingga selalu snap ke grid kalender.
//
//  Contoh intervalHistory = 3600000 ms (1 jam):
//    trigger di 00:00, 01:00, 02:00, dst — bukan relatif ke boot
//  Contoh intervalHistory = 1800000 ms (30 menit):
//    trigger di 00:00, 00:30, 01:00, dst
// ════════════════════════════════════════════════════════════════
  time_t epochNow = time(NULL);
  unsigned long intervalHistorySec = gSettings.intervalHistory / 1000UL;

  // Slot kalender saat ini (misal: epoch dibagi 3600 → slot jam ke-N)
  time_t currentSlot = (epochNow / intervalHistorySec) * intervalHistorySec;

  if (currentSlot > lastHistoryEpoch) {
    lastHistoryEpoch = currentSlot;

    noInterrupts();
    int pH       = pulseHistory;
    pulseHistory = 0;
    interrupts();

    float totalKm = (float)pH / PULSE_PER_KM;
    float avgKm   = (avgSampleCount > 0)
                      ? totalAvgWindweg / avgSampleCount
                      : 0.0f;

    Serial.printf("[History] Total: %.4f km | Avg/avg-interval: %.4f km | "
                  "Max: %.4f km | Pulsa: %d | Sample: %d\n",
                  totalKm, avgKm, maxAvgWindweg, pH, avgSampleCount);

    sendHistory(fbdo, totalKm, avgKm, maxAvgWindweg,
                pH, avgSampleCount, gSettings, fbConfig);

    // Reset akumulator history untuk periode berikutnya
    totalAvgWindweg = 0.0f;
    maxAvgWindweg   = 0.0f;
    avgSampleCount  = 0;
  }  

}
