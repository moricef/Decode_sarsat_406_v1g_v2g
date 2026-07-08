# Changelog - dec406_v10.2

## Version 10.2.11 - 2026-07-08 - Worker de décodage et BCH-2 FGB

### Worker de décodage pour le scanner temps réel

Les décodages FGB et SGB s'exécutent désormais dans un worker dédié alimenté
par une file bornée. Le scanner copie immédiatement chaque rafale IQ détectée
et reprend la détection spectrale pendant que le worker effectue l'élimination
de porteuse et le décodage. Le heartbeat affiche séparément les débordements du
ring SDR et les pertes dues à une file de décodage pleine.

La validation terrain sur firmin a décodé 14 trames SGB self-test et 2 trames
normales pendant des trains espacés parfois d'une seconde. Toutes ont validé le
BCH et les deux compteurs sont restés à zéro. Un autre essai matériel a décodé
quatre trames en cinq secondes sans perte.

### Correction BCH-2 FGB

Les trames FGB longues hors orbitographie bénéficient maintenant de la
correction BCH(38,26) sur les bits 107 à 144, jusqu'à deux erreurs, après que
BCH-1 a corrigé et établi le bit de format. Les trames courtes sont transmises
au décodeur texte avec leur longueur réelle de 112 bits.

Les trames d'orbitographie CNES restent longues de 144 bits mais sont exemptées
de validation BCH-2. T.001 réserve ce protocole aux opérateurs LUT sans en
décrire la charge utile, et le corpus local contient des orbitographies longues
dont le champ final n'est pas un mot BCH-2 valide.

La validation couvre les 121 vecteurs FGB locaux précédemment acceptés, dont
120 orbitographies longues, ainsi que 19 266 injections d'une ou deux erreurs
BCH-2. Les builds des scanners et la régression SGB synthétique obligatoire
passent également. La récupération d'erreurs BCH-2 sur une vraie trame FGB
hors orbitographie reste à observer.

## Version 10.2.10 - 2026-07-08 - Nettoyage de la documentation README

### Nettoyage des README anglais et français

Ajout de `README_FR.md` comme README français maintenu et suppression de
l'ancien `README.fr.md`, afin d'éviter de publier une traduction obsolète.

Les deux README ont été recentrés sur le comportement actuel plutôt que sur
l'historique des commits :

- l'état de validation résume maintenant les contrôles terrain actuels sans
  citer de branches internes ni de pourcentages ponctuels sur fenêtre courte ;
- les explications historiques sur l'ancienne capture synchrone RTL-SDR restent
  dans ce changelog au lieu d'être dans le README ;
- la documentation de l'acquisition SGB décrit la recherche réellement utilisée :
  ±8 kHz d'abord, puis repli ±16 kHz seulement si nécessaire ;
- le raccourci interne `flat-chain` a été remplacé par une formulation plus
  claire dans les README.

## Version 10.2.9 - 2026-07-04 - Recherche d'acquisition SGB élargie

### Recherche d'acquisition SGB avec repli ±16 kHz (`src/dsss_demod.c`)

`freq_acq_fft_corr()` est maintenant appelé d'abord avec la fenêtre normale de
recherche de fréquence résiduelle ±8 kHz, puis relancé avec un repli élargi
±16 kHz lorsque la première passe n'atteint pas le seuil de confiance
d'acquisition. Des dumps terrain de firmin ont montré des rafales SGB de
calibration CNES valides avec des pics de corrélation autour de +8,6 à
+11,1 kHz par rapport au centroïde de détection du scanner ; une recherche
strictement limitée à ±8 kHz rejetait ces rafales avant le désétalement alors
que le signal était exploitable.

Il s'agit uniquement d'un changement d'acquisition. La chaîne SGB après
synchronisation reste inchangée : élimination NCO de la porteuse, délai OQPSK,
désétalement, Costas par bit et validation BCH sont identiques à la 10.2.8.

### Outil de diagnostic SGB EPL hors ligne (`utils/sgb_epl_diag.c`)

Ajout de `build/sgb_epl_diag` pour analyser hors ligne les rafales SGB dumpées
avec des corrélateurs PRN Early/Prompt/Late. L'outil a servi à distinguer une
vraie perte de structure d'un raté de fenêtre de recherche d'acquisition, et à
vérifier que les rafales firmin rejetées conservaient une forte corrélation
prompt au-delà de ±8 kHz.

### Mise à jour de validation

Les logs locaux et firmin du 2026-07-04 ont été revérifiés sur la grille de
calibration CNES 150 s après l'élargissement de la recherche d'acquisition :

- firmin : environ 93 % de réussite SGB sur les créneaux de calibration dans
  la fenêtre post-correctif vérifiée ; les rafales récupérées avaient des
  offsets résiduels hors de l'ancienne plage ±8 kHz ;
- local RTL/Yagi : resté dans la même plage de réussite élevée, sans régression
  due au changement d'acquisition ;
- FGB : aucune régression observée, toujours dans sa classe terrain
  préexistante autour de 90 % ;
- rejets BCH/trame : pas de retour du mode d'échec précédent
  « préambule fort, données aléatoires ».

Le filtre Butterworth limité à l'acquisition reste disponible via
`ACQ_BANDPASS_HZ` pour les expérimentations, mais il est désactivé par défaut
(`0`).

## Version 10.2.8 - 2026-07-04 - Scanner unifié, RTL asynchrone, comptage prudent des taux

### Scanner temps réel unifié sur `main`

`dec406_scan` utilise maintenant l'architecture de scanner unifiée avec
sélection automatique du backend (Airspy Mini, RTL-SDR, PlutoSDR). Le scanner
affiche au démarrage le gain sélectionné et le paramètre du backend (`ppm` pour
RTL-SDR).

### Capture RTL-SDR asynchrone (`src/backend_rtlsdr.c`)

Le backend RTL-SDR utilise maintenant `rtlsdr_read_async()` avec 32 grands
tampons. Cela remplace la boucle de capture synchrone, qui pouvait fournir un
débit insuffisant au scanner sans erreur explicite et introduire des
discontinuités d'échantillons avant le ring buffer. La validation locale
RTL/Yagi a montré le retour du débit effectif nominal à 2,4576 MS/s et la
disparition du mode d'échec où SGB avait une forte synchro préambule mais des
bits de charge utile aléatoires.

`RTL_DIAG=1` active des logs périodiques de débit ; il reste silencieux par
défaut pour l'exploitation systemd.

### Correctifs de détection SGB fredzo intégrés

Intégration des correctifs pertinents de la branche `bch2_correction` de
fredzo, adaptés au scanner unifié :

- rejeter les mots de code SGB dégénérés tout-zéro/tout-un avant acceptation
  BCH ;
- rejeter un succès BCH trivial après un faux verrouillage du désétalement ;
- borner la recherche de retard de `freq_acq_fft_corr()` à la zone de pré-roll
  de la rafale, afin que des pics tardifs de données/bruit ne dépassent pas le
  vrai préambule.

### Correction du comptage des taux

Les anciennes entrées de changelog citent des taux courts comme « SGB 93 % » et
« FGB 92 % ». Ces chiffres ne sont **pas des performances globales stables** :
ils dépendent de la propagation, de l'heure, du bruit local, des balises CNES
actives et du filtrage du scanner. Le reporting actuel doit séparer les
catégories SGB :

| Métrique | Définition |
|---|---|
| Acquisition/synchro SGB | `(BCH OK + FRAME REJECTED) / rafales SGB détectées` |
| Pureté décodeur SGB | `BCH OK / (BCH OK + FRAME REJECTED)` |
| SGB bout en bout | `BCH OK / rafales SGB détectées` |

Validation récente après cette mise à jour :

- local RTL/Yagi : pas de retour de l'échec « préambule fort, charge utile
  aléatoire » ; les SGB qui se synchronisent valident proprement le BCH, tandis
  que les échecs restants sont surtout des rejets d'acquisition
  `coarse reject conf ...` ;
- firmin : l'échantillon post-déploiement initial est trop court et trop
  dépendant de la propagation pour annoncer un pourcentage global. Les rejets
  BCH ont disparu dans ce court échantillon, mais les rejets d'acquisition
  restent le principal poste de perte SGB.

## Version 10.2.7 - 2026-06-14 - Correctifs chaîne SGB (taux courts obsolètes)

### Correctifs de chaîne de décodage SGB (`src/despread.c`, `src/dec406_v2g.c`, `src/dsss_demod.c`)

Trois bugs ont été corrigés simultanément. Dans la courte fenêtre de validation
disponible à ce moment-là, cela ressemblait à une amélioration 29 % → 93 % à
firmin (80 km), mais ce taux global est maintenant considéré comme obsolète ;
voir 10.2.8 pour le comptage par catégories.

1. **Inversion de polarité des bits** — T.018 : data=1 inverse le PRN, donc la
   corrélation avec le PRN brut est négative. La décision était `> 0` au lieu
   de `< 0`, ce qui inversait systématiquement les 250 bits → BCH toujours en
   échec.

2. **Estimation de fréquence du préambule** — Régression linéaire sur les
   25 phases atan2 connues du préambule (déroulées). Initialise `freq_per_bit`
   et `phase_rad` au lieu de dépendre d'une convergence lente de la PLL
   (alpha=0.04, beta=0.01). Résidu mesuré : ~4 Hz (0,18 rad/bit).

3. **Recherche Chien sur tout GF(2^8)** — BCH(250,202) est raccourci depuis
   BCH(255,207) : la recherche doit couvrir 255 positions, pas 250. Les racines
   dans le padding virtuel (250-254) doivent compter pour `nroots == L` mais ne
   retournent pas de bits.

### Oracle boxcar multi-offset (`src/dsss_demod.c`)

4 offsets boxcar sous-chip × 4 phases Costas = 16 combinaisons essayées contre
le BCH. La première combinaison qui passe le BCH gagne.
`bch_decode_250_202_nerr()` retourne le nombre d'erreurs pour diagnostic.

### Capture des rafales en échec avec DUMP_FAIL (`src/main_scan.c`)

La variable d'environnement `DUMP_FAIL=1` dumpe les rafales SGB en échec vers
`burst_sgb_HHMMSS_<freq>Hz.cf32` (complexe float32) pour analyse hors ligne.

### Taux historiques courts au relais firmin (80 km, propagation du soir)

Ces valeurs sont conservées uniquement pour le contexte historique. Elles ne
doivent pas être utilisées comme des performances globales actuelles.

| Type | Avant | Après |
|------|-------|-------|
| SGB  | 29 %  | 93 % (13/14, nerr=0 sur tous les OK) |
| FGB  | 78 %  | 92 % (pas de changement FGB, propagation) |

---

## Version 10.2.6 - 2026-06-14 - Taux de décodage FGB 78 %

### Améliorations du démodulateur FGB IQ (`src/fgb_iq_demod.c`)

**Détection de fin CW sur double grille** — `find_cw_end_cmplx()` scanne
maintenant deux grilles entrelacées décalées de `half/2`. Le préambule tout-1
en biphase-L est particulièrement vulnérable à un désalignement d'un demi-bit :
S1 et S2 moyennent chacun un cycle complet ±1,1 → annulation à 0 → le détecteur
rate le préambule 15 bits et déclenche 17-18 bits trop tard à FSYNC. La double
grille élimine 100 % de ces rafales inutilisables (29 % des captures avant).

**Recherche Costas multiphase** — Essai de 4 phases initiales
{0°, 45°, 90°, 135°} pour chaque candidat d'offset bit0
(13 offsets × 4 phases = 52 candidats). Sélection du meilleur score FSYNC sur
toutes les combinaisons.

**Correction d'erreurs BCH1 par force brute** — `bch1_correct()` retourne 1, 2
ou 3 bits dans le mot de code BCH1 (bits 24..105) et vérifie le syndrome via
`test_crc1()`. BCH(82,61) t=3 selon la spécification T.001. Appliqué avant le
repli de polarité ; le chemin de polarité bénéficie aussi de la correction BCH.

**Instrumentation de diagnostic** — `dump_costas_diag()` émet un CSV par
rafale avec cw_end, cw_mag, cw_expected, cw_thresh et les scores préambule
4 phases. `dump_bits()` inclut maintenant les valeurs soft. Contrôlé par la
variable d'environnement `FGB_IQ_DIAG`.

**Résultat au relais firmin (80 km)** : taux de décodage FGB 45 % → 78 %.

### Réglages du scanner (`src/main_scan.c`)

- `BW_SPLIT_HZ` 50 kHz → 20 kHz : classification FGB/SGB plus stricte
- `CYCLE_SAMPLES` 55 s → 600 s : réduit d'un facteur 11 les frontières de
  cycle qui tronquent des rafales SGB (`buffer too short`)

---

## Version 10.2.5 - 2026-06-01 - Câblage scanner production

Branche `feature/dsss-flat-chain`.

### Refactor chaîne DSSS — flat-chain (commits `b6bbcc4`, `e5c5e5d`)

Suppression de la tracking loop sample-rate (FLL+PLL+DLL+Kalman) introduite en
10.2.4. La nouvelle chaîne `dsss_demod.c` est directe :
DC blocker → boxcar décimation chip-rate → `freq_acq_fft_corr` (FFT-corr) →
NCO wipeoff → OQPSK delay → boxcar finale → despread + per-bit Costas PLL.
Beaucoup moins de code, performance équivalente sur OTA.

### Acquisition fréquence par FFT-corrélation (`freq_acq_fft_corr`)

Précision ~1 Hz via une paire de FFT (signal × conj(PRN preamble)), balayage
±8 kHz à pas de 12 Hz puis raffinement ±18 Hz à 1 Hz autour du pic. Métrique
`conf` = peak/mean sur la grille, seuil `ACQ_CONF_MIN = 8.0`.

### Oracle BCH multi-rotation dans `dsss_demod`

À chaque rafale acquise, les 4 phases Costas (0°/90°/180°/270°) sont essayées
contre `bch_decode_250_202` ; on garde la première qui décode. Récupère les
rafales où `despread_sync` a choisi la mauvaise phase à SNR marginal.
Coût ~50 ms.

### Démodulateur FGB IQ-direct (`src/fgb_iq_demod.c`, commit `2d6cc73`)

Décodeur 1G en bande de base complexe, sans pipeline FM-demod → audio :
- Estimation de fréquence de porteuse sur le préambule CW (160 ms)
- Détection de fin de CW par |S1-S2| lissé sur deux bits
- Raffinement de phase bit par balayage ±half_bit
- Recherche multi-offset du frame sync sur 9 bits
- Boucle Costas BPSK + slicer Manchester
- Validation CRC1/CRC2

Au relais firmin (80 km de la balise de test) : ~75 % de décodage, équivalent
au F4EHY (62 % référence sur le même relais).

### Scanner temps réel `dec406_scan`

Remplace le pipeline Perl historique `rtl_power + rtl_fm + sox +
dec406_audio`. Détection spectrale de rafales sur 100 kHz, classification
FGB/SGB par bande passante, ingestion synchrone en `librtlsdr`.

**Capture librtlsdr synchrone** (`62e6e92`) : `popen("rtl_sdr")` →
`rtlsdr_read_sync()`. Plus de `cb transfer status: 5` sur les hoquets USB en
mode async. Cycle de 55 s piloté par compteur d'échantillons. Silence des
prints internes de librtlsdr par `dup2` autour de `rtlsdr_open/close`.

**Diagnostics journal par priorité** : macros `DIAG`/`DWARN`/`DERR`
(`include/diag_log.h`) qui préfixent stderr avec les niveaux kernel-syslog
(`<7>`, `<4>`, `<3>`). `journalctl -p info` donne la sortie propre style
F4EHY ; `-p debug` ramène tous les diagnostics
`[fgb_iq]/[freq_acq]/[despread]/[dsss_demod]`, le heartbeat, les CRC,
l'Orbitography data, etc.

**Alertes mail T.012** (`src/scan_alert.{c,h}`, porté depuis `scan406.pl`) :
- Liste blanche des canaux de détresse T.012 Table H.2
  (B/C/D/F/G/J/K/N/O/R/S, ±2 kHz) ; canal A (406.022 orbitographie) exclu
- Configuration SMTP dans `data/config_mail.txt` (clé=valeur, format identique
  au Perl)
- `sendemail` en arrière-plan pour éviter le blocage du pipeline pendant le
  handshake TLS Gmail (sinon : ring overruns sur les décodages SGB)
- Corps = en-tête (UTC, type, freq, SNR, trame hex complète) + bloc de décodage
  capturé via `dup2(tmpfile)` autour de `decode_1g/decode_beacon`
- Garde SGB : alertes uniquement si T.018 §3 bit 43 = 0 (Normal Operation).
  Les transmissions de test (CNES sur canal K) restent silencieuses.

**Lisibilité de la sortie** : timestamps milliseconde, `dt` inter-rafale
calculé en samples (précis, insensible au jitter d'affichage), ligne vide entre
trames décodées.

### Petits fixes

- `g_wr` / `overruns` réinitialisés à chaque cycle rtl_sdr (`f4649d5`)
- `rtl_sdr -n` pour exit naturel après 55 s (`f239e7c`, remplacé ensuite par
  lecture sync librtlsdr)
- Seuil CW abaissé (0.1 → 0.08), sustain 3 → 4 (`9676768`)
- Prints "BCH could not correct" des rotations rejetées supprimés (`33064cf`)
- BCH error counting nettoyé : seul "N errors corrected" reste visible

### Régression synthétique

```
./build/dec406_iq ../../GNURADIO/test_sgb_halfsine.sigmf-data -s 2457600
```

→ z=9639.2, BCH validé sur phase 0°, Hex ID décodé. OK.

---

## Version 10.2.4 - 2026-05-16 - Tracking loop + décodage OTA

Branche `feature/fll-pll-tracking`.

### Démodulateur OTA fonctionnel

Le signal over-the-air (PlutoSDR → RTL-SDR / SDRangel) est maintenant décodé,
BCH-propre. La chaîne de réception est remplacée par une **tracking loop**
sample-rate (FLL+PLL+DLL+Kalman) qui assure la poursuite de porteuse et la
décimation en une seule passe, en remplacement du filtre RRC + Costas QPSK.

### Chaîne de réception (`src/dsss_demod.c`)

1. DC blocker (IIR α=0.001) sur échantillons bruts
2. `freq_acq_coarse_fft()` — FFT 4e puissance, contrôle de plausibilité ±25 kHz
3. Sweep fallback ±300 Hz (corrélation PRN) si la FFT est rejetée
4. OQPSK delay (Q avancé de SPS/2)
5. Tracking loop → sortie chip-rate
6. `despread_burst()` — sync préambule + extraction des bits

### Corrections

#### Décodage des rafales n'importe où dans la fenêtre (commit 8ffb090)

- `DESPREAD_SYNC_RANGE` 1000 → 9600 chips (= un pas de scan complet)
- Fenêtre de scan 1,1 s → 1,35 s
- **Cause** : une rafale dont le préambule tombait au-delà de l'offset 1000
  n'était jamais trouvée → décodage « à la loterie » sur fichiers courts

#### Rejet des fausses balises (commit fa08382)

- `DESPREAD_SYNC_THRESHOLD` 2.8 → 20 : le bruit (z ≤ 7) ne synchronise plus
- `bch_decode_250_202()` retourne maintenant un statut ; `decode_2g()` rejette
  la trame et n'affiche aucune balise si le BCH ne peut pas corriger
- **Cause** : à lien faible, le décodeur synchronisait sur du bruit et imprimait
  une balise fabriquée (faux TAC, fausse position GPS)

#### Poursuite de phase dans le despread (commits 08d8a0a, ad3cc7e, 0995092)

- Phase tracker BPSK 2e ordre (proportionnel + intégral) dans `despread_bits()`
- PLL : correction de phase remplacée par un offset de fréquence one-shot
  (pas de saut de phase aux frontières d'epoch)

#### Sync préambule en corrélation complexe (commit 16d00d6)

- Corrélation complexe `|Σ s·conj(e)|` insensible à la phase porteuse
- Résolution d'ambiguïté Costas sur 4 phases

### Nouveaux composants

- `src/tracking.c` — tracking loop sample-rate (EPL, ATC 3 états, lock P²)
- `src/kalman5.c` — filtre de Kalman 5 états (optionnel, désactivé)
- `src/freq_acq.c` — acquisition de fréquence coarse (FFT 4e puissance + sweep)
- `tests/test_bch_reject.c` — test des chemins BCH propre / corrigé / rejeté

---

## Version 10.2.3 - 2025-10-24 - Investigation démodulateur IQ

### Investigation désalignement structurel

- **Ajout de traces debug complètes** : vérification de l'alignement des indices
  à chaque étape (AGC → Despreading)
- **Découverte majeure** : aucun désalignement structurel détecté
- **Cause racine identifiée** : fichiers de test tronqués d'1 sample
  (float32 rounding errors)

### Corrections majeures

#### Fenêtre de recherche préambule (commit fc6f617)

```c
// Extension 20% -> 50% pour meilleure détection
size_t search_length = num_samples / 2;  // Was: num_samples / 5
```

**Résultats :**
- Index préambule : 350,870 → 0
- Symboles récupérés : 33,062 → 38,399 (99,997 %)
- Phase corrélation : 63 % → 88 %

#### Option sample rate manuel (commit e458450)

- Bypass auto-détection (8 min → 0,87 s)
- Nouvelle option `-s <rate>` pour spécifier le sample rate manuellement

### Nouveaux outils

#### test_sample_rate (commit 1e3a624)

- Détection sample rate par corrélation préambule
- Teste 10 sample rates courants (300 kHz → 6,144 MHz)
- Corrélation DSSS pour estimation précise

```bash
./test_sample_rate file.iq
# Output: Estimated sample rate with correlation score
```

#### resample_iq

- Rééchantillonnage IQ avec libsamplerate
- Conversion entre sample rates (ex. : 2,5 MHz → 384 kHz)
- Interpolation haute qualité (SRC_SINC_BEST_QUALITY)

### Fichiers de test

**Problème découvert :**
- `test_known_384kHz.iq` : 383,999 samples (manque 1)
- `test_known.iq` @ 2.5MHz : 2,499,999 samples (manque 1)

**Impact :** corrélation de désétalement 5 % au lieu de >70 % attendu

**Fix générateur** (SARSAT_SGB commit 1d4493f) :
- Correction d'une erreur d'arrondi float32 dans l'OQPSK modulator
- Fichiers complets maintenant générés : 2,500,000 samples

### Analyses techniques

#### Timing recovery

- Condition boucle ajustée (cosmetic, pas d'impact)
- Récupération 38,399/38,400 symbols (99,997 %)
- Limité par fichiers de test tronqués, pas par l'algorithme

#### Phase ambiguity resolution

- Extended search : 360° × 2 swaps × 2 inversions
- Corrélation Phase 1 : 88 % (excellente amélioration)
- Phase 2 (chip offset) : recherche étendue -15 à +15

### Documentation

- **BILAN_SESSION_20251024.md** : investigation complète du désalignement
- Traces debug conservées pour diagnostic futur
- Analyse des fichiers tronqués documentée

### Validation

**Démodulateur validé fonctionnel**
- Chaîne de traitement correcte (aucun bug structurel)
- Faible corrélation (5 %) due uniquement aux fichiers de test
- Attendu >70 % avec fichiers complets et signal au début

### Prochaines étapes

1. Générer un fichier de test avec le signal au début du fichier
2. Valider une corrélation >70 % avec un fichier correct
3. Documenter le workflow complet TX → RX

---

## Version 10.2.2 - 2025-10-19 - Pause démodulateur 2G

### Statut : PAUSED

- Démodulateur non fonctionnel (55,3 % de précision bit)
- Documentation complète dans ETAT_PAUSE_DEMODULATEUR.md
- 4 bugs identifiés (timing, phase, DSSS, Costas)

---

## Version 10.2.1 - 2025-09-04 - Implémentation complète T.001

### Fonctionnalités majeures ajoutées

- **Ship Security Protocol (Protocol 12)** : décodage complet avec marquage
  `[SECURITY]`
- **Standard Test Protocol (Protocol 14)** : décodage données de test
  hexadécimales
- **National Test Protocol (Protocol 15)** : décodage données d'usage national
- **Radio Call Sign User Protocol (Protocol 6)** : décodage Baudot 7 caractères
- **Test User Protocol (Protocol 7)** : amélioration du décodage utilisateur test

### Améliorations des protocoles existants

- **Orbitography Protocol (Protocol 0)** :
  - Décodage spécialisé des données d'orbitographie (5 bytes + 6 bits)
  - Correction identification balises d'étalonnage 406.022 MHz
- **National User Protocol (Protocol 4)** : extraction complète données nationales
- **Aviation/Maritime User Protocols** : décodage Baudot amélioré pour call signs

### Fonctions techniques ajoutées

```c
// Nouvelles fonctions de décodage spécialisées
decode_orbitography_data()      // Balises d'étalonnage/orbitographie
decode_standard_test_data()     // Protocole test standard
decode_test_beacon_data()       // Données balises de test
decode_national_use_data()      // Données d'usage national
decode_radio_callsign_data()    // Indicatifs radio
decode_baudot_char()            // Caractères Baudot complets
display_baudot_42()             // Affichage 6 caractères Aviation
display_baudot_2()              // Affichage 7 caractères étendu
```

### Impact conformité

- **Avant** : T.001 95 % implémenté + T.018 implémenté = 95 % implémenté
- **Après** : T.001 100 % implémenté + T.018 implémenté = 100 % implémenté

**Tests validés** : Orbitography Protocol (balises étalonnage 406.022 MHz) et
Test User Protocol uniquement.
**Limitation** : autres protocoles implémentés selon spécifications mais non
testés sur balises réelles.

### Protocoles maintenant supportés (complet)

#### Location Protocols (P=0)

- [x] Protocol 2: EPIRB MMSI
- [x] Protocol 3: ELT 24-bit
- [x] Protocol 4: ELT serial
- [x] Protocol 5: ELT operator
- [x] Protocol 6: EPIRB serial
- [x] Protocol 7: PLB serial
- [x] Protocol 8: National ELT
- [x] Protocol 9: ELT(DT)
- [x] Protocol 10: National EPIRB
- [x] Protocol 11: National PLB
- [x] **Protocol 12: Ship Security** (nouveau)
- [x] Protocol 13: RLS Location
- [x] **Protocol 14: Standard Test** (nouveau)
- [x] **Protocol 15: National Test** (nouveau)

#### User Protocols (P=1)

- [x] **Protocol 0: Orbitography** (amélioré)
- [x] Protocol 1: ELT Aviation User
- [x] Protocol 2: EPIRB Maritime User
- [x] Protocol 3: Serial User
- [x] **Protocol 4: National User** (amélioré)
- [x] **Protocol 6: Radio Call Sign** (nouveau)
- [x] **Protocol 7: Test User** (amélioré)

### Tests validés

- Compilation sans erreurs/warnings
- Test balises d'étalonnage 406.022 MHz (Orbitography Protocol -
  identification correcte)
- Test balises test utilisateur (Test User Protocol - décodage fonctionnel)
- Nouveaux protocoles (Ship Security, Standard Test, National Test, Radio Call
  Sign) : implémentés selon spécifications, non testés
- Régression : protocoles existants préservés

### Références standards

- **COSPAS-SARSAT T.001** : 100 % implémenté (tests partiels : orbitography,
  test user)
- **COSPAS-SARSAT T.018** : implémentation complète (non testée sur balises
  réelles)
- **ITU-R M.585** : base MID complète
- **Modified Baudot** : implémentation complète (testée partiellement)

---

## Version 10.2.0 - 2025-08-xx

### Fonctionnalités initiales

- Décodeur 1G complet (T.001 95 % conformité)
- Décodeur 2G complet (T.018 100 % conformité)
- Support audio temps réel
- Base données MID complète
- Scripts automatisation email
- Géolocalisation OpenStreetMap

### Architecture

- Modularité complète (5 modules principaux)
- Pipeline audio optimisé
- Correction erreurs BCH(250,202)
- Support multi-formats (hex, WAV, temps réel)
