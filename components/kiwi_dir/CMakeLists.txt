# HINWEIS iRadioMini2: kiwi_dir_cmd.c (Konsolen-Befehlsparser des Originalprojekts) ist hier NICHT Teil des Builds - ungenutzt, und GCC meldet dort
# -Wformat-truncation (unter IDF mit -Werror ein Buildfehler). Die Datei bleibt zum Vergleich mit dem Original unveraendert im Verzeichnis.
# Portabler Verzeichnisdienst-Client (KiwiSDR-Serverliste) als ESP-IDF-Komponente.
# Dieselben Quelldateien baut auch das PC-Makefile (pc/Makefile).
# Nutzt nur die Portschicht (kiwi_port.h) von kiwi_core -> keine eigene Netzwerk-API.
idf_component_register(
    SRCS "src/kiwi_dir_db.c"
         "src/kiwi_dir_geo.c"
         "src/kiwi_dir_geo_data.c"
         "src/kiwi_dir_parse.c"
         "src/kiwi_dir_http.c"
         "src/kiwi_dir_update.c"
    INCLUDE_DIRS "include"
    REQUIRES kiwi_core
)
