#!/usr/bin/env bash
# Corre las pruebas de la base de datos en un PostgreSQL temporal local.
# Uso: npm run test:db
set -euo pipefail
cd "$(dirname "$0")/.."
PGBIN="${PGBIN:-$(ls -d /usr/lib/postgresql/*/bin 2>/dev/null | sort -V | tail -1)}"
DIR="$(mktemp -d)"
PORT="${PGPORT_TEST:-54329}"
AS=""
if [ "$(id -u)" = "0" ]; then chown postgres "$DIR"; AS="runuser -u postgres --"; fi
cleanup() { $AS "$PGBIN/pg_ctl" -D "$DIR/data" stop -m fast >/dev/null 2>&1 || true; rm -rf "$DIR"; }
trap cleanup EXIT
$AS "$PGBIN/initdb" -D "$DIR/data" -U postgres -A trust >/dev/null
$AS "$PGBIN/pg_ctl" -D "$DIR/data" -o "-p $PORT -k $DIR -c timezone=UTC" -l "$DIR/log" start -w >/dev/null
PSQL=(psql -h "$DIR" -p "$PORT" -U postgres -d postgres -v ON_ERROR_STOP=1 -q -X)
"${PSQL[@]}" -f supabase/tests/00_supabase_stub.sql
for f in supabase/migrations/*.sql; do "${PSQL[@]}" -f "$f"; done
"${PSQL[@]}" -f supabase/tests/helpers.sql
fail=0
for t in supabase/tests/test_*.sql; do
  echo "== $(basename "$t")"
  if ! "${PSQL[@]}" -f "$t" 2>&1 | sed -e 's/^psql:[^ ]* NOTICE:  /  /' -e 's/^NOTICE:  /  /'; then fail=1; fi
done
if [ "$fail" = "1" ]; then echo "❌ Hubo pruebas que fallaron"; exit 1; fi
echo "✅ Todas las pruebas de la base de datos pasaron"
