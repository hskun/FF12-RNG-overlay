#!/bin/sh
set -eu

IMAGE="${DEVKITPRO_IMAGE:-devkitpro/devkita64}"

docker run --rm \
  -v "$(pwd):/project" \
  -w /project \
  -e DEVKITPRO=/opt/devkitpro \
  "$IMAGE" \
  make
