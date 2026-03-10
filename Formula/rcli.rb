class Rcli < Formula
  desc "On-device voice AI for macOS — STT, LLM, TTS, 43 actions, and local RAG"
  homepage "https://github.com/RunanywhereAI/RCLI"
  url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.2.0/rcli-0.2.0-Darwin-arm64.tar.gz"
  sha256 "16bc4e5aab9225c7771b995929d200f92e93b95059e22b90a57242d61153ae74"
  license "MIT"
  version "0.2.0"

  depends_on :macos
  depends_on arch: :arm64

  def install
    bin.install "bin/rcli"
    lib.install Dir["lib/*.dylib"]
  end

  def post_install
    ohai "Run 'rcli setup' to download AI models and choose your engine"
  end

  def caveats
    <<~EOS
      RCLI requires Apple Silicon (M1+).

      Get started:
        rcli setup              # choose engine + download models (one-time)
        rcli                    # interactive mode (push-to-talk + text)
        rcli ask "open Safari"  # one-shot voice command

      Engine options (selected during setup):
        Open Source   llama.cpp + sherpa-onnx (~1 GB)
        MetalRT       GPU-accelerated engine (~1.5 GB) — 550 tok/s
        Both          recommended (~2.5 GB)

      MetalRT (GPU acceleration):
        rcli metalrt install    # install/update MetalRT engine
        rcli metalrt status     # check MetalRT installation

      Model management:
        rcli models             # manage all AI models (LLM, STT, TTS)
        rcli cleanup            # remove unused models to free disk space

      Benchmarks:
        rcli bench              # run all benchmarks (STT, LLM, TTS, E2E)
    EOS
  end

  test do
    assert_match "RCLI", shell_output("#{bin}/rcli --help")
  end
end
