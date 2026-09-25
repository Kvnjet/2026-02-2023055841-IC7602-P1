CC ?= cc
PKG_CONFIG ?= pkg-config

CPPFLAGS += -Iinclude $(shell $(PKG_CONFIG) --cflags libcurl libcjson)
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
LDLIBS += $(shell $(PKG_CONFIG) --libs libcurl libcjson) -lm

BIN_DIR := bin
SOURCES := src/main.c src/config.c src/firebase_auth.c src/firebase_client.c \
	src/aggregation.c src/tcp_check.c src/http_check.c src/scheduler.c

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
CREATE_BIN_DIR = powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(BIN_DIR)' | Out-Null"
PLATFORM_LDLIBS := -lws2_32
CLEAN_BINARIES = powershell.exe -NoProfile -Command "@('$(TARGET)','$(BIN_DIR)/config-test$(EXEEXT)','$(BIN_DIR)/firebase-auth-test$(EXEEXT)','$(BIN_DIR)/firebase-client-test$(EXEEXT)','$(BIN_DIR)/health-check-test$(EXEEXT)','$(BIN_DIR)/scheduler-test$(EXEEXT)') | ForEach-Object { if (Test-Path -LiteralPath $$_) { Remove-Item -LiteralPath $$_ -Force } }; exit 0"
else
EXEEXT :=
CREATE_BIN_DIR = mkdir -p $(BIN_DIR)
PLATFORM_LDLIBS := -pthread
CLEAN_BINARIES = rm -f $(TARGET) $(BIN_DIR)/config-test$(EXEEXT) $(BIN_DIR)/firebase-auth-test$(EXEEXT) $(BIN_DIR)/firebase-client-test$(EXEEXT) $(BIN_DIR)/health-check-test$(EXEEXT) $(BIN_DIR)/scheduler-test$(EXEEXT)
endif

TARGET := $(BIN_DIR)/health-checker$(EXEEXT)

.PHONY: all clean test test-config test-health-check test-scheduler build-auth-test build-client-test

all: $(TARGET)

$(TARGET): $(SOURCES) | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@ $(LDLIBS) $(PLATFORM_LDLIBS)

$(BIN_DIR):
	$(CREATE_BIN_DIR)

test-config: | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/config.c tests/config_test.c -o $(BIN_DIR)/config-test$(EXEEXT) -lm
	$(BIN_DIR)/config-test$(EXEEXT)

test-health-check: | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/aggregation.c src/tcp_check.c src/http_check.c tests/health_check_test.c -o $(BIN_DIR)/health-check-test$(EXEEXT) $(LDLIBS) $(PLATFORM_LDLIBS)
	$(BIN_DIR)/health-check-test$(EXEEXT)

test-scheduler: | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/config.c src/firebase_auth.c src/firebase_client.c src/aggregation.c src/tcp_check.c src/http_check.c src/scheduler.c tests/scheduler_test.c -o $(BIN_DIR)/scheduler-test$(EXEEXT) $(LDLIBS) $(PLATFORM_LDLIBS)
	$(BIN_DIR)/scheduler-test$(EXEEXT)

test: test-config test-health-check test-scheduler

build-auth-test: | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/firebase_auth.c tests/firebase_auth_test.c -o $(BIN_DIR)/firebase-auth-test$(EXEEXT) $(LDLIBS)

build-client-test: | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/config.c src/firebase_auth.c src/firebase_client.c tests/firebase_client_test.c -o $(BIN_DIR)/firebase-client-test$(EXEEXT) $(LDLIBS)

clean:
	$(CLEAN_BINARIES)
