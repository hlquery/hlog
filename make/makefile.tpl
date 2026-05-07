#!/usr/bin/make -f

ifndef MAKE_VERSION
$(error GNU make is required. Run: gmake <target>)
endif

.DEFAULT_GOAL := all

CONFIGURE_COMMAND ?= ./configure

ifeq ($(shell [ -t 1 ] && echo yes),yes)
  RED     = \033[0;31m
  YELLOW  = \033[0;33m
  GREEN   = \033[0;32m
  BLUE    = \033[1;34m
  CYAN    = \033[0;36m
  NC      = \033[0m
else
  RED     =
  YELLOW  =
  GREEN   =
  BLUE    =
  CYAN    =
  NC      =
endif

CXX ?= ${CXX}
CC ?= cc
BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj
TARGET := $(BIN_DIR)/hlog
RUN_DIR := run

ifeq ($(OS),Windows_NT)
  EXE_SUFFIX := .exe
  MODULE_SUFFIX := .dll
  FS_LIB :=
  THREAD_LIB :=
  SOCKET_LIBS := -lws2_32
  PIC_FLAG :=
  RDYNAMIC :=
  MODULE_SHARED_LDFLAGS :=
else
  EXE_SUFFIX :=
OS_NAME := $(shell uname -s 2>/dev/null || echo unknown)
ifneq ($(filter Darwin,$(OS_NAME)),)
  MODULE_SUFFIX := .dylib
  RDYNAMIC := -Wl,-export_dynamic
  MODULE_SHARED_LDFLAGS := -Wl,-undefined,dynamic_lookup
else
  MODULE_SUFFIX := .so
  RDYNAMIC := -rdynamic
  MODULE_SHARED_LDFLAGS :=
endif
FS_LIB := -lstdc++fs
ifneq ($(filter FreeBSD OpenBSD NetBSD DragonFly Darwin,$(OS_NAME)),)
  FS_LIB :=
endif
  THREAD_LIB := -pthread
  DL_LIB := -ldl
  SOCKET_LIBS :=
  PIC_FLAG := -fPIC
endif

ifeq ($(OS),Windows_NT)
  DL_LIB :=
endif

TARGET := $(BIN_DIR)/hlog$(EXE_SUFFIX)
MODULE_DIR := $(BUILD_DIR)/modules
INSTALL ?= install

CPPFLAGS := -Iinclude -Ivendor -I.
CXXFLAGS := ${CXXFLAGS} -std=c++20 -O2 $(PIC_FLAG) -Wall -Wextra -Wformat=2 -Wformat-security -Wno-unused-parameter
LDFLAGS := ${LDFLAGS} $(FS_LIB) $(THREAD_LIB) $(SOCKET_LIBS) $(DL_LIB) $(RDYNAMIC)

LOCAL_CPP_SRCS := $(sort \
	$(wildcard src/core/*.cpp) \
	$(wildcard src/common/*.cpp) \
	$(wildcard src/utils/*.cpp))

MODULE_CPP_SRCS := $(wildcard src/modules/*.cpp)
MODULE_TARGETS := $(patsubst src/modules/%.cpp,$(MODULE_DIR)/%$(MODULE_SUFFIX),$(MODULE_CPP_SRCS))

LOCAL_CPP_OBJS := $(patsubst src/%.cpp,$(OBJ_DIR)/local/%.o,$(LOCAL_CPP_SRCS))

OBJECTS := $(LOCAL_CPP_OBJS)

all: prepare $(TARGET) modules

configure:
	$(CONFIGURE_COMMAND)

prepare:
	@mkdir -p $(BIN_DIR) \
		$(OBJ_DIR)/local \
		$(OBJ_DIR)/local/core \
		$(OBJ_DIR)/local/common \
		$(OBJ_DIR)/local/utils \
		$(MODULE_DIR) \
		$(RUN_DIR)/bin \
		$(RUN_DIR)/conf \
		$(RUN_DIR)/data \
		$(RUN_DIR)/logs \
		$(RUN_DIR)/modules \
		$(RUN_DIR)/tests \
		$(RUN_DIR)/pid
	@test -f $(RUN_DIR)/tests/file.txt || printf 'hello world!\n' > $(RUN_DIR)/tests/file.txt

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

modules: prepare $(MODULE_TARGETS)

install: all
	@mkdir -p $(RUN_DIR)/bin $(RUN_DIR)/modules
	@echo ""
	@echo "Installing binaries to $(RUN_DIR)/bin/..."
	@if [ ! -f "$(TARGET)" ]; then \
		echo "$(RED) Error: Binary not found in $(BIN_DIR)/$(NC)"; \
		echo "$(YELLOW)   [TIP] Please run 'make' first to build hlog.$(NC)"; \
		echo ""; \
		exit 1; \
	fi
	@$(INSTALL) -m 0755 "$(TARGET)" "$(RUN_DIR)/bin/hlog$(EXE_SUFFIX)"
	@if [ -n "$(MODULE_TARGETS)" ]; then \
		echo "$(BLUE)   Modules already staged in $(RUN_DIR)/modules; refreshing copies.$(NC)"; \
		$(INSTALL) -m 0755 $(MODULE_TARGETS) "$(RUN_DIR)/modules/"; \
	fi
	@if [ -f "$(RUN_DIR)/hlog" ]; then \
		echo "$(BLUE)   Wrapper: $(RUN_DIR)/hlog$(NC)"; \
	fi
	@echo "$(GREEN) Installation complete!$(NC)"
	@echo "$(BLUE)   Binary:  $(RUN_DIR)/bin/hlog$(EXE_SUFFIX)$(NC)"
	@if [ -d "$(RUN_DIR)/modules" ]; then \
		echo "$(BLUE)   Modules: $(RUN_DIR)/modules$(NC)"; \
	fi
	@echo ""
	@echo "$(CYAN)Quick help:$(NC)"
	@echo "hlog - log pipeline"
	@echo ""
	@echo "To start hlog:"
	@echo "  Run in foreground: ./run/hlog start --nofork"
	@echo "  Run as daemon:     ./run/hlog start"
	@echo ""
	@echo "Useful commands:"
	@echo "  ./run/hlog status"
	@echo "  ./run/hlog stop"
	@echo "  ./run/hlog test"
	@echo ""
	@echo "Installed paths:"
	@echo "  Binary:  ./run/bin/hlog$(EXE_SUFFIX)"
	@echo "  Wrapper: ./run/hlog"
	@echo "  Modules: ./run/modules"
	@echo ""

$(MODULE_DIR)/%$(MODULE_SUFFIX): src/modules/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared $< -o $@ $(MODULE_SHARED_LDFLAGS)

$(OBJ_DIR)/local/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all configure prepare clean modules install
