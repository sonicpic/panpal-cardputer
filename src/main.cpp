// M5Stack Cardputer ADV — Milestone 0: Hello World + hardware self-check
// Prints chip / PSRAM / flash / heap info to both the LCD and USB serial,
// and echoes keyboard input. Validates the compile -> flash -> run -> serial loop.

#include <M5Cardputer.h>

static void printSelfCheck() {
  Serial.println();
  Serial.println("========== Cardputer ADV self-check ==========");
  Serial.printf("Chip model    : %s\n", ESP.getChipModel());
  Serial.printf("Chip revision : %d\n", ESP.getChipRevision());
  Serial.printf("CPU cores     : %d\n", ESP.getChipCores());
  Serial.printf("CPU freq      : %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash size    : %u bytes (%.1f MB)\n",
                ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1048576.0);
  Serial.printf("PSRAM size    : %u bytes (%.2f MB)\n",
                ESP.getPsramSize(), ESP.getPsramSize() / 1048576.0);
  Serial.printf("Free PSRAM     : %u bytes\n", ESP.getFreePsram());
  Serial.printf("Free heap     : %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Sketch used   : %u bytes\n", ESP.getSketchSize());
  Serial.printf("Sketch free   : %u bytes\n", ESP.getFreeSketchSpace());
  Serial.println("==============================================");
  Serial.println("(no PSRAM on Cardputer ADV — 512KB SRAM only, confirmed vs official specs)");
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);  // true = enable the built-in keyboard

  Serial.begin(115200);
  delay(300);
  printSelfCheck();

  auto& d = M5Cardputer.Display;
  d.setRotation(1);
  d.fillScreen(TFT_BLACK);

  d.setTextSize(2);
  d.setTextColor(TFT_GREEN, TFT_BLACK);
  d.setCursor(6, 6);
  d.println("Hello Cardputer");

  d.setTextSize(1);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(6, 32);  d.printf("Chip : %s r%d", ESP.getChipModel(), ESP.getChipRevision());
  d.setCursor(6, 44);  d.printf("Flash: %.1f MB", ESP.getFlashChipSize() / 1048576.0);
  d.setCursor(6, 56);  d.printf("PSRAM: %.2f MB", ESP.getPsramSize() / 1048576.0);
  d.setCursor(6, 68);  d.printf("Heap : %u B free", ESP.getFreeHeap());

  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setCursor(6, 88);  d.println("Type on keyboard:");
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(6, 102);
}

void loop() {
  M5Cardputer.update();

  static uint32_t last = 0;
  if (millis() - last > 2000) {
    last = millis();
    Serial.printf("[tick] heap=%u  free_psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
  }

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();
    for (char c : st.word) {
      Serial.printf("[key] %c\n", c);
      M5Cardputer.Display.print(c);
    }
    if (st.enter) { Serial.println("[key] ENTER"); M5Cardputer.Display.println(); }
    if (st.del)   { Serial.println("[key] DEL"); }
  }
}
