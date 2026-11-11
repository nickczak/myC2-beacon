#!/usr/bin/env bash
# Build the implant against an older glibc than the host ships, so the binary
# runs on targets that are not this exact Fedora build.
#
#   ./scripts/build-implant.sh            -> build-implant/agent
#   OUT_DIR=out/debian ./scripts/build-implant.sh
#   CMAKE_ARGS="-DCMAKE_BUILD_TYPE=MinSizeRel" ./scripts/build-implant.sh
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${IMAGE:-myc2-implant-build}"
out_dir="${OUT_DIR:-build-implant}"

docker build \
    -f "$repo/docker/implant.Dockerfile" \
    --build-arg "BASE_IMAGE=${BASE_IMAGE:-debian:bookworm}" \
    -t "$image" "$repo/docker" >/dev/null

# Run as the calling user so the build tree is not left owned by root.
# label=disable is required on SELinux hosts such as Fedora: a plain bind mount
# is labelled for the host and the container hits "Permission denied" on /src.
# It relaxes the container's own confinement instead of relabelling your files,
# which is what appending ':z' to the mount would do.
docker run --rm \
    --security-opt label=disable \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "$repo:/src" \
    -w /src \
    "$image" \
    bash -c "cmake -S . -B '$out_dir' -DBUILD_SERVER=OFF ${CMAKE_ARGS:-} >/dev/null \
             && cmake --build '$out_dir' --target agent"

echo
echo "built: $repo/$out_dir/agent"
echo
echo "--- dependencies (target glibc is the image's: $(docker run --rm "$image" ldd --version | head -1)) ---"
ldd "$repo/$out_dir/agent"
