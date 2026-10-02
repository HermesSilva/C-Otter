#!/usr/bin/env bash
# C-Otter -- monta o pacote Linux a partir de um build pronto.
#
#   tools/package.sh <versao> [dir-de-build]     # padrao: build/linux-release
#
# Gera build/dist/c-otter-<versao>-linux-x64.tar.gz e o .sha256 ao lado.
#
# O pacote e' a pasta do produto portatil (ADR 0020): o executavel e, ao lado
# dele, so' o que nao esta' embutido -- licencas e a pasta de idiomas. Os
# dados (.C-Otter/, .script/) nascem ali na primeira execucao.
set -euo pipefail

version=${1:?uso: tools/package.sh <versao> [dir-de-build]}
repo=$(cd "$(dirname "$0")/.." && pwd)
build=${2:-build/linux-release}

name="c-otter-$version-linux-x64"
dist="$repo/build/dist"
stage="$dist/$name"

rm -rf "$stage"
mkdir -p "$stage/lang"

cp "$repo/$build/bin/c-otter" "$stage/"
# Tira os simbolos (~2 MB a menos). --strip-unneeded preserva o que o
# carregador dinamico precisa: glibc, libGL e X11 entram como .so.
strip --strip-unneeded "$stage/c-otter"
chmod 755 "$stage/c-otter"

cp "$repo/docs/LICENSES.md"             "$stage/"
# Os icones embutidos sao os originais do DBeaver (Apache 2.0): o aviso viaja
# com o binario.
cp "$repo/assets/icons/dbeaver/NOTICE"  "$stage/"
cp "$repo"/lang/*                       "$stage/lang/"

# tar.gz, e nao zip: preserva o bit de execucao.
tar -C "$dist" -czf "$dist/$name.tar.gz" "$name"
(cd "$dist" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")

echo "$dist/$name.tar.gz"
