# Convenience Makefile that wraps cmake + ctest + packaging.
#
# This is NOT the legacy 1996 Xlib build (preserved verbatim in original/).
# It is a thin driver around the active CMake build defined in CMakeLists.txt
# and CMakePresets.json so that 'make build' / 'make test' / 'make run' work
# without having to remember the cmake invocations.
#
# Run 'make help' for the full target list.

# --- Configuration ---------------------------------------------------------

BUILD_DIR      ?= build
ASAN_BUILD_DIR ?= build-asan
JOBS           ?= $(shell nproc 2>/dev/null || echo 4)
PREFIX         ?= /usr/local

# Run quietly under cmake's own progress reporting.
.SILENT:

# Phony targets (no on-disk file maps to these names).
.PHONY: help all build configure rebuild test run \
        asan asan-build asan-test \
        clean distclean \
        install uninstall deb deb-lint \
        lint format format-check \
        cppcheck cppcheck-src cppcheck-tests \
        tidy check ci \
        docs

# --- Default ---------------------------------------------------------------

# 'make' with no args = 'make help' (safer than building accidentally).
help: ## Show this help.
	echo "XBoing convenience Makefile (wraps cmake + ctest)."
	echo
	echo "Targets:"
	awk 'BEGIN {FS = ":[^#]*## "} \
	     /^[a-zA-Z_-]+:[^#]*## / { printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2 }' \
	     $(MAKEFILE_LIST)
	echo
	echo "Variables (override on command line, e.g. 'make build JOBS=2'):"
	echo "  BUILD_DIR      = $(BUILD_DIR)"
	echo "  ASAN_BUILD_DIR = $(ASAN_BUILD_DIR)"
	echo "  JOBS           = $(JOBS)"
	echo "  PREFIX         = $(PREFIX) (used by 'make install')"

# --- Debug build (default flow) --------------------------------------------

all: build ## Alias for 'build'.

configure: $(BUILD_DIR)/CMakeCache.txt ## Configure (debug preset) if not done.

$(BUILD_DIR)/CMakeCache.txt:
	# -B overrides the preset's hardcoded binaryDir so BUILD_DIR is honored.
	cmake --preset debug -B $(BUILD_DIR)

build: configure ## Build the game and all tests (debug).
	cmake --build $(BUILD_DIR) -j$(JOBS)

rebuild: ## Wipe build/ and build from scratch.
	rm -rf $(BUILD_DIR)
	$(MAKE) build

test: build ## Run the full ctest suite (debug build).
	ctest --test-dir $(BUILD_DIR) --output-on-failure

run: build ## Build and run the game.
	./$(BUILD_DIR)/xboing

# --- Sanitizer build (ASan + UBSan) ----------------------------------------

asan: asan-build ## Configure + build the sanitizer preset.

$(ASAN_BUILD_DIR)/CMakeCache.txt:
	# -B overrides the preset's hardcoded binaryDir so ASAN_BUILD_DIR is honored.
	cmake --preset asan -B $(ASAN_BUILD_DIR)

asan-build: $(ASAN_BUILD_DIR)/CMakeCache.txt
	cmake --build $(ASAN_BUILD_DIR) -j$(JOBS)

asan-test: asan-build ## Run ctest under ASan + UBSan.
	ctest --test-dir $(ASAN_BUILD_DIR) --output-on-failure

# --- Install / packaging ---------------------------------------------------

install: build ## Install to PREFIX (default /usr/local; override with PREFIX=).
	cmake --install $(BUILD_DIR) --prefix $(PREFIX)

uninstall: ## Best-effort uninstall using install_manifest.txt.
	if [ -f $(BUILD_DIR)/install_manifest.txt ]; then \
	  xargs rm -fv < $(BUILD_DIR)/install_manifest.txt ; \
	else \
	  echo "no install_manifest.txt; nothing to uninstall" ; \
	fi

deb: ## Build a Debian package via dpkg-buildpackage (.deb lands in ../).
	dpkg-buildpackage -us -uc -b
	echo
	echo "Built: $$(ls -1 ../xboing_*.deb 2>/dev/null | tail -1)"

deb-lint: deb ## Build .deb + run lintian on it (Debian Policy compliance).
	lintian ../xboing_*.deb
	echo "lintian: clean"

# --- Cleanup ---------------------------------------------------------------

clean: ## Remove the debug build dir.
	rm -rf $(BUILD_DIR)

distclean: ## Remove all build artifacts (debug, asan, debian, in-source pollution).
	rm -rf $(BUILD_DIR) $(ASAN_BUILD_DIR) build-install build-coverage
	rm -rf obj-*/ debian/.debhelper debian/files debian/*.substvars \
	       debian/*.log debian/debhelper-build-stamp \
	       debian/xboing debian/xboing-dbgsym
	rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake

# --- Quality gates (mirror CI exactly) -------------------------------------

lint: ## Lint markdown files (markdownlint-cli2; mirrors docs.yml).
	markdownlint-cli2

format: ## Apply clang-format in-place to src/*.c and include/*.h.
	clang-format -i src/*.c include/*.h

format-check: ## Check formatting without modifying files (mirrors lint.yml clang-format job).
	clang-format --dry-run --Werror src/*.c include/*.h

cppcheck-src: ## Static analysis on src/ (mirrors lint.yml cppcheck (src) step).
	cppcheck \
	  --enable=warning,style,performance,portability \
	  --inline-suppr \
	  --error-exitcode=1 \
	  -I include/ \
	  src/

cppcheck-tests: ## Static analysis on tests/ (mirrors lint.yml cppcheck (tests) step).
	cppcheck \
	  --enable=warning,style,performance,portability \
	  --inline-suppr \
	  --suppress=missingIncludeSystem \
	  --error-exitcode=1 \
	  -I include/ \
	  tests/

cppcheck: cppcheck-src cppcheck-tests ## Run both cppcheck passes.

tidy: build ## Run clang-tidy across src/ (uses build/compile_commands.json).
	find src -name '*.c' -exec clang-tidy -p $(BUILD_DIR) {} +

# --- One-shot ---------------------------------------------------------------

check: ## Run every CI gate locally (format + cppcheck + lint + debug build/test + asan build/test + .deb lintian).  Use before pushing.
	$(MAKE) format-check
	$(MAKE) cppcheck
	$(MAKE) lint
	$(MAKE) test
	$(MAKE) asan-test
	$(MAKE) deb-lint
	echo
	echo "All checks passed."

ci: check ## Alias for 'check'.
