# Changelog - dec406_v10.2

## Version 10.2.1 - 2025-09-04 - Implémentation Complète T.001

### Fonctionnalités Majeures Ajoutées
- **Ship Security Protocol (Protocol 12)** : Décodage complet avec marquage `[SECURITY]`
- **Standard Test Protocol (Protocol 14)** : Décodage données de test hexadécimales  
- **National Test Protocol (Protocol 15)** : Décodage données d'usage national
- **Radio Call Sign User Protocol (Protocol 6)** : Décodage Baudot 7 caractères
- **Test User Protocol (Protocol 7)** : Amélioration du décodage utilisateur test

### Améliorations Protocoles Existants
- **Orbitography Protocol (Protocol 0)** : 
  - Décodage spécialisé des données d'orbitographie (5 bytes + 6 bits)
  - Correction identification balises d'étalonnage 406.022 MHz
- **National User Protocol (Protocol 4)** : Extraction complète données nationales
- **Aviation/Maritime User Protocols** : Décodage Baudot amélioré pour call signs

### Fonctions Techniques Ajoutées
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

### Impact Conformité
- **Avant** : T.001 95% implémenté + T.018 implémenté = 95% implémenté
- **Après** : T.001 100% implémenté + T.018 implémenté = 100% implémenté

**Tests validés** : Orbitography Protocol (balises étalonnage 406.022 MHz) et Test User Protocol uniquement.
**Limitation** : Autres protocoles implémentés selon spécifications mais non testés sur balises réelles.

### Protocoles Maintenant Supportés (Complet)

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

### Tests Validés
- Compilation sans erreurs/warnings
- Test balises d'étalonnage 406.022 MHz (Orbitography Protocol - identification correcte)  
- Test balises test utilisateur (Test User Protocol - décodage fonctionnel)
- Nouveaux protocoles (Ship Security, Standard Test, National Test, Radio Call Sign) : implémentés selon spécifications, non testés
- Régression : protocoles existants préservés

### Références Standards
- **COSPAS-SARSAT T.001** : 100% implémenté (tests partiels : orbitography, test user)
- **COSPAS-SARSAT T.018** : Implémentation complète (non testée sur balises réelles)
- **ITU-R M.585** : MID database complete
- **Modified Baudot** : Implémentation complète (testée partiellement)

---

## Version 10.2.0 - 2025-08-xx 

### 🚀 Fonctionnalités Initiales  
- Décodeur 1G complet (T.001 95% conformité)
- Décodeur 2G complet (T.018 100% conformité)
- Support audio temps réel
- Base données MID complète
- Scripts automatisation email
- Géolocalisation OpenStreetMap

### 🛠️ Architecture
- Modularité complète (5 modules principaux)
- Pipeline audio optimisé
- Correction erreurs BCH(250,202)
- Support multi-formats (hex, WAV, temps réel)