#!/bin/bash

set -euo pipefail

export DOCKER_BUILDKIT="${DOCKER_BUILDKIT:-1}"

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required"
    exit 1
fi

IMAGE="${CATHOOK_DOCKER_IMAGE:-cathook-builder:ubuntu24.04}"
WORKSPACE="/workspace"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKERFILE_PATH="${REPO_ROOT}/docker/builder.Dockerfile"

# shellcheck source=/dev/null
source "${REPO_ROOT}/cathook_mode.sh"

selected_mode="$(cathook_select_mode 1)"

if [ -z "${CATHOOK_DOCKER_IMAGE:-}" ]; then
    if [ "${CATHOOK_DOCKER_REBUILD:-0}" = "1" ] || ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
        docker build -t "${IMAGE}" -f "${DOCKERFILE_PATH}" "${REPO_ROOT}"
    fi
fi

docker run --rm \
    -e CAT_BUILD_MODE="${selected_mode}" \
    -e CATHOOK_TEXTMODE="${CATHOOK_TEXTMODE:-${TEXTMODE:-0}}" \
    -v "${REPO_ROOT}:${WORKSPACE}" \
    -w "${WORKSPACE}" \
    "${IMAGE}" \
    bash -lc '
        set -euo pipefail
        chmod +x build.sh
        if [ "${CATHOOK_DOCKER_INSTALL_PACKAGES:-0}" = "1" ]; then
            chmod +x packages/packages.sh
            ./packages/packages.sh
        fi
        ./build.sh
    '
