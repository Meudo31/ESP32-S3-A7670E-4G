#!/bin/bash
# Rôles utilisés par PostgREST (exécuté une seule fois, à la création de la base).
set -e
psql -v ON_ERROR_STOP=1 -U postgres -d moto <<SQL
create role authenticator noinherit login password '${MOT_DE_PASSE_AUTHENTICATOR}';
create role nobody nologin;
SQL
