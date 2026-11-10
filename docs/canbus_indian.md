# Bus CAN de l'Indian Scout Bobber 2025

Le module écoute le bus CAN de la moto avec le contrôleur CAN intégré à l'ESP32-S3 (TWAI),
en **mode écoute seule** : il n'envoie jamais rien et n'acquitte même pas les trames.
Code : `src/module_canbus.cpp`. Protocole vers l'appli : `docs/protocole_ble.md` (messages `can` et `can_ids`).

## Branchement

| Prise diagnostic de la moto (Delphi 8 broches) | Transceiver SN65HVD230 (3,3 V) | Carte ESP32-S3 |
|---|---|---|
| H : CAN High (fil jaune) | CANH | |
| G : CAN Low (fil vert foncé) | CANL | |
| D : masse (noir) | GND | GND |
| | 3V3 | 3V3 |
| | CTX (ou TX) | GPIO 3 |
| | CRX (ou RX) | GPIO 1 |

- **Retirer la résistance de 120 Ω** du module SN65HVD230 (souvent marquée R2) : le bus de la moto a déjà ses terminaisons.
- Ne pas relier le +12 V de la prise (broches A et B) au transceiver.
- Câble : adaptateur « Polaris 8 pin diagnostic » (vendu pour RZR / Slingshot).
- GPIO 1 et 3 sont les deux derniers GPIO libres de la carte V2. Sur une carte V1, le GPIO 3 est la jauge de batterie :
  le bus CAN est alors désactivé automatiquement.
- Pour désactiver le bus CAN : `BROCHE_CAN_TX` et `BROCHE_CAN_RX` à `-1` dans `include/configuration.h`.

## Fonctionnement

1. Au démarrage, le module cherche la vitesse du bus : 250, puis 500, 125 et 1000 kbit/s (1,5 s d'écoute chacune).
   On peut l'imposer dans l'appli (écran « Bus CAN de la moto ») ou par la commande `CAN_VITESSE`.
2. Sans trame (moto coupée), il réessaie toutes les 5 secondes.
3. Toutes les trames sont enregistrées sur la carte SD, un fichier par sortie :
   `/can/jj-mm-aaaa/jj-mm-aaaa_hh-mm-ss.csv`, une ligne par trame : `millisecondes;identifiant;X (29 bits) ou S (11 bits);longueur;données`.
   Ces fichiers servent à trouver les trames propres à la Scout. Ils se téléchargent depuis l'écran « Fichiers » de l'appli.
4. Les trames connues sont décodées et envoyées à l'appli chaque seconde.

## Trames décodées

Le bus Indian / Polaris est en **SAE J1939** (identifiants 29 bits, 250 kbit/s sur l'Indian Springfield 2017).
Ces décodages viennent de cette moto (projet open source Prinsessen/indian-thunderstroke-canbus, licence MIT) et de la norme :
**ils restent à vérifier sur la Scout 2025**, dont le calculateur (MG1) est différent.

| PGN | Source | Donnée |
|---|---|---|
| 61444 EEC1 | moteur (0) | régime (octets 3-4 × 0,125), couple demandé (octet 7 − 125) |
| 61445 ETC2 | moteur | rapport engagé (octet 5, ASCII « N », « 1 » à « 6 ») |
| 65265 CCVS | ABS (11) | vitesse (octets 1-2 / 256 km/h) |
| 65265 CCVS | moteur (0) | frein, embrayage, régulateur engagé (octet 3, champs de 2 bits) |
| 65215 EBC2 | ABS | vitesse roue avant (octets 0-1 / 256), patinage arrière ((octet 4 − 125) / 16) |
| 65262 ET1 | moteur | température moteur (octet 0 − 40) |
| 65266 LFE | moteur | débit d'essence, consommations, ouverture des gaz (octet 6 × 0,4 %) |
| 65276 | moteur | niveau d'essence (octet 1 × 0,4 %) |
| 65382 | moteur | autonomie en km (octets 3-4, propre à Indian) |
| 65271 VEP1 | | tension batterie (octets 4-5 × 0,05 V) |
| 65269 AMB | compteur (23) | température extérieure |
| 65217 HRVD | compteur | compteur total et trip 1 (× 0,005 km) |
| 65268 | | pneus : pression et température (si la moto a des capteurs) |
| 65381 | moteur / gestion (39) | coupe-circuit, béquille, clignotants, warning, feux (propre à Indian) |
| 2304 | | capteur de chute (127 = droite, ~113 = sur la béquille) — ce n'est pas un angle d'inclinaison |
| 65226 DM1 | toutes | codes défaut actifs (SPN, FMI) et voyants, aussi en messages longs (BAM) |

Si un outil de diagnostic interroge la moto en OBD-II (11 bits, réponses 0x7E8), les réponses sont aussi décodées
(régime, vitesse, température, gaz, essence, tension).

## Prochaine étape

Faire une sortie avec l'enregistrement actif, en notant quelques actions (clignotants, rapports, coupe-circuit,
béquille, mode de conduite), puis analyser le fichier `.csv` pour confirmer ou corriger les décodages de la Scout.
