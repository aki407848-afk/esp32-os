void setup() {
  Serial.begin(115200);
  Serial.println("HELLO WORLD");
}

void loop() {
  delay(1000);
  Serial.println("Still alive...");
}
