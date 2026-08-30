# frozen_string_literal: true

class Rcli < Formula
  desc "Run language, speech and image models on your own machine"
  homepage "https://github.com/RunanywhereAI/RCLI"
  version "0.5.2"
  license "MIT"

  # macOS arm64 ships the Swift MLX host
  # (llama.cpp + ONNX + Sherpa + MLX). Linux is not in this cut.
  on_macos do
    on_arm do
      url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.5.2/rcli-0.5.2-macos-arm64.tar.gz"
      sha256 "fc16e5e3fdef1e62b7d05d569ce8a69cc86cdd2b880d5983c495a5e240384430"
    end
  end

  def install
    bin.install "bin/rcli"
    bin.install Dir["bin/*.bundle"] if Dir["bin/*.bundle"].any?
    lib.install Dir["lib/*"] if Dir["lib/*"].any?
  end

  def caveats
    <<~EOS
      Models are downloaded on demand and kept in
        ~/.local/share/runanywhere

      Getting started:
        rcli list                    downloaded models
        rcli pull qwen3-0.6b         download one
        rcli run qwen3-0.6b          talk to it
        rcli run qwen3-0.6b "hi"     ask once and exit

      Also:
        rcli tts "hello"             speak text
        rcli stt recording.wav       transcribe audio
        rcli backends                which engines this build linked
    EOS
  end

  test do
    assert_match "rcli", shell_output("#{bin}/rcli version")
    backends = shell_output("#{bin}/rcli backends")
    assert_match(/llama/i, backends)
    assert_match(/onnx/i, backends)
    assert_match(/sherpa/i, backends)
    assert_match(/mlx/i, backends) if OS.mac? && Hardware::CPU.arm?
  end
end
