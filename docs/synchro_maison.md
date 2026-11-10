# Synchronisation au retour à la maison (firmware 0.6.2, appli 0.6.2)

Quand la moto arrive à portée du Wi-Fi de la maison, le module s'y connecte tout seul et envoie
chez Google tout ce que la carte SD a enregistré depuis la dernière fois. Rien n'est effacé de la carte.

| Sur la carte SD | Destination chez Google |
|---|---|
| `/journaux/jj-mm-aaaa.log` (logs) | Google Sheets « Moto ESP32 - 2026 », onglet **Journal** |
| `/gps/jj-mm-aaaa.csv` (trace GPS) | Google Sheets « Moto ESP32 - 2026 », onglet **GPS** (avec lien Google Maps) |
| `/can/jj-mm-aaaa/*.csv` (trames CAN brutes) | Google Drive, dossier **Moto ESP32** (fichiers CSV entiers) |
| `/photos/jj-mm-aaaa/*.jpg` | Google Photos, album **Moto ESP32** |

Tout est gratuit, dans les 15 Go de ton compte Google. Les trames CAN restent en fichiers (plus de 100 Mo
par heure de route : trop pour un tableau, qui est limité à 10 millions de cellules) ; elles s'ouvrent dans un
tableur ou avec SavvyCAN. Un classeur est créé par année.

## Quand le module synchronise

- Toutes les minutes, il cherche le Wi-Fi de la maison, sauf si la moto roule à plus de 15 km/h.
- Une fois connecté, il envoie tout, puis coupe le Wi-Fi. Il recommence au plus toutes les 15 minutes.
- Les trames CAN ne partent que moto coupée (le fichier en cours doit être fermé).
- Ce qui est déjà parti est noté dans `/synchro/progression.txt` : une coupure ne fait rien perdre.
  (Rare : si la coupure tombe pile pendant la réponse de Google, quelques lignes peuvent apparaître deux fois dans le classeur.)
- Le bouton « Synchroniser maintenant » de l'appli force une synchronisation.
- Le Wi-Fi « Moto_ESP32 » de l'appli (gros transferts, mise à jour) reste prioritaire : il interrompt la synchro.

## 1. Google : les 3 codes à donner au module (20 minutes, une seule fois)

Le module se connecte à **ton** compte Google avec trois codes : un **ID client**, un **code secret du client**
et un **jeton de rafraîchissement**. Il ne peut qu'ajouter des photos (il ne voit pas les autres) et, dans Drive,
il ne voit que les fichiers qu'il a créés lui-même.

### a) Créer le projet et activer les 3 services
1. Sur l'ordinateur, ouvrir <https://console.cloud.google.com> avec ton compte Google.
2. En haut à gauche, à côté du logo : **Sélectionner un projet → Nouveau projet** → nom « Moto ESP32 » → **Créer**, puis le sélectionner.
3. Menu ☰ → **API et services → Bibliothèque**. Chercher et cliquer **Activer** pour chacun :
   **Photos Library API**, **Google Drive API**, **Google Sheets API**.

### b) L'écran de consentement (qui a le droit de se connecter)
4. Menu ☰ → **API et services → Écran de consentement OAuth** (il peut s'appeler « Google Auth Platform »).
   **Commencer** → nom de l'appli « Moto ESP32 », ton adresse e-mail → audience **Externe** → coordonnées : ton e-mail → **Créer**.
5. Rubrique **Audience** : bouton **Publier l'application** → confirmer (état « En production »).
   Indispensable : en état « Test », Google annule le jeton au bout de 7 jours.
   L'appli reste « non validée » : normal pour un usage perso.

### c) L'ID client et le code secret du client
6. Rubrique **Clients** (ou **API et services → Identifiants → Créer des identifiants → ID client OAuth**) :
   type d'application **Application Web**, nom « Moto ESP32 ».
   Dans **URI de redirection autorisés** → **Ajouter un URI** → `https://developers.google.com/oauthplayground` → **Créer**.
7. Une fenêtre affiche **ID client** (finit par `.apps.googleusercontent.com`) et **Code secret du client** (commence par `GOCSPX-`).
   Les copier (ils restent visibles plus tard dans **Clients** → clic sur « Moto ESP32 »).

### d) Le jeton de rafraîchissement
8. Ouvrir <https://developers.google.com/oauthplayground>.
9. Roue dentée ⚙️ en haut à droite → cocher **Use your own OAuth credentials** → coller l'ID client et le code secret → fermer.
10. À gauche, dans la case **Input your own scopes**, coller exactement :
    `https://www.googleapis.com/auth/photoslibrary.appendonly https://www.googleapis.com/auth/drive.file`
    → **Authorize APIs**.
11. Choisir ton compte. Google prévient « Google n'a pas validé cette application » : **Paramètres avancés → Accéder à Moto ESP32 (non sécurisé)**,
    puis cocher les deux autorisations → **Continuer**.
12. De retour sur le Playground : **Exchange authorization code for tokens**. Copier le **Refresh token** (commence par `1//`).
    (Le « Access token » ne sert pas : il expire au bout d'une heure, le module en refait un tout seul.)

### e) Les donner au module
13. Dans l'appli, écran **Synchro à la maison → Compte Google** : ID client + code secret → **Enregistrer le client**,
    puis le jeton → **Enregistrer le jeton**. Le module lance aussitôt un test et affiche « Compte Google OK » ou la raison du refus.

Le classeur Google Sheets « Moto ESP32 - 2026 » (onglets **Journal** et **GPS**, avec un lien Google Maps par point)
et les trames CAN arrivent dans le dossier **Moto ESP32** de ton Google Drive ; les photos dans l'album **Moto ESP32** de Google Photos.
Elles y arrivent à la date de l'envoi (le JPEG de la caméra n'est pas daté) : la date de prise de vue est dans leur description.

## 2. Wi-Fi de la maison

Dans l'appli : **Synchro à la maison → Wi-Fi de la maison** : nom du réseau (2,4 GHz : l'ESP32 ne voit pas le 5 GHz)
et mot de passe → **Enregistrer et tester**, moto près de la box. Le résultat s'affiche sous le bouton :

- ✅ « Wi-Fi OK… Internet OK… Compte Google OK » : tout est prêt ;
- ❌ réseau introuvable (avec la liste des réseaux captés : nom mal écrit, majuscules, box en 5 GHz seulement, trop loin) ;
- ❌ mot de passe refusé, box en WPA3 seul, filtrage par adresse MAC, signal trop faible ;
- ❌ pas d'Internet, ou compte Google refusé (avec la raison : code secret faux, jeton expiré…).

Le bouton **Tester** refait le test à tout moment.

Les réglages peuvent aussi être mis à la compilation dans `include/secrets.h` (voir `secrets.example.h`).
Ils ne sont jamais publiés, et l'appli ne relit jamais un mot de passe ou une clé : elle affiche seulement « réglé ».

## Facultatif : une base de données en plus

### Supabase (5 minutes)

1. Créer un compte sur <https://supabase.com> puis un projet (région Europe, ex. Paris ou Francfort).
2. Menu **SQL Editor** → coller le contenu de [`serveur/schema.sql`](../serveur/schema.sql) → **Run**.
3. Puis, de la même façon, [`serveur/droits_supabase.sql`](../serveur/droits_supabase.sql) → **Run**.
   Le module pourra seulement *ajouter* des lignes : même avec sa clé, personne ne peut lire ta position.
4. **Project Settings → API** (ou « Data API ») : noter
   - l'adresse du projet `https://xxxxxxxx.supabase.co` : dans l'appli, mettre `https://xxxxxxxx.supabase.co/rest/v1` ;
   - la clé **publishable** (`sb_publishable_...`) ou, sur un ancien projet, la clé **anon**.
5. Dans l'appli : **Synchro à la maison → Base de données** : adresse + clé → Enregistrer.

À savoir : un projet gratuit Supabase se met en pause après 7 jours sans aucune requête.
Les synchros l'empêchent tant que tu roules ; sinon, il se relance d'un clic dans le tableau de bord.
Pour lire les données : **Table Editor**, ou **SQL Editor** avec par exemple :

```sql
select * from sorties_par_jour;                                   -- une ligne par jour de sortie
select * from journaux where texte like '%[CAN]%' order by horodatage desc limit 50;
select * from photos_avec_position;                               -- chaque photo avec sa position GPS
```

### Serveur maison

Dossier [`serveur/`](../serveur/) : `docker compose up -d` sur un Raspberry Pi ou un NAS lance PostgreSQL + PostgREST
avec les mêmes tables. `./creer_cle.sh` donne la clé. Adresse dans l'appli : `http://<ip-du-serveur>:3000`.
Avec un serveur maison, l'option « Trames CAN → Base » remplit aussi la table `trames_can` (vue `trames_can_horodatees`).

## Ce qui n'est pas encore testé sur la moto

Tout ce module (firmware compilé en CI, schéma SQL vérifié sur PostgreSQL 16, mais aucun envoi réel).
Points à surveiller : la mémoire pendant les échanges chiffrés avec le Bluetooth actif, et le débit réel vers Drive.
