# Implant build image. The binary records the glibc symbol versions it uses, so
# the base image sets the floor a victim must meet. Bookworm (glibc 2.36) and
# bullseye (glibc 2.31) both build cleanly; pick the oldest your victims can be.
#
#   docker build -f implant.Dockerfile --build-arg BASE_IMAGE=debian:bullseye .
ARG BASE_IMAGE=debian:bookworm
FROM ${BASE_IMAGE}

# Without this, packages that ask debconf questions (tzdata on Ubuntu bases)
# block the build waiting for input that never arrives.
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libssl-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
