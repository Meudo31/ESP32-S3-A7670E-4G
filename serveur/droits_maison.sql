-- Serveur maison (PostgreSQL + PostgREST, docker-compose.yml) : rôle « moto » pour le module,
-- qui peut seulement ajouter des lignes. La clé (JWT) se fabrique avec creer_cle.sh.
do $$ begin
    if not exists (select from pg_roles where rolname = 'moto') then create role moto nologin; end if;
end $$;
grant usage on schema public to moto;
grant insert on fichiers, journaux, positions_gps, trames_can, photos to moto;
grant select (id)                on fichiers      to moto;   -- clés uniques seulement
grant select (fichier_id, ligne) on journaux      to moto;   -- (nécessaire pour ignorer les doublons)
grant select (horodatage)        on positions_gps to moto;
grant select (fichier_id, ligne) on trames_can    to moto;
grant select (chemin)            on photos        to moto;
grant moto to authenticator;
