Syntax-/Typpruefung von irm_web.c ohne ESP-IDF (nur Compiler, kein Linken/Ausfuehren):
  C=../../..; gcc -std=gnu2x -Os -Wall -Wextra -Werror -fsyntax-only -I. -I$C/irm_kiwi/test/host/shim -I$C/irm_web/include -I$C/irm_control/include \
     -I$C/irm_core/include -I$C/irm_formats/include -I$C/irm_dir/include -I$C/kiwi_dir/include -I$C/kiwi_core/include -I$C/irm_out/include -I/pfad/zu/cJSON ../../src/irm_web.c
Hinweis: der Stub bildet die Signaturen nach (ESP-IDF 5.x/6.x); ein echter Build ersetzt diese Pruefung.
