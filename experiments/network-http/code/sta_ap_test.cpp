#if defined(STA_AP_TEST)

#include <WiFi.h>

static bool scanInProgress = false;
static unsigned long lastScanKick = 0;
static const unsigned long SCAN_INTERVAL_MS = 30000;

void kickOffAsyncScan() {
  int s = WiFi.scanComplete();
  if (s == WIFI_SCAN_RUNNING) {
    return;
  }

  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  scanInProgress = true;
  lastScanKick = millis();
  Serial.println("Started async scan");
}

void handleAsyncScanResults() {
  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  if (n == WIFI_SCAN_FAILED || n == WIFI_SCAN_FAILED) {
    scanInProgress = false;
    return;
  }
  if (n < 0) {
    scanInProgress = false;
    return;
  }

  Serial.printf("Scan done: %d networks\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("  %2d) %s  RSSI:%d  CH:%d  %s\n",
                  i + 1,
                  WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i),
                  WiFi.channel(i),
                  (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured");
  }

  WiFi.scanDelete();
  scanInProgress = false;
}

void setup() {
  Serial.begin(115200);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, true);

  WiFi.mode(WIFI_AP_STA);

  WiFi.softAP("ESP32-S3-Setup", "password123");
  Serial.println("AP started");

  kickOffAsyncScan();
}

void loop() {
  if (!scanInProgress && (millis() - lastScanKick) > SCAN_INTERVAL_MS) {
    kickOffAsyncScan();
  }

  handleAsyncScanResults();
}

#endif // STA_AP_TEST
