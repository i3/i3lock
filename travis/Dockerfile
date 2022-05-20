# vim:ft=Dockerfile
FROM debian:sid

RUN echo force-unsafe-io > /etc/dpkg/dpkg.cfg.d/docker-apt-speedup
# Paper over occasional network flakiness of some mirrors.
RUN echo 'APT::Acquire::Retries "5";' > /etc/apt/apt.conf.d/80retry

# NOTE: I tried exclusively using gce_debian_mirror.storage.googleapis.com
# instead of httpredir.debian.org, but the results (Fetched 123 MB in 36s (3357
# kB/s)) are not any better than httpredir.debian.org (Fetched 123 MB in 34s
# (3608 kB/s)). Hence, let’s stick with httpredir.debian.org (default) for now.

# Install mk-build-deps (for installing the i3 build dependencies),
# clang and clang-format-9 (for checking formatting and building with clang),
# lintian (for checking spelling errors),
# test suite dependencies (for running tests)
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential clang git meson libxcb-randr0-dev pkg-config libpam0g-dev \
    libcairo2-dev libxcb1-dev libxcb-dpms0-dev libxcb-image0-dev libxcb-util0-dev \
    libxcb-xrm-dev libev-dev libxcb-xinerama0-dev libxcb-xkb-dev libxkbcommon-dev \
    libxkbcommon-x11-dev  && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src
