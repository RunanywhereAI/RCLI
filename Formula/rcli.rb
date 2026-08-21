class Rcli < Formula
  desc "Run language, speech and image models on your own machine"
  homepage "https://github.com/RunanywhereAI/RCLI"
  url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.4.0/rcli-0.4.0-Darwin-arm64.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "MIT"
  version "0.4.0"

  depends_on :macos
  depends_on arch: :arm64

  def install
    # libexec rather than bin: MLX keeps its Metal shaders in a resource bundle
    # that has to sit beside the executable, and a bundle loose in Homebrew's
    # bin/ would be shared with every other formula.
    libexec.install "libexec/rcli", "libexec/mlx-swift_Cmlx.bundle"
    bin.install_symlink libexec/"rcli"
  end

  def caveats
    <<~EOS
      Models are downloaded on demand and kept in
        ~/.local/share/runanywhere

      Getting started:
        rcli list --all              every model in the catalog
        rcli pull qwen3-0.6b         download one
        rcli run qwen3-0.6b          talk to it, /? for commands
        rcli run qwen3-0.6b "hi"     ask once and exit

      Also:
        rcli tts "hello"             speak text
        rcli stt recording.wav       transcribe audio
        rcli imagine "a red apple"   generate an image
        rcli bench                   measure generation speed
        rcli engines                 which backends are available here
    EOS
  end

  test do
    assert_match "rcli", shell_output("#{bin}/rcli --version")
    # Proves the Metal bundle survived the install: without it MLX silently
    # drops out and the other engines carry on, which is exactly the failure a
    # smoke test should catch.
    assert_match "mlx", shell_output("#{bin}/rcli engines")
  end
end
