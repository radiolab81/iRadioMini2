# iRadioMini2 v2

Neuaufbau von iRadioMini für **ESP-IDF 6.1** (Ziel: ESP32).

## Highlights
* Komponentenarchitektur (`irm_*`), Kommando-Queue + Status-Pub/Sub, eine Steuer-Task.
* Audio: ESP-GMF + esp_codec_dev, 10-Band-EQ, HTTPS mit Zertifikatsprüfung.
* Anzeigen: SSD1306, HD44780, Servo, LVGL (Skins basic/linear/round/cassette).
* Optional: AM-Träger (AD9835/AD9832/AD9850/LEDC) und inet2RF.
* Audio-Ausgabe auch ohne externen DAC: PDM (GPIO) und interner ESP32-DAC.
* Web-UI + REST-API, NVS-Persistenz, Reconnect-Backoff, Host-Tests der Parser.

## Verifikationsstand
Build unter ESP-IDF v6.1 (ESP32) für alle Anzeige-/Treiber-Varianten mit Component-Manager-Auflösung mit Registry, reale Hardware.

