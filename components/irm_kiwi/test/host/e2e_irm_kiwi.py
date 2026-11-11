#!/usr/bin/env python3
"""Host-Ende-zu-Ende-Test fuer irm_kiwi: startet den Mock-KiwiSDR aus dem ESP32_KiwiSDR-Projekt (pc/mock_kiwi.py), laesst das echte
irm_kiwi.c (mit POSIX-Shim) dagegen laufen und analysiert das an der Fake-Senke ausgegebene 48-kHz-Stereo-Audio.

Aufruf:  python3 e2e_irm_kiwi.py <Pfad zu mock_kiwi.py> <Pfad zum gebauten test_irm_kiwi_host>   (siehe README, Abschnitt Tests)"""
import math, os, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(sys.argv[1])))
from mock_kiwi import MockKiwi

fails = 0
def check(c, msg):
    global fails
    print(("  ok   " if c else "  FEHLER ") + msg)
    if not c: fails += 1

def goertzel(x, fs, f):
    w = 2 * math.pi * f / fs; c = 2 * math.cos(w); s1 = s2 = 0.0
    for v in x: s1, s2 = v + c * s1 - s2, s1
    return math.sqrt(max(s1 * s1 + s2 * s2 - c * s1 * s2, 0)) / (len(x) / 2)

a = MockKiwi(tone_hz=1000.0); b = MockKiwi(tone_hz=1000.0)
a.start(); b.start()
dead = MockKiwi(); port_dead = dead.port; dead.sock.close()      # Port ohne Gegenstelle
dump = os.path.join(tempfile.gettempdir(), "irm_kiwi_dump.raw")
p = subprocess.run([sys.argv[2], str(a.port), str(b.port), str(port_dead), dump], capture_output=True, text=True, timeout=120)
print(p.stdout)
if p.stderr.strip(): print(p.stderr)
a.join(timeout=5); b.join(timeout=5)
print("== Mock-Seite (Kiwi A)")
c = a.commands
check(p.returncode == 0, "C-Test Exit-Code 0")
check("SET mod=am low_cut=-4900 high_cut=4900 freq=225.000" in c, "Start: AM 225.000 kHz")
check(any(x.startswith("SET mod=usb") and "freq=7055.500" in x for x in c), "tune: USB 7055.500 kHz angekommen")
check(any("freq=7056.500" in x for x in c), "step +1 kHz angekommen")
check(any(x.startswith("SET mod=usn") for x in c), "naechster Modus (usn) angekommen")
check(any(x.startswith("SET mod=usb") and "freq=14150.000" in x for x in c), "schnelle Folge: Endzustand 14150 kHz USB angekommen (zusammengefasst)")
check(a.got_close and a.closed_cleanly, "Kiwi A: Close-Frame und sauberes Schliessen bei stop")
check(c[0].startswith("SET auth t=kiwi") and "SERVER DE CLIENT" in c[1], "Anmeldung")
check(any("irm-host-test" in x for x in c), "Benutzername (IRM_KIWI_USER_NAME) wird gesendet")

print("== Audio an der Senke (48 kHz Stereo)")
raw = open(dump, "rb").read()
n = len(raw) // 4
s = struct.unpack("<%dh" % (n * 2), raw[:n * 4])
L, R = s[0::2], s[1::2]
check(n > 48000 * 5, "%.1f s Audio aufgezeichnet" % (n / 48000))
check(L == R, "beide Kanaele identisch (Mono -> Stereo)")
# Ton suchen: erste Sekunde ist Vorpufferung (Stille), dann 1-kHz-Ton
seg_start = None
for i in range(0, n - 4800, 2400):
    if max(abs(v) for v in L[i:i + 2400]) > 2000: seg_start = i; break
check(seg_start is not None and 0.3 < seg_start / 48000 < 1.5, "Stille bis ca. %.2f s (Vorpufferung), dann Ton" % ((seg_start or 0) / 48000))
seg = L[seg_start + 24000: seg_start + 24000 + 48000]       # 1 s, nach dem Einschwingen
amp = goertzel(seg, 48000, 1000.0); off = max(goertzel(seg, 48000, f) for f in (800.0, 1200.0, 2000.0, 11000.0, 13000.0))
peak = max(abs(v) for v in seg)
check(abs(amp - 8000 * 10 ** (6 / 20)) / (8000 * 10 ** (6 / 20)) < 0.04, "1000 Hz: Amplitude %.0f (Soll %.0f = 8000 x 6 dB Kiwi-Gain)" % (amp, 8000 * 10 ** (6 / 20)))
check(off < amp / 100, "Nachbarfrequenzen/Spiegelfrequenzen > 40 dB unter dem Ton (%.0f vs %.0f)" % (off, amp))
check(peak < 32000, "keine Uebersteuerung (Spitze %d)" % peak)
# Frequenzgenauigkeit: Nulldurchgaenge ueber 1 s
zc = sum(1 for i in range(1, len(seg)) if seg[i - 1] < 0 <= seg[i])
check(abs(zc - 1000) <= 2, "Frequenz %d Hz (Nulldurchgaenge/s)" % zc)
# keine Aussetzer: Huellkurve in 10-ms-Bloecken ueber den ganzen Tonbereich
blocks = [max(abs(v) for v in L[i:i + 480]) for i in range(seg_start + 24000, min(n, seg_start + 24000 + 48000 * 4) - 480, 480)]
check(min(blocks) > 0.8 * max(blocks), "keine Aussetzer/Pegeleinbrueche (kleinster Block %d, groesster %d)" % (min(blocks), max(blocks)))

print("\nERGEBNIS:", "OK" if fails == 0 else "%d FEHLER" % fails)
sys.exit(1 if fails else 0)
