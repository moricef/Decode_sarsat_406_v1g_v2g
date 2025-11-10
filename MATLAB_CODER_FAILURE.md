# Échec de la Migration MATLAB Coder

**Date:** 2025-11-09
**Commit:** 529f2ce

---

## Résumé

Migration vers MATLAB Coder **ÉCHEC TOTAL**.

Le code MATLAB Coder compile, retourne `rxStatus=SUCCESS`, mais **décode incorrectement les bits**.

**Taux d'erreur: 56% sur les 50 premiers bits**

---

## Vérification

### Trame de référence (hex)
```
89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4
```

### Bits attendus (50 premiers bits de données)
```
Référence: 00100111000011111101000101011000111000110110010101
```
Source: `/home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/tools/generate_test_from_hex.c`

### Bits décodés par MATLAB Coder
```
MATLAB:    00001001111000110111100101011000000001011001000001
```

### Comparaison
```
Ref:  00100111000011111101000101011000111000110110010101
MATL: 00001001111000110111100101011000000001011001000001
      ✗✗✗   ✗✗✗✗✗✗✗✗✗✗✗✗   ✗    ✗✗✗✗✗✗✗✗✗✗✗✗✗✗✗✗
```

**28 erreurs sur 50 bits = 56% d'erreurs**

---

## Erreur de Raisonnement

### Affirmations fausses faites
1. ✗ "MATLAB a correctement démodulé"
2. ✗ "BCH Errors: 0 signifie 0 erreur BCH"
3. ✗ "rxStatus=SUCCESS signifie décodage réussi"

### Réalité
1. MATLAB Coder décode **MAL** (56% erreurs)
2. `errs=0` est hardcodé (ligne 425 dsss_receiver.c), jamais modifié
3. `rxStatus=true` signifie juste "j'ai extrait des bits", pas "bits corrects"
4. **Aucun décodage BCH** dans le code MATLAB Coder

---

## Code MATLAB Coder - Analyse

### Paramètre `errs` (TROMPEUR)
```c
// Ligne 425: Initialisation
*errs = 0;
*rxStatus = false;

// Ligne 633: Si extraction de bits réussie
*rxStatus = true;

// errs n'est JAMAIS modifié
```

### Condition de succès (INSUFFISANTE)
```c
// Ligne 623: Condition pour rxStatus=true
if (k >= 51) {  // Au moins 51 bits extraits
    memcpy(&rxPayload[0], &despreadMessage_data[50], ...);
    *rxStatus = true;  // SUCCESS même si bits incorrects!
}
```

**Conclusion:** `rxStatus=SUCCESS` ne garantit PAS que les bits sont corrects.

---

## Conséquences

### Code archivé
- `archive/dsss_demod.c` (1305 lignes - implémentation originale)
- `archive/dsss_demod.h`

### Code ajouté (inutile)
- `test_matlab_coder/` (29 fichiers, 2600+ lignes)
- `dsss_demod_matlab.c` (140 lignes adapter)

### Migration à annuler
```bash
git revert 529f2ce
# OU
mv archive/dsss_demod.c dsss_demod.c
mv archive/dsss_demod.h dsss_demod.h
rm -rf test_matlab_coder/
git checkout Makefile
```

---

## Leçons

1. ❌ **Ne JAMAIS faire confiance à un status de retour sans vérifier les données**
2. ❌ **Ne JAMAIS assumer qu'un code "auto-généré" fonctionne correctement**
3. ❌ **Toujours vérifier bit-par-bit avec des données de référence CONNUES**
4. ❌ **"rxStatus=SUCCESS" ≠ "décodage correct"**
5. ❌ **"errs=0" peut être hardcodé**

---

## Action Requise

Restaurer le code original et corriger les **7 divergences identifiées** dans l'implémentation manuelle:

1. Carrier sync: traiter TOUS les échantillons
2. Corrélation: temporelle vs FFT
3. Phase detector: i-1 vs i-sps/2
4. Précision: float vs double
5. Fonctions math: standard vs MATLAB optimisé
6. Architecture: à simplifier intelligemment
7. Gestion mémoire: statique vs dynamique

---

**Échec documenté: 2025-11-09**
