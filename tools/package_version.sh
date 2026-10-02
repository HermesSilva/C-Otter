#!/usr/bin/env bash
# C-Otter -- versao que vai no nome do pacote.
#
# N e' o numero de build (OTTER_BUILD_NUMBER, o run do workflow de Release):
# sobe sozinho a cada entrega, e e' o mesmo que o binario mostra na janela
# About (src/base/version.cpp).
#
#   entrega manual  -> X.Y.Z.N        (X.Y.Z e' o VERSION do CMakeLists.txt)
#   tag vX.Y.Z      -> X.Y.Z.N        (a tag tem de bater com o VERSION)
#   tag vX.Y.Z-rc1  -> X.Y.Z.N-rc1    (o sufixo e' livre)
#   sem numero      -> X.Y.Z-<commit> (pacote de conferencia, nao e' release)
#
# A checagem da tag existe porque o numero que o programa conhece vem do
# CMake: uma tag v0.2.0 sobre um CMakeLists que diz 0.1.0 publicaria um pacote
# cujo nome desmente o binario.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake_version=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+(\.[0-9]+)*).*/\1/p' \
                    CMakeLists.txt | head -1)
if [ -z "$cmake_version" ]; then
    echo "package_version: VERSION nao encontrado no CMakeLists.txt" >&2
    exit 1
fi

build=${OTTER_BUILD_NUMBER:-}
case "$build" in
    '' | *[!0-9]*) build= ;;
esac

if [ -z "$build" ]; then
    version="$cmake_version-$(git rev-parse --short HEAD)"
elif [ "${GITHUB_REF_TYPE:-}" = "tag" ]; then
    tag=${GITHUB_REF_NAME#v}
    case "$tag" in
        "$cmake_version")   version="$cmake_version.$build" ;;
        "$cmake_version"-*) version="$cmake_version.$build-${tag#"$cmake_version"-}" ;;
        *)
            echo "package_version: a tag $GITHUB_REF_NAME nao bate com o VERSION" \
                 "$cmake_version do CMakeLists.txt" >&2
            exit 1
            ;;
    esac
else
    version="$cmake_version.$build"
fi

echo "$version"
