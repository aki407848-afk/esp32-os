#include <Arduino.h>  // ← ОБЯЗАТЕЛЬНО для Serial, delay и т.д.

void setup() {
  Serial.begin(115200);
  // Ждем инициализацию USB на ESP32-S3
  delay(3000);
  Serial.println("\n=== TEST START ===");
  Serial.println("✅ Serial OK");
  Serial.printf("✅ Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("✅ PSRAM Size: %d bytes\n", ESP.getPsramSize());
}

void loop() {
  Serial.println("Still alive...");
  delay(1000);
}
