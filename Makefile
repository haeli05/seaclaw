CC      ?= gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -O2 -I deps/cjson -I src
LDFLAGS = -lm

# Source files
SRCS = src/main.c src/config.c src/workspace.c src/log.c src/arena.c \
       src/http.c src/provider.c src/provider_openai.c \
       src/tools.c src/tool_shell.c src/tool_file.c \
       src/session.c src/telegram.c \
       src/memory.c src/ws.c src/cron.c \
       deps/cjson/cJSON.c

OBJS = $(SRCS:.c=.o)
BIN  = cclaw

# TLS backend (mbedtls by default)
CFLAGS  += -I deps/mbedtls/include
LDFLAGS += -L deps/mbedtls/library -lmbedtls -lmbedx509 -lmbedcrypto -lpthread -lsqlite3

# Static build (Linux + musl)
.PHONY: static
static: CFLAGS += -static
static: CC = musl-gcc
static: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean test
clean:
	rm -f $(OBJS) $(BIN)

test: $(BIN)
	cd tests && sh run_tests.sh
