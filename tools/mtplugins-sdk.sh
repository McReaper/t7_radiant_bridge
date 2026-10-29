#!/bin/sh
# Copies the SDK headers of MTPlugins' latest release into third_party/mtplugins_sdk/ and points its README at that
# release. Prints "tag=…", "sha=…" and "changed=true|false" (GitHub Actions output lines). Needs gh, signed in or with
# GH_TOKEN set. Run from the repository root; .github/workflows/mtplugins-sdk.yml runs it weekly.
set -eu

repo=Aidan601/MTPlugins
dir=third_party/mtplugins_sdk
tag=$(gh api "repos/$repo/releases/latest" --jq .tag_name)
sha=$(gh api "repos/$repo/commits/$tag" --jq .sha)

for f in modtools_plugin.h modtools_plugin.hpp; do
    curl -fsSL "https://raw.githubusercontent.com/$repo/$sha/sdk/$f" | tr -d '\r' > "$dir/$f"
done
sed -i -E "s/at commit \`[0-9a-f]+\` \(release [^)]+\)/at commit \`$sha\` (release ${tag#v})/" "$dir/README.md"

echo "tag=$tag"
echo "sha=$sha"
if git diff --quiet -- "$dir"; then
    echo "changed=false"
else
    echo "changed=true"
fi
