# Programmes de test

Un petit programme par fonctionnalité de la carte, à valider un par un avant d'activer le programme complet.
Ils suivent le [plan de test](../docs/plan_de_test.md) : d'abord tout ce qui n'a pas besoin du modem.

| Environnement | Ce qui est testé | OK si (moniteur série, 115200 bauds) |
|---|---|---|
| `test_carte` | Puce, flash, PSRAM | Flash 16 Mo, PSRAM ≈ 8 Mo, « Test PSRAM 1 Mo : OK », LED verte |
| `test_batterie` | Jauge MAX17048 (I2C SDA 15 / SCL 16) | Adresse 0x36 trouvée, tension identique au multimètre |
| `test_sd` | Carte micro-SD en SDMMC 1 bit (CLK 5, CMD 4, D0 6, CD 46) | Relecture OK, effacement OK |
| `test_heure` | Heure système à travers un deep sleep de 20 s | Écart de l'ordre de la seconde à chaque réveil |

## Utilisation

Ouvrir **ce dossier `tests/`** dans VS Code (PlatformIO), choisir l'environnement en bas de la fenêtre, puis **Upload and Monitor**.
En ligne de commande :

```
cd tests
pio run -e test_carte -t upload -t monitor
```

La carte est branchée sur le port USB-C (CH343). Si le téléversement échoue : maintenir **BOOT**, appuyer sur **RESET**, relâcher **BOOT**.
Pour une puce ESP32-S3**R2**, remplacer `qio_opi` par `qio_qspi` dans `platformio.ini`.
