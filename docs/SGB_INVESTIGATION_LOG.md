# Journal d'enquête SGB — historique des hypothèses

Annexe de **[SGB_STATUS.md](SGB_STATUS.md)** (état courant validé). Ci-dessous : couches d'hypothèses successives, **dont beaucoup annulées plus bas ou remplacées par SGB_STATUS.md**. Ne pas lire comme l'état courant.

---

## ⮕ [DÉPASSÉE — voir la CORRECTION 21 juin dans SGB_STATUS.md] FICHE : distinguer balise décodable vs rafales, et calculer le taux honnête

**À lire en premier par toute instance qui calcule un taux SGB depuis les logs `scan406`.**

Le flux firmin contient DEUX populations de bursts SGB qu'il ne faut pas mélanger :

| | **Balise décodable** (orbitographie CNES) | **Rafales** (indécodables) |
|---|---|---|
| **`dt`** (champ du log) | **~13 s, isolé** (>3 s) | **~1.0-1.1 s, dos à dos** |
| BW | ~60-65 kHz | ~100 kHz (nuit) ou ~50 kHz (matin) — variable |
| SNR | ~11-12 dB | ~7-12 dB |
| despread | z ≥ 50, BCH OK | « decode failed (z=0.0) » |
| Hex ID | réel (9C77FFFE Toulouse, 9C621A74, 9C821A7492…) | n/a |
| Préambule | cohérent (cohérence par bloc ~0.65) | **incohérent ~0.09** (prouvé 20 juin) |

**Le discriminateur propre = `dt`** (confirmé sur 2 logs) : balise = bursts **isolés** (dt > 3 s) ; rafales = **dos à dos** (dt ~1 s). BW et SNR aident mais sont moins fiables (la BW des rafales a changé de 100 → 50 kHz entre la nuit et le matin du 21).

**Pourquoi exclure les rafales du taux** : elles n'ont **pas de préambule PRN cohérent** (analyse contrôlée du 20 juin, cf. section « cohérence par blocs »). Aucun décodeur ne peut les décoder — c'est un défaut signal/RF, pas du décodeur. Les compter en échec écrase artificiellement le taux.

**Calcul du taux honnête** (depuis `journalctl -u scan406` ou un log) :
- Apparier chaque ligne `BURST … SGB` avec son verdict suivant (`SGB frame decoded` = OK / `SGB burst — decode failed` = FAIL). Les lignes `REJECT … (out of range)` ne sont PAS des tentatives (burst partiel filtré) — les ignorer.
- **Taux honnête = décodés / (décodés + échecs à dt > 3 s)** — exclut les rafales dos-à-dos.
- FGB : succès = ligne `burst=N CRC OK` (PAS « frame decoded ») ; échecs = `CRC FAIL` + `no frame`.

**Mesures de référence (21 juin) :**
| Période | FGB | SGB brut | SGB balises isolées (dt>3s) |
|---|---|---|---|
| Nuit 00:00→07:47 | 1008/1088 = **92.6 %** | 137/1245 = 11 % (tempête de rafales larges) | — |
| Matin 05:00→08:08 | 507/529 = **95.8 %** | 69/155 = 44.5 % | 60/81 = **74.1 %** |

Le taux brut est trompeur (dominé par les rafales). Le chiffre qui compte est le **taux sur balises isolées**. L'écart avec la référence 93 % (fichier 0614) = vrai sujet décodeur, distinct des rafales.

**Règle pour le dénominateur** : n'exclure QUE les rafales dos-à-dos (dt~1 s). Les échecs **isolés** (dt>3 s) **restent dans le dénominateur** — ce sont de vrais ratés de balise, pas des rafales : ils ont un profil balise (BW étroit ~50-65 kHz, SNR fort) mais `z=0.0` (sync échouée), et n'ont pas de `bits250` (rien produit). Ne PAS chercher à les reclasser comme rafales pour gonfler le taux — c'est le phénomène 0619 (préambule fort en énergie mais incohérent), c'est-à-dire précisément ce qu'on veut mesurer comme « raté décodeur ».

**Si ambigu sur un burst précis** : l'extraire aligné sur l'onset et lancer `prn_onset.m` (Octave, au dépôt) → cohérence moyenne ≥ ~0.4 = balise ; ~0.1 = rafale.

---

### Ce qu'on observe

Sur le fichier du 14 juin (qui marche), l'acquisition de fréquence donne une confiance de 41 et la synchronisation PRN un score de 45. Sur le fichier du 19 juin, ces valeurs tombent à 8 et 5.5 respectivement. Les seuils sont 8 pour l'acquisition et 20 pour la synchronisation.

### Cause identifiée : excès d'énergie dans la bande SGB

Une analyse spectrale sur la fenêtre t=25–35 s (autour du burst SGB) montre que le fichier du 19 juin a beaucoup plus d'énergie que celui du 14 juin, concentrée autour de la fréquence centrale :

| Bande de fréquence | Ratio 0619/0614 |
|--------------------|-----------------|
| ±25 kHz (cœur du signal SGB) | **4.7×** (+6.7 dB) |
| ±38 kHz (bande complète SGB) | **4.1×** (+6.1 dB) |
| ±100 kHz | 2.4× (+3.8 dB) |
| 100–500 kHz | 1.9× (+2.7 dB) |
| 500–1000 kHz | **1.0×** (identique) |

Le bruit de fond loin du centre (au-delà de 500 kHz) est le même entre les deux fichiers. L'excès est une bosse lisse d'environ 50 kHz de large, centrée pile sur 406.051 MHz. Ce n'est pas un signal discret (pas de raie nette).

L'inspectrum confirme : le fichier du 19 juin montre un environnement RF chargé avec de nombreux bursts courts (FGB) et un signal continu au centre, tandis que celui du 14 juin est plus propre. Le 14 juin a aussi des FGB, mais le décodeur marche quand même — les FGB seuls ne posent pas de problème.

### Ce qu'on a essayé (tout a échoué)

1. **Filtre FIR passe-bas (161 taps, 50 kHz)** — aucun effet. L'énergie parasite est dans la bande du signal, pas en dehors.

2. **Filtre FFT brick-wall (4096 pts, 45 kHz)** — aucun effet non plus, confirme que le bruit est dans la bande.

3. **Médiane au lieu de moyenne pour le score de confiance** (`freq_acq.c`) — la confiance passe de 7.8 à 8.1, juste au-dessus du seuil. Mais la synchronisation PRN échoue quand même à z=5.5. Modif non committée.

4. **Recherche de fréquence en force brute** (±500 Hz par pas de 10 Hz) — aucune fréquence ne se démarque. Les 10 meilleurs résultats ont des corrélations presque identiques, étalées de -470 à +460 Hz.

### Hypothèses fausses éliminées

- « Plusieurs balises SGB à des fréquences différentes » — **faux**. Il n'y a qu'une balise à ~406.051 MHz. Les acquisitions à des fréquences variées (521, -2467, -975 Hz…) sont des faux pics sur le bruit.
- « Interférence à 405.950 MHz (hors bande) » — **faux**. Le bruit est dans la bande SGB, pas en dehors.
- « Interférence large bande centrée sur 406.051 MHz » — **faux**. Le signal de 1000 ms, régulier, à la bonne fréquence et la bonne bande passante (~50 kHz), c'est le burst SGB lui-même. L'excès d'énergie mesuré en PSD EST le signal utile, pas une interférence.

### Observation clé : le burst 2 du 0614 échoue aussi

Le fichier 0614 (qui fonctionne) ne décode qu'un burst sur deux :
- Burst 1 (t=28 s) : freq_acq conf=40.2, despread z=44.2 → **décodé**
- Burst 2 (t=178 s) : freq_acq conf=39.5, despread z=7.6 → **échoué**

Même symptôme que le 0619. Le problème n'est pas spécifique à l'environnement RF du 0619 — le décodeur rate certains bursts même dans de bonnes conditions.

### Diagnostic (20 juin)

Diagnostics étendus ajoutés dans `despread_sync` (pic, 2ème pic, ratio, lags, mean, std), affichés aussi sur les échecs. Comparaison burst 1 (marche) vs burst 2 (échoue) du 0614 :

| | freq_acq conf | despread z | pic | 2ème pic | ratio pic/2nd |
|---|---|---|---|---|---|
| Burst 1 (t=28s) | 40.2 | 44.2 ✓ | ≈360 | — | 7.67 |
| Burst 2 (t=178s) | 39.5 | 7.6 ✗ | ≈65 | ≈52 | 1.3 |

**Fait majeur** : freq_acq est quasi identique sur les deux bursts (conf 40.2 vs 39.5). Le PRN est donc présent à pleine force dans le burst 2. L'effondrement (×5.5 sur le pic) n'apparaît qu'au stade despread_sync, APRÈS le wipeoff et le décalage OQPSK.

### Test OQPSK (20 juin) — INNOCENTÉ

Toggle `NO_OQPSK=1` ajouté dans `dsss_demod.c` pour court-circuiter le décalage OQPSK.

| | avec OQPSK | sans OQPSK |
|---|---|---|
| Burst 1 (t=28s) | z=44.2 ✓ | z=50.8 ✓ |
| Burst 2 (t=178s) | z=6.6–8.4 ✗ | z=7.3–9.0 ✗ |

Enlever le décalage OQPSK ne récupère pas le burst 2. **Le décalage OQPSK n'est pas la cause.** (Effet secondaire : il dégradait légèrement le burst 1, z 44→51.)

Le timing sous-chip / SPS est aussi écarté comme cause principale : les 4 offsets boxcar (0,16,32,48) balaient le timing entier et échouent tous, alors que freq_acq à offset 0 réussit. Une erreur de timing frapperait freq_acq identiquement.

### Hypothèse retenue : précision du wipeoff

Ce qui reste entre freq_acq (réussit) et despread_sync (échoue), OQPSK exclu : **le wipeoff**. freq_acq **cherche** la fréquence (balayage), despread_sync fait confiance à l'unique fréquence wipée et corrèle le préambule en magnitude. La magnitude tolère une phase constante mais pas un résidu de fréquence qui tourne sur toute la longueur du préambule.

Chiffres : pic ×5.5 plus faible ⇔ sinc ≈ 0.18 ⇔ **~1.6 cycle** sur le préambule ⇔ résidu **~10 Hz** non corrigé. (Corrigé 21/06 : la version d'origine disait « ~1 cycle / 15-20 Hz », faux et auto-contradictoire — 1 cycle = ~6 Hz, cf. premier zéro juste plus bas.) freq_acq ne le voit pas (balayage), despread_sync s'effondre.

### Test sweep wipeoff (20 juin) — CAUSE RACINE CONFIRMÉE

Balayage de la fréquence de wipeoff (`WIPE_SWEEP=1` dans `dsss_demod.c`), z de despread_sync à chaque offset :

**Burst 2 (échoue) :**
| freq | 40 | 45 | 50 | 55 | 60 |
|---|---|---|---|---|---|
| z | **0** | **67.3** | 37.2 | 23.4 | 0 |

**Burst 1 (marche) :**
| freq | 42 | 47 | 52 | 57 | 62 |
|---|---|---|---|---|---|
| z | 0 | **91.9** | 50.8 | 32.5 | 0 |

**Conclusion :**
1. La réponse z(fréquence) de despread_sync est **très étroite** : ~±10 Hz à mi-hauteur, zéro au-delà de ±15 Hz. C'est le sinc du préambule (~160 ms → premier zéro ~6 Hz).
2. **freq_acq n'est pas assez précis** : il maximise sa propre corrélation, pas celle de despread_sync. Écart de 5-7 Hz sur les deux bursts (vrai optimum ~45-47 Hz, freq_acq donne 52 et 40).
3. Le burst 1 survit par chance (freq_acq=52, flanc doux, z=50). Le burst 2 meurt (freq_acq=40, sur la falaise, z=0). Même erreur de 5 Hz, issue opposée selon le côté de la pente.

Ce n'est ni l'OQPSK, ni le timing, ni le BCH, ni le signal : **despread_sync fait confiance à une fréquence à 5 Hz près que freq_acq ne garantit pas.**

### Fix raffinement de fréquence (20 juin) — VALIDÉ sur 0614

`dsss_demod.c`, étape 3a : après freq_acq, traiter le buffer une fois (wipeoff + OQPSK + décimation) puis sonder ±15 Hz par dérotation chip-rate, garder la fréquence donnant le meilleur z de despread_sync. La différence de phase OQPSK sur ±15 Hz est ~1e-3 rad, négligeable.

`despread.c` : despread_sync expose `z_comb` même en échec, pour que le balayage puisse comparer les fréquences sous le seuil.

Résultat **0614 = 2/2 bursts** (était 1/2) :
| Burst | freq_acq | raffiné | z | BCH |
|---|---|---|---|---|
| 1 (t=28s) | 52 Hz | 47 Hz | 90.5 | nerr=0 ✓ |
| 2 (t=178s) | 40 Hz | 45 Hz | 80.2 | nerr=0 ✓ |

Synthétique toujours OK (z=9639, nerr=0). Aucune régression.

Première implémentation cassée : le balayage tournait sur les chips **sans** OQPSK → z=0 partout sur le synthétique (qui a besoin de l'OQPSK) → fréquence garbage choisie, nerr=6. Corrigé en incluant l'OQPSK dans le balayage.

### 0619 : échoue à l'ACQUISITION, pas à despread

Le fix de fréquence ne touche pas le 0619. Raison : sur 0619 le burst SGB est **rejeté à freq_acq** (conf 5-6, seuil 8), il n'atteint jamais despread_sync.

freq_acq sur les fenêtres du burst 0619 : offset -2228 conf 6.3, -1844 conf 5.3, -236 conf 5.7, -97 conf 5.1, 279 conf 5.3. Toutes sous 8. Le bruit in-band (énergie ×4 dans la bande) remonte la médiane de corrélation → conf chute.

Les deux fichiers échouent à des étages différents :
- 0614 : passe freq_acq, échouait à despread → corrigé.
- 0619 : échoue freq_acq → goulot = gate d'acquisition.

### Piste 0619 : baisser le gate — TESTÉE, ÉCHEC

`ACQ_CONF_MIN=5` (env var ajoutée dans `dsss_demod.c`) sur 0619 : 17 fenêtres atteignent despread. **Toutes donnent z = 5.8 à 7.6. Aucune ≥ 20. Zéro BCH.**

Baisser le gate ne récupère rien : le burst 0619 **ne passe pas despread**. Le goulot n'est pas le gate — c'est despread_sync qui est écrasé.

**Contraste 0614 / 0619 :**
- 0614 burst 2 : raffiner la fréquence fait sauter z de 7 à 80. Problème d'**alignement fréquentiel**, pic fort mal placé.
- 0619 : raffiner bouge z de ~6 à ~7.6. **Pas un problème de fréquence.**

Cohérent avec la mesure PSD (plancher ×4) : `conf` et `z` sont des métriques **relatives au plancher** ((pic − moyenne)/écart-type). Un pic même fort ne ressort pas quand le plancher est élevé. Même effet qui écrase conf<8 et z<20, même cause.

Mistral avait recommandé « baisser le gate, despread filtrera » : invalidé, despread ne trouve pas le burst.

### Pics absolus despread sur 0619 (20 juin) — signature de bruit

Capture des `peak`/`2nd`/`mean`/`std` de despread_sync sur 0619 (gate=5), triée par pic décroissant — les 8 meilleures fenêtres :

| z | peak | 2nd | mean | std | pic/2ème |
|---|---|---|---|---|---|
| 8.3 | 98.7 | 75.8 | 18.4 | 9.9 | 1.30 |
| 7.6 | 98.3 | 74.6 | 19.6 | 10.6 | 1.32 |
| 7.4 | 95.6 | 74.6 | 19.9 | 10.6 | 1.28 |
| 7.2 | 94.0 | 82.3 | 18.9 | 10.8 | 1.14 |
| 6.8 | 93.9 | 79.5 | 20.2 | 11.2 | 1.18 |

Comparaison avec 0614 :
- 0614 burst aligné : peak ≈ **570**, mean ≈ 12, pic/2ème ≈ **25**.
- 0619 meilleures fenêtres : peak ≈ **95**, mean ≈ 19, pic/2ème ≈ **1.3**.

**Signatures de bruit sur 0619 (et non signal dégradé) :**
1. **pic ≈ 2ème pic** (ratio 1.1-1.3 contre 25 pour un vrai burst) — aucune corrélation dominante.
2. **lags aléatoires** entre les sous-essais du raffinement (lag1 saute : 3773, 4338, 1490, 2488…), contre lag stable 9049 sur 0614 — pas de préambule à position fixe.
3. **bits250 tout à zéro** — aucun bit cohérent.

despread fonctionne correctement : il rejette ces fenêtres de bruit.

### Correction de cadrage (20 juin)

- Les hits freq_acq à -1352 Hz (et -1112, -975…) sont à **-1.35 kHz** (kHz, pas MHz), c'est-à-dire **DANS la bande SGB** (±50 kHz d'après inspectrum). Pas un interféreur externe — un point de l'énergie DSSS étalée / du bruit dans la bande. (Une hypothèse de repliement d'un interféreur à -1.35 MHz a été émise puis retirée : raisonnement motivé sur une prémisse fausse.)
- Inspectrum 0619 : SGB de ~-50 kHz à ~+50 kHz, centré. Rien d'extérieur identifié.

### État réel 0619 / firmin live (20 juin) — investigation approfondie

**Confirmé en direct sur firmin** (pas un défaut du fichier 0619) : le détecteur d'énergie voit des bursts SGB à SNR 7-13 dB, durée ~1.0s, cadence 1.1s. freq_acq les rejette TOUS à conf 2-3. Avec 30 dB de gain de traitement, conf 2-3 sur 13 dB de SNR = le PRN ne corrèle quasiment pas.

Diagnostic sur les bursts 78.43 et 119.43 (COARSE_DIAG du vrai code) :
- Préambule **trouvé en plage** (lag ~6290, cluster cohérent en fréquence) mais corrèle **faiblement : ×2.9-4.2** le median, contre **×40 sur 0614**. Match PRN **partiel**.
- Porteuse variable d'un burst à l'autre : 78.43 → -1352 Hz, 119.43 → -104 Hz. ~1.25 kHz de variation.

**Découverte annexe (vraie, démontrée)** : freq_acq ne cherche que les lags 0→10976 (~285ms). Le burst 0614 décalé de 100ms dans la fenêtre passe de conf 40 (préambule lag 9103, en plage) à conf 1.7 (préambule chip ~12940, hors plage). → l'alignement burst→fenêtre est critique. Mais ce n'est PAS la cause 0619 (là le préambule est en plage et corrèle quand même faiblement).

**Hypothèses testées et ÉLIMINÉES sur 0619 :**
| Hypothèse | Test | Résultat |
|---|---|---|
| Décalage OQPSK | NO_OQPSK | éliminé (n'affecte pas) |
| Timing sous-chip | 4 offsets boxcar | éliminé (tous échouent) |
| Gate d'acquisition trop haut | ACQ_CONF_MIN=5 | éliminé (despread z<8 quand même) |
| Inversion spectrale | conjugaison | porteuse passe en + mais despread ne verrouille pas |
| Précision fréquence | sweep wipeoff | éliminé (despread plat à z~7) |
| Désaccord cadence chip | sweep -s ±0.3% | éliminé (conf reste 2-5) |

**Conclusion** : le PRN ne corrèle que partiellement (×3 au lieu de ×40) avec les bursts firmin/0619, et aucun réglage du décodeur (fréquence, signe, gate, cadence, alignement) ne le corrige. Cause structurelle non identifiée.

**Seul moyen de débloquer** : référence externe. Un autre décodeur (GNU Radio de référence, SDRangel) décode-t-il un burst de ce flux firmin ? Si oui → bug ciblé dans dec406 à trouver. Si non → les bursts capturés ont un défaut que « fort et visible » ne garantit pas absent (le détecteur d'énergie ne valide pas l'intégrité du préambule PRN).

**Problème temps-réel distinct** : `ring overrun`, `SGB window overwritten before decode` — le scanner sature sur la cadence 1.1s.

### État (20 juin) — 0619 : corrélation PRN 10× plus faible que 0614, cause non tranchée

Tests de référence externe :
- **jbirby `sgb-codec`** (Python) : son PRN est **identique** à dec406 (seeds 0x000001 / 0x1AC1FC, LFSR tap 18) — PRN dec406 confirmé correct. Mais son `demodulate` est naïf (burst à l'échantillon 0, porteuse 0, pas de tracking) → **synthé-only, inutilisable OTA**.
- **gr-cospas `decode_sgb.grc`** (RRC + symbol_sync MMSE + Costas) : décode le **synthétique** (chips 100% du PRN, 248 bits) mais **échoue sur le 0614 OTA valide** (chips 52.8% aléatoire). → **synthé-only aussi**, écarté comme référence OTA.

**Mesuré par dec406 (décodeur validé, qui décode 0614) :**
- Corrélation préambule **×40 sur 0614** (décode, BCH nerr=0), **×4.3 sur 0619** (échoue). Différence réelle, large, par la vraie chaîne.

**Chaîne indépendante (Octave) — TRANCHÉ : PRN présent mais dégradé.** Script `prn_check.m` : corrélation par FFT (tous lags) + métrique magnitude (indépendante de la phase) — implémentation distincte de dec406 et du numpy maison. **Validée sur contrôles obligatoires** : synthétique peak/median=80.9, **0614=53.1** (cohérent avec dec406 ×40). Donc le chiffre 0619 est fiable.

| Burst | peak/median (Octave, validé) |
|---|---|
| Synthétique | 80.9 |
| 0614 (décode) | 53.1 |
| **0619** | **6.9** @ f0=-105 Hz |

**0619 = 6.9, ce n'est PAS du bruit** (bruit pur ≈ 4-5 = max sur ~44000 lags d'un fond). Et le pic tombe à **f0=-105 Hz, exactement la fréquence trouvée indépendamment par dec406** (-104/-116), par une chaîne complètement différente → c'est une vraie corrélation PRN, pas une fluctuation statistique (du bruit ferait sauter fréquence et lag partout). Mais la signature est **≈8× moins saillante** que sur 0614 (6.9 vs 53).

**Précision (revue externe)** : 53.1/6.9 ≈ 7.7 est un rapport de **métriques de corrélation**, PAS un facteur linéaire sur l'énergie PRN ni sur le nombre de chips corrects. Formulation exacte : « signature PRN détectable mais ≈8× moins saillante que 0614 », pas « PRN dégradé ×8 ».

**Conclusion :** le burst 0619 contient une **signature compatible avec le PRN T.018, mais nettement moins saillante** que 0614. Ni « absent » (erreur du harnais numpy buggé : « 53% aléatoire » était faux), ni « franc ». dec406 a raison de le rejeter : 6.9 est trop faible pour verrouiller. Trois hypothèses tombent : **mauvais PRN** (réfutée — corrélation reproductible à la bonne fréquence contre le PRN T.018), **PRN absent** (réfutée), **bug grossier dec406** (affaiblie — comportement cohérent fort→décode / faible→rejette confirmé par mesure indépendante).

**Non résolu, désormais côté signal/RF (pas l'algo de désétalement)** : pourquoi la signature PRN est si peu saillante sur 0619 alors que l'énergie est forte. Pistes physiques :
- **Multipath fréquence-sélectif** : le DSSS exige une cohérence de chips, pas seulement de l'énergie ; des délais comparables à la durée d'un chip détruisent la corrélation en conservant l'enveloppe.
- **Distorsion de capture** : saturation/AGC, décrochage SDR, timestamping, rééchantillonnage dégradé — conservent l'énergie, détruisent la structure DSSS.
- **Chaîne RX locale** : la porteuse varie de -1352 Hz (burst 78.43) à -104 Hz (burst 119.43), ~1.25 kHz entre bursts d'une source fixe — à creuser (oscillateur/capture). Caveat : l'estimation -1352 venait d'une corrélation faible (×2.9), donc peu fiable ; le « saut » de porteuse peut être un artefact de la dégradation plutôt qu'une vraie instabilité RX. Vérifiable en passant le burst 78.43 dans `prn_check.m`.

Note méthodo : « 0619 ne porte pas de PRN » (affirmé puis retiré) venait d'un harnais numpy non validé. La chaîne Octave, validée sur 0614/synthé AVANT de lire 0619, donne la réponse fiable.

### Correction finale (20 juin) — cohérence par blocs : 0619 n'a PAS de préambule cohérent

La métrique peak/median (6.9) était trompeuse (avertissement revue externe). Le bon critère pour « préambule présent » = **cohérence moyenne des 25 blocs de 256 chips** (corrélation normalisée [0,1] par bloc, `prn_blocks.m` / `prn_onset.m` / `prn_sub2.m`).

| Burst | cohérence-moyenne 25 blocs (tout offset sous-chip balayé) | profil |
|---|---|---|
| **0614** (décode) | **0.646** (off=0, lag=3266=dec406) | uniforme 0.58–0.70 sur les 25 blocs |
| **0619** | **0.094** (≈ bruit 0.06) | bruit + pics isolés de hasard |

**Conclusion contrôlée (supersède « ≈8× moins saillant ») :** avec le bon critère, **0619 n'a aucun préambule T.018 cohérent** — cohérence moyenne au niveau du bruit, à TOUT offset sous-chip (critique #1 de la revue fermée) et au bon lag (FFT-exact, pas de bug de pas). Le « 6.9 » venait de **fragments isolés** captés par peak/median, pas d'un préambule. Un vrai préambule = 25 blocs uniformément hauts (0614). 0619 = bruit + hasard.

3 revirements sur 0619 (absent → dégradé → pas de préambule cohérent) ; le dernier est le plus contrôlé : adresse les 3 critiques externes (peak/median, boxcar-timing sous-chip, uniforme-vs-localisé), contrôle 0614 passe à chaque run.

Nuance honnête : l'accord -105/-104 entre deux outils sur le pic peak/median suggère qu'un fragment réel existe peut-être (pas que du hasard pur), mais **il n'y a pas de préambule exploitable** quel que soit le statut de ce fragment.

**Reste ouvert (côté signal/RF, hors dec406)** : pourquoi le préambule 0619 est incohérent alors que l'énergie est forte et la forme SGB. Pistes : multipath fréquence-sélectif, distorsion de capture (saturation/AGC/décrochage/rééchantillonnage), chaîne RX. Le boxcar matched-filter est écarté comme cause (il donne 0.646 sur 0614, même chaîne) ; un demi-sinus adapté gagnerait ~1 dB, pas de quoi remonter 0.094 à 0.6.

Outils Octave validés laissés au dépôt : `prn_check.m` (saillance globale), `prn_onset.m` (cohérence par bloc au bon lag), `prn_sub2.m` (balayage sous-chip FFT-lag-exact, avec contrôle 0614).

### MAJ 21 juin (soir) — null mesuré, phase/fréquence par bloc, lag check ; les blocs 19-21 ne sont PAS un préambule survivant

Nouvelles mesures (scripts `prn_null.m`, `prn_multipeak.m`, `prn_envelope.m`, `prn_quant.m`, `prn_phase.m`, `prn_onsetcheck.m`, tous validés sur 0614) :

1. **Null mesuré** (300 PRN aléatoires sur les données 0619, max-sur-lags) : μ=0.079 σ=0.0022. Cohérence 0619 = 0.087 = **3.9σ, 0/300 dépassent**. Donc « 0.094 ≈ bruit » était **faux faute de null mesuré** : il y a une **faible** corrélation PRN réelle. (0614 = 0.646 = 270σ.)
2. **Phase/fréquence par bloc** : les blocs faibles ont une **phase aléatoire** (pas une rampe) et une fréquence optimale **erratique** → **élimine le résidu de fréquence *stationnaire*** comme cause unique. (N'élimine PAS les défauts de sync non-stationnaires : glissement temporel variable, cadence chip, forme d'impulsion, multipath à retards évolutifs.)
3. **Lag check (décisif)** : le run cohérent à phase stable (blocs 19-21, ~-168°) est à **lag 26385 = 0,66 s dans le burst = MID-BURST, PAS le préambule**. Au préambule (zone onset, chip ~999), la corrélation est **z~6** (faible, dispersée, sans pic dominant). **Donc les blocs 19-21 ne sont PAS un segment de préambule survivant** — c'est le plus fort d'une poignée de pics faibles (~6-7), situé dans la zone données.

**Trois niveaux nets, robustes à 5 métriques (FFT, cohérence-bloc, null, phase, lag) : 0614 préambule z=53 ≫ 0619 meilleur pic z~6.9 > bruit z~4-5.** Quantification écartée (8 bits pour les deux ; 0614 décode à RMS ~1 quantum). Synthèse : le préambule 0619 **n'est pas absent mais n'est pas exploitable** (trop faible, dispersé) ; une **faible structure PRN réelle existe ailleurs** dans le burst ; **cause physique ouverte** (résidu fréquentiel stationnaire exclu, le reste — canal ou sync non-stationnaire — non distingué). Conclusion principale du `SGB_STATUS.md` **renforcée**, pas changée.

## Stash git — expériences non committées (19 juin)

| Fichier | Modification | Verdict |
|---------|-------------|---------|
| `despread.c` | Recherche pleine plage au lieu de ±5 chips | **Régressif** |
| `despread.h` | Seuil de sync 20→5 | **Dangereux** — le bruit donne des scores jusqu'à 7 |
| `dsss_demod.c` | Retrait du cap 12000 chips pour l'acquisition | **À évaluer** |
| `dsss_demod.c` / `freq_acq.c` | Longueur de corrélation réduite à 6400 | **À évaluer** |

## Chronologie complète des hypothèses SGB

| Date | Hypothèse | Résultat | Référence |
|------|-----------|----------|-----------|
| 04 mai | Tracking loop (FLL/PLL/DLL/Kalman) | **Abandonnée** — 0/5 décodé en OTA | branche `feature/fll-pll-tracking` |
| 13 mai | Lock indicator trop bas, FLL perturbé par les transitions | **Confirmé** — raison de l'abandon | CLAUDE.md |
| 19 mai | Phase de décimation boxcar (1 burst sur 4 par chance) | **Résolu** — multi-offset 4 phases | committé dans `dsss_demod.c` |
| 22 mai | Chaîne plate : FFT-corrélation + NCO fixe | **Adoptée** — remplace le tracking | `e5c5e5d` |
| 23 mai | Rotation BCH multi-Costas (4 phases) | **Adopté** — récupère 4/7 bursts | committé dans `dsss_demod.c` |
| 14 juin | Polarité inversée | **Fix #1** | `c6e3914` |
| 14 juin | Estimation fréquence par régression sur préambule | **Fix #2** | `c6e3914` |
| 14 juin | Recherche Chien sur 255 positions au lieu de 250 | **Fix #3** | `c6e3914` |
| 18 juin | Interférence hors bande firmin | **Diagnostic révisé** — c'est dans la bande | — |
| 19 juin | Offset Q ≠ offset I (OQPSK) | **Non validé** — abandonné | stash |
| 19 juin | Mauvaise correction BCH = bug | **Faux** — comportement normal quand trop d'erreurs | `test_bch_reject.c` |
| 20 juin | Filtres passe-bas (FIR + FFT) | **Inefficaces** — le bruit est dans la bande | — |
| 20 juin | Médiane au lieu de moyenne pour la confiance | **Marginal** — passe le seuil de 0.3, ne résout pas le despread | non committé |

---

# Archive complète de l'ancien SGB_STATUS — 5 juillet 2026

Cette section conserve l'ancien contenu complet de `SGB_STATUS.md` avant sa réduction en fiche d'état courant. Elle contient volontairement des conclusions anciennes, des hypothèses retirées et des corrections successives. Pour l'état actuel, lire `SGB_STATUS.md`.

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
