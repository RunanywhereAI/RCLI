class RcliBeta < Formula
  desc "RCLI with MetalRT support (beta)"
  homepage "https://github.com/RunanywhereAI/RCLI"
  url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.2.0-beta.1/rcli-0.2.0-Darwin-arm64.tar.gz"
  sha256 "6ee8e62485d6cdcab75322f2288a0fe63ba01e93311e034a836ee621b19837cf"
  license "MIT"
  version "0.2.0-beta.1"

  depends_on :macos
  depends_on arch: :arm64

  # Installs as 'rcli-beta' so it doesn't conflict with stable 'rcli'
  def install
    bin.install "bin/rcli" => "rcli-beta"
    lib.install Dir["lib/*.dylib"]
  end

  def caveats
    <<~EOS
      This is the BETA version of RCLI with MetalRT support.
      It installs as 'rcli-beta' and does NOT replace your stable 'rcli'.

      Get started:
        rcli-beta setup
        rcli-beta metalrt install
        rcli-beta engine metalrt

      Both rcli and rcli-beta can coexist on the same machine.
      If the beta doesn't work, just use your stable 'rcli'.

      To remove:
        brew uninstall rcli-beta
    EOS
  end

  test do
    assert_match "rcli", shell_output("#{bin}/rcli-beta info 2>&1", 0)
  end
end
