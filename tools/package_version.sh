#!/usr/bin/env bash
# C-Otter -- versao que vai no nome do pacote.
#
#   tag vX.Y.Z      -> X.Y.Z          (tem de bater com o VERSION do CMakeLists.txt)
#   tag vX.Y.Z-rc1  -> X.Y.Z-rc1      (o sufixo e' livre)
#   fora de tag     -> X.Y.Z-<commit> (pacote de conferencia, nao e' release)
#
# A checagem existe porque o numero que o programa conhece vem do CMake: uma
# tag v0.2.0 sobre um CMakeLists que diz 0.1.0 publicaria um pacote cujo nome
# desmente o binario.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake_version=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+(\.[0-9]+)*).*/\1/p' \
                    CMakeLists.txt | head -1)
if [ -z "$cmake_version" ]; then
    echo "package_version: VERSION nao encontrado no CMakeLists.txt" >&2
    exit 1
fi

if [ "${GITHUB_REF_TYPE:-}" = "tag" ]; then
    version=${GITHUB_REF_NAME#v}
    case "$version" in
        "$cmake_version" | "$cmake_version"-*) ;;
        *)
            echo "package_version: a tag $GITHUB_REF_NAME nao bate com o VERSION" \
                 "$cmake_version do CMakeLists.txt" >&2
            exit 1
            ;;
    esac
else
    version="$cmake_version-$(git rev-parse --short HEAD)"
fi

echo "$version"
