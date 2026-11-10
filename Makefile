SHELL := /bin/bash

# Nothing here is worth running in parallel at this level - cmake parallelises
# inside its own steps, and the release targets read build outputs that the
# targets above them write.
.NOTPARALLEL:

ROOT_DIR := $(CURDIR)
BUILD_DIR ?= $(ROOT_DIR)/build
PLUGIN_BUILD_DIR ?= $(ROOT_DIR)/build-plugin
PREVIEW_BUILD_DIR ?= $(ROOT_DIR)/build-preview
DIST_DIR ?= $(ROOT_DIR)/dist
RELEASE_DIR ?= $(DIST_DIR)/zoal-atc
BUILD_TYPE ?= Release
# whisper.cpp, for transcribing push-to-talk in the cockpit. Pinned with its
# archive checksum in scripts/whisper-version.txt.
WHISPER_VERSION := $(shell cut -d' ' -f1 "$(ROOT_DIR)/scripts/whisper-version.txt")
ZOAL_ATC_WHISPER_SOURCE ?= $(ROOT_DIR)/.cache/whisper.cpp-$(WHISPER_VERSION)
# sherpa-onnx, the runtime the controllers' voices are synthesised with, in
# the flavour this machine builds. Pinned with its archive and header checksums
# in scripts/sherpa-onnx-version.txt.
SHERPA_ONNX_VERSION := $(shell tr -d '\r' < "$(ROOT_DIR)/scripts/sherpa-onnx-version.txt" | awk '$$1 == "version" { print $$2 }')
# The two models every plugin ships with, inside its own folder. Pinned in
# scripts/ensure-models.sh and downloaded once.
ZOAL_ATC_MODELS ?= $(ROOT_DIR)/.cache/models
# Dear ImGui, which the panel and the HUD are drawn with. Pinned with its archive checksum in
# scripts/imgui-version.txt.
IMGUI_VERSION := $(shell cut -d' ' -f1 "$(ROOT_DIR)/scripts/imgui-version.txt")
ZOAL_ATC_IMGUI_SOURCE ?= $(ROOT_DIR)/.cache/imgui-$(IMGUI_VERSION)
# FreeType, which rasterizes the panel's and the HUD's text with hinting. Pinned with its
# archive checksum in scripts/freetype-version.txt.
FREETYPE_VERSION := $(shell cut -d' ' -f1 "$(ROOT_DIR)/scripts/freetype-version.txt")
ZOAL_ATC_FREETYPE_SOURCE ?= $(ROOT_DIR)/.cache/freetype-$(FREETYPE_VERSION)
# Inter, the typeface the panel and the HUD are set in. Pinned with its archive
# checksum in scripts/inter-version.txt.
INTER_VERSION := $(shell cut -d' ' -f1 "$(ROOT_DIR)/scripts/inter-version.txt")
ZOAL_ATC_INTER ?= $(ROOT_DIR)/.cache/inter-$(INTER_VERSION)
LOCAL_XPLANE_PLUGIN_DIR ?= $(HOME)/X-Plane 12/Resources/plugins/zoal-atc

# SDK 4.4.0 (X-Plane 12.4.4): the panel-graphics ImGui helpers the panel and
# the HUD are drawn with. The sentinel is a 4.4-only header, so a tree that
# still holds an older SDK fetches this one rather than building against what
# it has.
SDK_VERSION ?= 440-b5
SDK_ZIP_URL ?= https://www.x-plane.com/wp-content/uploads/2026/10/XPSDK440-b5.zip
ATC_XPLANE_SDK ?= $(ROOT_DIR)/sdk
SDK_SENTINEL := $(ATC_XPLANE_SDK)/XPLM/XPLMPanelGraphics.h
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

ifeq ($(PLATFORM),macos)
SHERPA_ONNX_FLAVOUR ?= osx-arm64-static-lib
else ifeq ($(PLATFORM),windows)
SHERPA_ONNX_FLAVOUR ?= win-x64-shared-MT-Release-lib
else
SHERPA_ONNX_FLAVOUR ?= linux-x64-shared-lib
endif
ZOAL_ATC_SHERPA_ONNX ?= $(ROOT_DIR)/.cache/sherpa-onnx-$(SHERPA_ONNX_VERSION)-$(SHERPA_ONNX_FLAVOUR)

# On Windows the default CMake generator is Visual Studio (multi-config), which
# ignores -DCMAKE_BUILD_TYPE and places outputs in a <Config>/ subdirectory.
# Force MinGW Makefiles (a single-config generator) so the plugin lands at
# build-plugin/zoal_atc.xpl, matching the packaging step below.
ifeq ($(OS),Windows_NT)
CMAKE_GENERATOR := -G "MinGW Makefiles"
else
CMAKE_GENERATOR :=
endif

.PHONY: help build test plugin preview gate whisper-src imgui-src freetype-src inter sherpa-onnx models \
	install-plugin release-plugin archive-plugin release-all clean

.DEFAULT_GOAL := help

help:
	@echo "zoal-atc - the X-Plane plugin"
	@echo
	@echo "  test            build and run the SDK-free C++ core tests"
	@echo "  plugin          build the .xpl (fetches the X-Plane SDK, miniaudio, ImGui, FreeType)"
	@echo "  preview         render the panel off-screen to PNGs in build-preview/ (no X-Plane)"
	@echo "  gate            the pre-done gate: the core tests"
	@echo "  install-plugin  build and install into X-Plane (LOCAL_XPLANE_PLUGIN_DIR=)"
	@echo "  models          fetch the transcription model and the controller voices"
	@echo "  release-plugin  assemble the release tree in dist/zoal-atc"
	@echo "  archive-plugin  zip release-plugin into dist/"
	@echo "  release-all     macOS + Windows + Linux merged into one tree (macOS host, needs Docker)"
	@echo "  clean           remove build outputs and dist/, keeping .cache/"

# --- build -------------------------------------------------------------------

# The SDK-free core. Builds and runs with no X-Plane SDK and no ImGui, which is
# what lets CI run it on three platforms in seconds.
# --config and -C are what make this work on Windows, where the default CMake
# generator is Visual Studio: a multi-config generator ignores CMAKE_BUILD_TYPE,
# builds Debug, and drops the executables in build/<Config>/. Without them cmake
# built Debug while ctest looked for Release, so every test reported "Not Run" -
# a green-looking build with zero tests executed. Single-config generators accept
# both flags and ignore them, so macOS and Linux are unchanged.
build:
	cmake -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DZOAL_ATC_BUILD_TESTS=ON
	cmake --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --parallel

test: build
	ctest --test-dir "$(BUILD_DIR)" -C "$(BUILD_TYPE)" --output-on-failure

# gate is the full pre-done gate for this repo. Run before considering work
# done; CI runs the same target.
gate: test
	@echo "gate: all checks passed"

# The .xpl. The two sentinel prerequisites fetch the X-Plane SDK and miniaudio
# once; the ensure scripts re-check their pins on every build, because a pin can
# move under a tree that already has one.
whisper-src:
	ZOAL_ATC_WHISPER_SOURCE="$(ZOAL_ATC_WHISPER_SOURCE)" "$(ROOT_DIR)/scripts/ensure-whisper-src.sh"

imgui-src:
	ZOAL_ATC_IMGUI_SOURCE="$(ZOAL_ATC_IMGUI_SOURCE)" "$(ROOT_DIR)/scripts/ensure-imgui.sh"

freetype-src:
	ZOAL_ATC_FREETYPE_SOURCE="$(ZOAL_ATC_FREETYPE_SOURCE)" "$(ROOT_DIR)/scripts/ensure-freetype.sh"

inter:
	ZOAL_ATC_INTER="$(ZOAL_ATC_INTER)" "$(ROOT_DIR)/scripts/ensure-inter.sh"

sherpa-onnx:
	ZOAL_ATC_SHERPA_ONNX="$(ZOAL_ATC_SHERPA_ONNX)" "$(ROOT_DIR)/scripts/ensure-sherpa-onnx.sh" $(SHERPA_ONNX_FLAVOUR)

# models fetches what ships inside the plugin folder: whisper's model, which
# hears the pilot, and Kokoro's voices, which are the controllers.
models:
	ZOAL_ATC_MODELS="$(ZOAL_ATC_MODELS)" "$(ROOT_DIR)/scripts/ensure-models.sh"

plugin: $(SDK_SENTINEL) $(MINIAUDIO_SENTINEL) whisper-src imgui-src freetype-src sherpa-onnx
	cmake -S . -B "$(PLUGIN_BUILD_DIR)" $(CMAKE_GENERATOR) -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DZOAL_ATC_BUILD_PLUGIN=ON -DZOAL_ATC_BUILD_TESTS=OFF -DATC_XPLANE_SDK="$(ATC_XPLANE_SDK)" -DZOAL_ATC_WHISPER_SOURCE="$(ZOAL_ATC_WHISPER_SOURCE)" -DZOAL_ATC_IMGUI_SOURCE="$(ZOAL_ATC_IMGUI_SOURCE)" -DZOAL_ATC_FREETYPE_SOURCE="$(ZOAL_ATC_FREETYPE_SOURCE)" -DZOAL_ATC_SHERPA_ONNX="$(ZOAL_ATC_SHERPA_ONNX)"
	cmake --build "$(PLUGIN_BUILD_DIR)" --target zoal_atc

# The panel rendered off-screen in every theme, Home and Settings, from the
# same code the plugin runs: a look at a change without starting the sim.
PREVIEW_THEMES := graphite daylight night
preview: imgui-src freetype-src inter
	cmake -S . -B "$(PREVIEW_BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DZOAL_ATC_BUILD_PREVIEW=ON -DZOAL_ATC_BUILD_TESTS=OFF -DZOAL_ATC_IMGUI_SOURCE="$(ZOAL_ATC_IMGUI_SOURCE)" -DZOAL_ATC_FREETYPE_SOURCE="$(ZOAL_ATC_FREETYPE_SOURCE)"
	cmake --build "$(PREVIEW_BUILD_DIR)" --target panel_preview --parallel
	@for theme in $(PREVIEW_THEMES); do for tab in home settings; do \
		"$(PREVIEW_BUILD_DIR)/panel_preview" "$(ZOAL_ATC_INTER)" "$(PREVIEW_BUILD_DIR)/$$tab-$$theme.ppm" $$tab $$theme 1120 740 2 || exit 1; \
		if command -v sips >/dev/null; then sips -s format png "$(PREVIEW_BUILD_DIR)/$$tab-$$theme.ppm" --out "$(PREVIEW_BUILD_DIR)/$$tab-$$theme.png" >/dev/null; fi; \
	done; done
	@echo "panel previews in $(PREVIEW_BUILD_DIR)"

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

install-plugin: plugin models
	@plugins_dir="$$(dirname "$(LOCAL_XPLANE_PLUGIN_DIR)")"; \
	if [ ! -d "$$plugins_dir" ]; then \
		echo "no X-Plane plugins folder at $$plugins_dir" >&2; \
		echo "pass LOCAL_XPLANE_PLUGIN_DIR=/path/to/X-Plane/Resources/plugins/zoal-atc" >&2; \
		exit 1; \
	fi
	rm -rf "$(LOCAL_XPLANE_PLUGIN_DIR)"
	ZOAL_ATC_MODELS="$(ZOAL_ATC_MODELS)" ./scripts/package-release.sh "$(PLUGIN_PLATFORM_DIR)" "$(PLUGIN_XPL)" "$(LOCAL_XPLANE_PLUGIN_DIR)"
	./scripts/verify-release-tree.sh "$(LOCAL_XPLANE_PLUGIN_DIR)" "$(PLUGIN_PLATFORM_DIR)"
	@echo "installed into $(LOCAL_XPLANE_PLUGIN_DIR)"

release-plugin: plugin models
	rm -rf "$(RELEASE_DIR)"
	ZOAL_ATC_MODELS="$(ZOAL_ATC_MODELS)" ./scripts/package-release.sh "$(PLUGIN_PLATFORM_DIR)" "$(PLUGIN_XPL)" "$(RELEASE_DIR)"
	./scripts/verify-release-tree.sh "$(RELEASE_DIR)" "$(PLUGIN_PLATFORM_DIR)"

# cmake -E tar is the archiver because cmake is already a hard dependency on all
# three platforms - Windows runners have no zip and macOS no zip -X. .DS_Store is
# deleted rather than filtered because the archiver has no exclude option.
# The .DS_Store sweep is macOS-only on purpose. Finder is the only thing that
# creates them, and on a Windows runner `find` is as likely to resolve to
# System32's unrelated find.exe as to the MSYS one - which would fail the
# release for a file that cannot exist there.
archive-plugin: release-plugin
ifeq ($(PLATFORM),macos)
	find "$(RELEASE_DIR)" -name .DS_Store -delete
endif
	cd "$(DIST_DIR)" && cmake -E tar cf "zoal-atc-$(PLATFORM)-$(ARCH).zip" --format=zip "zoal-atc"

release-all:
	DIST_DIR="$(DIST_DIR)" ./scripts/build-all-on-mac.sh

# .cache/ survives: it holds the pinned downloads, which are not build outputs.
clean:
	cmake -E rm -rf "$(BUILD_DIR)" "$(PLUGIN_BUILD_DIR)" "$(PREVIEW_BUILD_DIR)" "$(DIST_DIR)"
