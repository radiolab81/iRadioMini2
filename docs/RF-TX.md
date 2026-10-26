# AM-Sender und inet2RF (`irm_tx`)

> **Rechtlicher Hinweis:** Das Aussenden von Hochfrequenz unterliegt nationalen Vorschriften (Deutschland: TKG/EMVG, Frequenzzuteilung;
> nur extrem schwache, genehmigungsfreie Aussendungen bzw. leitungsgebundene/abgeschirmte Aufbauten). Verantwortung und Konformität
> liegen beim Betreiber. Die Komponente ist standardmäßig **aus**.

## Prinzip
Ein Träger (Mittelwelle, Standard 1000 kHz) wird von einem DDS-Baustein oder dem ESP32-LEDC erzeugt; das NF-Signal des Radios wird
extern aufmoduliert. Optional wählt **inet2RF** den Sender über die Skala eines Analogradios:
Die Oszillatorfrequenz des Radios wird per PCNT gemessen, daraus folgen Skalenposition → Sender (`IRM_CMD_GOTO`) → Trägerfrequenz.

## Treiber (`CONFIG_IRM_TX_*`)
| Treiber | Takt | Pins (A / B / C / D) |
|---|---|---|
| AD9835 | 50 MHz | FSYNC / SCLK / SDATA |
| AD9832 | 25 MHz | FSYNC / SCLK / SDATA |
| AD9850 | 125 MHz | RESET / FQ_UD / W_CLK / DATA |
| ESP32-LEDC | – | Ausgang (nur ESP32 Classic, High-Speed-Timer) |

Alle Treiber implementieren dieselbe Schnittstelle (`irm_tx_driver_t`); es wird genau einer per Kconfig eingebunden. Der Referenztakt ist einstellbar.
Trägerfrequenz: `/sdcard/AM.txt` (Zahl in kHz) > Kconfig.

## inet2RF
Messung zeitbasiert (100-ms-Takt, Oszillator in kHz), Skalenposition muss 3 Messungen (300 ms) stabil sein, bevor umgeschaltet wird.
`f_Empfang = f_Oszillator − ZF` (Kconfig *Zwischenfrequenz*, z. B. 455 kHz; das Altprojekt **addierte** die ZF – bei Standardwert 0 ohne Wirkung).
Senderplätze liegen gleichmäßig zwischen unterer und oberer Bandgrenze (526…1606 kHz); der Träger folgt dem gewählten Platz.
Eingang: 3,3-V-Signal, Pegel ggf. über Spannungsteiler/Komparator anpassen.

## Änderungen gegenüber dem Altprojekt
Neue PCNT-API (Legacy-ISR-Zählung entfällt; Überlauf-Akkumulation im Treiber), Treiber nicht mehr per Symbolkollision, Bit-Schieben als Schleife,
pro Treiber konfigurierbare Pins/Takte statt `#define`, Kommando statt Queue-Zeiger an den Player, AD9850-Zeitverhalten ohne `vTaskDelay` je Taktflanke.
Nicht hardwaregetestet.
