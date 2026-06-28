# typed: true
# frozen_string_literal: true

# Homebrew formula for XBoing.
#
# Mirrors debian/ packaging for the brew install path.  The load-bearing
# part is post_install: it provisions the shared cross-user high-score
# state that the game requires to exist (src/highscore_io.c's
# write_table_inplace opens the file without O_CREAT — ADR-041), exactly
# as debian/xboing.postinst does on apt systems.
#
# macOS has no setgid-games convention and Homebrew installs unprivileged,
# so the shared store is /Users/Shared/xboing (the system's cross-user
# location), sticky + world-writable rather than 2755 root:games.  See
# ADR-046.
#
# Release wiring (url/sha256) is finalized when the first vX.Y tag is
# pushed: `brew create https://github.com/jmf-pobox/xboing-c/archive/\
# refs/tags/v2.4.tar.gz` computes the sha256.  Until then this formula is
# installable from source via `brew install --HEAD ./packaging/homebrew/xboing.rb`.
class Xboing < Formula
  desc "Classic breakout-style arcade game (1993, modernized for SDL2)"
  homepage "https://github.com/jmf-pobox/xboing-c"
  url "https://github.com/jmf-pobox/xboing-c/archive/refs/tags/v2.4.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000" # set at release tag
  license "MIT"
  head "https://github.com/jmf-pobox/xboing-c.git", branch: "master"

  depends_on "cmake" => :build
  depends_on "pkg-config" => :build
  depends_on "sdl2"
  depends_on "sdl2_image"
  depends_on "sdl2_mixer"
  depends_on "sdl2_ttf"

  # Shared cross-user high-score directory.  macOS has no /var/games or
  # games group; /Users/Shared is the system's cross-user location.
  SHARED_DIR = "/Users/Shared/xboing"

  # Seed contents — matches highscore_io_init_table() and the JSON the
  # debian postinst writes.
  SCORES_SEED = <<~JSON
    {
      "version": 1,
      "master_name": "",
      "master_text": "Anyone play this game?",
      "entries": []
    }
  JSON

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  # Provision the shared high-score state.  The game will not create the
  # global scores file itself, so a fresh install must seed it or the
  # first player to rank #1 silently loses their score (and the table
  # renders empty on next launch).  Equivalent to debian/xboing.postinst.
  def post_install
    scores = "#{SHARED_DIR}/scores.dat"
    lock = "#{SHARED_DIR}/scores.dat.lock"

    # Refuse to follow a symlink at any leaf — defense in depth, since
    # /Users/Shared is world-writable and a local user could pre-plant one.
    return if File.symlink?(SHARED_DIR) || File.symlink?(scores) || File.symlink?(lock)

    mkdir_p(SHARED_DIR) unless File.directory?(SHARED_DIR)
    # Sticky (1777): every user can create/update the shared file, but
    # only the owner of an entry can delete it.  No games group on macOS.
    chmod(01777, SHARED_DIR)

    File.write(scores, SCORES_SEED) unless File.exist?(scores)
    chmod(0666, scores)

    # On macOS the world-writable dir lets the running process create the
    # lock at runtime, but pre-seeding it matches the apt deployment and
    # avoids a first-run permission surprise under a restrictive umask.
    File.write(lock, "") unless File.exist?(lock)
    chmod(0666, lock)
  end

  test do
    assert_path_exists bin/"xboing"
  end
end
