# dec406 — Décodeur COSPAS-SARSAT 406 MHz

Décodeur pour les balises d’urgence COSPAS-SARSAT de première génération
(FGB) et de seconde génération (SGB) à 406 MHz.

**Branche** : `main`

---

## Ce qui fonctionne

### Décodeurs
- **1G (FGB)** : T.001 biphase-L PSK ±1.1 rad, 400 bps, tous les protocoles
  Location/User — `dec406_v1g.c`
- **2G (SGB)** : BCH(250,202) avec correction complète Berlekamp-Massey +
  Chien, tous les champs T.018 — `dec406_v2g.c`

### Démodulateurs
- **DSSS OQPSK (2G)** — chaîne plate dans `dsss_demod.c` :
  blocage DC → acquisition fréquentielle par FFT-corrélation → wipeoff NCO →
  retard OQPSK → décimation boxcar multi-offset → despread avec estimation
  fréquence/phase par ajustement linéaire du préambule + PLL Costas par bit →
  BCH.
- **FGB IQ-direct (1G)** — `fgb_iq_demod.c` : décodeur baseband complexe
  BPSK biphase-L sans détour FM/audio. Détection CW en grille double,
  recherche Costas multi-phase, slicer Manchester, correction BCH1 brute
  force (t=3), CRC.

### Scanner temps réel
`dec406_scan` est un scanner temps réel unifié avec sélection de backend SDR
forcée ou automatique selon l’usage. Les backends disponibles sont
Airspy Mini, RTL-SDR, PlutoSDR et HackRF. Avec RTL-SDR, la capture est
asynchrone via `rtlsdr_read_async()` afin d’éviter les sous-alimentations
silencieuses qui corrompaient les bursts longs.

Pour les services, on peut forcer le backend avec :

```bash
DEC406_BACKEND=rtl|airspy|pluto|hackrf
```

### État de validation

Les taux dépendent fortement de la propagation, de l’heure, du bruit local et
des balises CNES actives. Suivre séparément :

| Métrique | Définition |
|----------|------------|
| Acquisition/sync SGB | `(BCH OK + FRAME REJECTED) / bursts SGB détectés` |
| Pureté du décodeur SGB | `BCH OK / (BCH OK + FRAME REJECTED)` |
| SGB bout en bout | `BCH OK / bursts SGB détectés` |

Après la correction RTL asynchrone, les fixes fredzo et la recherche ±16 kHz,
les SGB qui synchronisent valident le BCH proprement. Sur les logs firmin de
2026-07-04, les bursts de calibration récupérés avaient un résiduel de l’ordre
de +8.6 à +11.1 kHz, hors de l’ancienne fenêtre ±8 kHz.

---

## Compilation

```bash
make build/dec406_iq
make build/dec406_scan
make
```

Binaries produits dans `build/` :

| Binaire | Rôle |
|---------|------|
| `dec406_iq` | Démodulateur SGB DSSS/OQPSK depuis un fichier IQ |
| `dec406_hex` | Décodeur 1G/2G depuis une chaîne hexadécimale |
| `dec406_audio` | Décodeur 1G depuis un WAV (pipeline FM historique) |
| `dec406_scan` | Scanner de bandes FGB+SGB en temps réel |
| `dec406_dsss_test` | Driver de tests unitaires pour le DSSS |
| `generate_2g_hex` | Générateur de trames de test 2G |
| `reset_usb` | Utilitaire de reset USB |

---

## Utilisation

### Fichiers IQ

```bash
./build/dec406_iq signal.iq -s 2457600
./build/dec406_iq recording.sigmf-data -s 2457600 -I
./build/dec406_iq recording.iq -s 2457600 -u
```

Formats supportés : float32 complex par défaut, `-u` pour RTL-SDR uint8,
`-i` pour int16 entrelacé, `-I` pour int32 SDRangel ci32_le.

### Trames hexadécimales

```bash
./build/dec406_hex 09C4745638D95999A02B33326C3EC4400003FFF00C02832000002B774C24FE4
./build/dec406_hex 8E3301E2402B002BBA863609670908
```

### Scanner temps réel

```bash
./build/dec406_scan 406.0M 406.1M
./build/dec406_scan 406.0M 406.1M 0 30
```

Le scanner lit à 2.4576 Msps, détecte les bursts sur un spectrogramme de
puissance, les classe par largeur de bande (`BW_SPLIT_HZ = 20 kHz` ; les
bursts plus larges sont des SGB), puis lance le bon décodeur.

Diagnostics utiles :

```bash
RTL_DIAG=1 ./build/dec406_scan 406.0M 406.1M
DSSS_DIAG=1 ./build/dec406_scan 406.0M 406.1M
DUMP_FAIL=1 ./build/dec406_scan 406.0M 406.1M
make build/sgb_epl_diag
```

`DUMP_FAIL=1` écrit les bursts SGB ratés dans `burst_sgb_*.cf32` pour
analyse hors ligne. `ACQ_BANDPASS_HZ` reste un filtre de diagnostic pour
l’acquisition uniquement ; sa valeur par défaut est `0`.

### Service 1544 MHz

`systemd/scan1544.service` est l’unité dédiée à la descente 1544 MHz. Elle
force le backend, active le mode d’alerte géographique et remplace la
whitelist 406 MHz par une zone configurée :

```ini
Environment=DEC406_BACKEND=hackrf
Environment=DEC406_ALERT_MODE=downlink
Environment=DEC406_ALERT_CENTER=43.0,1.5
Environment=DEC406_ALERT_RADIUS_KM=80
```

Si la station utilise le Pluto au lieu du HackRF, seul `DEC406_BACKEND`
change. En mode downlink, un mail n’est envoyé que si la position décodée est
valide et dans le rayon configuré.

### Alertes mail

Si `data/config_mail.txt` existe, le scanner envoie un mail quand une balise
décode sur un canal de détresse T.012. Le binaire `sendemail` doit être
installé.

Au démarrage, le bandeau affiche les informations utiles :

```text
alerts  : enabled
mail smtp : smtp.gmail.com:587
mail user : xxxx@gmail.com
mail to   : a@b.fr,c@d.fr
```

Sans ce fichier, le scanner fonctionne normalement et n’envoie aucun mail.

---

## Chaîne 2G

```
IQ @ 2.4576 MHz
  → blocage DC (IIR α=0.001)
  → décimation boxcar vers le chip rate
  → acquisition fréquence par FFT-corrélation (±16 kHz)
  → wipeoff NCO
  → retard OQPSK
  → décimation boxcar multi-offset
  → despread avec estimation fréquence/phase du préambule
  → PLL Costas par bit
  → 250 bits → BCH → `decode_beacon`
```

---

## Chaîne 1G

```
IQ
  → décimation moyenne mobile multi-étages vers ~9.6 kHz
  → estimation fréquence porteuse du préambule CW
  → wipeoff fréquence
  → détection de fin CW en double grille
  → recherche Costas multi-phase
  → slicer Manchester
  → correction BCH1 brute force
  → 144 bits → CRC1/CRC2
```

Pas de démodulation FM, pas de détour par l’audio.

---

## Arborescence

```text
src/         sources C
include/     en-têtes
build/       binaires compilés
tests/       tests unitaires
utils/       outils de diagnostic hors ligne
scripts/     scripts d’analyse
docs/        documentation et notes de déploiement
data/        données runtime
```

---

## Documentation

- `CHANGELOG.md` — historique des versions
- `CLAUDE.md` — conventions du projet
- `docs/ARCHITECTURE_dec406.md` — architecture détaillée
- `docs/TESTS_VALIDATION.md` — procédures de validation

---

## Licence

Ce projet est distribué sous licence MIT. Voir `LICENSE`.

