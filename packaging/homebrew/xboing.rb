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
# HEAD-only until the first vX.Y release is tagged: install with
#   brew install --HEAD ./packaging/homebrew/xboing.rb
# There is intentionally no `url`/`sha256` yet — a placeholder tarball + a
# zero checksum would make the stable `brew install` path fail every time.
# When a release exists, add `url` + `sha256` for the tarball (`brew create
# <tarball-url>` computes the checksum) to enable the stable install path.
class Xboing < Formula
  desc "Classic breakout-style arcade game (1993, modernized for SDL2)"
  homepage "https://github.com/jmf-pobox/xboing-c"
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
    args = std_cmake_args
    # Pin the compiled-in shared-score path to the exact directory
    # post_install provisions, so the binary and the seeded store cannot
    # drift.  macOS only — on Linux the binary must keep its FHS default
    # (/var/games), which this formula does not provision.
    args << "-DXBOING_GLOBAL_SCORE_DIR=#{SHARED_DIR}" if OS.mac?
    system "cmake", "-S", ".", "-B", "build", *args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  # Provision the shared high-score state.  The game will not create the
  # global scores file itself, so a fresh install must seed it or the
  # first player to rank #1 silently loses their score (and the table
  # renders empty on next launch).  Equivalent to debian/xboing.postinst.
  #
  # /Users/Shared is world-writable and sticky, so a local user can race
  # or pre-plant filesystem state at any of these paths to hijack or block
  # the shared store.  Every operation therefore works on a NOFOLLOW file
  # descriptor (never a re-resolved pathname), verifies ownership, and
  # fails loudly (odie) on anything unexpected — a silent skip would let
  # `brew install` succeed while leaving global scores broken.
  def post_install
    # /Users/Shared is macOS-only.  On Homebrew-on-Linux the binary
    # resolves the FHS /var/games path (XBOING_GLOBAL_SCORE_DIR is not
    # APPLE there), which an unprivileged brew install cannot provision —
    # that is the apt package's job.  Skip rather than create a bogus
    # /Users/Shared tree, and tell the user where global scores live.
    unless OS.mac?
      opoo "xboing: shared high scores use /var/games on Linux; install the " \
           "system package to provision it. Skipping macOS shared-store setup."
      return
    end

    provision_shared_dir
    # Only scores.dat must be pre-seeded: write_table_inplace opens it
    # without O_CREAT (ADR-041), so a missing file makes the first global
    # write fail.  The lock file is NOT pre-seeded — the world-writable
    # (1777) shared dir lets the game create it at runtime via O_CREAT, so
    # pre-creating a world-writable lock here would be needless exposure.
    seed_shared_file("#{SHARED_DIR}/scores.dat", SCORES_SEED)
  end

  # Create (if absent) and lock down the shared directory.  All checks and
  # the chmod run against a NOFOLLOW descriptor, so a swap of the leaf for
  # a symlink between check and chmod cannot redirect the chmod onto
  # another inode.  The directory must be a real directory owned by the
  # installing user; a symlink leaf raises ELOOP and is refused.
  def provision_shared_dir
    present = File.symlink?(SHARED_DIR) || File.exist?(SHARED_DIR)
    mkdir_p(SHARED_DIR) unless present
    File.open(SHARED_DIR, File::RDONLY | File::NOFOLLOW) do |d|
      st = d.stat
      if !st.directory? || st.uid != Process.euid
        odie "#{SHARED_DIR} is not a directory owned by this install; remove it and reinstall."
      end
      # Sticky (1777): every user can create/update the shared file, but
      # only the owner of an entry can delete it.  No games group on macOS.
      d.chmod(01777)
    end
  rescue Errno::ELOOP
    odie "#{SHARED_DIR} is a symlink; remove it and reinstall."
  end

  # Seed one shared file.  Exclusive, no-follow creation means a race in
  # the world-writable dir cannot redirect the write through a planted
  # symlink or into another user's file; the mode is set via the
  # descriptor (fchmod), never a re-resolved path.
  #
  # An already-present path (normal on reinstall) must be a plain,
  # UNSHARED, install-owned regular file: a symlink raises ELOOP and is
  # refused; lstat then rejects non-regular files, extra hard links
  # (nlink != 1, which would alias an unrelated inode), and any file a
  # local user pre-created (uid != euid).  The existing file keeps its
  # mode; re-chmod here would reintroduce a path-based TOCTOU.
  def seed_shared_file(path, contents)
    File.open(path, File::WRONLY | File::CREAT | File::EXCL | File::NOFOLLOW, 0666) do |f|
      f.write(contents)
      f.chmod(0666)
    end
  rescue Errno::ELOOP
    odie "#{path} is a symlink; remove it and reinstall."
  rescue Errno::EEXIST
    st = File.lstat(path)
    return if st.file? && st.nlink == 1 && st.uid == Process.euid

    odie "#{path} is not a plain, unshared, install-owned regular file; remove it and reinstall."
  end

  test do
    assert_path_exists bin/"xboing"
  end
end
