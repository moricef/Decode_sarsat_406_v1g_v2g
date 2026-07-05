# État du décodeur SGB — 20 juin 2026

## ⮕ ÉTAT ACTUEL VALIDÉ (21 juin) — à lire en premier

Synthèse fidèle aux données, sans aller au-delà. Les sections suivantes (correction CNES, baseline 93 %, verdict 0619) détaillent cet état validé. Le **journal d'enquête** complet — couches d'hypothèses, dont beaucoup annulées — est en annexe : **[SGB_INVESTIGATION_LOG.md](SGB_INVESTIGATION_LOG.md)** (ne pas le lire comme l'état courant).

- **0614** : échec du burst 2 = **précision fréquentielle**, résolu (fix `f5582a9`, raffinement post-acquisition, 2/2 décodés).
- **Critère `dt`** : **invalidé** — ne sépare pas balise/bruit (cf. CORRECTION ci-dessous). Toute la table de taux dt-filtrée est **caduque**.
- **firmin** = **balises système CNES** (calibration TAC 65535 + simulateur TAC 65532), pays 227, protocole test. Position décodée **réelle** (vrai champ lat/lon T.018), vérifiée IBRD + BCH valide.
- **0619** : **pas du bruit pur** (corrélation PRN 3.9σ au-dessus du null mesuré 0.079), **pas une collision simple**, structure PRN **faible mais non exploitable**.
- **Cause exacte 0619** : **ouverte**. Établi : cohérence DSSS détruite alors que l'énergie est là. Origine **non distinguée** (canal fading/multipath, ou chaîne de capture AGC/SDR/forme d'impulsion, ou interférence partielle) — aucune piste privilégiée par les données.
- **Récupération 0619** : aucun ajustement *mineur* du décodeur actuel n'y parvient ; un récepteur plus sophistiqué (RAKE/égalisation/diversité) n'est pas écarté.

**Outil** : `Corrected 250 bits: <hex>` (sortie SGB, left-pad T.018) → re-décodable directement par l'IBRD.

---

## ⮕ CORRECTION 21 juin (vérifiée) : firmin = balises système CNES, pas « balise + rafales-bruit »

**Corrige la fiche `dt` (déplacée en annexe), dont la prémisse « rafale = bruit incohérent indécodable » est fausse.** Établi par recoupement logs `juin_21*.log` + décodages officiels T.018 (outil IBRD) + dump `bits250` brut.

### Ce que décode réellement firmin — des balises système CNES Toulouse

Pays 227, protocole de test, « non-operational », même site, identités distinctes :

| 23 Hex ID | TAC | Type | Serial | Présence 21 juin |
|---|---|---|---|---|
| `9C77FFFE1A7F00000000000` | 65535 | **Balise de calibration** | 8615 | permanente, continue |
| `9C77FFF1B00F0001D000202` / `…203` | 65532 | **Simulateur de balise** | 6912 | intermittente, **06:45→06:56 seulement** |

`…202` et `…203` = **la même unité** (serial 6912) : les Vessel ID ne diffèrent que d'**un bit** (`…010` vs `…011`), un compteur de test. Pas deux balises.

### La position décodée est RÉELLE, pas fabriquée (sur-correction évitée)

dec406 sort pour la calibration FFFE « 43.56049°N 1.48080°E » = **CNES Toulouse**, à 5 décimales, répétable. Vérifié sur `bits250` : si elle venait des bits de l'UIN (23-Hex), `decode_position` lirait bits 45-51 = `1111000` = 120 → lat **−120°** (impossible). Donc la position vient du **vrai champ localisation T.018** (champ principal 154 bits), pas de l'UIN. Une balise de calibration émet sa position fixe connue. **Pas de bug de position** — l'hypothèse « dec406 fabrique la position » est retirée. L'outil IBRD ne montre pas de position car il ne décode que le 23-Hex identifiant (sans localisation) ; son silence ≠ absence.

### Conséquences pour le taux

- **Le critère `dt` ne sépare PAS balise/bruit.** Beaucoup de bursts `dt<3 s` syncent z>100 puis échouent au **BCH** (`FRAME REJECTED`, 14 matin / 23 nuit), d'autres décodent. `dt` = « burst précédé de près » vs « isolé », sans rapport avec la décodabilité. Avec plusieurs balises voisines une vraie balise hérite souvent d'un `dt<3 s`. **Exclure les `dt<3 s` jette de vraies balises.**
- **Succès = `bch … status=OK` / `BCH validated`, PAS `SGB frame decoded`** (sync seule ; le BCH rejette ensuite). Compter `frame decoded` comme succès gonfle.
- Compter par **événement de décode distinct** (Hex ID dédupliqué) vs échecs réels (`decode failed` z=0 OU `FRAME REJECTED`). Ne pas filtrer par `dt`.

### Outil ajouté (21 juin)

dec406 imprime désormais `Corrected 250 bits: <hex>` à côté du `23 Hex ID` (post-BCH), pour re-décoder les champs hors-ligne et croiser avec l'IBRD. Code : `decode_2g` + `bch_decode_250_202` (param `cw_out`), `src/dec406_v2g.c`. Le dump brut DIAG `bits250=` (`src/dsss_demod.c`) garde son format right-pad d'origine (non touché).

**Format = convention T.018 : 63 hex = 252 bits, 2 zéros de rembourrage à GAUCHE** (pas à droite). Bug attrapé le 21 juin via cross-check IBRD : un rembourrage à droite décale tout de 2 bits → l'IBRD lit TAC 65534 au lieu de 65535, position 46.24 S au lieu de 43.56 N, « BCH mismatch ». Corrigé. Le hex IBRD-décodable pour la calibration FFFE est `3FFFE1A738C95C7BE00BD8BE00000000001FFFF0FFFF7FFFEF0470400A448FD`.

**Confirmation IBRD finale (preuve définitive)** : ce hex left-paddé décode dans l'IBRD avec « Left pad 00 », TAC 65535 calibration, serial 8615, France, **Latitude 43.56049 N** (bits 44-66), **Longitude 1.4808 E** (bits 67-90), Vessel ID Type 111 (system testing, bits 94-137 à zéro), champ rotatif G.008, et surtout **`VALID BCH: COMPUTED BCH MATCHES`**. Tout matche la sortie dec406. Clôt les 3 sur-corrections : la position est réelle (vrai champ lat/lon T.018, pas l'UIN), le codeword est authentique, dec406 décode juste.

### Leçon méthodo (2 sur-corrections ce fil)

(1) « balise + rafales-bruit » → faux ; (2) « positions au mètre = balises physiques distinctes » puis « positions fabriquées = bug » → les deux faux. À chaque fois, conclusion confiante avant vérification. Ce qui a tranché : **recouper sur la donnée brute (`bits250`) et un décodeur de référence (IBRD)**, jamais sur l'impression.

---

## Taux SGB — décomposition par étage (validé 21 juin)

**Le taux SGB n'est PAS un nombre, c'est trois — un par étage de la chaîne.** Un seul chiffre conflate décodeur et réception et induit en erreur.

Chaîne : `détection → freq_acq + despread sync (DÉMODULATEUR) → BCH/trame (DÉCODEUR PUR)`.

**Calcul depuis les logs `scan406`** (par burst, pas par tentative) : suivre le **meilleur `combined=` z** depuis chaque ligne `BURST … SGB` jusqu'à son issue.
- **Décode** = `BCH validated`.
- **BCH KO** = `FRAME REJECTED` (a synchronisé, best-z ≥ 20, mais BCH non corrigeable).
- **Jamais-synchro** = `decode failed` avec best-z < 20 (rejeté à freq_acq ou despread).

| Étage | Définition | Mesure |
|---|---|---|
| **Décodeur pur** | décode / (décode + BCH KO) — *conditionné à la synchro* | **~81 %** (stable nuit/jour) |
| **Démodulateur** | (décode + BCH KO) / total détecté — synchro \| détecté | **~33 %** |
| **Bout-en-bout** (démod+décodeur) | décode / total détecté | **12 % nuit → 26 % jour** |

**Mesures 21 juin** (`juin_21.log` 00:00→ avec nuit, `juin_21-0500.log` 05:00→ sans nuit) :

| | avec nuit | sans nuit |
|---|---|---|
| décodent / jamais-synchro / BCH KO | 202 / 1375 / 46 | 137 / 351 / 33 |
| **décodeur pur** | **81 %** | **81 %** |
| **bout-en-bout** | **12 %** | **26 %** |
| FGB (CRC OK) | 94.2 % | 95.6 % |

**Lecture :**
- **Le décodeur pur est invariant (81 %)** nuit/jour. Ce n'est PAS le décodeur le facteur limitant.
- **Le bout-en-bout chute la nuit (26 → 12 %)**, et l'écart est **entièrement** dans le bucket « jamais-synchro » (351 → 1375, +1024). C'est la **famille 0619** (réception dégradée, préambule incohérent, z plafonné ~5-9 famille-entière — voir VERDICT 0619) + l'interférence nocturne.
- **Donc 0619 pollue le taux *système*, jamais le taux *décodeur*** : ces bursts ne synchronisent jamais (z<20), ils tombent en réception. Le démodulateur les rejette **correctement** (0614 cohérent → z=53 ; 0619 incohérent → z~5).
- ⚠️ Le bucket « jamais-synchro » mélange **0619-réception-limitée** ET **faux positifs bruit/interférence** (classés SGB par bande passante) — **non séparables du log seul** (les deux donnent z≈0, rejet avant despread). Pour le ratio exact : forcer despread offline (`ACQ_CONF_MIN`) sur un échantillon (z~5 = vraie 0619 vs z<4 = bruit).
- ⚠️ Piège `dt`/exclusion : ne PAS retirer les « jamais-synchro » pour annoncer 81 % comme taux global. 81 % = décodeur pur (conditionnel). Le système reste à 12-26 %.

**Levier pour monter le bout-en-bout : le front-end (SDR, gain, antenne), pas l'algorithme.** Le décodeur tourne déjà à 81 %.

---

## Situation actuelle : 93 % sur firmin (13/14 bursts)

Commit de référence : `b401697` sur `main`.

Fichier OTA de référence : `logs/gqrx_20260614_132453_406051000_2457600_fc.sigmf-data` — 2 bursts SGB, les deux décodent sans erreur BCH.

### Les 3 corrections du 14 juin (commit c6e3914)

Ces trois bugs corrigés ensemble ont fait passer le taux de 29 % à 93 % :

1. **Polarité inversée** (`despread.c`) — la spec T.018 dit que data=1 inverse le code PRN, ce qui donne une corrélation négative. Le code testait `> 0` au lieu de `< 0`, donc tous les bits sortaient à l'envers.

2. **Estimation de fréquence sur le préambule** (`despread.c`) — au lieu de laisser la boucle de phase converger lentement (trop lentement pour un burst court), on fait une régression linéaire sur les 25 phases du préambule connu pour initialiser directement la fréquence et la phase.

3. **Recherche BCH incomplète** (`dec406_v2g.c`) — le code BCH(250,202) est raccourci depuis un BCH(255,207). La recherche de racines (Chien) doit parcourir les 255 positions, pas seulement 250 — sinon on rate les racines dans la zone de bourrage.

## Problème ouvert : l'enregistrement du 19 juin ne décode pas

Fichier : `logs/gqrx_20260619_101522_406051000_2457600_fc.sigmf-data` (125 s, une balise SGB à ~406.051 MHz, 0 burst décodé).

### ⮕ VERDICT 21 juin (attaque par élimination) — PAS bruit, PAS collision simple ; cohérence DSSS perdue, cause physique ouverte

**Remplace les conclusions « ≈ bruit » / « cohérence 0.094 » de l'annexe, qui comparaient au mauvais null.** Trois expériences contrôlées (scripts `prn_null.m`, `prn_multipeak.m`, `prn_envelope.m`, toutes validées sur 0614 connu-bon).

1. **Distribution nulle (le point clé).** Le null du doc (0.06) était le plancher *par bloc*, pas la stat réellement utilisée (max sur 6000 lags). Vrai null mesuré (300 références PRN aléatoires sur les données 0619, coloration préservée) : **μ=0.079, σ=0.0022**, quantile 99.9% = 0.0864. Cohérence-bloc 0619 = **0.087 → 3.9σ au-dessus, 0/300 au-dessus**. **0619 n'est PAS du bruit** : corrélation PRN réelle, spécifique au bon code, à -105 Hz (= fréquence trouvée indépendamment par dec406). Contrôle 0614 = 0.646 = **270σ**. **Précision (lag check 21/06)** : ce 3.9σ est au **pic global, lag 26385 = mid-burst (0,66 s), PAS au préambule** ; le préambule (zone onset) reste à **z~6**, dispersé, sans pic dominant. Trois niveaux nets, robustes à 5 métriques : **0614 préambule z=53 ≫ 0619 meilleur pic z~6.9 > bruit z~4-5**. La structure PRN réelle existe mais **faible ET non localisée au préambule** → despread ne synchronise pas. (Le « patch » blocs 19-21 est mid-burst, **pas** un segment de préambule survivant.)

2. **Carte multi-pics.** 0614 = un pic dominant (53 vs ≤5). 0619 = **aucun pic dominant** (6.2/5.6/4.8… éparpillés). → **pas de collision** (qui donnerait 2 pics forts à 2 lags). La corrélation réelle est diluée, pas concentrée.

3. **Enveloppe temporelle.** 0619 vs 0614 : durée 1122 vs 1148 ms (**pas de troncature**), clipping 3.5e-7 identique (**pas d'écrêtage**), 0 dropout profond (**pas de gap**), mais **CV d'enveloppe 0.608 vs 0.316 = 2× plus variable**. Fait brut. L'interprétation « fading » n'est PAS prouvée : AGC, co-canal, somme de signaux, fenêtrage, artefact SDR augmentent aussi la variance d'enveloppe.

**Bilan par élimination** — écartés par mesure : bruit/PRN absent, collision deux-bursts, troncature, écrêtage, gaps de samples, erreur de timing sous-chip simple (les offsets testés). ⚠️ « écarté » = le concurrent ne rend pas compte des données, pas « impossible dans l'absolu » (un défaut de synchro plus subtil reste concevable). **Ce qui reste établi** : une dégradation qui **détruit la cohérence DSSS en préservant l'énergie**. **Son origine n'est PAS distinguée par les données** — canal (fading/multipath), chaîne de capture (AGC/SDR/rééchantillonnage/désaccord de forme d'impulsion) et interférence DSSS partielle restent **toutes compatibles** ; aucune n'est privilégiée par les mesures. ⚠️ Les 3 observations (corrélation étalée, enveloppe plus variable, bosse spectrale ~50 kHz) ne sont **pas indépendantes** : même signal reçu, manifestations possibles d'un seul phénomène, pas 3 preuves. **Statut : cause physique ouverte. Écarter des concurrents ≠ prouver le dernier survivant.**

**Conséquence (bornée)** : rien ne montre qu'un **ajustement mineur** du décodeur actuel récupère les bursts type-0619 (fréquence, signe, gate, cadence, alignement tous testés). Ce n'est PAS « aucun décodeur ne peut » : si c'est du canal, un récepteur plus sophistiqué (RAKE, égalisation, diversité, corrélateur avancé) pourrait en principe récupérer une part de l'énergie corrélée. À compter pour l'instant comme perte côté décodeur actuel. Le détail des essais (freq, gate, sweep wipeoff, cohérence par blocs…) est dans le journal d'enquête en annexe (certaines conclusions y sont dépassées par ce verdict).

---

## Journal d'enquête (annexe)

La trace complète des hypothèses (dont beaucoup annulées), la fiche `dt` dépassée, le stash git et la chronologie SGB sont dans **[SGB_INVESTIGATION_LOG.md](SGB_INVESTIGATION_LOG.md)**. Ne pas la lire comme l'état courant.

---

## Diagnostic local RTL/Yagi — 4 juillet 2026

### Fait déclencheur

Sur le signal local RTL+Yagi, le préambule SGB synchronise très fort (`z` typiquement 70-110) mais les 250 bits extraits sont aléatoires : environ 125 bits de différence entre bursts et contre la référence, donc ~50 %. `nerr=6` n'est pas "6 erreurs" mais la saturation Berlekamp-Massey sur un mot aléatoire.

### Hypothèse testée

La rupture n'est pas une réception faible : le signal est visible et fort. Hypothèse mesurable : perte/discontinuité d'échantillons dans le chemin RTL unifié avant le ring scanner. Une perte silencieuse casse la cohérence PN après le sync ; le préambule court peut encore corréler, mais les bits data deviennent incohérents. Signature attendue : débit RTL effectif inférieur à 2.4576 MS/s et sauts de phase sur la porteuse CW FGB.

### Instrumentation ajoutée temporairement

- `DUMP_SGB=1` dans `src/scanner.c` : dump de chaque fenêtre SGB dispatchée, post-NCO, en `sgbwin_*.cf32`.
- `DUMP_FGB=1` dans `src/scanner.c` : dump de chaque fenêtre FGB dispatchée, post-NCO, en `fgbwin_*.cf32`.
- compteur de débit dans `src/backend_rtlsdr.c` : log `[rtl] throughput ... S/s`.

### Résultats mesurés

Commande locale : `DUMP_FGB=1 DSSS_DIAG=1 ./build/dec406_scan 406.0M 406.1M`.

Débit observé sur ~85 s : environ 2 449 000 à 2 450 700 S/s pour un nominal de 2 457 600 S/s, soit un déficit d'environ 7 000 à 8 600 samples/s (~0.3 %, ~3000 ppm). Ce niveau est trop grand pour être interprété comme une simple erreur de quartz ppm dans ce contexte logiciel : le scanner reçoit moins d'échantillons qu'il n'en suppose.

FGB locaux sur le même run : CRC FAIL systématiques, préambules variables (9-14/15) et phases 0/45/90 degrés. Même signature générale que SGB local : la synchronisation courte survit partiellement, la cohérence longue échoue.

### Prochain test avant correction

Analyser la phase CW des dumps `fgbwin_*.cf32`. Si la phase présente des sauts courts et nets, le diagnostic "échantillons perdus avant ring" est confirmé. Si la phase est continue, il faut repartir du backend/scanner sans conclure à la perte USB.

### Résultats phase/rejeu offline

Analyse hors ligne des dumps FGB `fgbwin_114130_-27935Hz.cf32`, `fgbwin_114200_-27886Hz.cf32`, `fgbwin_114230_-27884Hz.cf32`, en reproduisant la décimation interne FGB vers 9600 Hz puis la zone CW utilisée par `estimate_cw_freq()` :

- fréquences CW retrouvées identiques au log scanner : +31.7 Hz, -10.6 Hz, -20.0 Hz ;
- sauts de phase ponctuels dans la zone CW : jusqu'à -2.73 rad sur `114130`, -2.42 rad et -1.23 rad sur `114230` ; `114200` plus propre mais bruité ;
- ordre de grandeur cohérent avec des pertes courtes : déficit moyen ~7400 samples/s, soit ~80 us par bloc de 65536 samples si la boucle synchrone ne lit pas en continu.

Test concurrent : rejouer `sgbwin_113253_3693Hz.cf32` avec `./build/dec406_iq ... -s 2450200` au lieu de 2457600 ne récupère pas le BCH et dégrade le `z` (nominal `z=85`, taux mesuré `z=24`). Le problème n'est donc pas seulement un mauvais taux constant corrigeable par `-s`; la signature pointe plutôt vers des discontinuités/jitter d'échantillons dans le flux RTL synchrone.

### Hypothèse courante

Le backend RTL synchrone (`rtlsdr_read_sync` + conversion + `scanner_push`) ne garantit pas un flux continu à 2.4576 MS/s dans ces conditions. Les pertes ou micro-trous ne sont pas visibles comme `scanner` ring overruns parce qu'ils se produisent avant l'écriture dans le ring. Effet attendu :

- FGB : CW/preamble partiellement OK, phases Costas erratiques, CRC FAIL ;
- SGB : corrélation préambule très forte, puis PN/data incohérents après quelques discontinuités, BCH aléatoire (`nerr=6`).

### Prochaine correction proposée, à valider avant codage

Remplacer le chemin RTL temps réel par une capture asynchrone librtlsdr (`rtlsdr_read_async`) avec plusieurs buffers larges, et ne garder le compteur de débit/jitter qu'en diagnostic. Test minimal attendu : le débit doit se stabiliser à 2.4576 MS/s dans la tolérance, les sauts de phase FGB doivent disparaître ou fortement baisser, puis FGB CRC OK et SGB BCH OK doivent être retestés localement.

Validation utilisateur reçue le 4 juillet 2026 : appliquer cette correction en première tentative.

Implémentation tentative 1 : `src/backend_rtlsdr.c` utilise maintenant `rtlsdr_read_async` avec 32 buffers de 256 KiB et conversion IQ dans le callback. `rtlsdr_stop_dev()` appelle `rtlsdr_cancel_async()` pour arrêter proprement la lecture. Build vérifié : `make build/dec406_scan` OK. Régression synthétique vérifiée : `./build/dec406_iq ../../GNURADIO/test_sgb_halfsine.sigmf-data -s 2457600` OK, BCH validé `nerr=0`.

Validation matérielle encore à faire : relancer le scanner local RTL/Yagi et comparer le débit `[rtl] throughput`, les CRC FGB et les BCH SGB.

Validation matérielle locale `logs/CNES_local_20260704_1203.log` :

- débit RTL stabilisé à ~2 457 580 S/s après quelques secondes, écart final ~15-20 S/s du nominal 2 457 600, contre ~7 400 S/s manquants avant async ;
- 0 ring overrun ;
- FGB : 22 CRC OK / 0 CRC FAIL sur le run, contre échecs systématiques avant async ;
- SGB : 2 BCH validés (`nerr=0`, hex corrigé CNES calibration identique), 0 `FRAME REJECTED`;
- SGB restants : 6 `decode failed (z=0.0)` avec rejet acquisition coarse `conf 2.5-3.7`, donc pas le symptôme précédent "sync fort puis bits aléatoires".

Conclusion tentative 1 : correction validée pour le problème local RTL/Yagi "sync préambule fort, data bruit". La cause principale était bien le chemin RTL synchrone qui sous-alimentait le scanner / introduisait des discontinuités avant le ring. Les échecs SGB restants appartiennent à un autre bucket : acquisition faible/rejet front-end sur bursts SNR 6-7 dB, à traiter séparément si nécessaire.

Nettoyage pré-commit validé : les dumps temporaires `DUMP_FGB` et `DUMP_SGB` ont été retirés de `src/scanner.c`. Le compteur throughput RTL est conservé uniquement derrière `RTL_DIAG=1` pour pouvoir contrôler le débit terrain sans polluer les logs de production.

Run propre final `logs/CNES_local_async_clean_20260704_122327.log` (sans dumps, avec `RTL_DIAG=1`) :

- débit final stable à ~2 457 590 S/s, soit ~10 S/s sous le nominal ;
- 0 overrun reporté ;
- FGB : 12 CRC OK / 0 CRC FAIL ;
- SGB : 2 BCH validés (`nerr=0`) / 0 `FRAME REJECTED` / 0 `decode failed` ;
- le symptôme initial "préambule fort puis data bruit pur" n'apparaît plus.

Statut : fix prêt à commit après sélection des fichiers (inclure `src/backend_rtlsdr.c` et ce journal ; exclure les dumps `.cf32` et autres fichiers de diagnostic non suivis).

## Intégration fredzo BCH2 / SGB detection — 4 juillet 2026

Contexte : la branche locale `fredzo-sgb-fix` contient l'adaptation des correctifs fredzo issus de `fredzo/bch2_correction`. Le remote brut est basé sur une architecture plus ancienne et supprimerait le scanner unifié ; il ne doit pas être mergé directement. Stratégie validée : cherry-pick de `fredzo-sgb-fix` vers `main`, puis build + synthétique.

## Acquisition SGB — bandpass sample-rate avant boxcar — 4 juillet 2026

Contexte après async RTL + correctifs fredzo : sur firmin, le décodeur post-sync ne rejette plus les trames SGB validées (`FRAME REJECTED=0` après 13:12), mais la métrique périodique de la balise CNES reste autour de 23 validations sur 37 créneaux de 150 s, soit environ 62 %. Les échecs restants sont dominés par l'acquisition (`coarse reject conf 2-3`) et non par le BCH ou par l'extraction de bits après synchronisation.

Hypothèse : le boxcar d'acquisition décime directement de 2.4576 MS/s à 38.4 kchips/s. Sa Nyquist chip-rate est +/-19.2 kHz ; les composantes hors bande présentes après mixage du burst (interférences ou rafales voisines vers +/-55 à 100 kHz) se replient donc dans le flux chip-rate avant `freq_acq_fft_corr()`. Cela augmente la médiane/mean de corrélation et fait tomber la confiance coarse sans forcément toucher au pic vrai.

Correction proposée et validée : appliquer un passe-bas acquisition-only au sample rate, juste après le DC blocker et avant le boxcar utilisé par `freq_acq_fft_corr()`. Le chemin post-sync reste inchangé : NCO wipeoff, OQPSK delay, boxcar final, `despread_bits()` et BCH continuent d'utiliser le buffer DC-blocked non filtré. Le filtre est réglable par `ACQ_BANDPASS_HZ`, défaut 45000 Hz, `0` désactive le filtre pour A/B.

Critères de validation : synthétique SGB obligatoire, builds `dec406_iq` et `dec406_scan`, puis terrain local/firmin. Sur firmin, comparer la grille 150 s avant/après : référence avant bandpass ~23/37 = 62 %, avec `FRAME REJECTED` qui doit rester à 0. Si les créneaux manqués deviennent des BCH OK sans réintroduire de rejets post-sync, l'hypothèse acquisition est confirmée.

Résultat intermédiaire local/firmin : le bandpass 45 kHz améliore nettement le local RTL/Yagi (~89-90 % sur grille 150 s, `FRAME REJECTED=0`) mais pas firmin, qui reste autour de 57-58 % sur la fenêtre mesurée contre ~62 % avant. Les rejets firmin conservent des `conf` autour de 1.9-3.5, proche des rejets avant filtre. Conclusion : ne pas élargir le cutoff à l'aveugle. La donnée manquante est `peak/median/mean` sur les `coarse reject`, déjà logguée sur les acquisitions acceptées.

Instrumentation validée : enrichir la ligne `coarse reject` dans `freq_acq_fft_corr()` avec `freq`, `lag`, `peak`, `median`, `mean` et `phase`, sans modifier le seuil ni le chemin de décodage. Test attendu sur firmin : comparer les vrais créneaux SGB isolés rejetés avec et sans filtre pour déterminer si la confiance basse vient d'un pic faible, d'une médiane/mean élevée ou d'une mauvaise localisation lag/fréquence.

Résultat firmin `scan406_butterworth_diag_20260704_1632.log` : les vraies SGB isolées ratées ont une médiane normale, typiquement `1.3e12` à `2.2e12`, proche des SGB OK (`1.4e12` à `1.8e12`). En revanche leur `peak` est très faible, typiquement `2.7e12` à `5.2e12`, alors que les SGB OK montent à `7.3e13` à `1.5e14`. Le ratio de pic est donc de l'ordre de 20 à 40, tandis que le niveau de fond de corrélation n'est pas le facteur discriminant. Les paquets groupés ont souvent une médiane plus haute (`~3e12` à `4e12`) mais ce ne sont pas les créneaux isolés décodables recherchés.

Conclusion : le bandpass ne traite pas la cause des échecs firmin sur vraies SGB isolées. La `conf` basse vient d'un pic PRN absent/faible, pas d'une `mean`/`median` gonflée par hors-bande. Ne pas régler le cutoff à l'aveugle ; le prochain chantier doit expliquer pourquoi le pic de préambule disparaît sur certains créneaux pourtant détectés en énergie/BW/SNR.

Instrumentation suivante validée : capturer des fenêtres IQ SGB comparables pour analyse offline. `DUMP_FAIL=1` existe déjà dans `scanner.c` pour les échecs SGB. Ajouter `DUMP_OK=1` côté succès afin de récupérer aussi un burst décodé, avec la même fenêtre post-NCO que celle transmise à `dsss_receive_burst()`. Les dumps OK/FAIL serviront à comparer la cohérence par blocs et le pic PRN sans modifier la chaîne de réception.

Résultat cohérence par blocs (méthode `prn_blocks.m`, cohérence normalisée par blocs de 256 chips, bruit attendu ~0.06) sur les dumps firmin :

- `sgb_ok_173027_-6028Hz.cf32` : `global z=60.8`, cohérence bloc min/mean/max `0.50/0.54/0.57`.
- `sgb_ok_172757_-6084Hz.cf32` : `global z=75.7`, cohérence bloc `0.65/0.67/0.71`.
- `burst_sgb_173255_-7478Hz.cf32` : `global z=6.1` sur un pic tardif, cohérence bloc `0.03/0.08/0.13`.
- `burst_sgb_173525_-8449Hz.cf32` : `global z=5.7`, cohérence bloc `0.01/0.08/0.18`.

Interprétation : les bursts OK ont une cohérence PRN forte et uniforme sur tout le préambule. Les FAIL ne montrent pas de zones localement bonnes ou de rupture progressive ; la cohérence reste au niveau bruit sur les 25 blocs. Cela confirme le profil 0619 : énergie détectée mais structure DSSS absente/incohérente à l'acquisition, plutôt qu'un problème de fond de corrélation ou de cutoff.

Décision de stabilisation : `ACQ_BANDPASS_HZ` repasse à `0` par défaut. Le filtre reste dans le code pour diagnostics A/B (`ACQ_BANDPASS_HZ=45000`) mais ne doit pas être présenté comme correctif firmin. Les éléments utiles à conserver temporairement sont l'instrumentation `peak/median/mean` des rejets coarse et les dumps conditionnels `DUMP_FAIL`/`DUMP_OK`.

## Chronologie fine du log firmin 16:32→17:13 — 4 juillet 2026 (soir)

Analyse burst par burst de `scan406_butterworth_diag_20260704_1632.log` (41 bursts SGB, dt et peak/mean par rejet). Trois faits nouveaux :

1. **La « salve » est une séquence programmée de période 30 minutes pile.** Départs mesurés 16:40:07 et 17:10:07 (écart 30:00) : 6 bursts espacés de 5 s, puis ~8 espacés de 30 s. BW 44-53 kHz, SNR 10-11, p/m 2-4, jamais décodable. C'est la « source unique » diagnostiquée le 18/06, désormais identifiable par sa signature temporelle. Elle pollue tout tri par grille pendant ses fenêtres d'activité.

2. **La calibration 65535 rate aussi quand le canal est libre.** Fenêtre 16:47:53→17:07:53 sans aucune salve : 2 décodées sur 9 créneaux de 150 s (p/m des ratés 1.9-2.2). La collision avec la salve n'explique donc pas les échecs d'acquisition.

3. **Le basculement décodé/raté est binaire.** 16:55:23 raté p/m 2.0 → 16:57:53 décodé p/m ~50, même balise, même trajet, SNR identique (8-9 dB). Les conf observées sont toujours soit 2-4 soit 45-65, jamais intermédiaires. Un fading continu donnerait des valeurs intermédiaires ; le tout-ou-rien reste à expliquer.

Taux calibration sur cette fenêtre : 4/17 créneaux = 24 %, contre 62 % à 13-14 h le même jour, récepteur identique, FGB stable à ~91 % pendant toute la période → la variabilité est côté trajet/émission, pas côté récepteur.

Rapprochement : ce profil (énergie présente, structure PRN détruite, z/conf plafonnés bas, famille entière) est la signature du dossier 0619 (voir VERDICT 21 juin). Le goulot d'acquisition firmin sur vraies SGB isolées = vraisemblablement le même phénomène, cause physique toujours ouverte. Les dumps `DUMP_OK`/`DUMP_FAIL` + cohérence par blocs (scripts `prn_*.m` validés sur 0614/0619) sont l'outil prévu pour trancher.

## Branche tracking offline — 4 juillet 2026

Branche : `experiment/sgb-tracking-offline`.

Objectif validé : ne pas réintégrer directement la tentative historique FLL/PLL/DLL/Kalman. Avant tout tracking sample-rate, mesurer offline si les bursts FAIL contiennent des mesures Prompt/Early/Late exploitables. Si le Prompt bloc est au niveau bruit, une boucle FLL/DLL/Kalman ne peut pas verrouiller proprement.

Outil ajouté : `utils/sgb_epl_diag.c`, cible `make build/sgb_epl_diag`. L'outil lit une fenêtre `cf32`, reproduit le front-end SGB actuel jusqu'au chip-rate (DC blocker, acquisition FFT-corr, wipeoff sample-rate, délai OQPSK, boxcar), cherche le meilleur préambule même sous seuil, puis sort un CSV par bloc de 256 chips :

- `prompt_mag`, `prompt_coh`, `prompt_phase_rad` ;
- fréquence bloc à bloc dérivée de la phase Prompt ;
- Early/Late à +/-1 chip et discriminateur DLL approximatif.

Résultat sur les dumps firmin déjà analysés :

| Dump | freq_acq conf | sync_z | Prompt cohérence min/mean/max | blocs > 0.25 | freq RMS bloc |
|---|---:|---:|---:|---:|---:|
| `sgb_ok_173027_-6028Hz.cf32` | 60.5 | 84.9 | 0.768 / 0.794 / 0.822 | 25/25 | 0.9 Hz |
| `sgb_ok_172757_-6084Hz.cf32` | 64.6 | 91.8 | 0.845 / 0.867 / 0.884 | 25/25 | 0.8 Hz |
| `burst_sgb_173255_-7478Hz.cf32` | 2.0 | 6.0 | 0.024 / 0.072 / 0.135 | 0/25 | 42.6 Hz |
| `burst_sgb_173525_-8449Hz.cf32` | 1.9 | 5.6 | 0.003 / 0.061 / 0.129 | 0/25 | 38.2 Hz |

Interprétation : les OK offrent un Prompt fort, uniforme, et une phase bloc stable. Les FAIL n'ont aucun bloc localement exploitable ; le meilleur pic et le second pic sont quasi égaux et la phase bloc saute comme du bruit. À ce stade, un tracking FLL/DLL/Kalman classique n'a pas de mesure fiable à verrouiller sur ces FAIL. Le tracking peut rester une piste pour des captures "faibles mais cohérentes", mais les FAIL firmin mesurés appartiennent au bucket "préambule PRN incohérent", pas au bucket "tracking insuffisant".

### Compléments cross-validation (2e chaîne, Octave) — 4 juillet 2026 soir

La même paire a été mesurée indépendamment avec `prn_blocks.m` (Octave) : mêmes conclusions que `sgb_epl_diag` (OK z=57.8 coh 0.50-0.57 uniforme ; FAIL au null 0.07 sur 25/25 blocs). Trois points supplémentaires :

1. **Granularité** : la cohérence des FAIL est détruite en dessous de 6,7 ms (un bloc de 256 chips), sur tout le préambule — pas de rupture progressive ni de zones survivantes.
2. **Cause physique (fait brut, non tranché)** : une décorrélation sous 6,7 ms est inhabituelle pour un fading de trajet fixe 80 km (normalement lent, échelle de secondes). « Émission dégradée par intermittence côté source CNES » monte au même rang que « canal » parmi les candidates.
3. **⚠️ Piège d'application `prn_blocks.m` sur les dumps scanner** : le résiduel post-NCO peut atteindre ±8 kHz (ex. OK 173027 = +7075 Hz). Une plage de recherche trop étroite fait sortir même un burst décodable au niveau bruit (1er essai invalide sur ce piège). Contrôle positif obligatoire avant toute conclusion sur un FAIL. Le FAIL 173255 a été balayé sur tout ±8 kHz au pas de 10 Hz : aucun pic nulle part, pas même à +7075 Hz où la même balise émettait 150 s plus tôt.

## ⮕ RENVERSEMENT — les « FAIL » firmin étaient des bursts SAINS hors fenêtre d'acquisition — 4 juillet 2026 (soir)

**Corrige les sections « cohérence par blocs » et « tracking offline » ci-dessus pour les dumps firmin du 4/07 : leurs conclusions (« structure PRN détruite », « profil 0619 », « rien à tracker ») étaient des artefacts d'une recherche de fréquence trop étroite — dans les scripts ET dans le décodeur lui-même.**

### Découverte

Incohérence relevée dans les noms de dumps : la balise calibration est fixe à ~+1047 Hz du centre (établi via l'OK : mixé −6028, résiduel retrouvé +7075). Or les FAIL étaient mixés à −7478 et −8449 → résiduels attendus **+8525 et +9496 Hz, hors de la fenêtre ±8 kHz de `freq_acq`** (et hors du balayage ±8 kHz des scripts d'analyse, qui héritaient du même angle mort).

Vérification ciblée (`prn_blocks.m`, plages 8200-8900 et 9100-9900) :

- `burst_sgb_173255` à **f0=+8525** : z=53,9, préambule au lag exact (7670), cohérence **0,48-0,55 uniforme** ;
- `burst_sgb_173525` à **f0=+9495** : z=71,5, cohérence **0,61-0,69**.

Bursts parfaitement sains. Le « basculement binaire » = résiduel dans la fenêtre (décode, les OK étaient à +7075, près du bord) ou hors fenêtre (conf ~2 garanti).

### Cause amont

Le **centroïde de `measure_burst()` dérape de 7 à 9,5 kHz** (moyenne spectrale seuillée à −10 dB, tirée par bruit/interférence in-band). Ce biais n'est pas borné par nature. Pourquoi il est systématiquement négatif ce soir-là : non élucidé, mais rendu inoffensif par le fix.

### Fix appliqué (une ligne + commentaire)

`src/dsss_demod.c` : fenêtre `freq_acq_fft_corr` élargie de ±8 kHz à **±16 kHz**. Dimensionnement : pire dérive mesurée 9,5 kHz (non bornée, 4 échantillons) + garde ; plafond physique = repli du boxcar chip-rate à ±19,2 kHz. Le coût est ~2× le temps d'acquisition coarse ; les faux pics restent contenus par le seuil de conf (vrais pics 40-70 vs seuil 8), le cap de lag et le BCH.

### Validation offline

- Synthétique : conf 68,5, BCH nerr=0 (pas de régression).
- `burst_sgb_173255` : **BCH validated nerr=1**, +8524 Hz, conf 43,3, calibration 65535.
- `burst_sgb_173525` : **BCH validated nerr=0**, +9495 Hz, conf 41,0.

### Conséquences à retenir

1. Les conclusions « famille 0619 » appliquées aux échecs firmin du 4/07 sont **retirées** — ces bursts étaient sains. Le VERDICT 0619 originel (fichier gqrx du 19/06, capture centrée sur la balise, pas de pré-mix scanner) reste valide tel quel : son analyse cherchait à la bonne fréquence.
2. Le bucket « jamais-synchro » historique de firmin (ex. 1375 bursts la nuit du 21/06) contient une part inconnue de **hors-fenêtre récupérables** — le taux bout-en-bout historique est sous-estimé. À re-mesurer avec la fenêtre ±16 kHz.
3. Leçon méthodo (3e occurrence du même piège, désormais à trois niveaux : script d'analyse, outil EPL, décodeur) : **toute recherche de fréquence doit couvrir l'incertitude réelle de son a priori, et tout « pas de signal » vaut seulement dans la plage cherchée.** Contrôle positif obligatoire, et vérifier les bornes AVANT de conclure à la destruction du signal.

Reste à faire : test firmin live (grille 150 s attendue en forte hausse vs 62 %/24 %), puis commit après validation terrain.

### Validation terrain de la fenêtre ±16 kHz — 4 juillet 2026, 21:20→21:58

Déploiement sur firmin (branche `experiment/sgb-tracking-offline`, commit `ad52646`) via le nouveau workflow git (`docs/DEPLOY_FIRMIN.md`), service redémarré vers 21:19. Logs suivis en parallèle : `scan406_butterworth_diag_20260704_2120.log` (firmin) et `CNES_local_async_Butterworth_20260704_212211.log` (local).

Résultats sur ~37 min (comparer à 62 % l'après-midi et 24 % en début de soirée, même méthode grille) :

- **SGB firmin : 14/15 créneaux de 150 s = 93 %.**
- **Les 14 décodées ont TOUTES un résiduel entre +8,9 et +11,0 kHz** — chacune aurait été rejetée par l'ancienne fenêtre ±8 kHz. Avec l'ancien code, la soirée serait à 0 %. Preuve directe et individuelle de l'effet du fix.
- Le dérapage du centroïde atteint désormais ~11 kHz (vs 9,5 kHz mesuré l'après-midi) : le dimensionnement ±16 kHz (plutôt que ±12) était justifié — confirme que l'erreur du centroïde n'est pas bornée par les observations passées.
- SGB local : 14/28, rien de cassé (résiduels locaux < 8 kHz : le centroïde local ne dérape pas comme firmin).
- **FGB : ~90 % des deux côtés.** Creux transitoire 21:33→21:40 observé simultanément sur les DEUX sites (récepteurs et antennes indépendants, 80 km d'écart) → balise/propagation, sans lien avec le code ; refermé de lui-même. 0 fenêtre écrasée, 0-1 overrun.

Note de comptage : ces 62 %/24 %/93 % sont la grille 150 s de la calibration seule (rafales exclues par construction). Ne pas confondre avec le « ~32 % bout-en-bout » qui divise par tous les bursts détectés, rafales comprises.

Statut : fix validé terrain. Prochaine étape : merge `experiment/sgb-tracking-offline` → `main`, remettre firmin en checkout `main`, et re-mesurer sur une longue durée (les taux historiques « jamais-synchro » sont à réévaluer avec la nouvelle fenêtre).

### Validation longue après merge main — nuit/matin 5 juillet 2026

Logs de référence :

- firmin : `logs/scan406_butterworth_diag_20260705_0000.log`
- local RTL/Yagi recalé : `logs/CNES_local_async_Butterworth_20260705_084647.log`

Résultats firmin 00:00→09:05 :

- FGB : **1495/1542 = 97,0 %**.
- SGB brute : 202 OK / 451 détectées = 44,8 % — chiffre volontairement non retenu comme taux utile, car il mélange la calibration et les salves non décodables.
- SGB calibration sur grille 150 s : **202/219 = 92,2 %**.
- `FRAME REJECTED` SGB : **0**.
- Les 17 créneaux manqués tombent tous dans les salves périodiques `xx:10` / `xx:40`, avec deux rejets détectés au voisinage du créneau.
- Résiduels des SGB OK : **−3245 à +11088 Hz** → confirme que la fenêtre ±16 kHz reste nécessaire sur firmin.

Résultats local 08:47→11:27 :

- FGB : **413/446 = 92,6 %**.
- SGB brute : 59 OK / 129 détectées = 45,7 % — même piège de comptage que firmin.
- SGB calibration sur grille 150 s : **59/64 = 92,2 %**.
- `FRAME REJECTED` SGB : **0**.
- Les 5 créneaux manqués tombent tous dans les salves périodiques `xx:10` / `xx:40`.
- Résiduels des SGB OK : **−3752 à +825 Hz** → localement l'ancienne fenêtre ±8 kHz suffisait, mais le fix ne coûte rien.

Conclusion consolidée : après async RTL + correctifs fredzo + fenêtre `freq_acq` ±16 kHz, le taux utile de la **calibration SGB 65535** est stable autour de **92 % sur les deux sites**. Les taux bruts autour de 45 % ne décrivent pas le décodeur SGB : ils comptent les salves périodiques non-calibration comme des échecs. Le chemin post-sync reste propre (`FRAME REJECTED=0`) ; les pertes restantes sont des collisions temporelles avec ces salves ou de la détection RF, pas du BCH/data après synchro.
