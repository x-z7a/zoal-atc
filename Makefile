SHELL := /bin/bash

# Nothing here is worth running in parallel at this level - cmake and npm
# parallelise inside their own steps, and the release targets read build outputs
# that the targets above them write.
.NOTPARALLEL:

ROOT_DIR := $(CURDIR)
BUILD_DIR ?= $(ROOT_DIR)/build
PLUGIN_BUILD_DIR ?= $(ROOT_DIR)/build-plugin
DIST_DIR ?= $(ROOT_DIR)/dist
RELEASE_DIR ?= $(DIST_DIR)/zoal-atc
BUILD_TYPE ?= Release
NPM ?= npm

# Skyscript is not optional: the in-sim panel is the ATC GUI. There is no
# opt-out, because a configuration nobody builds is a configuration nobody
# tests, and the plugin it produces has no GUI at all.
ZOAL_ATC_SKYSCRIPT_ROOT ?= $(ROOT_DIR)/.cache/skyscript/SkyScript-lib
LOCAL_XPLANE_PLUGIN_DIR ?= $(HOME)/X-Plane 12/Resources/plugins/zoal-atc

SDK_VERSION ?= 430
SDK_ZIP_URL ?= https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK$(SDK_VERSION).zip
ATC_XPLANE_SDK ?= $(ROOT_DIR)/sdk
SDK_SENTINEL := $(ATC_XPLANE_SDK)/XPLM/XPLMPlugin.h
MINIAUDIO_SENTINEL := $(ROOT_DIR)/vendor/miniaudio/miniaudio.h

PLUGIN_XPL := $(PLUGIN_BUILD_DIR)/zoal_atc.xpl

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(OS),Windows_NT)
	PLATFORM := windows
	PLUGIN_PLATFORM_DIR := win_x64
else ifeq ($(UNAME_S),Darwin)
	PLATFORM := macos
	PLUGIN_PLATFORM_DIR := mac_x64
else
	PLATFORM := linux
	PLUGIN_PLATFORM_DIR := lin_x64
endif

ifeq ($(UNAME_M),arm64)
	ARCH := arm64
else ifeq ($(UNAME_M),aarch64)
	ARCH := arm64
else
	ARCH := x64
endif

# On Windows the default CMake generator is Visual Studio (multi-config), which
# ignores -DCMAKE_BUILD_TYPE and places outputs in a <Config>/ subdirectory.
# Force MinGW Makefiles (a single-config generator) so the plugin lands at
# build-plugin/zoal_atc.xpl, matching the packaging step below.
ifeq ($(OS),Windows_NT)
CMAKE_GENERATOR := -G "MinGW Makefiles"
else
CMAKE_GENERATOR :=
endif

.PHONY: help build test plugin gui-build gui-test gate skyscript-lib \
	install-plugin release-plugin archive-plugin release-all clean

.DEFAULT_GOAL := help

help:
	@echo "zoal-atc - the X-Plane plugin"
	@echo
	@echo "  test            build and run the SDK-free C++ core tests"
	@echo "  plugin          build the .xpl (fetches the X-Plane SDK, miniaudio, Skyscript)"
	@echo "  gui-build       build the in-sim panel"
	@echo "  gui-test        run the in-sim panel's unit tests"
	@echo "  gate            the pre-done gate: core tests, panel build + tests"
	@echo "  install-plugin  build and install into X-Plane (LOCAL_XPLANE_PLUGIN_DIR=)"
	@echo "  release-plugin  assemble the release tree in dist/zoal-atc"
	@echo "  archive-plugin  zip release-plugin into dist/"
	@echo "  release-all     macOS + Windows + Linux merged into one tree (macOS host, needs Docker)"
	@echo "  clean           remove build outputs and dist/, keeping .cache/"

# --- build -------------------------------------------------------------------

# The SDK-free core. Builds and runs with no X-Plane SDK, no Skyscript and no
# CEF, which is what lets CI run it on three platforms in seconds.
build:
	cmake -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DZOAL_ATC_BUILD_TESTS=ON
	cmake --build "$(BUILD_DIR)"

test: build
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure

gui-build:
	cd gui/zoal-atc && $(NPM) ci && $(NPM) run build

# The panel's own unit tests: bridge, store, formatters and components, all
# against fakes. No X-Plane, no Skyscript host, no console.
gui-test:
	cd gui/zoal-atc && $(NPM) ci && $(NPM) test

# gate is the full pre-done gate for this repo. Run before considering work
# done; CI runs the same three targets.
gate: test gui-build gui-test
	@echo "gate: all checks passed"

skyscript-lib:
	ZOAL_ATC_SKYSCRIPT_ROOT="$(ZOAL_ATC_SKYSCRIPT_ROOT)" "$(ROOT_DIR)/scripts/ensure-skyscript-lib.sh"

# The .xpl. The two sentinel prerequisites fetch the X-Plane SDK and miniaudio
# once; skyscript-lib re-checks the pinned Skyscript on every build because the
# pin can move under a tree that already has one.
plugin: $(SDK_SENTINEL) $(MINIAUDIO_SENTINEL) skyscript-lib
	cmake -S . -B "$(PLUGIN_BUILD_DIR)" $(CMAKE_GENERATOR) -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DZOAL_ATC_BUILD_PLUGIN=ON -DZOAL_ATC_BUILD_TESTS=OFF -DATC_XPLANE_SDK="$(ATC_XPLANE_SDK)" -DZOAL_ATC_SKYSCRIPT_ROOT="$(ZOAL_ATC_SKYSCRIPT_ROOT)"
	cmake --build "$(PLUGIN_BUILD_DIR)" --target zoal_atc

$(SDK_SENTINEL):
	@echo "Downloading X-Plane SDK $(SDK_VERSION)..."
	@cmake -DATC_XPLANE_SDK="$(ATC_XPLANE_SDK)" -DSDK_VERSION="$(SDK_VERSION)" -DSDK_ZIP_URL="$(SDK_ZIP_URL)" -P cmake/SetupXPlaneSDK.cmake
	@echo "X-Plane SDK installed at $(ATC_XPLANE_SDK)"

$(MINIAUDIO_SENTINEL):
	@echo "Downloading miniaudio..."
	@cmake -P cmake/SetupMiniaudio.cmake
	@echo "miniaudio installed at $(MINIAUDIO_SENTINEL)"

# --- install & release -------------------------------------------------------
#
# One assembler for every release tree: scripts/package-release.sh, proved
# complete by scripts/verify-release-tree.sh. What you install locally is
# assembled and verified by the same two scripts that guard the shipped
# artifact, so what you fly is what ships.

install-plugin: plugin gui-build
	@plugins_dir="$$(dirname "$(LOCAL_XPLANE_PLUGIN_DIR)")"; \
	if [ ! -d "$$plugins_dir" ]; then \
		echo "no X-Plane plugins folder at $$plugins_dir" >&2; \
		echo "pass LOCAL_XPLANE_PLUGIN_DIR=/path/to/X-Plane/Resources/plugins/zoal-atc" >&2; \
		exit 1; \
	fi
	rm -rf "$(LOCAL_XPLANE_PLUGIN_DIR)"
	./scripts/package-release.sh "$(PLUGIN_PLATFORM_DIR)" "$(PLUGIN_XPL)" "$(LOCAL_XPLANE_PLUGIN_DIR)"
	./scripts/verify-release-tree.sh "$(LOCAL_XPLANE_PLUGIN_DIR)" "$(PLUGIN_PLATFORM_DIR)"
	@echo "installed into $(LOCAL_XPLANE_PLUGIN_DIR)"

release-plugin: plugin gui-build
	rm -rf "$(RELEASE_DIR)"
	./scripts/package-release.sh "$(PLUGIN_PLATFORM_DIR)" "$(PLUGIN_XPL)" "$(RELEASE_DIR)"
	./scripts/verify-release-tree.sh "$(RELEASE_DIR)" "$(PLUGIN_PLATFORM_DIR)"

# cmake -E tar is the archiver because cmake is already a hard dependency on all
# three platforms - Windows runners have no zip and macOS no zip -X. .DS_Store is
# deleted rather than filtered because the archiver has no exclude option.
archive-plugin: release-plugin
	find "$(RELEASE_DIR)" -name .DS_Store -delete
	cd "$(DIST_DIR)" && cmake -E tar cf "zoal-atc-$(PLATFORM)-$(ARCH).zip" --format=zip "zoal-atc"

release-all:
	DIST_DIR="$(DIST_DIR)" ./scripts/build-all-on-mac.sh

# .cache/ survives: it holds the pinned Skyscript library, which is a download
# rather than a build output.
clean:
	cmake -E rm -rf "$(BUILD_DIR)" "$(PLUGIN_BUILD_DIR)" "$(DIST_DIR)" "$(ROOT_DIR)/gui/zoal-atc/dist"
