# Synchronisation au retour à la maison (firmware 0.6.0, appli 0.6.0)

Quand la moto arrive à portée du Wi-Fi de la maison, le module s'y connecte tout seul et envoie
tout ce que la carte SD a enregistré depuis la dernière fois. Rien n'est effacé de la carte.

| Sur la carte SD | Destination | Gratuit jusqu'à |
|---|---|---|
| `/journaux/jj-mm-aaaa.log` (logs) | Supabase, table `journaux` | 500 Mo de base |
| `/gps/jj-mm-aaaa.csv` (trace GPS, nouveau) | Supabase, table `positions_gps` | (même base) |
| `/can/jj-mm-aaaa/*.csv` (trames CAN brutes) | Google Drive, dossier « Moto ESP32 - trames CAN » | 15 Go (compte Google) |
| `/photos/jj-mm-aaaa/*.jpg` | Google Photos, album « Moto ESP32 » + table `photos` | 15 Go (compte Google) |

**Pourquoi ce partage ?** Les trames CAN brutes font plus de 100 Mo par heure de route : aucune base
gratuite ne les tient (Supabase 500 Mo, Firestore 20 000 écritures par jour). En fichiers CSV dans
Google Drive, elles restent complètes et s'ouvrent dans un tableur ou avec SavvyCAN. Les tables
(journaux, GPS) vont dans une vraie base PostgreSQL chez Supabase, que l'ESP32 sait remplir directement.
Plus tard, la même chose peut tourner sur un serveur maison (dossier `serveur/`, Docker) :
il suffit de changer l'adresse dans l'appli, et les trames CAN peuvent alors aller dans la table `trames_can`.

## Quand le module synchronise

- Toutes les minutes, il cherche le Wi-Fi de la maison, sauf si la moto roule à plus de 15 km/h.
- Une fois connecté, il envoie tout, puis coupe le Wi-Fi. Il recommence au plus toutes les 15 minutes.
- Les trames CAN ne partent que moto coupée (le fichier en cours doit être fermé).
- Ce qui est déjà parti est noté dans `/synchro/progression.txt` : une coupure ne fait rien perdre,
  et renvoyer un paquet ne crée pas de doublon.
- Le bouton « Synchroniser maintenant » de l'appli force une synchronisation.
- Le Wi-Fi « Moto_ESP32 » de l'appli (gros transferts, mise à jour) reste prioritaire : il interrompt la synchro.

## 1. Supabase (base de données, 5 minutes)

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

## 2. Google (photos et trames CAN, 15 minutes, une seule fois)

Le module se connecte à **ton** compte Google avec un « jeton de rafraîchissement ».
Il ne peut qu'ajouter des photos (il ne voit pas les autres) et ne voit dans Drive que les fichiers qu'il a créés.

1. <https://console.cloud.google.com> → créer un projet « Moto ESP32 ».
2. **API et services → Bibliothèque** : activer **Photos Library API** et **Google Drive API**.
3. **API et services → Écran de consentement OAuth** (ou « Google Auth Platform ») :
   type **Externe**, nom « Moto ESP32 », ton adresse e-mail. Ajouter ton adresse dans les utilisateurs test.
   Puis **Publier l'application** (état « En production ») : sinon Google annule le jeton au bout de 7 jours.
   L'appli restera « non validée » : c'est normal pour un usage perso, Google affichera juste un avertissement à l'étape 6.
4. **Identifiants → Créer des identifiants → ID client OAuth** : type **Application Web**,
   URI de redirection autorisé : `https://developers.google.com/oauthplayground`.
   Noter l'**ID client** et le **code secret du client**.
5. Ouvrir <https://developers.google.com/oauthplayground> → roue dentée en haut à droite →
   cocher **Use your own OAuth credentials** → coller l'ID et le secret.
6. À gauche, dans « Input your own scopes », coller :
   `https://www.googleapis.com/auth/photoslibrary.appendonly https://www.googleapis.com/auth/drive.file`
   → **Authorize APIs** → choisir ton compte → « Paramètres avancés → Accéder à Moto ESP32 (non sécurisé) » → Autoriser.
7. **Exchange authorization code for tokens** → copier le **Refresh token** (commence par `1//`).
8. Dans l'appli : **Synchro à la maison → Compte Google** : ID client + secret → « Enregistrer le client »,
   puis le jeton → « Enregistrer le jeton ».

Remarque : les photos arrivent dans Google Photos à la date de l'envoi (le JPEG de la caméra n'a pas de date
intégrée) ; la date de prise de vue est dans leur description et dans la table `photos`.

## 3. Wi-Fi de la maison

Dans l'appli : **Synchro à la maison → Wi-Fi de la maison** : nom du réseau (2,4 GHz : l'ESP32 ne voit pas le 5 GHz)
et mot de passe → Enregistrer. Puis « Synchroniser maintenant » pour tester, en restant près de la box.

Les réglages peuvent aussi être mis à la compilation dans `include/secrets.h` (voir `secrets.example.h`).
Ils ne sont jamais publiés, et l'appli ne relit jamais un mot de passe ou une clé : elle affiche seulement « réglé ».

## Serveur maison (facultatif, plus tard)

Dossier [`serveur/`](../serveur/) : `docker compose up -d` sur un Raspberry Pi ou un NAS lance PostgreSQL + PostgREST
avec les mêmes tables. `./creer_cle.sh` donne la clé. Adresse dans l'appli : `http://<ip-du-serveur>:3000`.
Avec un serveur maison, l'option « Trames CAN → Base » remplit aussi la table `trames_can` (vue `trames_can_horodatees`).

## Ce qui n'est pas encore testé sur la moto

Tout ce module (firmware compilé en CI, schéma SQL vérifié sur PostgreSQL 16, mais aucun envoi réel).
Points à surveiller : la mémoire pendant les échanges chiffrés avec le Bluetooth actif, et le débit réel vers Drive.
