#!/bin/bash
# Fabrique la clé (JWT signé HS256, rôle « moto », sans date d'expiration) à mettre dans l'appli.
# Usage : ./creer_cle.sh   (lit SECRET_JWT dans .env)
set -e
source "$(dirname "$0")/.env"
b64url() { openssl base64 -A | tr '+/' '-_' | tr -d '='; }
entete=$(printf '{"alg":"HS256","typ":"JWT"}' | b64url)
contenu=$(printf '{"role":"moto"}' | b64url)
signature=$(printf '%s.%s' "$entete" "$contenu" | openssl dgst -sha256 -hmac "$SECRET_JWT" -binary | b64url)
echo "$entete.$contenu.$signature"
