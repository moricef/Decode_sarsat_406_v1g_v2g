# dec406 — Décodeur COSPAS-SARSAT 406 MHz

Décodeur pour balises de détresse 1ère et 2ème génération (406 MHz).

**Statut** : Développement actif (mai 2026)
**Branche** : `feature/no-matched-filter`

---

## Ce qui fonctionne

### Décodeurs (bit-perfect)
- **1G (FGB)** : Biphase-L, 400 bps, protocoles Location/User
- **2G (SGB)** : BCH(250,202) Berlekamp-Massey + Chien, tous les champs T.018

### Démodulateur DSSS OQPSK 2G
- ✅ Signal synthétique : bit-perfect
- 🟡 Signal avec offset fréquentiel (8.5 kHz) : preamble sync OK, erreurs BCH sur le message
- ❌ Signal OTA : pas encore fonctionnel

---

## Compilation

```bash
make
```

Produit les binaires dans `build/` :
- `dec406_iq` — Démodulateur DSSS OQPSK depuis fichier IQ
- `dec406_hex` — Décodeur 1G/2G depuis chaîne hex
- `dec406_audio` — Décodeur 1G depuis fichier WAV
- `generate_2g_hex` — Génération de trames de test 2G

---

## Utilisation

```bash
# Démoduler un fichier IQ 2G (2.4576 MHz, float32 complex)
./build/dec406_iq signal.iq -s 2457600

# Décoder une trame hex
./build/dec406_hex 09C4745638D95999A02B33326C3EC4400003FFF00C02832000002B774C24FE4
```

---

## Documentation

- `ARCHITECTURE_dec406.md` — Architecture détaillée et état actuel
- `docs/TESTS_VALIDATION.md` — Procédures de validation
- `docs/2024/` — Extraits de la spécification T.018
- `docs/archives/` — Documentation obsolète (sessions 2025, MATLAB Coder)

---

## Structure

```
src/         Sources C (démodulateur DSSS, décodeurs 1G/2G)
include/     Headers
build/       Binaires compilés
tests/       Tests unitaires (test_sgb_codec: 94 tests BCH/PRN/message)
scripts/     Utilitaires (scan406.pl, etc.)
utils/       Outils (resample_iq, generate_2g_hex, reset_usb)
data/        Fichiers IQ de test
docs/        Documentation et spécifications
```
