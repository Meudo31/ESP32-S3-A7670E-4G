-- ════════════════════════════════════════════════════════════════════════════
-- Tables qui reçoivent les données du module moto (module_synchro, firmware 0.6.0)
-- Pour Supabase (SQL Editor → coller → Run) comme pour le serveur maison (Docker).
-- Ensuite : droits_supabase.sql OU droits_maison.sql.
-- Chaque ligne a une clé unique : le module peut renvoyer un paquet sans créer de doublon.
-- ════════════════════════════════════════════════════════════════════════════

-- Un fichier de la carte SD (journal du jour, enregistrement CAN...).
-- id = empreinte FNV-1a 64 bits du chemin, calculée par le module.
create table if not exists fichiers (
    id             bigint primary key,
    chemin         text        not null,
    genre          text        not null check (genre in ('journal', 'can', 'gps', 'photo')),
    debut          timestamptz,            -- journal : minuit du jour ; CAN : début de l'enregistrement
    debut_ms       bigint,                 -- CAN : millis() du module à « debut »
    vitesse_kbits  integer,                -- CAN : vitesse du bus
    drive_id       text,                   -- CAN : fichier CSV dans Google Drive
    firmware       text,
    recu_le        timestamptz not null default now()
);

-- Journaux (logs) du module : une ligne par ligne de /journaux/jj-mm-aaaa.log
create table if not exists journaux (
    fichier_id  bigint  not null references fichiers (id) on delete cascade,
    ligne       integer not null,
    horodatage  timestamptz,
    texte       text    not null,
    primary key (fichier_id, ligne)
);
create index if not exists journaux_horodatage on journaux (horodatage);

-- Trace GPS : un point toutes les 2 s en roulant, toutes les minutes à l'arrêt.
create table if not exists positions_gps (
    horodatage   timestamptz primary key,
    latitude     double precision not null,
    longitude    double precision not null,
    altitude     real,
    vitesse_kmh  real,
    cap          real,
    satellites   smallint,
    hdop         real
);

-- Trames CAN brutes (seulement en mode « base » : prévu pour un serveur maison,
-- trop volumineux pour une offre gratuite ; par défaut elles vont dans Google Drive).
create table if not exists trames_can (
    fichier_id   bigint   not null references fichiers (id) on delete cascade,
    ligne        integer  not null,
    ms           bigint   not null,       -- millis() du module à la réception
    identifiant  text     not null,       -- hexadécimal, 3 (11 bits) ou 8 (29 bits) chiffres
    etendue      boolean  not null,       -- true = 29 bits (J1939)
    dlc          smallint not null,
    donnees      text     not null,       -- hexadécimal, 2 chiffres par octet
    primary key (fichier_id, ligne)
);
create index if not exists trames_can_identifiant on trames_can (identifiant);

-- Photos envoyées dans Google Photos.
create table if not exists photos (
    chemin       text primary key,
    horodatage   timestamptz,
    taille       integer,
    google_id    text,
    google_url   text,
    envoyee_le   timestamptz not null default now()
);

-- ─── Vues pratiques ───

-- Trames CAN avec leur heure réelle.
create or replace view trames_can_horodatees with (security_invoker = true) as
select t.*, f.debut + (t.ms - f.debut_ms) * interval '1 millisecond' as horodatage
from trames_can t join fichiers f on f.id = t.fichier_id;

-- Chaque photo avec la position GPS la plus proche (à 2 minutes près).
create or replace view photos_avec_position with (security_invoker = true) as
select p.*, g.latitude, g.longitude
from photos p
left join lateral (
    select latitude, longitude from positions_gps g
    where g.horodatage between p.horodatage - interval '2 minutes' and p.horodatage + interval '2 minutes'
    order by abs(extract(epoch from g.horodatage - p.horodatage))
    limit 1
) g on true;

-- Sorties : points GPS regroupés par jour.
create or replace view sorties_par_jour with (security_invoker = true) as
select (horodatage at time zone 'Europe/Paris')::date as jour,
       min(horodatage) as depart, max(horodatage) as arrivee,
       count(*) as points, max(vitesse_kmh) as vitesse_max_kmh
from positions_gps group by 1 order by 1 desc;
