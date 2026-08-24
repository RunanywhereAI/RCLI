class Rcli < Formula
  desc "Run language, speech and image models on your own machine"
  homepage "https://github.com/RunanywhereAI/RCLI"
  license "MIT"
  version "0.5.0"

  # 0.5.0 is the kit-consumer CMake bottle (llama.cpp + ONNX). Linux is not
  # in this cut; install from the GitHub Release zip when it exists.
  on_macos do
    on_arm do
      url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.5.0/rcli-0.5.0-macos-arm64.tar.gz"
      sha256 "aa524eef31405d99c1fda4b90c3b1f3e8f74302fe209ba7fe81b343a1f92a266"
    end
  end

  def install
    bin.install "bin/rcli"
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
  end
end
