#!/usr/bin/env bash
# Put the current github.io build on this box, as the multiplayer deployment.
#
#   sudo tools/deploy/sync-from-pages.sh
#
# The two deployments are the same wasm. Rather than build it twice -- a second
# build is a second thing to keep in step, and there is no emsdk on the serving
# box -- this fetches the bytes Pages is already serving and changes the two
# constants that decide whether multiplayer exists.
#
# So Pages is the build artifact store. That is not a trick: the workflow there
# is the only thing that compiles, its output is public, and taking it verbatim
# is what makes "one build, two deployments" literally true instead of a claim
# about two builds that ought to match. Run this after every Pages deploy.
#
# It is idempotent, and it refuses rather than half-applies: nothing is moved
# into place until every file has been fetched and both constants have been
# rewritten and checked.
set -euo pipefail

SOURCE="${MCPE_SOURCE:-https://luispl77.github.io/mcpe-0.6.1-web}"
ROOT="${MCPE_ROOT:-/var/www/mcpe}"
ORIGIN="${MCPE_ORIGIN:-https://mcpe.continualmi.com}"

# Where the board and the switch are, as the page will ask for them. Same origin
# as the page, so there is no CORS to get wrong and one certificate to renew.
LOBBY_URL="${MCPE_LOBBY_URL:-$ORIGIN/lobby}"
RELAY_URL="${MCPE_RELAY_URL:-${ORIGIN/https:/wss:}/lobby/relay}"

FILES=(index.html minecraftpe.js minecraftpe.wasm minecraftpe.data)
OPTIONAL=(menu.mp3)

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT

echo "fetching from $SOURCE"
for f in "${FILES[@]}"; do
	curl -fsS --retry 3 -o "$staging/$f" "$SOURCE/$f"
	printf '  %-18s %s bytes\n' "$f" "$(stat -c%s "$staging/$f")"
done
for f in "${OPTIONAL[@]}"; do
	if curl -fsS --retry 2 -o "$staging/$f" "$SOURCE/$f"; then
		printf '  %-18s %s bytes\n' "$f" "$(stat -c%s "$staging/$f")"
	else
		echo "  $f not published, skipping"
		rm -f "$staging/$f"
	fi
done

# The two constants, each declared exactly once in its own block in shell.html
# and carried through into the built page. Anchored on the `var NAME = '';` form
# so this cannot match a mention in a comment.
rewrite() {
	local name="$1" value="$2" file="$3" before after
	before="$(grep -c "var $name = '';" "$file" || true)"
	if [ "$before" != "1" ]; then
		echo "refusing: expected exactly one \`var $name = '';\` in $file, found $before" >&2
		echo "  the page has changed shape -- check shell.html before trusting this script" >&2
		exit 1
	fi
	sed -i "s|var $name = '';|var $name = '$value';|" "$file"
	after="$(grep -c "var $name = '$value';" "$file" || true)"
	[ "$after" = "1" ] || { echo "refusing: rewrite of $name did not take" >&2; exit 1; }
	echo "  $name -> $value"
}

echo "pointing the page at this deployment"
rewrite LOBBY_URL "$LOBBY_URL" "$staging/index.html"
rewrite RELAY_URL "$RELAY_URL" "$staging/index.html"

mkdir -p "$ROOT"
for f in "$staging"/*; do
	install -m 0644 "$f" "$ROOT/$(basename "$f")"
done

echo "installed into $ROOT"
ls -l "$ROOT"
