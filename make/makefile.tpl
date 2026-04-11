#!/usr/bin/make -f

ifndef MAKE_VERSION
$(error GNU make is required. Run: gmake <target>)
endif

.DEFAULT_GOAL := all

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
	@cp "$(TARGET)" "$(RUN_DIR)/bin/"
	@if [ -n "$(MODULE_TARGETS)" ]; then cp $(MODULE_TARGETS) "$(RUN_DIR)/modules/"; fi

$(MODULE_DIR)/%$(MODULE_SUFFIX): src/modules/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared $< -o $@ $(MODULE_SHARED_LDFLAGS)

$(OBJ_DIR)/local/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all prepare clean modules install
