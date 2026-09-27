# typed: true
# frozen_string_literal: true

# Homebrew formula for XBoing (macOS / Linux brew).
#
# Build + install only.  The brew install provides the game and the
# per-user (personal) high-score table, which the game writes under the
# user's XDG data dir at runtime — no install-time provisioning needed.
#
# There is intentionally NO shared/global leaderboard on this channel:
# the cross-user "machine" board ships only via the Debian .deb (setgid
# games + /var/games, see ADR-041/ADR-047), and cross-machine standings
# are a future API leaderboard (ADR-048).  Homebrew sandboxes post_install
# and cannot provision shared state outside its prefix anyway.
#
# Distribution requires a Homebrew tap — modern Homebrew refuses to install
# a loose formula file. Until a tap repo and a tagged release exist, test
# via a local tap: `brew tap-new <user>/<tap>`, copy this file into its
# Formula/, then `brew install --HEAD <user>/<tap>/xboing`. When a release
# is tagged, add `url` + `sha256` (`brew create <tarball-url>` computes the
# checksum) to enable the stable `brew install` path.
class Xboing < Formula
  desc "Classic breakout-style arcade game (1993, modernized for SDL2)"
  homepage "https://github.com/jmf-pobox/xboing-c"
  license "MIT"
  head "https://github.com/jmf-pobox/xboing-c.git", branch: "master"

  # Placeholder scaffold — release CI (.github/workflows/release.yml's
  # `bottle` job) fills in the real per-platform sha256 values and root_url
  # at release time via `brew bottle --json`, then a human copies the
  # complete block from the Release notes into the tap repo
  # (jmf-pobox/homebrew-xboing). The zeroed shas here keep this in-repo
  # formula syntactically valid; they are never installed from directly —
  # only the tap's copy (with real shas) is. See docs/RELEASING.md.
  bottle do
    root_url "https://github.com/jmf-pobox/xboing-c/releases/download/PLACEHOLDER"
    # arm64_sonoma: the macos-14 CI runner (release.yml's `bottle` job) is
    # macOS 14 "Sonoma" — Homebrew's bottle tag names the OS codename, not
    # the runner label, so this must track whatever macOS version that
    # runner image resolves to.
    sha256 arm64_sonoma:  "0000000000000000000000000000000000000000000000000000000000000000"
    sha256 x86_64_linux:  "0000000000000000000000000000000000000000000000000000000000000000"
    sha256 aarch64_linux: "0000000000000000000000000000000000000000000000000000000000000000"
  end

  depends_on "cmake" => :build
  depends_on "pkg-config" => :build
  depends_on "sdl2"
  depends_on "sdl2_image"
  depends_on "sdl2_mixer"
  depends_on "sdl2_ttf"

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    assert_path_exists bin/"xboing"
  end
end
