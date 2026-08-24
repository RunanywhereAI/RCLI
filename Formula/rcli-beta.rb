class RcliBeta < Formula
  desc "Run language, speech and image models on your own machine (beta)"
  homepage "https://github.com/RunanywhereAI/RCLI"
  url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.4.0-beta.1/rcli-0.4.0-beta.1-Darwin-arm64.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "MIT"
  version "0.4.0-beta.1"

  depends_on :macos
  depends_on arch: :arm64

  # Installed as rcli-beta so it can sit alongside a stable rcli. Each formula
  # gets its own libexec, so the two Metal bundles do not collide.
  def install
    libexec.install "libexec/rcli", "libexec/mlx-swift_Cmlx.bundle"
    bin.install_symlink libexec/"rcli" => "rcli-beta"
  end

  def caveats
    <<~EOS
      This is a prerelease. It installs as rcli-beta and leaves a stable rcli
      alone, so both can be on the machine at once.

      Models are downloaded on demand and kept in
        ~/.local/share/runanywhere

      Getting started:
        rcli-beta list --all         every model in the catalog
        rcli-beta pull qwen3-0.6b    download one
        rcli-beta run qwen3-0.6b     talk to it, /? for commands

      To remove:
        brew uninstall rcli-beta
    EOS
  end

  test do
    assert_match "rcli", shell_output("#{bin}/rcli-beta --version")
    # Proves the Metal bundle survived the install: without it MLX drops out
    # silently and the other five engines carry on.
    assert_match "mlx", shell_output("#{bin}/rcli-beta backends")
  end
end
