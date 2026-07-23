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

### Support SGB self-test PRN (en cours)

Diagnostic validé depuis les specs locales C/S T.018 Issue 1 Rev.13 :
une transmission SGB self-test n'est pas seulement signalée par le bit 43
du champ principal. Elle utilise aussi des séquences PRN spécifiques
définies par T.018 Table 2.2. Le code contient déjà les constantes
self-test dans `include/prn_generator.h`, mais la chaîne active
`freq_acq.c` / `despread.c` utilise actuellement les seeds normales
`DESPREAD_PRN_SEED_I/Q`.

Hypothèse de correction : tester les deux modes PRN pendant
`freq_acq_fft_corr()` (normal puis self-test), conserver le meilleur mode,
puis utiliser le même mode dans `despread_sync()` et `despread_bits()`.
Critères de validation : le synthétique SGB normal reste OK, les bursts CNES
normaux restent décodés avec PRN normal, et un futur burst self-test SGB doit
être affiché explicitement `SELF-TEST` sans déclencher d'alerte.

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

## À part — filtre alertes FGB ELT-DT bench

Deux alertes mail FGB du 6 juillet 2026 à 15:04:34 UTC et 15:06:15 UTC
ont le même Hex ID `1C720000003FDFF`, le protocole `9 (ELT-DT Location
Protocol)` et l'identification `Aircraft 000000`. Rejeu local via
`dec406_hex` :

- `FFFE2F8E390000000AE018A81700EDA84498` : CRC1/CRC2 OK, ELT-DT, aircraft
  `000000`, position composite valide.
- `FFFE2F8E390000003F5FD2B4ED8F1E0F01EE` : CRC1/CRC2 OK, ELT-DT, aircraft
  `000000`, coordonnées invalides.

Conclusion : ce n'est pas une erreur de décodage. C'est une trame ELT-DT
non programmée / bench-test qui passe les filtres actuels parce qu'elle est
répétée, sur fréquence autorisée, et ne contient pas `ID-NOT-AVAIL`.
Correctif validé : ne pas envoyer d'alerte FGB si le body contient à la fois
`Protocol: 9 (ELT-DT Location Protocol)` et `Identification: Aircraft 000000`.

## À part — filtre alertes FGB frame-sync test RLS

Deux alertes mail FGB du 7 juillet 2026 à 08:00:36 UTC et 08:01:14 UTC
portent le Hex ID `21FA2BC00B3FDFF`, le pays `271`, le protocole
`13 (RLS Location Protocol)` et l'identification `RLS ELT TAC:2350 Serial:22`.
Le validateur Cospas-Sarsat indique pour la trame propre :

- `FFFED090FD15E0059FEFFC28BEB861F0FABE`
- frame sync `011010000` : test protocol message / non-operational use ;
- latitude PDF-1 `011111111` : default / no location ;
- longitude PDF-1 `0111111111` : default / no location ;
- BCH1 et BCH2 valides.

Diagnostic : la démodulation et les BCH sont corrects, mais le décodeur FGB
n'affiche pas encore le caractère test/non-opérationnel du frame sync, et le
décodeur RLS transforme les valeurs PDF-1 par défaut en coordonnées invalides
`127.50000 N, 255.50000 E`. Le filtre mail ne peut donc pas écarter cette
trame de test.

Correctif appliqué : exposer le frame sync test dans le décodage texte
(`Test Protocol: Active`), afficher `Position (PDF-1): Default - no location`
pour les valeurs RLS par défaut, et bloquer les alertes FGB dont le body
contient `Test Protocol: Active`.

Validation locale : `dec406_hex FFFED090FD15E0059FEFFC28BEB861F0FABE`
affiche `Test Protocol: Active` et `Position (PDF-1): Default - no location`
avec CRC1/CRC2 OK.

## Temps réel firmin après self-test PRN

Le support SGB self-test PRN double le coût d'acquisition car les deux codes
d'étalement normal et self-test sont testés. L'élargissement permanent de la
fenêtre fréquentielle de +/-8 kHz à +/-16 kHz double aussi le nombre de pas de
fréquence du coarse search. Sur firmin, les logs du 7 juillet 2026 montrent un
décodage SGB autour de 11 s par burst alors que le ring scanner couvre environ
6,8 s : les `ring overrun` observés sont donc des débordements de calcul, pas
des pertes USB.

Correctif appliqué : tenter d'abord l'acquisition sur +/-8 kHz et n'élargir à
+/-16 kHz que si la confiance reste sous le seuil. Le chemin après acquisition
reste inchangé. Objectif : revenir au coût normal pour les bursts dont le
résiduel fréquence est déjà dans +/-8 kHz, tout en conservant le secours
+/-16 kHz pour les cas de centroïde décalé observés le soir.

## Investigation — découplage décodage scanner

Logs firmin du 8 juillet 2026 : les SGB self-test décodent proprement, mais
des `ring overrun` apparaissent pendant les trains de bursts rapprochés. Le
débit RTL reste nominal à 2.4576 MS/s ; le problème observé n'est donc pas une
perte USB, mais le fait que `scanner_process()` exécute encore le décodage
FGB/SGB complet dans le même fil que la détection spectrale.

Hypothèse retenue : copier immédiatement la fenêtre IQ de la salve détectée
puis pousser ce travail dans une file bornée traitée par un worker unique. Le
fil scanner doit ainsi reprendre la FFT de détection sans attendre
`dsss_receive_burst_ex()` ni le décodage BCH/texte.

Critères de validation : build scanner OK, régression SGB synthétique OK, puis
test firmin sur trains de bursts. Les `ring overrun` doivent diminuer ou
disparaître ; si la charge dépasse encore la capacité de décodage, le symptôme
attendu devient une file de décodage pleine explicitement loggée plutôt qu'un
débordement silencieux du ring d'échantillons.

Correctif local préparé sur `feature/scanner-decode-worker` : file bornée de
16 décodages, worker unique, copie immédiate de la fenêtre IQ brute dans le fil
scanner, wipeoff NCO et décodage dans le worker. `capture_decode()` est protégé
par un verrou stdout pour éviter qu'une ligne `BURST` concurrente soit capturée
dans le corps texte du décodage.

Validation locale : `make -B build/dec406_scan`, `make -B build/dec406_iq`,
`make -B build/dec406_scan_rtlsdr build/dec406_scan_airspy` et régression
synthétique SGB OK. Validation terrain firmin encore à faire avant commit de
fix confirmé.

Validation terrain du 8 juillet 2026 : 14 trames SGB self-test et 2 trames
normales ont toutes validé le BCH sur firmin pendant des trains espacés parfois
d'une seconde. Les compteurs sont restés à `overruns 0, decode drops 0`.
Un second essai matériel a décodé 4 trames en 5 secondes sans perte. Le
découplage du worker est donc confirmé.

### Validation longue firmin du 9 juillet 2026

Log : `logs/scan406_20260709_0000.log`

Validation longue après découplage du décodage scanner par worker, de
00:00 à 08:59 :

- SGB : **450/450 décodées = 100,0 %**.
- Répartition SGB : **252 self-test**, **198 normales**.
- PRN sélectionnés : **252 SELF-TEST**, **198 NORMAL**.
- Acquisition SGB : **419** bursts acceptés en fenêtre **+/-8 kHz**,
  **31** via fallback **+/-16 kHz**.
- SGB `FRAME REJECTED` : **0**.
- `decode drops` : **0**.
- Overruns réels : **0**. Les lignes horaires `stopped — 0 ring overrun(s)`
  confirment l'arrêt propre du cycle scanner.
- FGB : **1449/1506 = 96,2 %**.

Conclusion : le worker de décodage tient la charge sur un cycle long firmin.
Les SGB détectées sont toutes décodées, y compris les self-test, sans perte de
file ni overrun réel.

## Investigation — correction BCH-2 FGB

T.001, section 3.2 et annexe B, définit BCH-2 comme un BCH(38,26) raccourci
capable de corriger deux erreurs sur les bits 107 à 144. Il existe uniquement
pour les messages longs, indiqués par le bit de format 25. Les messages courts
s'arrêtent au bit 112 et leurs bits 107 à 112 ne sont pas protégés.

Le démodulateur corrige déjà jusqu'à trois erreurs BCH-1, mais accepte
actuellement une trame dès qu'elle valide comme longue ou comme courte, sans
tenir compte du bit 25. Il transmet ensuite systématiquement 144 bits à
`decode_1g()`. Une erreur BCH-2 peut donc provoquer un rejet avant le décodeur,
et une trame courte acceptée est affichée à tort comme longue.

Correctif validé pour implémentation : corriger d'abord BCH-1 (qui protège le
bit 25), déterminer ensuite la longueur avec le bit 25 corrigé, puis
corriger/valider BCH-2 uniquement pour une trame longue. Le démodulateur doit
retourner la longueur réelle au scanner. Les deux corrections doivent partager
le même essai de polarité afin de couvrir une trame ayant des erreurs dans les
deux champs protégés.

Test sur le corpus `logs/fgb_iq_bits.csv` : 120 trames acceptées sont des
messages longs d'orbitographie (`protocol flag=1`, protocole utilisateur
`000`), mais 94 ne valident pas BCH-2. L'application stricte du gate au seul
bit 25 provoque également des `CRC FAIL` reproductibles sur les orbitographies
CNES reçues à 406.0221 MHz, malgré un frame sync 9/9. T.001 réserve ce protocole
aux opérateurs LUT et n'en décrit pas le contenu.

Décision après validation matérielle : conserver ces orbitographies à 144 bits
et ne pas leur appliquer BCH-2. La correction BCH-2 reste limitée aux autres
messages longs. Les messages courts sont transmis au décodeur avec 112 bits.

Implémentation finale commitée dans `39429c1` : correction BCH-2 jusqu'à deux
erreurs pour les trames longues hors orbitographie, exception orbitographie et
propagation de la longueur réelle vers les trois scanners. Validation locale :
121/121 vecteurs FGB historiques acceptés, 19 266 injections BCH-2 d'une ou
deux erreurs corrigées, builds des scanners unifié/RTL/Airspy et régression SGB
synthétique réussis. La correction BCH-2 reste à confirmer sur une vraie trame
longue hors orbitographie comportant une ou deux erreurs.

## Alertes downlink 1544 MHz

Le filtrage mail par code pays n'est pas adapté au downlink satellite : le MID
décrit l'enregistrement administratif de la balise, pas le lieu de l'accident.
Une balise étrangère peut être en Ariège ou dans les départements limitrophes,
et une balise française peut être n'importe où.

Règle retenue pour un service `1544 MHz` séparé : ne pas utiliser la whitelist
des canaux 406, mais exiger une position décodée valide dans une zone
géographique configurée par l'environnement. Le mode 406 direct reste inchangé.

Configuration prévue :

- `DEC406_ALERT_MODE=downlink`
- `DEC406_ALERT_CENTER=<latitude>,<longitude>`
- `DEC406_ALERT_RADIUS_KM=<rayon>`

Si le mode `downlink` est actif mais que la position est absente, invalide ou
hors rayon, aucun mail n'est envoyé. Les filtres test/bench existants restent
appliqués avant ce critère.

## Backend forcé pour services séparés

Le binaire unifié garde le probing automatique par défaut pour les usages
interactifs, mais les services doivent pouvoir fixer leur SDR sans ambiguïté.
La sélection explicite du backend se fait via `DEC406_BACKEND=rtl|airspy|pluto|hackrf`
et permet de couper l'autoprobing au démarrage du service. HackRF est
désormais supporté dans l'arbre courant.

### Identification du modèle Airspy

La bannière annonçait toujours `Airspy Mini`, y compris avec l'Airspy R2 de la
station F4KLO. Les deux modèles partageant le même `board_id` libairspy, le
test minimal utilise le débit maximal annoncé : 10 MS/s pour le R2 et 6 MS/s
pour le Mini. Le backend mémorise désormais le modèle après ouverture et la
bannière l'affiche sans modifier la sélection ni l'acquisition SDR.

## Investigation — champ tournant SGB TWC RF#4

La comparaison du parseur `decode_rot_field()` avec C/S T.018 Issue 1
Revision 13, Table 3.7, confirme un bug latent dans le décodage du champ
tournant #4 Two-Way Communication. Le code lit correctement l'identifiant du
fournisseur RLS, la version du dataset et l'acquittement RLM Type 3, mais il
traite actuellement les 33 bits de messages TWC comme trois slots courts
`7 bits question + 4 bits réponse` dans tous les cas.

Le bit 14 du champ tournant est en réalité l'`Answer Format Flag`; seul le
bit 15 est réservé. Lorsque ce flag vaut 1, T.018 impose un slot long
`7 bits question + 15 bits réponses`, suivi d'un slot court `7 + 4`. Une
trame TWC au format long serait donc affichée comme trois fausses réponses
courtes. Aucune balise TWC opérationnelle n'a encore été observée : il
s'agit d'une correction anticipée de conformité, à valider par vecteurs
synthétiques.

Correctif validé pour implémentation : conserver le chemin court à
l'identique lorsque le flag vaut 0, ajouter le stockage et l'affichage du
format long lorsque le flag vaut 1, puis tester les deux formats avec des
trames SGB dont le BCH est valide. La régression SGB synthétique habituelle
reste obligatoire.

Implémentation locale validée : le format court restitue les trois couples
de test `15/2`, `22/1`, `8/4` sans changement ; le format long restitue la
question 42, le bitmap de réponses `0x4215`, puis le couple court `9/3`. Les
deux vecteurs possèdent un BCH(250,202) valide et passent également par
`dec406_hex`. Les builds `dec406_hex`, `dec406_iq` et `dec406_scan` réussissent.
La régression `test_sgb_halfsine.sigmf-data` reste BCH valide avec `nerr=0`.

Validation de bout en bout avec le modulateur
`ADALM-PLUTO/SARSAT_SGB` : deux nouveaux modes `twc-short` et `twc-long`
construisent le RF#4, calculent son BCH et génèrent le signal DSSS/OQPSK.
Les fichiers SigMF produits sont acquis par `dec406_iq` avec `nerr=0`. Le
format court restitue `15/2`, `22/1`, `8/4`; le format long restitue la
question 42, le bitmap `0x4215`, puis `9/3`. Cette validation traverse donc
la chaîne complète constructeur de trame, BCH, modulation IQ, acquisition,
désétalement et décodage RF#4, sans émission matérielle.

La lecture concomitante de T.018 Table 3.9 a identifié un sujet distinct :
pour RF#15, `10` signifie désactivation manuelle et `01` désactivation
automatique. L'affichage actuel de `dec406_v2g.c` associe ces deux valeurs
dans l'ordre inverse. Correction validée et appliquée : `01` affiche
`Automatic deactivation by external means` et `10` affiche
`Manual deactivation by user`. Les deux valeurs sont vérifiées avec des
trames SGB synthétiques dont le BCH est valide.

### Investigation matérielle — seule la première trame Pluto est décodée

Premier essai RF TWC court à 431,975 MHz : la première rafale est acquise et
décodée avec BCH `nerr=0`, et restitue correctement `15/2`, `22/1`, `8/4`.
Les trames suivantes n'ont toutefois pas été décodées.

Le constructeur de trame alterne volontairement RF#4 et RF#0 ; cela ne peut
pas expliquer une absence totale de décodage, car les deux champs sont valides
et la troisième transmission doit de nouveau porter RF#4. L'hypothèse à
tester est donc un problème de répétition de l'émission one-shot Pluto ou de
détection des rafales suivantes, pas le contenu TWC.

Test minimal : comparer, pour les transmissions #2 et #3, les messages
`TX enabled`, `TX buffer push`, `Burst complete` côté modulateur avec la
présence ou l'absence de lignes `BURST` côté scanner. Aucun correctif ne doit
être appliqué avant d'avoir isolé si la perte se situe à l'émission ou à la
détection.

Résultat intermédiaire : les transmissions #23 à #28 sont toutes construites
avec BCH valide et `iio_buffer_push()` se termine par `Burst complete`. Les
champs alternent correctement RF#4/RF#0. La boucle applicative et le contenu
des trames sont donc hors de cause. Ce log ne prouve toutefois pas que le
Pluto rayonne après la première salve : les retours des commandes d'activation
et de désactivation TX sont actuellement ignorés. Le prochain test minimal est
l'observation directe des salves suivantes sur une cascade SDR, afin de
séparer une absence RF d'un défaut de détection scanner.

Le log scanner tranche ce point : une salve ultérieure est bien observée à
431,9743 MHz avec une largeur SGB de 64 kHz, mais rejetée avec
`dur 0.55 s (out of range)`. Dans ce message, `out of range` qualifie la
durée, pas la fréquence : le gate SGB impose 0,80 à 1,25 s. Le Pluto rayonne
donc après la première transmission ; le scanner perd le cluster spectral en
cours de salve et mesure une durée trop courte. Le prochain test minimal est
de refaire l'essai avec un intervalle de 30 s. Si les rafales redeviennent
complètes, la cause est un temps de récupération entre salves identiques
(plancher spectral/chaîne de réception). Si elles restent tronquées, il faudra
instrumenter la continuité du cluster pendant une salve avant tout correctif.

Observation SDRangel : les salves sont régulières et conformes. La cause est
donc confirmée dans le suivi spectral du scanner. Relecture de
`scanner_process()` : lorsque le cluster principal n'est momentanément pas
`near`, un fallback d'énergie centrée peut maintenir la rafale active. Le
chemin commun met ensuite malgré tout à jour `bcenter_sum` avec `off`, qui vaut
zéro si aucun cluster n'a été trouvé ou peut appartenir à un autre cluster.
La référence de suivi peut ainsi dériver jusqu'à provoquer une fin de rafale
artificielle. Dans le cas `have == false`, le calcul SNR parcourt en outre
`best_lo == best_hi == -1`, ce qui constitue un accès hors limites.

Hypothèse à valider avant correction : les rafales tronquées utilisent le
fallback pendant leur seconde moitié, puis le centroïde corrompu produit 16
frames `below`. Test minimal proposé : loguer uniquement les transitions vers
le fallback avec `have`, `off`, `burst_center` et le compteur `below` sur une
rafale Pluto. Aucun changement des seuils ni du gate de durée à ce stade.

Le log complet apporte une seconde hypothèse, à tester en premier car elle ne
demande aucune modification : le backend RTL fonctionne en gain automatique.
Les salves RF#4 et RF#0 sont parfois décodées avec `nerr=0`, mais d'autres sont
tronquées à des durées variables de 0,55 à 0,69 s alors que SDRangel les
montre identiques. Une réduction du gain RTL en cours de rafale forte peut
faire repasser le spectre sous le seuil du détecteur et produire exactement ce
symptôme. Test A/B prioritaire : relancer le scanner avec un gain RTL fixe de
30 dB (`431.9M 432.0M 30 0`). Si les durées se stabilisent, l'AGC est la cause;
sinon, reprendre le log ciblé du fallback/centroïde.

Résultat affiné : le gain RTL manuel améliore nettement la détection et permet
de décoder successivement RF#0 et RF#4 avec `nerr=0`, ce qui confirme la
validation matérielle RF#4 court. Il ne supprime toutefois pas tous les rejets :
avec un gain fixe de 30 dB, certaines salves restent mesurées à 0,47-0,68 s,
sur RF#0 comme sur RF#4. L'AGC était donc un facteur aggravant, pas la cause
unique. L'hypothèse du fallback/centroïde reste à tester par le log ciblé
proposé ci-dessus ; le seuil `SGB_DUR_MIN` ne doit toujours pas être abaissé.

Résultat final de l'essai de gain : à 20 dB fixe, aucune trame n'est rejetée.
Le gain automatique tronquait fortement les salves, et 30 dB fixe restait trop
élevé pour cette liaison locale forte ; 20 dB maintient toute la rafale dans
la dynamique exploitable du RTL. Aucun log supplémentaire ni correctif du
détecteur n'est retenu. Pour les essais locaux Pluto à 431,975 MHz, utiliser
`./build/dec406_scan 431.9M 432.0M 20 0`.

### MSG-4 1544,5 MHz — position sentinelle SGB mal reconnue

Capture de Bernard F6BVP `Fichiers_IQ/capture_1544_45.iq`, enregistrée avec
`airspy_rx -f 1544.5 -a 2500000 -t 2 -g 21 -n 100000000`. La lecture correcte
est donc `./build/dec406_iq <file> -i -s 2500000`. Une lecture à 2,048 Msps est
fausse et ne synchronise pas.

Résultat confirmé sur un extrait 24-28 s : trame MSG-4 à t=24,75 s, PRN normal,
offset +578 Hz, `conf=10.3`, `combined z=129.3`, BCH corrigé avec 1 erreur.
Le 23 Hex ID `9C77FFFEEAAF00000000000` est cohérent avec le décodeur en ligne
Cospas-Sarsat : TAC 65535, s/n 11946, pays 227 France, balise système de test.

Bug observé : avant d'afficher correctement `No position data available`,
`dec406` émet :

`Validation: Invalid latitude -127.03027`
`Validation: Invalid longitude -255.96970`

Hypothèse validée : le champ position contient une valeur sentinelle "pas de
position", mais `decode_position()` ne la reconnaît pas. Les bits décodés sont
`ns=1 lat_deg=127 lat_frac=0x03E0 ew=1 lon_deg=255 lon_frac=0x7C1F`, ce qui
donne mécaniquement une latitude/longitude impossibles. Le bug est local au
décodage/validation GNSS SGB ; la démodulation, le BCH et l'identification sont
corrects.

Correction appliquée : faire retourner explicitement par `decode_position()`
si une position est disponible, reconnaître la sentinelle observée, et éviter
les warnings de validation sur une position absente.
