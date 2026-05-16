# Tests de validation

## 1. Tests unitaires (BCH, PRN, Message)

Fichier : `tests/test_sgb_codec.c`

94 tests portés depuis la référence `sgb-codec` (jbirby), vecteurs C/S T.018 Rev 7.

```bash
gcc -std=c11 -O2 -o tests/test_sgb_codec tests/test_sgb_codec.c -lm
./tests/test_sgb_codec
```

Couvre :
- **BCH(250,202)** : polynôme générateur, vecteur Appendix B.1, correction 1-6 erreurs, rejet 7 erreurs
- **PRN LFSR** : 4 seeds Table 2.2, segments 38 400 chips, période 2^23-1
- **Message** : round-trip Appendix B.1, GNSS Appendix C, rotating fields, vessel ID, 23-hex ID

---

## 2. Validation inter-codebase

Génération du signal par le modulateur de référence (jbirby), démodulation par `dec406_iq`.

```bash
cd sgb-codec/scripts
python3 -c "
from sgb_message import SGBMessage
from sgb_bch import bch_encode
from sgb_modulation import ModulationParams, modulate
import numpy as np

msg = SGBMessage(tac=9999, serial=13398, country=227, homing=0,
                 rls_function=1, test_protocol=1, lat_deg=None, lon_deg=None,
                 vessel_id_type=1, vessel_id_params={'mmsi':227006600},
                 beacon_type=1).build()
cw = msg + bch_encode(msg)

params = ModulationParams(sample_rate=192000, pulse='rect', mode='normal')
sig = modulate(cw, params)

# Resample 192 kHz → 2.4576 MHz (linear)
from scipy import signal
iq_2457k = signal.resample_poly(sig, 64, 5)

out = np.empty(2*len(iq_2457k), dtype=np.float32)
out[0::2] = iq_2457k.real.astype(np.float32)
out[1::2] = iq_2457k.imag.astype(np.float32)
out.tofile('/tmp/ref_2457k.cf32')
"

# Démoduler
./build/dec406_iq /tmp/ref_2457k.cf32
```

Résultat attendu : TAC=9999, Country=227, MMSI=227006600, BCH OK.

---

## 3. Robustesse AWGN

Ajout de bruit gaussien au signal de référence pour déterminer le SNR minimal
de démodulation.

```bash
cd sgb-codec/scripts && PYTHONPATH=. python3 -c "
import numpy as np, subprocess
from sgb_message import SGBMessage
from sgb_bch import bch_encode
from sgb_modulation import ModulationParams, modulate

DEMOD = '../dec406_V10.2/build/dec406_iq'
main = SGBMessage(tac=9999, serial=13398, country=227, homing=0, rls_function=1,
                  test_protocol=1, lat_deg=None, lon_deg=None,
                  vessel_id_type=1, vessel_id_params={'mmsi':227006600},
                  beacon_type=1).build()
EXPECTED = [int(c) for c in main + bch_encode(main)]

params = ModulationParams(sample_rate=192000, pulse='rect', carrier_hz=0, mode='normal')
sig = modulate(main + bch_encode(main), params)
sp = np.mean(np.abs(sig)**2)
rng = np.random.RandomState(42)

for snr in [10, 8, 6, 5, 4, 3.5, 3, 2.5, 2, 1.5, 1]:
    npow = sp / (10**(snr/10))
    noise = np.sqrt(npow/2)*(rng.randn(len(sig)) + 1j*rng.randn(len(sig)))
    iq = sig + noise.astype(np.complex64)

    fs_in, fs_out = 192000, 2457600
    nout = int(len(iq) * fs_out / fs_in)
    t_in = np.arange(len(iq))/fs_in
    t_out = np.arange(nout)/fs_out
    real = np.interp(t_out, t_in, iq.real)
    imag = np.interp(t_out, t_in, iq.imag)
    out = np.empty(2*nout, dtype=np.float32)
    out[0::2] = np.float32(real); out[1::2] = np.float32(imag)
    out.tofile('/tmp/awgn.cf32')

    r = subprocess.run([DEMOD, '/tmp/awgn.cf32'], capture_output=True, text=True, timeout=30)
    txt = r.stdout + r.stderr
    for line in txt.split('\n'):
        s = line.strip()
        if len(s)==63 and all(c in '0123456789ABCDEF' for c in s):
            bits = []
            for c in s:
                v=int(c,16)
                for b in range(3,-1,-1): bits.append((v>>b)&1)
            bits = bits[2:252]
            err = sum(1 for i in range(250) if bits[i]!=EXPECTED[i])
            bch_ok = 'BCH' in txt and 'Errors detected' not in txt
            print(f'SNR {snr:4.1f} dB: {err:3d}/250 bit errors  BCH={\"OK\" if bch_ok else \"FAIL\"}')
            break
    else:
        print(f'SNR {snr:4.1f} dB: FAIL — preamble not found')
"
```

### Résultats (Oct 2024)

| SNR (dB) | Bit errors | BCH  | Statut |
|----------|:----------:|:----:|--------|
| ≥ 2.5    | 0/250      | OK   | ✅ Parfait |
| 2.0      | 90/250     | FAIL | ❌ Effondrement |
| 1.5      | 155/250    | FAIL | ❌ ~50% (aléatoire) |
| ≤ 1.0    | —          | FAIL | ❌ Perte de sync |

Le seuil est à 2.5 dB. En dessous, le Costas loop / despreader décroche
brutalement — le gain d'étalement (256 chips/bit = 24 dB) tient jusqu'à
cette limite puis s'effondre. Le BCH (6 erreurs max) n'est jamais sollicité
en zone de transition : soit 0 erreur, soit >90 erreurs.

Cette courbe en « waterfall » est caractéristique des systèmes DSSS.

---

## 4. Rejet des trames invalides

Fichier : `tests/test_bch_reject.c`

Vérifie que `decode_2g()` n'affiche jamais de balise à partir d'un mot de code
que le BCH ne peut pas corriger.

```bash
cc -Iinclude -O2 tests/test_bch_reject.c src/dec406_v2g.c src/display_utils.c \
   -lm -o /tmp/test_bch_reject
/tmp/test_bch_reject
```

| Entrée | Résultat attendu |
|--------|------------------|
| Mot de code propre (0 erreur) | balise affichée |
| 3 erreurs (≤ t=6) | « BCH: 3 errors corrected », balise affichée |
| 32 erreurs (≫ t=6) | « FRAME REJECTED », aucune balise |

---

## 5. Validation OTA (Pluto → RTL-SDR / SDRangel)

### Test de régression synthétique (obligatoire avant commit)

```bash
make build/dec406_iq
./build/dec406_iq <fichier_synthetique>.sigmf-data -s 2457600
```

Résultat attendu : décodage bit-perfect, z-score préambule ≈ 240, BCH 0 erreur.

### Décodage OTA

```bash
# SDRangel ci32_le
./build/dec406_iq <enregistrement>.sigmf-data -s 2457600 -I
```

Résultat attendu : TAC / Serial / Country / Position corrects, BCH-propre.
z-score des bursts complets : 33 à 300+.

### Courbe de sensibilité (antennes à ~2.5 m)

| Gain TX Pluto | meilleur z | Résultat |
|---------------|:----------:|----------|
| -20 dB | 64 | décode |
| -30 dB | 62 | décode |
| -35 dB | 6 | bruit — rejeté |
| -40 dB | 6 | bruit — rejeté |

Le DSSS décroche en tout-ou-rien : un burst complet corrèle à z ≥ 33 ou
s'effondre dans le bruit (z ≤ 7) — rien dans l'intervalle (16, 33). Le seuil
de sync `DESPREAD_SYNC_THRESHOLD` est fixé à 20, au centre de cet intervalle
vide. En dessous du seuil, ou si le BCH ne corrige pas, la trame est rejetée :
le décodeur n'imprime jamais de balise fabriquée à partir de bruit.
