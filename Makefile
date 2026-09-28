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

# Files dpkg-buildpackage drops into the source tree.  Both `deb` (after a
# successful build) and `distclean` (unconditional wipe) remove this set —
# keep the list here so the two callers cannot drift.
DPKG_INTERMEDIATES := obj-*/ debian/.debhelper debian/files debian/*.substvars \
                      debian/*.log debian/debhelper-build-stamp \
                      debian/xboing debian/xboing-dbgsym

# Run quietly under cmake's own progress reporting.
.SILENT:

# Phony targets (no on-disk file maps to these names).
.PHONY: help all build configure rebuild test run \
        asan asan-build asan-test \
        clean distclean \
        install uninstall deb deb-lint dogfood bottle \
        lint format format-check docs-gen docs-pdf \
        cppcheck cppcheck-src cppcheck-tests \
        tidy check ci \
        audio-literals audio-literals-check \
        golden-screen golden-all golden-bonus golden-bonus-all \
        modern-screen modern-bonus modern-bonus-all bonus-fixtures \
        original-build capture-original visual-check visual-check-setup

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

run-original: ## Run the preserved 1996 Xlib binary (./original/xboing) with -usedefcmap so it works against a TrueColor X / XWayland server (the 1996 build crashes with BadMatch otherwise).
	./original/xboing -usedefcmap

coverage: ## Build with --coverage, run ctest, print line/function/branch summary via gcovr.
	cmake -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="--coverage -O0 -g" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
	cmake --build build-coverage -j$(JOBS)
	ctest --test-dir build-coverage --output-on-failure
	gcovr --root . --filter 'src/' --filter 'include/' --exclude 'tests/' --print-summary

coverage-html: ## coverage + browseable HTML report under build-coverage/html/.
	cmake -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="--coverage -O0 -g" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
	cmake --build build-coverage -j$(JOBS)
	ctest --test-dir build-coverage --output-on-failure
	mkdir -p build-coverage/html
	gcovr --root . --filter 'src/' --filter 'include/' --exclude 'tests/' --html-details build-coverage/html/index.html
	@echo "Open build-coverage/html/index.html"

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

# A Debian package has to be built against Debian's own libraries.  When
# Homebrew is installed it puts its bin directory at the front of PATH, so
# dpkg-buildpackage picks up Homebrew's cmake and pkg-config, finds
# Homebrew's SDL2 under /home/linuxbrew, and builds a package that only
# works on this machine.  Right now it doesn't even get that far: Homebrew's
# SDL2_mixer asks for fluidsynth, fluidsynth asks for libsystemd, and that
# isn't installed, so the configure step stops.  Take Homebrew out of PATH
# for the package build only -- everything else still uses it.
DEB_PATH := $(shell printf '%s' "$$PATH" | tr ':' '\n' \
              | grep -v -i -e linuxbrew -e homebrew | paste -s -d ':' -)
DEB_PATH := $(if $(DEB_PATH),$(DEB_PATH),/usr/local/bin:/usr/bin:/bin)

deb: ## Build a Debian package via dpkg-buildpackage (.deb lands in ../).
	PATH="$(DEB_PATH)" dpkg-buildpackage -us -uc -b
	echo
	echo "Built: $$(ls -1 ../xboing_*.deb 2>/dev/null | tail -1)"
	# Wipe dpkg-buildpackage intermediates now that the .deb is in ../.
	# Runs only on successful builds — make stops on the first failing
	# line, so a failed build leaves obj-*/ in place for debugging.
	rm -rf $(DPKG_INTERMEDIATES)

deb-lint: deb ## Build .deb + run lintian on it (Debian Policy compliance).
	lintian ../xboing_*.deb
	echo "lintian: clean"

# Requires Homebrew (brew.sh) on PATH — macOS or Linuxbrew. `brew bottle`
# refuses to bottle a --HEAD-only install (Error: Formula has no stable
# version) — a stable url+sha256 formula is mandatory, so this target
# archives the CURRENT working tree's HEAD commit into a local tarball,
# sha256s it, and uses packaging/homebrew/compose-formula.sh (the same
# script release.yml's `bottle`/`smoke-brew` jobs use) to compose a stable
# formula pinned to that local tarball via a file:// url — the same shape
# CI uses, just pointed at a local archive instead of a GitHub tag tarball.
# Uncommitted changes are NOT included (git archive only sees committed
# tree state) — commit locally first if you need to bottle-test a change.
# Then builds --build-bottle (now legal, since the formula has a stable
# url+sha256), uninstalls, pours the built *.bottle.tar.gz fresh, asserts
# the install receipt says poured_from_bottle=true, and runs
# packaging/homebrew/verify-bottle.sh against it — the same pour+verify
# pattern release.yml's `bottle` job uses — so this actually proves the
# bottle runs, not just that it built.
# Deliberately NOT part of `make check`: it needs brew, mutates a local tap
# under $(brew --repository), and is slow.
# Idempotent: removes any tap/keg left behind by a prior run before
# rebuilding, so it can be re-run without manual cleanup. The uninstall
# guard targets the BARE formula name (`xboing`), not the tap-qualified
# one — Homebrew's Cellar keys kegs by bare name, so a keg from an earlier
# run's tap incarnation survives `brew untap` and gets silently reused
# ("already installed, it's just not linked") instead of freshly rebuilt
# from this run's local tarball, unless the bare name is uninstalled too.
# HOMEBREW_DEVELOPER=1 is required for the final pour step: Homebrew
# refuses to install a formula from a bare/relative bottle-tarball path
# (Formulary::FromBottleLoader short-circuits to nil) unless
# HOMEBREW_DEVELOPER or HOMEBREW_TESTS is set — true by default on any
# machine that hasn't opted in, so this sets it explicitly rather than
# relying on the invoking shell's environment.
# `brew trust` (Homebrew's tap-trust gate, tap-trust.md) must be granted
# explicitly right after `tap-new` — a freshly created tap's first
# install is auto-trusted, but a SECOND install of the same tap (as this
# recipe's pour-verification step does, after uninstalling) is not, and
# fails "Refusing to load formula ... from untrusted tap" without this.
bottle: ## Build a Homebrew bottle from the local working tree's HEAD commit, pour it, and run verify-bottle.sh against it (requires brew; not part of 'make check'). See docs/RELEASING.md.
	if ! command -v brew >/dev/null 2>&1; then \
	    echo "FAIL: brew not found on PATH — see https://brew.sh"; \
	    exit 1; \
	fi
	brew uninstall --force xboing 2>/dev/null || true
	brew uninstall jmf-pobox/xboing-local-bottle/xboing 2>/dev/null || true
	brew untap jmf-pobox/xboing-local-bottle 2>/dev/null || true
	rm -f "$(CURDIR)/.tmp/xboing-local-bottle.tar.gz"
	mkdir -p "$(CURDIR)/.tmp"
	git archive --format=tar.gz --prefix=xboing-local-bottle/ \
	    -o "$(CURDIR)/.tmp/xboing-local-bottle.tar.gz" HEAD
	tarball_sha256="$$(shasum -a 256 "$(CURDIR)/.tmp/xboing-local-bottle.tar.gz" | awk '{print $$1}')"; \
	version="$$(sed -n 's/^project(xboing VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"; \
	brew tap-new --no-git jmf-pobox/xboing-local-bottle; \
	brew trust --taps jmf-pobox/xboing-local-bottle; \
	tap_path="$$(brew --repository jmf-pobox/xboing-local-bottle)"; \
	packaging/homebrew/compose-formula.sh \
	    packaging/homebrew/xboing.rb \
	    "file://$(CURDIR)/.tmp/xboing-local-bottle.tar.gz" \
	    "$$tarball_sha256" \
	    "$$tap_path/Formula/xboing.rb" \
	    "$$version"
	brew install --build-bottle jmf-pobox/xboing-local-bottle/xboing
	# Remove any stale bottle artifacts from a prior run BEFORE bottling, so
	# the glob below matches exactly the one this run produces — otherwise a
	# version bump (e.g. 1.0.9 -> 1.0.11) leaves an old `*.bottle.tar.gz`
	# that `ls | tail -1` could pick, or that makes the bare glob expand to
	# multiple paths and break the pour. (Copilot PR #230 review.)
	rm -f ./*.bottle.tar.gz ./*.bottle.json
	brew bottle --no-rebuild jmf-pobox/xboing-local-bottle/xboing
	echo
	echo "Built: $$(ls -1 ./*.bottle.tar.gz 2>/dev/null | tail -1)"
	echo
	echo "Pouring the built bottle to verify it actually runs (not just builds)..."
	brew uninstall jmf-pobox/xboing-local-bottle/xboing
	bottle_file="$$(ls -1 ./*.bottle.tar.gz | tail -1)"; \
	HOMEBREW_DEVELOPER=1 brew install "$$bottle_file"
	poured="$$(brew info --json=v2 jmf-pobox/xboing-local-bottle/xboing | jq -r '.formulae[0].installed[0].poured_from_bottle')"; \
	if [ "$$poured" != "true" ]; then \
	    echo "FAIL: install receipt says poured_from_bottle=$$poured after installing a local bottle tarball" >&2; \
	    exit 1; \
	fi
	version="$$(sed -n 's/^project(xboing VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"; \
	packaging/homebrew/verify-bottle.sh "$$version" xboing
	rm -f "$(CURDIR)/.tmp/xboing-local-bottle.tar.gz"

original-build: ## Build the legacy 1996 Xlib binary in original/ (used for visual-fidelity reference capture).
	$(MAKE) -C original

capture-original: original-build ## Capture visual-fidelity reference PNGs from original/xboing under Xvfb (one-time; commits to tests/golden/original/).
	scripts/capture_original.sh tests/golden/original/

visual-check-setup: ## Set up the managed venv and Python deps for `make visual-check`.
	@if [ ! -x ~/.local/bin/uv ]; then \
		echo "ERROR: uv not installed. Run: curl -LsSf https://astral.sh/uv/install.sh | sh"; \
		exit 1; \
	fi
	~/.local/bin/uv venv .tmp/venv
	~/.local/bin/uv pip install --python .tmp/venv/bin/python anthropic pyyaml

visual-check: build bonus-fixtures ## LLM-based visual-fidelity comparison (modern vs. tests/golden/original/). Reads ANTHROPIC_API_KEY from env or `secret-tool lookup service anthropic`. Run `make visual-check-setup` once to install deps.
	@if [ -n "$$DISPLAY" ]; then \
		for screen in presents intro instruct demo keys keysedit preview highscore editor; do \
			if ! [ -d .tmp/visual-check/modern/$$screen ]; then \
				echo "Capturing modern $$screen screenshots..."; \
				BUILD_DIR=$(BUILD_DIR) scripts/visual_capture.sh modern "$$screen:200" .tmp/visual-check/modern/; \
			fi; \
		done; \
		for n in 1 2 3 4; do \
			if ! [ -d .tmp/visual-check/modern/bonus-$$n ]; then \
				echo "Capturing modern bonus scenario $$n screenshots..."; \
				BUILD_DIR=$(BUILD_DIR) BONUS_SCENARIO="$$n" scripts/visual_capture.sh modern "bonus:2400" ".tmp/visual-check/modern/bonus-$$n/"; \
			fi; \
		done; \
	fi
	@echo "Running LLM comparison..."
	.tmp/venv/bin/python scripts/visual_check.py

# --- Visual-capture targets (state-driven screenshot capture) ----------------

golden-screen: original-build ## Capture original goldens for one screen. Usage: make golden-screen SCREEN=intro INTERVAL=200
	scripts/visual_capture.sh original "$(SCREEN):$(or $(INTERVAL),200)" tests/golden/original/

golden-all: original-build ## Capture original goldens for all attract screens (one-time).
	scripts/visual_capture.sh original "all:$(or $(INTERVAL),200)" tests/golden/original/

golden-bonus: original-build ## Capture original goldens for one bonus scenario. Usage: make golden-bonus SCENARIO=1 [INTERVAL=2400]
	BONUS_SCENARIO="$(or $(SCENARIO),1)" scripts/visual_capture.sh original "bonus:$(or $(INTERVAL),2400)" "tests/golden/original/bonus-$(or $(SCENARIO),1)/"

golden-bonus-all: original-build ## Capture original goldens for all 4 bonus scenarios.
	for n in 1 2 3 4; do \
	    BONUS_SCENARIO="$$n" scripts/visual_capture.sh original "bonus:$(or $(INTERVAL),2400)" "tests/golden/original/bonus-$$n/"; \
	done

modern-screen: build ## Capture modern screenshots for one screen. Usage: make modern-screen SCREEN=intro INTERVAL=200
	scripts/visual_capture.sh modern "$(SCREEN):$(or $(INTERVAL),200)" .tmp/visual-check/modern/

bonus-fixtures: build ## Generate savegame v2 fixtures for bonus-screen modern capture (4 scenarios).
	$(BUILD_DIR)/gen_bonus_fixtures tests/fixtures/bonus/

modern-bonus: build bonus-fixtures ## Capture modern bonus screenshots for one scenario. Usage: make modern-bonus SCENARIO=1 [INTERVAL=2400]
	BUILD_DIR=$(BUILD_DIR) BONUS_SCENARIO="$(or $(SCENARIO),1)" scripts/visual_capture.sh modern "bonus:$(or $(INTERVAL),2400)" ".tmp/visual-check/modern/bonus-$(or $(SCENARIO),1)/"

modern-bonus-all: build bonus-fixtures ## Capture modern bonus screenshots for all 4 scenarios.
	for n in 1 2 3 4; do \
	    BUILD_DIR=$(BUILD_DIR) BONUS_SCENARIO="$$n" scripts/visual_capture.sh modern "bonus:$(or $(INTERVAL),2400)" ".tmp/visual-check/modern/bonus-$$n/"; \
	done

dogfood: deb ## Install .deb, launch xboing from .tmp/, verify window opens (requires sudo; skips window check if headless or xwininfo missing).
	mkdir -p .tmp
	rm -f .tmp/xboing_*.deb .tmp/dogfood.deb
	ver="$$(dpkg-parsechangelog -S Version)"; \
	arch="$$(dpkg --print-architecture)"; \
	expected="../xboing_$${ver}_$${arch}.deb"; \
	if [ ! -f "$$expected" ]; then \
	    echo "FAIL: expected package $$expected not found (did make deb succeed?)"; \
	    exit 1; \
	fi; \
	cp "$$expected" .tmp/dogfood.deb; \
	sudo dpkg -i .tmp/dogfood.deb
	( cd .tmp && exec /usr/games/xboing ) & echo $$! > .tmp/dogfood.pid
	sleep 3
	if [ -n "$$DISPLAY" ] || [ -n "$$WAYLAND_DISPLAY" ]; then \
	    if command -v xwininfo >/dev/null 2>&1; then \
	        xwininfo -name "- XBoing II -" > .tmp/dogfood-window.txt 2>&1 || { \
	            echo "FAIL: xboing window not detected"; \
	            kill "$$(cat .tmp/dogfood.pid)" 2>/dev/null || true; \
	            rm -f .tmp/dogfood.pid; \
	            exit 1; \
	        }; \
	        echo "PASS: xboing launched from .tmp/, window detected"; \
	    else \
	        if kill -0 "$$(cat .tmp/dogfood.pid)" 2>/dev/null; then \
	            echo "INFO: display detected but xwininfo unavailable; window check skipped"; \
	            echo "PASS: xboing started from .tmp/ without immediate crash"; \
	        else \
	            echo "FAIL: xboing exited before window-detection grace period"; \
	            rm -f .tmp/dogfood.pid; \
	            exit 1; \
	        fi; \
	    fi; \
	else \
	    if kill -0 "$$(cat .tmp/dogfood.pid)" 2>/dev/null; then \
	        echo "INFO: no display detected ($$DISPLAY/$$WAYLAND_DISPLAY unset); window check skipped"; \
	        echo "PASS: xboing started from .tmp/ without immediate crash"; \
	    else \
	        echo "FAIL: xboing exited before window-detection grace period"; \
	        rm -f .tmp/dogfood.pid; \
	        exit 1; \
	    fi; \
	fi
	kill "$$(cat .tmp/dogfood.pid)" 2>/dev/null || true
	rm -f .tmp/dogfood.pid

# --- Cleanup ---------------------------------------------------------------

clean: ## Remove the debug build dir.
	rm -rf $(BUILD_DIR)

distclean: ## Remove all build artifacts (debug, asan, debian, in-source pollution).
	rm -rf $(BUILD_DIR) $(ASAN_BUILD_DIR) build-install build-coverage
	rm -rf $(DPKG_INTERMEDIATES)
	rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake

# --- Quality gates (mirror CI exactly) -------------------------------------

# npx fallback pinned to match docs.yml's DavidAnson/markdownlint-cli2-action
# @05f32210e8... (v19.1.0), which bundles markdownlint-cli2 0.17.2. Bump this
# in lockstep whenever that action pin moves.
lint: ## Lint markdown files (markdownlint-cli2; mirrors docs.yml).
	if command -v markdownlint-cli2 >/dev/null 2>&1; then \
	    markdownlint-cli2; \
	else \
	    npx --yes markdownlint-cli2@0.17.2; \
	fi

format: ## Apply clang-format in-place to src/*.c and include/*.h.
	clang-format -i src/*.c include/*.h

format-check: ## Check formatting without modifying files (mirrors lint.yml clang-format job).
	clang-format --dry-run --Werror src/*.c include/*.h

docs-gen: ## Regenerate derived LaTeX (docs/adr_table.tex from DESIGN.md, docs/metrics.tex from metrics.json; needs jq).
	scripts/gen_adr_table.sh
	scripts/gen_metrics_tex.sh

docs-pdf: docs-gen ## Regenerate derived tables then compile the three report PDFs (needs jq, pdflatex + bibtex).
	cd docs && pdflatex -interaction=nonstopmode -halt-on-error MODERNIZATION_CASE_STUDY.tex >/dev/null && bibtex MODERNIZATION_CASE_STUDY >/dev/null && pdflatex -interaction=nonstopmode -halt-on-error MODERNIZATION_CASE_STUDY.tex >/dev/null && pdflatex -interaction=nonstopmode -halt-on-error MODERNIZATION_CASE_STUDY.tex >/dev/null
	cd docs && pdflatex -interaction=nonstopmode -halt-on-error ARCHITECTURE_MODERN.tex >/dev/null && pdflatex -interaction=nonstopmode -halt-on-error ARCHITECTURE_MODERN.tex >/dev/null
	cd docs && pdflatex -interaction=nonstopmode -halt-on-error ARCHITECTURE_LEGACY.tex >/dev/null && pdflatex -interaction=nonstopmode -halt-on-error ARCHITECTURE_LEGACY.tex >/dev/null
	cd docs && rm -f *.aux *.log *.out *.toc *.fls *.fdb_latexmk *.bbl *.bcf *.blg *.run.xml
	@echo "Rebuilt docs/*.pdf. Review and commit the .tex, generated tables, and PDFs together."

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
	# `--extra-arg-before=-Wno-unknown-warning-option` so clang-tidy
	# tolerates GCC-only warning flags (-Wformat-overflow=2,
	# -Wformat-truncation, -Wnull-dereference variants) that CMake
	# wrote into the compile database for the gcc build.  Without
	# this, every translation unit fails on the first unknown flag.
	find src -name '*.c' -exec clang-tidy \
	  --extra-arg-before=-Wno-unknown-warning-option \
	  -p $(BUILD_DIR) {} +

# --- One-shot ---------------------------------------------------------------

audio-literals: ## Print sorted unique sound names passed to sdl2_audio_play() in src/.
	scripts/audio-literals.sh

audio-literals-check: ## Verify k_known_literals[] in tests matches source call sites.
	scripts/audio-literals-check.sh

check: ## Run every CI gate locally (format + cppcheck + lint + debug build/test + asan build/test + .deb lintian).  Use before pushing.
	$(MAKE) format-check
	$(MAKE) cppcheck
	$(MAKE) lint
	$(MAKE) audio-literals-check
	$(MAKE) test
	$(MAKE) asan-test
	$(MAKE) deb-lint
	echo
	echo "All checks passed."

ci: check ## Alias for 'check'.
