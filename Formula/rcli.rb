class Rcli < Formula
  desc "Run language, speech and image models on your own machine"
  homepage "https://github.com/RunanywhereAI/RCLI"
  license "MIT"
  version "0.4.0"

  # Two builds of the same application. The macOS one adds MLX and NeuRT, which
  # are Metal and the Apple Neural Engine and have no counterpart elsewhere;
  # Linux ships llama.cpp, sherpa and ONNX. `rcli engines` reports what a given
  # build actually has, so the difference is visible rather than implied.
  #
  # Windows is not here because Homebrew does not run there. It is installed
  # with install.ps1 from the same release.
  on_macos do
    on_arm do
      url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.4.0/rcli-0.4.0-macos-arm64.tar.gz"
      sha256 "ae83a45625d056e031044e865d2852de4cfcedee807a5856a3c01cd25e4b0fe3"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.4.0/rcli-0.4.0-linux-x86_64.tar.gz"
      sha256 "a3e47f598ee715b9394c25dece48fbf933087ba5e4fdb56fad20ff498bf83883"
    end
  end

  def install
    # libexec rather than bin: on macOS MLX keeps its Metal shaders in a
    # resource bundle that has to sit beside the executable, and on Linux the
    # sherpa and onnxruntime shared objects are reached through an $ORIGIN/lib
    # rpath. Either one loose in Homebrew's bin/ would be shared with every
    # other formula.
    libexec.install Dir["libexec/*"]
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
    engines = shell_output("#{bin}/rcli engines")
    # llama.cpp is on every platform, so this proves the binary starts and the
    # plugin registry came up rather than proving anything platform-specific.
    assert_match(/^llamacpp\s+\d+/, engines)
    # On macOS it also proves the Metal bundle survived the install: without it
    # MLX drops out silently and the other engines carry on.
    assert_match(/^mlx\s+\d+/, engines) if OS.mac?
  end
end
