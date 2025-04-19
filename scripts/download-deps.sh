#!/bin/bash
set -xeu
cd "$(dirname "$0")"

: "${TARGET_TRIPLET:="$(uname -m)-linux-gnu"}"
TARGET_ARCH="${TARGET_TRIPLET%%-*}"
DEPSDIR=../deps
source ./deps.sh

download_dep () {
    local tarname url hashsum
    tarname=$1; url=$2; hashsum=$3

    if [ ! -e "$tarname" ]; then
        curl -fo "$tarname.downloading" "$url"
        mv "$tarname.downloading" "$tarname"
    fi
    echo "$hashsum *$tarname" | sha256sum -c
}

mkdir -p "$DEPSDIR"

if [ "$TARGET_ARCH" = "x86_64" ]; then
    echo "$vscode_cli_x64_version" > "$DEPSDIR/vscode-version.txt"
    vscode_cli_filename="$vscode_cli_x64_filename"
    vscode_cli_url="$vscode_cli_x64_url"
    vscode_cli_sha256="$vscode_cli_x64_sha256"
    vscode_server_filename="$vscode_server_x64_filename"
    vscode_server_url="$vscode_server_x64_url"
    vscode_server_sha256="$vscode_server_x64_sha256"
elif [ "$TARGET_ARCH" = "aarch64" ]; then
    echo "$vscode_cli_arm64_version" > "$DEPSDIR/vscode-version.txt"
    vscode_cli_filename="$vscode_cli_arm64_filename"
    vscode_cli_url="$vscode_cli_arm64_url"
    vscode_cli_sha256="$vscode_cli_arm64_sha256"
    vscode_server_filename="$vscode_server_arm64_filename"
    vscode_server_url="$vscode_server_arm64_url"
    vscode_server_sha256="$vscode_server_arm64_sha256"
elif [ "$TARGET_ARCH" = "arm" ]; then
    echo "$vscode_cli_armhf_version" > "$DEPSDIR/vscode-version.txt"
    vscode_cli_filename="$vscode_cli_armhf_filename"
    vscode_cli_url="$vscode_cli_armhf_url"
    vscode_cli_sha256="$vscode_cli_armhf_sha256"
    vscode_server_filename="$vscode_server_armhf_filename"
    vscode_server_url="$vscode_server_armhf_url"
    vscode_server_sha256="$vscode_server_armhf_sha256"
else
    echo "Unsupported CPU architecture $TARGET_ARCH" >&2
    exit 1
fi

for dep in binutils gcc glibc gmp linux mpc mpfr vscode_cli vscode_server; do
    dep_filename="${dep}_filename"
    dep_url="${dep}_url"
    dep_sha256="${dep}_sha256"
    download_dep "$DEPSDIR/${!dep_filename}" "${!dep_url}" "${!dep_sha256}"
done
