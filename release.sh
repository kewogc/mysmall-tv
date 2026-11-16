#!/usr/bin/env bash
# Cut a release: set the version, build, commit, tag and push. The tag push
# starts .github/workflows/release.yml, which builds the same way and attaches
# mysmall-tv-firmware.bin to a GitHub release. Devices pick it up from there
# (web UI: System -> Check for latest -> Update now).
#
#   ./release.sh 1.2.0
#
# The version must be a.b.c and newer than the current one: the device compares
# it numerically and only offers a release that is newer than what it runs.
set -euo pipefail

cd "$(dirname "$0")"

version=${1:-}
if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Usage: ./release.sh <a.b.c>" >&2
  exit 2
fi
tag="v$version"

current=$(sed -nE 's/^#define FW_VERSION +"([^"]+)".*/\1/p' src/config.h)
if [ "$(printf '%s\n%s\n' "$current" "$version" | sort -V | tail -1)" != "$version" ]; then
  echo "$version is older than the current $current" >&2
  exit 1
fi
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
  echo "Tag $tag already exists" >&2
  exit 1
fi
if [ -n "$(git status --porcelain)" ] && [ "${ALLOW_DIRTY:-}" != 1 ]; then
  echo "Working tree has uncommitted changes. Commit them first, or set ALLOW_DIRTY=1 to include them in the release commit." >&2
  exit 1
fi

sed -i -E "s/^(#define FW_VERSION +)\"[^\"]+\"/\1\"$version\"/" src/config.h

# Fail here, not in CI, if the tree does not build.
./build.sh

git add -A
if ! git diff --cached --quiet; then
  git commit -q -m "Release $version"
fi
git tag -a "$tag" -m "Release $version"
git push origin HEAD "$tag"

echo
echo "Pushed $tag. GitHub Actions is building the release:"
echo "  https://github.com/kewogc/mysmall-tv/actions"
