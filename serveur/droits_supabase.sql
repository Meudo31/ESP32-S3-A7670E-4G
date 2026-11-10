-- Supabase : le module utilise la clé « publishable » (rôle anon).
-- Il peut AJOUTER des lignes, mais ni les lire, ni les modifier, ni les effacer :
-- même si la clé fuitait, personne ne pourrait lire ta position GPS avec.
-- Seules les colonnes de la clé unique sont lisibles (sans elles, PostgreSQL ne peut pas
-- vérifier qu'une ligne renvoyée deux fois existe déjà : « ON CONFLICT DO NOTHING »).
-- Toi, tu lis tout dans le tableau de bord Supabase (Table Editor / SQL Editor).

alter table fichiers      enable row level security;
alter table journaux      enable row level security;
alter table positions_gps enable row level security;
alter table trames_can    enable row level security;
alter table photos        enable row level security;

revoke all on fichiers, journaux, positions_gps, trames_can, photos from anon, authenticated;
revoke all on trames_can_horodatees, photos_avec_position, sorties_par_jour from anon, authenticated;
grant insert on fichiers, journaux, positions_gps, trames_can, photos to anon;
grant select (id)                on fichiers      to anon;
grant select (fichier_id, ligne) on journaux      to anon;
grant select (horodatage)        on positions_gps to anon;
grant select (fichier_id, ligne) on trames_can    to anon;
grant select (chemin)            on photos        to anon;

create policy "le module ajoute" on fichiers      for insert to anon with check (true);
create policy "le module ajoute" on journaux      for insert to anon with check (true);
create policy "le module ajoute" on positions_gps for insert to anon with check (true);
create policy "le module ajoute" on trames_can    for insert to anon with check (true);
create policy "le module ajoute" on photos        for insert to anon with check (true);
create policy "doublons" on fichiers      for select to anon using (true);   -- limité aux colonnes ci-dessus
create policy "doublons" on journaux      for select to anon using (true);   -- limité aux colonnes ci-dessus
create policy "doublons" on positions_gps for select to anon using (true);   -- limité aux colonnes ci-dessus
create policy "doublons" on trames_can    for select to anon using (true);   -- limité aux colonnes ci-dessus
create policy "doublons" on photos        for select to anon using (true);   -- limité aux colonnes ci-dessus
