class Rcli < Formula
  desc "On-device voice AI for macOS — STT, LLM, TTS, 43 actions, and local RAG"
  homepage "https://github.com/RunanywhereAI/RCLI"
  url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.3.7/rcli-0.3.7-Darwin-arm64.tar.gz"
  sha256 "631349d585231b8f9b4590022c8c1fc3525e6112cce2308ee9871856cd327e3c"
  license "MIT"
  version "0.3.7"

  depends_on :macos
  depends_on arch: :arm64

  def install
    bin.install "bin/rcli"
    bin.install "bin/rcli_overlay" if File.exist? "bin/rcli_overlay"
    bin.install "bin/rcli_camera_preview" if File.exist? "bin/rcli_camera_preview"
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
        Open Source   llama.cpp + sherpa-onnx (~1 GB)   — all Apple Silicon
        MetalRT       GPU-accelerated engine (~0.9 GB)  — M3+ only, 550 tok/s
        Both          recommended (~1.9 GB)

      MetalRT (GPU acceleration):
        rcli metalrt install    # install/update MetalRT engine
        rcli metalrt status     # check MetalRT installation

      Model management:
        rcli models             # manage all AI models (LLM, STT, TTS)
        rcli cleanup            # remove unused models to free disk space

    EOS
  end

  test do
    assert_match "RCLI", shell_output("#{bin}/rcli --help")
  end
end
