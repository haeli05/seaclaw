# Building CClaw

## Prerequisites

- **C compiler**: GCC or Clang with C11 support
- **System libraries**: `libsqlite3-dev`, `libpthread`
- **Vendored dependencies** (fetched during build):
  - [cJSON](https://github.com/DaveGamble/cJSON) — JSON parsing/generation
  - [mbedTLS](https://github.com/Mbed-TLS/mbedtls) v2.28 — TLS 1.2/1.3

## Quick Build

```bash
# 1. Fetch dependencies
mkdir -p deps && cd deps

git clone --depth 1 https://github.com/DaveGamble/cJSON.git cjson
cp cjson/cJSON.c cjson/cJSON.h .

git clone --depth 1 --branch v2.28.8 https://github.com/Mbed-TLS/mbedtls.git
cd mbedtls && make lib && cd ../..

# 2. Build
make
```

This produces `./cclaw` (~709KB with optimizations).

## Manual Build

```bash
gcc -D_GNU_SOURCE -Wall -Wextra -std=c11 -O2 \
  -I deps -I deps/mbedtls/include -I src \
  src/*.c deps/cJSON.c \
  -L deps/mbedtls/library -lmbedtls -lmbedx509 -lmbedcrypto \
  -lpthread -lsqlite3 -lm \
  -o cclaw
```

## Static Build (Linux + musl)

For a fully static binary with no runtime dependencies:

```bash
# Requires musl-gcc (install: apt install musl-tools)
make static
```

This uses `musl-gcc` with `-static` flag. The resulting binary runs on any Linux system without shared libraries.

## Makefile Targets

| Target | Description |
|--------|-------------|
| `make` or `make cclaw` | Default dynamic build |
| `make static` | Static build with musl-gcc |
| `make clean` | Remove object files and binary |
| `make test` | Build and run tests from `tests/run_tests.sh` |

## Compiler Flags

The Makefile uses:

```makefile
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -O2 -I deps/cjson -I src
LDFLAGS = -lm -lmbedtls -lmbedx509 -lmbedcrypto -lpthread -lsqlite3
```

### Include Paths
- `src/` — CClaw headers
- `deps/cjson/` — cJSON header
- `deps/mbedtls/include/` — mbedTLS headers

### Link Libraries
- `mbedtls`, `mbedx509`, `mbedcrypto` — TLS (from `deps/mbedtls/library/`)
- `pthread` — Threading (cron, WebSocket)
- `sqlite3` — Memory database (system library)
- `m` — Math (cosine similarity in memory search)

## Cross-Compilation

### ARM64 (aarch64)

```bash
CC=aarch64-linux-gnu-gcc make
```

You'll need cross-compiled versions of mbedTLS and SQLite3 for the target architecture.

### Alpine / musl (for Docker)

```bash
# In an Alpine container:
apk add gcc musl-dev sqlite-dev make
# Build mbedTLS with musl
cd deps/mbedtls && CC=gcc make lib && cd ../..
make static
```

## Docker

```dockerfile
FROM alpine:3.19 AS builder
RUN apk add --no-cache gcc musl-dev make sqlite-dev git

WORKDIR /build
COPY . .

# Fetch and build dependencies
RUN mkdir -p deps && cd deps && \
    git clone --depth 1 https://github.com/DaveGamble/cJSON.git cjson && \
    cp cjson/cJSON.c cjson/cJSON.h . && \
    git clone --depth 1 --branch v2.28.8 https://github.com/Mbed-TLS/mbedtls.git && \
    cd mbedtls && make lib && cd ../..

RUN make static

FROM scratch
COPY --from=builder /build/cclaw /cclaw
COPY --from=builder /etc/ssl/certs /etc/ssl/certs
ENTRYPOINT ["/cclaw"]
```

Build and run:

```bash
docker build -t cclaw .
docker run -e ANTHROPIC_API_KEY=sk-ant-... cclaw "Hello"
```

The resulting image is ~3MB.

## Troubleshooting

### `fatal error: sqlite3.h: No such file or directory`
Install SQLite3 development headers:
```bash
# Debian/Ubuntu
apt install libsqlite3-dev
# Alpine
apk add sqlite-dev
# macOS
brew install sqlite3
```

### `cannot find -lmbedtls`
Build mbedTLS first:
```bash
cd deps/mbedtls && make lib
```

### `musl-gcc: command not found`
Install musl tools for static builds:
```bash
apt install musl-tools
```

### TLS certificate errors at runtime
CClaw loads CA certs from `/etc/ssl/certs`. Ensure this directory exists and contains root certificates:
```bash
# Debian/Ubuntu
apt install ca-certificates
# Alpine
apk add ca-certificates
```
