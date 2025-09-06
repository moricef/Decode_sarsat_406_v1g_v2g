# Décodeur de Balises de Détresse 406 MHz - Version 10.2

[![Conformité T.001](https://img.shields.io/badge/T.001%20Implémentée-100%25-yellow)](https://cospas-sarsat.int)
[![Conformité T.018](https://img.shields.io/badge/T.018%20Implémentée-100%25-yellow)](https://cospas-sarsat.int)
[![Tests Validés](https://img.shields.io/badge/Tests-Orbitography%2C%20Test%20User-green)](#conformité-aux-standards)

## Description

Décodeur complet pour les balises de détresse COSPAS-SARSAT 406 MHz avec support des protocoles de première génération (1G) et deuxième génération (2G). **Implémentation complète T.001 et T.018** selon spécifications. Tests validés sur balises d'étalonnage (orbitography) et balises de test utilisateur.

### Fonctionnalités principales
- **Décodage 1G** : Protocoles Standard, National, User-Location, ELT-DT, RLS, Ship Security, Test (implémentation complète)
- **Décodage 2G** : Support complet SGB avec correction BCH(250,202) (implémentation complète, non testée)
- **Base de données MID** : 200+ codes pays selon ITU-R M.585 maritime
- **Capture audio** : Temps réel via microphone/SDR ou fichiers WAV
- **Décodage hexadécimal** : Direct depuis la ligne de commande
- **Géolocalisation** : Liens OpenStreetMap cliquables avec résolution 3.4m
- **23 Hex ID** : Génération automatique pour balises 2G

## Installation

### Prérequis (Linux/macOS)

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential sox libmath-dev sendemail

# CentOS/RHEL
sudo yum install gcc make sox sendemail

# macOS (avec Homebrew)
brew install sox
# Pour sendemail: installer manuellement ou utiliser un client SMTP alternatif
```


### Compilation
```bash
# Cloner et compiler
git clone [repository]
cd dec406_v10.2

# Compiler tous les programmes
make all

# Ou compiler individuellement

make dec406        # Programme complet (audio + hex)
make dec406_hex    # Décodeur hexadécimal seul
make dec406_audio  # Décodeur audio seul
```

## Utilisation


### 1. Décodage de trame hexadécimale

```bash
# Trame 1G courte (112 bits = 28 caractères hex)
./dec406_hex FFFED08E39048D158AC01E3AA482

# Trame 1G longue (144 bits = 36 caractères hex)
./dec406_hex FFFED08E39048D158AC01E3AA482856824CE

# Trame 2G (250 bits = 63-64 caractères hex)
./dec406_hex 9C7080C0C0D0E40A1806004D4C5F37FE0D2C601E55555555555555555555555D5A
```


### 2. Capture audio temps réel

```bash
# Capture depuis microphone avec filtrage optimal
sox -t alsa default -t wav - lowpass 3000 highpass 10 gain -l 6 2>/dev/null | ./dec406

# Avec timeout de 55 secondes (intégration scan406) - OBSOLÈTE depuis v10.2
# sox -t alsa default -t wav - lowpass 3000 highpass 10 | ./dec406 --une_minute

# Ajuster le seuil de détection pour signaux faibles
sox -t alsa default -t wav - | ./dec406 --20  # Plus sensible
sox -t alsa default -t wav - | ./dec406 --50  # Compromis
sox -t alsa default -t wav - | ./dec406 --100 # Défaut (robuste)

# Capture en temps réel avec timeout adaptatif (RECOMMANDÉ)
./decode_MIC_email_406.pl
```


### 3. Décodage de fichier WAV

```bash
# Fichier WAV standard
./dec406 enregistrement.wav

# Avec options spécifiques
./dec406 --une_minute --canal1 enregistrement_stereo.wav
```

### 4. Scripts automatisés

#### Configuration email
```bash
# Configuration interactive des paramètres mail
./config_mail.pl
```

#### Décodage avec notification email et timeout adaptatif
```bash
# Décodage automatique avec adaptation intelligente aux différents types de balises
./decode_MIC_email_406.pl

# Version avec liens OpenStreetMap
./decode_MIC_email_406.pl osm

# Timeout manuel pour balises spécifiques
./decode_MIC_email_406.pl timeout 90

# Combinaison timeout + OSM
./decode_MIC_email_406.pl osm 60
```

#### Scripts de décodage rapide
```bash
# Décodage simple depuis microphone
./decode.sh

# Décodage avec liens OpenStreetMap
./decode_osm.sh
```


## Options de ligne de commande


| Option | Description |
|--------|-------------|
| `--help` | Affiche l'aide complète |
| `--une_minute` |  OBSOLÈTE v10.2 - Timeout fixe 55s (remplacé par timeout adaptatif) |
| `--canal1` | Utilise le canal droit pour les fichiers stéréo |
| `--2` à `--100` | Coefficient seuil détection (2=très sensible, 100=robuste) |
| `--osm` | Génère des liens OpenStreetMap cliquables |
| `timeout XX` | *(Script Perl)* Timeout manuel en secondes (30-120s) |



## Architecture du projet


### Structure des fichiers
```
dec406_v10.2/
├── dec406.h                 # Interface principale
├── dec406.c                 # Dispatcher de décodage
├── dec406_main.c           # Programme hex uniquement
├── main_audio.c            # Programme avec support audio
├── dec406_v1g.c            # Décodeur balises 1G
├── dec406_v2g.c            # Décodeur balises 2G
├── audio_capture.c         # Module capture audio
├── display_utils.c         # Utilitaires affichage/cartes
├── country_codes.h         # Base de données MID
├── config_mail.pl          # Configuration email interactive
├── decode_MIC_email_406.pl # Décodage automatique avec email
├── decode.sh              # Script de décodage simple
├── decode_osm.sh          # Script avec liens OpenStreetMap
└── Makefile               # Compilation automatisée
```


### Architecture logicielle
```
┌─────────────────┐    ┌──────────────────┐
│   main_audio    │    │   dec406_main    │
│  (audio + hex)  │    │   (hex seul)     │
└────────┬────────┘    └────────┬─────────┘
│                      │
└──────────┬───────────┘
│
┌───▼────┐
│ dec406 │ ← Dispatcher
└───┬────┘
│
┌───────┴────────┐
│                │
┌───▼───┐       ┌────▼────┐
│ 1G    │       │   2G    │
│Decoder│       │ Decoder │
└───────┘       └─────────┘
```


## Protocoles supportés


### Balises 1G (T.001) - Implémentation complète
- **Standard Location** (P=0, codes 2-7) : MMSI, Aircraft Address, Serial Numbers
- **National Location** (P=0, codes 8,10,11) : Identifiants nationaux avec position
- **ELT-DT Location** (P=0, code 9) : Emergency Locator Transmitter - Distress Tracking
- **Ship Security** (P=0, code 12) : Protocole de sécurité maritime avec marquage [SECURITY]
- **RLS Location** (P=0, code 13) : Return Link Service
- **Test Protocols** (P=0, codes 14,15) : Standard et National Test
- **User Protocols** (P=1, codes 0-7) : Orbitography, Aviation, Maritime, Serial User, National User, Radio Call Sign, Test User


### Balises 2G/SGB (T.018) - Implémentation complète
- **Main Field** (154 bits) : TAC, Serial, Country (MID), Position GNSS
- **Rotating Fields** (48 bits) :
- **RF#0** : G.008 Objective Requirements (temps, position, altitude, DOP, batterie)
- **RF#1** : In-Flight Emergency (activation, altitude, déclenchement)
- **RF#2** : RLS Type 1&2 Acknowledgement (Galileo/GLONASS/BDS)
- **RF#4** : RLS Type 3 TWC (Two-Way Communication)
- **RF#15** : Cancellation Message (désactivation)
- **BCH Error Correction** : BCH(250,202) avec polynôme officiel
- **Vessel ID** : MMSI, Call Signs, Aircraft Address, Modified Baudot
- **23 Hex ID** : Génération selon Table 3.11 de T.018

## Rythmes de transmission des balises

### Types de balises et intervalles de répétition
- ** Balises de test** : **5 secondes** - Pour vérification et développement
- ** Balises d'exercice** : **50 secondes** - Exercices CROSS/ADRASEC  
- ** Vraies balises de détresse** : **50 secondes** - Norme COSPAS-SARSAT officielle

### Système adaptatif
Le script `decode_MIC_email_406.pl` s'adapte automatiquement :
- **Démarre en mode test** (15s) pour une réactivité maximale
- **Auto-détecte le type** et passe en mode balise réelle (55s) si nécessaire
- **Recherche approfondie** (jusqu'à 120s) pour signaux faibles
- **Maintient le mode optimal** une fois le type de balise identifié



## Exemples de sortie


### Balise 1G ELT-DT
```

== 406 MHz BEACON DECODE (1G LONG) ===
Protocol: 9 (ELT-DT Location Protocol)
Country: 227 (France)
Hex ID: LG-ELT-00E3-00003456
Identification: Aircraft 123456 - manual activation, Alt:>400m≤800m, Loc:≤2s old
Position (PDF-1): 43.85660°N, 2.35217°E
Latitude offset: +2 min 12 sec
Longitude offset: -1 min 8 sec
Composite position: 43.89027°N, 2.33317°E
 OpenStreetMap: https://www.openstreetmap.org/?mlat=43.89027&mlon=2.33317#map=18/43.89027/2.33317
[14:23:45] 1G decoding completed
```

### Balise 2G EPIRB
```
=== 406 MHz SECOND GENERATION BEACON (SGB) ===
[IDENTIFICATION]
23 Hex ID: 1E3A048D158AC01E3AA4828
TAC Number: 12345 (0x3039)
Serial Number: 67890 (0x10932)
Country Code: 227 (France)
Type: EPIRB
Vessel ID: MMSI:123456789

[STATUS]
Homing Device: Available/Active
RLS Capability: Enabled
Test Protocol: Normal Operation

[ENCODED GNSS LOCATION]
Position: 43.85660°N, 2.35217°E
Coordinates: 43.85660°N, 2.35217°E
Resolution: ~3.4 meters maximum
 OpenStreetMap: https://www.openstreetmap.org/?mlat=43.85660&mlon=2.35217#map=18/43.85660/2.35217


[ROTATING FIELD 0]
Type: G.008 Objective Requirements
Elapsed time: 12 hours
Last position: 5 minutes ago
Altitude: 1250 meters
HDOP: DOP >2 and ≤3
VDOP: DOP ≤1
Battery: >75% and ≤100% remaining
GNSS Status: 3D location

[COMPLIANCE]
Standard: COSPAS-SARSAT T.018 Second Generation Beacon
BCH Error Correction: BCH(250,202) - 48 bits
Data Rate: 300 bps, Spread Spectrum: DSSS-OQPSK
====================================================
```

## Tests et validation

```bash
# Lancer la suite de tests automatisée
make test

# Test avec échantillons audio fournis
./dec406 wav_pour_tests/trame_257_STANDARD_LocN43_43_56_E0_58_52.wav

# Validation avec trames de référence COSPAS-SARSAT
./dec406_hex FFFED08E39048D158AC01E3AA482856824CE  # ELT-DT
./dec406_hex 9C7080C0C0D0E40A1806004D4C5F37FE0D2C # 2G EPIRB
```


## Conformité aux standards


### Standards respectés
- **C/S T.001** (Balises 1G) - Implémentation complète (tests partiels : orbitography, test user)
- **C/S T.018** (Balises 2G) - Implémentation complète (non testée sur balises réelles)
- **ITU-R M.633-4** (Fréquences 406 MHz)
- **ITU-R M.1478** (Satellites LEOSAR/GEOSAR)
- **ITU-R M.585** (Maritime Identification Digits)
- **RTCM 11901.3** (Format messages position)


### Statut de validation
**Implémentation complète** : Tous les protocoles 1G et 2G COSPAS-SARSAT sont implémentés selon spécifications.

**Tests validés** : 
- **Balises d'étalonnage** : Orbitography Protocol (406.022 MHz) - décodage correct
- **Balises de test utilisateur** : Test User Protocol - décodage fonctionnel
- **Autres protocoles** : Implémentés selon spécifications mais non testés sur balises réelles

**Limitation** : Validation complète nécessiterait des échantillons de tous types de balises (ELT, EPIRB, PLB réelles, balises 2G, etc.).


## Dépannage

### Problèmes audio courants

**"sox: command not found"**
```bash
sudo apt-get install sox  # Linux
brew install sox          # macOS
```

**Pas de capture audio**
```bash
# Lister les périphériques
arecord -l

# Tester la capture
sox -t alsa default test.wav trim 0 5
```

**Erreurs CRC persistantes**
- Signal faible : Rapprocher l'antenne
- Interférences : Ajuster `--2` à `--100` pour modifier le seuil
- Vérifier la fréquence de réception (406.025 MHz pour tests)

### Problèmes avec les scripts automatisés

**"sendemail: command not found"**
```bash
sudo apt-get install sendemail  # Linux
# macOS: installer manuellement
```

**Configuration email**
- Exécuter `./config_mail.pl` avant le premier usage
- Vérifier les paramètres SMTP dans `config_mail.txt`
- Pour Gmail : utiliser un mot de passe d'application

**Scripts ne trouvent pas dec406**
- Vérifier que `make all` a été exécuté
- Décompresser si nécessaire : `decode_osm.sh` référence `./dec406_V7` (version obsolète)


### Problèmes de décodage


**Position "0.0, 0.0"**  
→ Normal pour balises sans capacité GNSS ou valeurs par défaut

**"Unknown Protocol"**  
→ Vérifier la trame hexadécimale et les bits de synchronisation

## Développement

### Structure des commits
- `feat:` Nouvelles fonctionnalités
- `fix:` Corrections de bugs
- `docs:` Documentation
- `test:` Tests et validation

### Tests de régression
```bash
# Tests automatiques
make test

# Validation conformité T.001/T.018
./validate_standards.sh
```

## Licence

Ce projet est sous licence Creative Commons Attribution - Pas d'Utilisation Commerciale - Partage dans les Mêmes Conditions 4.0 International (CC BY-NC-SA 4.0).

[![Licence Creative Commons](https://i.creativecommons.org/l/by-nc-sa/4.0/88x31.png)](http://creativecommons.org/licenses/by-nc-sa/4.0/)

### Vous êtes autorisé à :

- **Partager** : copier, distribuer et communiquer le matériel par tous moyens et sous tous formats
- **Adapter** : remixer, transformer et créer à partir du matériel

### Selon les conditions suivantes :

- **Attribution** : Vous devez créditer l'œuvre, intégrer un lien vers la licence et indiquer si des modifications ont été effectuées
- **Pas d'Utilisation Commerciale** : Vous n'êtes pas autorisé à faire un usage commercial de cette œuvre
- **Partage dans les Mêmes Conditions** : Dans le cas où vous remixez, transformez ou créez à partir du matériel composant l'œuvre originale, vous devez diffuser l'œuvre modifiée dans les mêmes conditions

**Avertissement** : Cette licence ne s'applique pas aux données de décodage des balises de détresse réelles, qui restent soumises aux réglementations nationales et internationales sur les communications d'urgence.

Pour plus de détails, consultez le texte complet de la licence : https://creativecommons.org/licenses/by-nc-sa/4.0/deed.fr


## Auteurs et contributions

- **Code original** : F4EHY (2020)
  - Décodeur 1G initial (dec406_v7)
  - Algorithmes de décodage COSPAS-SARSAT de base
- **Version 10.2** : F4MLV/ADRASEC09 avec assistance Claude (2025)
  - Refactoring complet et modularisation
  - Support 2G avec correction BCH
  - Intégration complète protocoles T.001
  - Scripts SDR et base de données MID étendue


## Références techniques

- [C/S T.001](https://cospas-sarsat.int) : Specification for Cospas-Sarsat 406 MHz Distress Beacons
- [C/S T.018](https://cospas-sarsat.int) : Specification for Second-Generation 406-MHz Distress Beacons  
- [ITU-R M.585](https://www.itu.int) : Maritime Identification Digits Database
- [ICAO Doc 10150](https://icao.int) : Manual on LADR (Location of Aircraft in Distress)

---

⚠️ **Important** : Ce décodeur est destiné uniquement à des fins éducatives et de recherche. L'utilisation pour le décodage de vraies balises de détresse doit respecter la réglementation locale sur les radiocommunications d'urgence.
