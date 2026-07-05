# État courant SGB — 5 juillet 2026

Ce fichier est la fiche de statut courant. Le détail chronologique des hypothèses, tests, revirements et mesures terrain est dans [SGB_INVESTIGATION_LOG.md](SGB_INVESTIGATION_LOG.md). Ne pas recopier ici tout le journal : ne garder que ce qui décrit l'état validé du décodeur.

## Résumé validé

Après async RTL + correctifs fredzo + fenêtre d'acquisition `freq_acq` élargie à +/-16 kHz, le décodeur SGB fonctionne correctement sur les bursts CNES calibration qui atteignent la synchronisation. Le symptôme ancien "préambule fort puis bits data aléatoires" n'est plus observé sur les tests récents.

État opérationnel consolidé :

- **RTL-SDR temps réel** : backend asynchrone `rtlsdr_read_async()`, débit stable à 2.4576 MS/s, plus de sous-alimentation silencieuse du scanner.
- **SGB post-sync** : `FRAME REJECTED=0` sur les logs longs récents ; le BCH/data après synchro est propre.
- **Acquisition SGB** : la fenêtre résiduelle est maintenant **+/-16 kHz**. C'est nécessaire sur firmin, où le centroïde détecteur peut être biaisé jusqu'à environ +11 kHz.
- **Bandpass acquisition** : `ACQ_BANDPASS_HZ=0` par défaut. Le filtre reste disponible pour diagnostic A/B mais n'est pas un correctif validé.
- **Dumps IQ** : `DUMP_OK` / `DUMP_FAIL` utiles pour analyse ponctuelle, à laisser désactivés en production et à ne pas committer.

## Correctifs intégrés

### Backend RTL asynchrone

Le chemin RTL synchrone perdait ou espaçait des échantillons sans ring overrun visible. Mesure locale avant correction : environ 2.449-2.451 MS/s au lieu de 2.4576 MS/s, avec FGB CRC FAIL systématiques et SGB sync fort / data bruit.

Correction validée : passage à `rtlsdr_read_async()` avec buffers larges. Validation locale : débit stable, FGB OK, SGB BCH OK, disparition du symptôme data aléatoire.

### Correctifs fredzo adaptés au scanner unifié

Les correctifs de la branche fredzo `bch2_correction` ont été adaptés localement sans merger l'ancienne architecture :

- rejet des mots SGB dégénérés tout-zéro / tout-un avant acceptation BCH ;
- garde contre l'acceptation d'un BCH trivial issu d'un faux lock ;
- limitation de la recherche de lag dans `freq_acq_fft_corr()` à la zone de pré-roll du burst.

### Acquisition SGB +/-16 kHz

Les dumps firmin du 4 juillet semblaient d'abord incohérents, mais le diagnostic était faux : ils étaient simplement hors fenêtre de recherche +/-8 kHz. Les mêmes dumps décodent avec une recherche autour de +8.5 à +9.5 kHz.

Correction validée : `freq_acq_fft_corr()` appelé avec une fenêtre **+/-16 kHz**.

Validation offline :

- synthétique SGB OK, BCH `nerr=0` ;
- `burst_sgb_173255_-7478Hz.cf32` : BCH validé, résiduel +8524 Hz ;
- `burst_sgb_173525_-8449Hz.cf32` : BCH validé, résiduel +9495 Hz.

## Taux terrain récents

Les taux utiles SGB doivent être comptés sur la grille 150 s de la calibration CNES 65535. Le taux brut `SGB OK / SGB détectées` est trompeur parce qu'il mélange la calibration avec des salves périodiques non-calibration.

### Firmin, 5 juillet 2026, 00:00->09:05

Log : `logs/scan406_butterworth_diag_20260705_0000.log`

- FGB : **1495/1542 = 97,0 %**.
- SGB brute : 202 OK / 451 détectées = 44,8 % — non retenu comme métrique de décodeur.
- SGB calibration grille 150 s : **202/219 = 92,2 %**.
- SGB `FRAME REJECTED` : **0**.
- Créneaux manqués : tous dans les salves périodiques `xx:10` / `xx:40`.
- Résiduels SGB OK : **-3245 à +11088 Hz**.

### Local RTL/Yagi, 5 juillet 2026, 08:47->11:27

Log : `logs/CNES_local_async_Butterworth_20260705_084647.log`

- FGB : **413/446 = 92,6 %**.
- SGB brute : 59 OK / 129 détectées = 45,7 % — non retenu comme métrique de décodeur.
- SGB calibration grille 150 s : **59/64 = 92,2 %**.
- SGB `FRAME REJECTED` : **0**.
- Créneaux manqués : tous dans les salves périodiques `xx:10` / `xx:40`.
- Résiduels SGB OK : **-3752 à +825 Hz**.

## Interprétation courante

Le décodeur SGB actuel est validé sur les bursts calibration CNES qui atteignent une acquisition correcte. Les pertes restantes observées dans ces logs ne sont pas des erreurs BCH/data après synchro ; elles sont associées aux salves périodiques, collisions temporelles ou à la détection/acquisition RF.

La famille historique 0619 reste un sujet séparé : le fichier du 19 juin montre une structure PRN faible/non exploitable selon les outils Octave, mais les FAIL firmin du 4 juillet ne doivent plus être assimilés à 0619. Ils étaient récupérables par l'élargissement de fenêtre fréquentielle.

## Références

- Journal complet : [SGB_INVESTIGATION_LOG.md](SGB_INVESTIGATION_LOG.md)
- Outil diagnostic EPL : `utils/sgb_epl_diag.c`
- Commandes de base : `make build/dec406_iq`, `make build/dec406_scan`
