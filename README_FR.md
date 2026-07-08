# dec406 — Décodeur de balises COSPAS-SARSAT 406 MHz

Décodeur pour balises de détresse COSPAS-SARSAT de 1re génération (FGB) et
2e génération (SGB) à 406 MHz.

**Branche** : `main`

---

## Fonctionnalités opérationnelles

### Décodeurs (exactitude bit à bit)
- **1G (FGB)** : PSK biphase-L T.001 ±1,1 rad, 400 bps, tous les protocoles
  de localisation et utilisateur — `dec406_v1g.c`
- **2G (SGB)** : BCH(250,202) avec correction d'erreurs complète
  Berlekamp-Massey + Chien, tous les champs T.018 — `dec406_v2g.c`

### Démodulateurs
- **DSSS OQPSK (2G)** — `dsss_demod.c` : chaîne de traitement linéaire (sans
  boucle de suivi de la cadence d'échantillonnage) : filtre de suppression de
  la composante continue (DC blocker) → acquisition de fréquence par
  corrélation FFT → élimination de la porteuse (NCO wipeoff) → retard OQPSK →
  décimation par fenêtre rectangulaire (boxcar) avec décalages multiples →
  désétalement avec estimation de fréquence par régression linéaire sur le
  préambule + PLL de Costas par bit → décodage BCH. L'oracle teste
  4 décalages de fenêtre × 4 phases Costas (16 combinaisons) avant
  d'abandonner. La recherche du décalage temporel d'acquisition est limitée à
  la durée du préambule de la salve (pre-roll) afin d'éviter que des pics de
  bruit ou de données tardifs ne soient pris à tort pour le véritable
  préambule. La recherche de fréquence par corrélation FFT essaie d'abord
  ±8 kHz, puis s'élargit à ±16 kHz seulement si nécessaire pour couvrir les
  écarts de fréquence plus importants observés sur des salves SGB réelles du
  CNES.
- **FGB IQ-direct (1G)** — `fgb_iq_demod.c` : décodeur BPSK biphase-L en bande
  de base complexe, sans détour par démodulation FM puis audio. Détection de
  fin de porteuse CW sur double grille, recherche Costas multiphase
  (4 phases initiales × 13 décalages), seuillage Manchester, correction BCH-1
  (t=3) et correction BCH-2 des trames longues hors orbitographie (t=2).

### Scanner en temps réel
`dec406_scan` est un scanner unifié fonctionnant en temps réel, avec sélection
automatique du moteur SDR (Airspy Mini, RTL-SDR, PlutoSDR, HackRF) pour une
utilisation interactive. Le moteur RTL-SDR utilise `rtlsdr_read_async()` avec
de grands tampons. Le scanner applique un détecteur de rafales spectrales sur
la bande de 100 kHz, classe chaque rafale comme FGB ou SGB en fonction de sa
largeur de bande, puis confie son décodage à un worker dédié afin que la capture
et la détection spectrale continuent pendant les traitements coûteux. Le
heartbeat distingue les débordements du ring SDR des pertes dues à une file de
décodage pleine. Pour les services, le moteur peut être imposé explicitement via
`DEC406_BACKEND=rtl|airspy|pluto|hackrf`, évitant ainsi la phase de détection
au démarrage.

### État actuel de validation

Les taux dépendent fortement de la propagation, de l'heure, du bruit local et
des balises système CNES actives. Suivre séparément trois catégories SGB :

| Métrique | Définition |
|----------|------------|
| Acquisition/synchro SGB | `(BCH OK + FRAME REJECTED) / rafales SGB détectées` |
| Pureté décodeur SGB | `BCH OK / (BCH OK + FRAME REJECTED)` |
| SGB bout en bout | `BCH OK / rafales SGB détectées` |

Sur les dernières validations terrain, les SGB qui se synchronisent valident
proprement le BCH. Les essais du relais firmin et les validations locales
RTL/Yagi donnent des taux élevés, avec FGB restant dans sa plage habituelle.
Ces valeurs doivent être traitées comme des contrôles terrain propres à chaque
passe, pas comme des taux globaux fixes.

---

## Compilation

```bash
make build/dec406_iq        # Décodeur SGB depuis fichier IQ
make build/dec406_scan      # Scanner en temps réel (nécessite librtlsdr-dev)
make                        # toutes les cibles
```

Binaires produits dans `build/` :

| Binaire | Rôle |
|---------|------|
| `dec406_iq` | Démodulateur SGB DSSS/OQPSK depuis fichier IQ |
| `dec406_hex` | Décodeur 1G/2G depuis chaîne hexadécimale |
| `dec406_audio` | Décodeur 1G depuis fichier WAV (ancienne chaîne avec démodulation FM) |
| `dec406_scan` | Scanner en temps réel bande FGB+SGB (Airspy/RTL-SDR/PlutoSDR) |
| `dec406_dsss_test` | Pilote de test unitaire du démodulateur DSSS |
| `generate_2g_hex` | Générateur de trames de test 2G |
| `reset_usb` | Utilitaire de réinitialisation de périphérique USB |

---

## Utilisation

### Fichiers IQ hors ligne

```bash
# SGB depuis fichier IQ (complexe float32, par défaut)
./build/dec406_iq signal.iq -s 2457600

# Enregistrement SDRangel ci32_le (complexe int32, ordre little-endian)
./build/dec406_iq recording.sigmf-data -s 2457600 -I

# Enregistrement RTL-SDR uint8
./build/dec406_iq recording.iq -s 2457600 -u
```

Formats d'entrée : complexe float32 (par défaut), `-u` RTL-SDR uint8,
`-i` int16 entrelacé, `-I` SDRangel ci32_le int32.

### Trames hexadécimales

```bash
./build/dec406_hex 09C4745638D95999A02B33326C3EC4400003FFF00C02832000002B774C24FE4
./build/dec406_hex 8E3301E2402B002BBA863609670908
```

### Scanner en temps réel

```bash
./build/dec406_scan 406.0M 406.1M             # AGC, ppm=0
./build/dec406_scan 406.0M 406.1M 0 30        # ppm=0, gain fixe 30 dB
```

Le scanner lit les échantillons à 2,4576 Msps, détecte les rafales dans un
spectrogramme de puissance, les classe par largeur de bande
(`BW_SPLIT_HZ = 20 kHz` ; les rafales plus larges sont SGB), puis exécute le
décodeur adapté. Sur RTL-SDR, il réinitialise le périphérique USB au démarrage,
puis capture en continu via l'API asynchrone de librtlsdr.

Définir `RTL_DIAG=1` pour journaliser le débit RTL effectif toutes les quelques
secondes :

```bash
RTL_DIAG=1 ./build/dec406_scan 406.0M 406.1M
```

Diagnostics SGB utiles :

```bash
DSSS_DIAG=1 ./build/dec406_scan 406.0M 406.1M
DUMP_FAIL=1 ./build/dec406_scan 406.0M 406.1M
make build/sgb_epl_diag
```

`DUMP_FAIL=1` écrit les fenêtres de rafales SGB en échec sous forme
`burst_sgb_*.cf32` pour rejeu hors ligne. `build/sgb_epl_diag` analyse ces
fichiers avec des corrélateurs EPL sur des plages plus larges de fréquence
résiduelle.
`ACQ_BANDPASS_HZ` est conservé comme filtre de diagnostic limité à
l'acquisition ; la valeur par défaut `0` le laisse désactivé.

#### Service de liaison descendante 1544 MHz

`systemd/scan1544.service` est l'unité dédiée à la liaison descendante. Elle
garde le moteur explicite et bascule le chemin d'alerte vers un filtrage
géographique au lieu de la liste blanche des canaux de détresse 406 MHz :

```ini
Environment=DEC406_BACKEND=hackrf
Environment=DEC406_ALERT_MODE=downlink
Environment=DEC406_ALERT_CENTER=43.0,1.5
Environment=DEC406_ALERT_RADIUS_KM=80
```

Si Pluto est utilisé à la place de HackRF sur une station donnée, seul
`DEC406_BACKEND` change. En mode liaison descendante, un courriel est envoyé
uniquement lorsque la position décodée de la balise est valide et située dans
le rayon configuré.

#### Comme service systemd

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

Stderr est balisé avec les préfixes de priorité du journal systemd :

```bash
journalctl -u scan406 -p info      # trace propre (trames décodées seulement)
journalctl -u scan406 -p debug     # flux de diagnostic complet
```

#### Alertes courriel sur les canaux de détresse T.012

Si un fichier `data/config_mail.txt` est présent, le scanner envoie par
courriel la trame décodée dès qu'une balise est décodée sur un canal de
détresse de la table H.2 T.012 (B/C/D/F/G/J/K/N/O/R/S, ±2 kHz). Le binaire
`sendemail` doit être installé.

Au démarrage, la bannière affiche les paramètres de courriel importants pour
l'exploitation :

```text
alerts  : enabled
mail smtp : smtp.gmail.com:587
mail user : xxxx@gmail.com
mail to   : a@b.fr,c@d.fr
```

Pour activer les alertes, copier le modèle puis renseigner les quatre valeurs :

```bash
cp data/config_mail.txt.example data/config_mail.txt
$EDITOR data/config_mail.txt          # définir les identifiants SMTP + destinataires
chmod 600 data/config_mail.txt        # contient un mot de passe d'application
```

Format (`key=value`, une entrée par ligne) :

```
smtp_serveur=smtp.gmail.com:587
utilisateur=user@example.org
password=app_password
destinataires=a@b.com,c@d.fr
```

Sans ce fichier, le scanner fonctionne normalement ; la bannière affiche
`alerts : disabled` et aucun courriel n'est envoyé.

Le canal A (406.022 — orbitographie/calibration) est exclu par conception.
Filtres de silence supplémentaires appliqués au-dessus de la liste blanche des
canaux :
- Les balises FGB identifiées comme Orbitography ou avec `ID-NOT-AVAIL`
  (sortie usine / tests banc) ne déclenchent jamais d'alerte.
- Les émissions de test SGB (T.018 §3 bit 43 = 1) ne déclenchent jamais
  d'alerte.
- Une seconde observation du même Hex ID dans les 3 min est exigée avant envoi
  du courriel (filtre les rafales uniques de test banc ; une vraie balise de
  détresse répète environ toutes les 50 s et est confirmée à la seconde
  rafale).

---

## Chaîne de démodulation 2G

```
IQ @ 2.4576 MHz
  → filtre de suppression de la composante continue (IIR α=0.001)
  → décimation par fenêtre rectangulaire au débit des chips (acquisition seulement)
  → freq_acq_fft_corr (corrélation FFT au débit des chips, ±8 kHz puis repli ±16 kHz, précision ~1 Hz)
  → élimination de la porteuse par NCO au taux d'échantillonnage
  → délai OQPSK (Q avancé de SPS/2)
  → décimation par fenêtre rectangulaire avec décalages multiples (4 décalages sous-chip)
  → désétalement (estimation fréquence/phase du préambule par régression linéaire,
       PLL Costas BPSK par bit sur 256 chips)
  → 250 bits → bch_decode_250_202 (nerr) → decode_beacon
```

Pour chaque rafale acquise, la chaîne essaie 4 décalages de fenêtre ×
4 phases Costas (16 combinaisons) jusqu'à validation BCH, et conserve la
première combinaison qui se décode proprement.
La régression linéaire sur le préambule initialise la fréquence et la phase de
porteuse, remplaçant la lente convergence de la PLL. Une trame dont le mot de
code BCH ne peut pas être corrigé est rejetée.

---

## Chaîne de démodulation 1G (IQ-direct)

```
IQ
  → décimation par moyenne mobile multi-étage vers ~9.6 kHz
  → estimation de la fréquence porteuse du préambule CW (différences de phase entre échantillons)
  → correction de fréquence
  → détection de fin CW sur double grille (deux grilles décalées de half/2)
  → recherche Costas multiphase (4 phases × 13 décalages)
  → seuillage Manchester (biphase-L ±1.1 rad)
  → correction BCH1 par force brute (t=3, bits 24..105)
  → 144 bits → validation CRC1/CRC2 + essai de polarité inverse en repli
```

Pas de démodulation FM, pas de détour audio, pas de dépendance à un codec
biphase-L.

---

## Structure du projet

```
src/         Sources C (dsss_demod, despread, freq_acq, fgb_iq_demod,
              dec406_v1g, dec406_v2g, main_iq, main_scan, scan_alert, ...)
include/     En-têtes
build/       Binaires compilés
tests/       Tests unitaires du codec SGB, tests de rejet BCH
utils/       Outils de diagnostic hors ligne
scripts/     Scripts d'analyse / débogage
docs/        Spécifications, notes de déploiement, notes d'état SGB
data/        Données d'exécution (config_mail.txt, etc.)
```

---

## Documentation

- `CHANGELOG.md` — Historique des versions
- `CLAUDE.md` — Conventions du projet, méthodologie de débogage
- `docs/ARCHITECTURE_dec406.md` — Architecture détaillée (français)
- `docs/TESTS_VALIDATION.md` — Procédures de validation
- `scripts/scan406.pl` — Ancien scanner Perl (remplacé par dec406_scan)

---

## Licence

Ce projet est distribué sous licence MIT. Voir `LICENSE`.

---

## Références

- **C/S T.001** — Spécification des balises de 1re génération (FGB)
- **C/S T.012** — Homologation des types de balises (liste des canaux,
  table H.2)
- **C/S T.018** — Spécification des balises de 2e génération (SGB)
- `gr-cospas` / `gr-satellites` — Récepteurs GNU Radio
- `sgb-codec` (jbirby) — Codec SGB de référence en Python
- `sarsat_sgb` (ADALM-PLUTO) — Modulateur SGB pour PlutoSDR
- `scan406.pl` (F4EHY 2020) — Ancêtre du scanner Perl, porté en C dans
  `dec406_scan` + `scan_alert`
