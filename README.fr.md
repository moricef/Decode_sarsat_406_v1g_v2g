# dec406 — Décodeur de balises COSPAS-SARSAT 406 MHz

Décodeur pour les balises de détresse COSPAS-SARSAT de 1ʳᵉ génération (FGB)
et de 2ᵉ génération (SGB) à 406 MHz.

**Branche** : `main`

---

## Ce qui fonctionne

### Décodeurs (exacts au bit près)
- **1G (FGB)** : T.001 biphase-L PSK ±1.1 rad, 400 bps, tous les protocoles
  Location/User — `dec406_v1g.c`
- **2G (SGB)** : BCH(250,202) avec correction d'erreurs complète
  Berlekamp-Massey + Chien, tous les champs T.018 — `dec406_v2g.c`

### Démodulateurs
- **DSSS OQPSK (2G)** — `dsss_demod.c`, chaîne plate (sans boucle de tracking
  au rythme d'échantillonnage) : blocage DC → acquisition fréquentielle par
  FFT-corrélation → wipeoff NCO → retard OQPSK → décimation boxcar
  multi-offset → despread avec estimation de fréquence par ajustement linéaire
  du préambule + PLL Costas par bit → BCH. L'oracle essaie 4 offsets boxcar ×
  4 phases Costas (16 combinaisons) avant d'abandonner. La recherche de
  décalage à l'acquisition est bornée au pré-roll du burst, pour éviter que des
  pics tardifs de bruit/données ne battent le vrai préambule. La recherche de
  fréquence par FFT-corrélation couvre ±16 kHz, ce qui absorbe les erreurs de
  centroïde du scanner observées sur de vrais bursts SGB du CNES.
- **FGB IQ-direct (1G)** — `fgb_iq_demod.c` : décodeur BPSK biphase-L en
  bande de base complexe, sans détour par une démodulation FM → audio.
  Détection de fin de CW en double grille, recherche Costas multi-phase
  (4 phases initiales × 13 offsets), slicer Manchester, correction d'erreurs
  BCH1 par force brute (t=3), CRC.

### Scanner temps réel
`dec406_scan` est un scanner temps réel unifié avec sélection automatique du
backend SDR pour un usage interactif (Airspy Mini, RTL-SDR, PlutoSDR, HackRF).
Le backend RTL-SDR utilise `rtlsdr_read_async()` avec de grands buffers ; le
chemin synchrone précédent pouvait sous-alimenter le scanner en silence et
corrompre les longs bursts FGB/SGB. Le scanner exécute un détecteur de bursts
spectral sur la bande de 100 kHz, classe chaque burst en FGB ou SGB selon sa
largeur de bande, et décode en conséquence. Pour les services, le backend peut
être forcé explicitement avec `DEC406_BACKEND=rtl|airspy|pluto|hackrf`, ce qui
supprime le sondage au démarrage.

### État de validation actuel

Les taux dépendent fortement de la propagation, de l'heure, du bruit local et
des balises système CNES actives. Suivre séparément trois indicateurs SGB :

| Métrique | Définition |
|----------|------------|
| Acquisition/sync SGB | `(BCH OK + FRAME REJECTED) / bursts SGB détectés` |
| Pureté du décodeur SGB | `BCH OK / (BCH OK + FRAME REJECTED)` |
| SGB bout en bout | `BCH OK / bursts SGB détectés` |

La validation récente, après la correction RTL asynchrone, les corrections SGB
de fredzo et la recherche d'acquisition ±16 kHz, ne montre plus d'échecs du
type « préambule qui synchronise fort puis données aléatoires » : les SGB qui
synchronisent valident le BCH proprement. Le 2026-07-04, le relais firmin a
récupéré des bursts SGB de calibration dont les décalages résiduels étaient de
l'ordre de +8,6 à +11,1 kHz, hors de l'ancienne fenêtre de recherche ±8 kHz ;
le même run affichait environ 93 % de réussite sur les créneaux de calibration
SGB de la grille 150 s. La validation locale RTL/Yagi est restée dans la même
plage, et le FGB s'est maintenu autour de sa classe préexistante de 90 %. À
considérer comme des contrôles terrain propres à un run, pas comme des taux
globaux figés.

---

## Compilation

```bash
make build/dec406_iq        # Décodeur SGB depuis un fichier IQ
make build/dec406_scan      # Scanner temps réel (nécessite librtlsdr-dev)
make                        # Toutes les cibles
```

Binaires produits dans `build/` :

| Binaire | Rôle |
|---------|------|
| `dec406_iq` | Démodulateur SGB DSSS/OQPSK depuis un fichier IQ |
| `dec406_hex` | Décodeur 1G/2G depuis une chaîne hexadécimale |
| `dec406_audio` | Décodeur 1G depuis un WAV (pipeline FM-demod historique) |
| `dec406_scan` | Scanner de bande FGB+SGB temps réel (Airspy/RTL-SDR/PlutoSDR) |
| `dec406_dsss_test` | Driver de tests unitaires du démodulateur DSSS |
| `generate_2g_hex` | Générateur de trames de test 2G |
| `reset_usb` | Utilitaire de reset de périphérique USB |

---

## Utilisation

### Fichiers IQ hors ligne

```bash
# SGB depuis un fichier IQ (float32 complex, par défaut)
./build/dec406_iq signal.iq -s 2457600

# Enregistrement SDRangel ci32_le (int32 complex little-endian)
./build/dec406_iq recording.sigmf-data -s 2457600 -I

# Enregistrement RTL-SDR uint8
./build/dec406_iq recording.iq -s 2457600 -u
```

Formats d'entrée : float32 complex (par défaut), `-u` RTL-SDR uint8, `-i`
int16 entrelacé, `-I` int32 SDRangel ci32_le.

### Trames hexadécimales

```bash
./build/dec406_hex 09C4745638D95999A02B33326C3EC4400003FFF00C02832000002B774C24FE4
./build/dec406_hex 8E3301E2402B002BBA863609670908
```

### Scanner temps réel

```bash
./build/dec406_scan 406.0M 406.1M             # AGC, ppm=0
./build/dec406_scan 406.0M 406.1M 0 30        # ppm=0, gain fixe 30 dB
```

Le scanner lit les échantillons à 2,4576 Msps, détecte les bursts sur un
spectrogramme de puissance, les classe par largeur de bande
(`BW_SPLIT_HZ = 20 kHz` ; les bursts plus larges sont des SGB), et lance le
décodeur approprié. Sur RTL-SDR, il réinitialise le périphérique USB au
démarrage, puis capture en continu via l'API asynchrone de librtlsdr.

Mettre `RTL_DIAG=1` pour journaliser le débit RTL effectif toutes les
quelques secondes :

```bash
RTL_DIAG=1 ./build/dec406_scan 406.0M 406.1M
```

Diagnostics SGB utiles :

```bash
DSSS_DIAG=1 ./build/dec406_scan 406.0M 406.1M
DUMP_FAIL=1 ./build/dec406_scan 406.0M 406.1M
make build/sgb_epl_diag
```

`DUMP_FAIL=1` écrit les fenêtres de bursts SGB ratés dans `burst_sgb_*.cf32`
pour rejeu hors ligne. `build/sgb_epl_diag` sonde ces dumps avec des
corrélateurs EPL sur des plages de fréquence résiduelle plus larges.
`ACQ_BANDPASS_HZ` est conservé comme filtre de diagnostic pour l'acquisition
uniquement ; la valeur par défaut `0` le laisse désactivé.

#### Service de descente 1544 MHz

`systemd/scan1544.service` est l'unité dédiée à la descente satellite. Elle
garde le backend explicite et bascule le chemin d'alerte sur un filtrage
géographique au lieu de la liste blanche des canaux de détresse 406 MHz :

```ini
Environment=DEC406_BACKEND=hackrf
Environment=DEC406_ALERT_MODE=downlink
Environment=DEC406_ALERT_CENTER=43.0,1.5
Environment=DEC406_ALERT_RADIUS_KM=80
```

Si une station utilise le Pluto au lieu du HackRF, seul `DEC406_BACKEND`
change. En mode downlink, un mail n'est envoyé que si la position décodée de
la balise est valide et à l'intérieur du rayon configuré.

#### En service systemd

```ini
[Unit]
Description=Scan406 Service
After=network.target

[Service]
Type=simple
SyslogIdentifier=scan406
WorkingDirectory=/home/firmin/balise_406MHz/dec406_scan/
ExecStart=/home/firmin/balise_406MHz/dec406_scan/build/dec406_scan 406.0M 406.1M
Restart=always
RestartSec=25
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
```

La sortie d'erreur est préfixée par les niveaux de priorité du journal
systemd :

```bash
journalctl -u scan406 -p info      # trace propre (trames décodées seulement)
journalctl -u scan406 -p debug     # flux de diagnostic complet
```

#### Alertes mail sur les canaux de détresse T.012

Si un fichier `data/config_mail.txt` est présent, le scanner envoie par mail
la trame décodée dès qu'une balise décode sur un canal de détresse T.012
Table H.2 (B/C/D/F/G/J/K/N/O/R/S, ±2 kHz). Le binaire `sendemail` doit être
installé.

Au démarrage, le bandeau affiche les réglages mail qui comptent pour
l'exploitation :

```text
alerts  : enabled
mail smtp : smtp.gmail.com:587
mail user : xxxx@gmail.com
mail to   : a@b.fr,c@d.fr
```

Pour activer les alertes, copier le modèle et renseigner les quatre valeurs :

```bash
cp data/config_mail.txt.example data/config_mail.txt
$EDITOR data/config_mail.txt          # SMTP + destinataires
chmod 600 data/config_mail.txt        # contient un mot de passe d'application
```

Format (clé=valeur, une par ligne) :

```
smtp_serveur=smtp.gmail.com:587
utilisateur=user@example.org
password=app_password
destinataires=a@b.com,c@d.fr
```

Sans ce fichier, le scanner fonctionne normalement ; le bandeau affiche
`alerts : disabled` et aucun mail n'est envoyé.

Le canal A (406,022 — orbitographie/calibration) est exclu par conception.
Filtres de silence supplémentaires, empilés sur la liste blanche des canaux :
- les balises FGB identifiées comme Orbitography ou avec `ID-NOT-AVAIL`
  (sortie d'usine / tests au banc) ne déclenchent jamais ;
- les émissions de test SGB (T.018 §3 bit 43 = 1) ne déclenchent jamais ;
- une deuxième détection du même Hex ID sous 3 min est exigée avant l'envoi
  (filtre les bursts uniques de test au banc ; une vraie balise de détresse
  se répète toutes les ~50 s et est confirmée au deuxième burst).

---

## Chaîne de démodulation 2G

```
IQ @ 2.4576 MHz
  → blocage DC (IIR α=0.001)
  → décimation boxcar vers le chip rate (acquisition uniquement)
  → freq_acq_fft_corr (FFT-corrélation au chip rate, ±16 kHz, précision ~1 Hz)
  → wipeoff NCO au rythme d'échantillonnage
  → retard OQPSK (Q avancé de SPS/2)
  → décimation boxcar multi-offset (4 offsets sous-chip)
  → despread (estimation fréquence/phase par ajustement linéaire du préambule,
       PLL Costas BPSK par bit sur 256 chips)
  → 250 bits → bch_decode_250_202 (nerr) → decode_beacon
```

Pour chaque burst acquis, la chaîne essaie 4 offsets boxcar × 4 phases Costas
(16 combinaisons) contre le BCH, et garde la première qui décode proprement.
La régression linéaire du préambule initialise la fréquence/phase de la
porteuse, remplaçant la convergence lente d'un PLL. Une trame dont le BCH ne
peut pas corriger le mot de code est rejetée.

---

## Chaîne de démodulation 1G (IQ-direct)

```
IQ
  → décimation par moyenne mobile multi-étages vers ~9,6 kHz
  → estimation de la fréquence porteuse du préambule CW (différences de phase
     au niveau échantillon)
  → wipeoff de fréquence
  → détection de fin de CW en double grille (deux grilles décalées de half/2)
  → recherche Costas multi-phase (4 phases × 13 offsets)
  → slicer Manchester (biphase-L ±1.1 rad)
  → correction BCH1 par force brute (t=3, bits 24..105)
  → 144 bits → validation CRC1/CRC2 + repli de polarité
```

Pas de démodulation FM, pas de détour par l'audio, pas de dépendance à un
codec biphase-L.

---

## Structure du projet

```
src/         Sources C (dsss_demod, despread, freq_acq, fgb_iq_demod,
              dec406_v1g, dec406_v2g, main_iq, main_scan, scan_alert, ...)
include/     En-têtes
build/       Binaires compilés
tests/       Tests unitaires du codec SGB, tests de rejet BCH
utils/       Outils de diagnostic hors ligne
scripts/     Scripts d'analyse / debug
docs/        Spécifications, notes de déploiement, notes d'état SGB
data/        Données d'exécution (config_mail.txt, etc.)
```

---

## Documentation

- `CHANGELOG.md` — Historique des versions
- `CLAUDE.md` — Conventions du projet, méthodologie de debug
- `docs/ARCHITECTURE_dec406.md` — Architecture détaillée (français)
- `docs/TESTS_VALIDATION.md` — Procédures de validation
- `scripts/scan406.pl` — Scanner Perl historique (remplacé par dec406_scan)

---

## Licence

Ce projet est distribué sous licence MIT. Voir `LICENSE`.

---

## Références

- **C/S T.001** — Spécification des balises de 1ʳᵉ génération (FGB)
- **C/S T.012** — Agrément de type des balises (liste des canaux, Table H.2)
- **C/S T.018** — Spécification des balises de 2ᵉ génération (SGB)
- `gr-cospas` / `gr-satellites` — Récepteurs GNU Radio
- `sgb-codec` (jbirby) — Codec SGB Python de référence
- `sarsat_sgb` (ADALM-PLUTO) — Modulateur SGB pour PlutoSDR
- `scan406.pl` (F4EHY 2020) — Ancêtre du scanner en Perl, porté en C dans
  `dec406_scan` + `scan_alert`
