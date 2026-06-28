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
  #
  # /Users/Shared is world-writable and sticky, so a local user can
  # pre-plant a symlink or a non-regular file at any of these paths to
  # hijack or block the shared store.  Every refusal is loud (odie) — a
  # silent skip would let `brew install` succeed while leaving global
  # scores broken with no install-time signal.  This mirrors the symlink
  # and not-a-regular-file refusals in debian/xboing.postinst.
  def post_install
    odie "#{SHARED_DIR} is a symlink; remove it and reinstall." if File.symlink?(SHARED_DIR)
    if File.exist?(SHARED_DIR) && !File.directory?(SHARED_DIR)
      odie "#{SHARED_DIR} exists but is not a directory; remove it and reinstall."
    end

    mkdir_p(SHARED_DIR) unless File.directory?(SHARED_DIR)
    # Sticky (1777): every user can create/update the shared file, but
    # only the owner of an entry can delete it.  No games group on macOS.
    chmod(01777, SHARED_DIR)

    seed_shared_file("#{SHARED_DIR}/scores.dat", SCORES_SEED)
    # The lock self-creates at runtime under the world-writable dir, but
    # pre-seeding matches the apt deployment and avoids a first-run
    # permission surprise under a restrictive umask.
    seed_shared_file("#{SHARED_DIR}/scores.dat.lock", "")
  end

  # Create a shared file with exclusive, no-follow semantics so a race in
  # the world-writable directory cannot redirect the write through a
  # planted symlink or into another user's file.  An already-present path
  # must be a plain regular file (odie otherwise).  The mode is forced to
  # world-writable AFTER creation because the open mode is masked by the
  # installing user's umask — every local user must be able to update the
  # shared table.
  def seed_shared_file(path, contents)
    File.open(path, File::WRONLY | File::CREAT | File::EXCL | File::NOFOLLOW, 0666) do |f|
      f.write(contents)
    end
    chmod(0666, path)
  rescue Errno::EEXIST
    if File.symlink?(path) || !File.file?(path)
      odie "#{path} exists but is not a regular file; remove it and reinstall."
    end
    chmod(0666, path)
  end

  test do
    assert_path_exists bin/"xboing"
  end
end
