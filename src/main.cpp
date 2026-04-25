#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(3000); // Ждём инициализацию USB на S3
  Serial.println("=== ESP32-S3 N16R8 TEST ===");
  Serial.printf("Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("PSRAM: %d bytes\n", ESP.getPsramSize());
  Serial.println("✅ ALIVE!");
}

void loop() {
  Serial.print(".");
  delay(1000);
}
