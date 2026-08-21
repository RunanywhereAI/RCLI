class Rcli < Formula
  desc "Run language, speech and image models on your own machine"
  homepage "https://github.com/RunanywhereAI/RCLI"
  license "MIT"
  version "0.4.1"

  # Two builds of the same application. The macOS one adds MLX and NeuRT, which
  # are Metal and the Apple Neural Engine and have no counterpart elsewhere;
  # Linux ships llama.cpp, sherpa and ONNX. `rcli engines` reports what a given
  # build actually has, so the difference is visible rather than implied.
  #
  # Windows is not here because Homebrew does not run there. It is installed
  # with install.ps1 from the same release.
  on_macos do
    on_arm do
      url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.4.1/rcli-0.4.1-macos-arm64.tar.gz"
      sha256 "2458af9cd0cac623453b0963d2736d4c5f8b34bb0d9f298fb6a7d9d1b1081b3b"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.4.1/rcli-0.4.1-linux-x86_64.tar.gz"
      sha256 "5b6e100346f146732d59c630a33f7cb3cd44eec0b8664253af7b215303a5e208"
    end
  end

  def install
    # libexec rather than bin: on macOS MLX keeps its Metal shaders in a
    # resource bundle that has to sit beside the executable, and on Linux the
    # sherpa and onnxruntime shared objects are reached through an $ORIGIN/lib
    # rpath. Either one loose in Homebrew's bin/ would be shared with every
    # other formula.
    libexec.install Dir["libexec/*"]
    # A wrapper that execs, not a symlink. MLX finds its Metal shaders relative
    # to the path the process was launched from, and through a symlink in bin/
    # that is bin/, where the bundle is not. The engine still reports itself
    # available, because the check that answers that question does resolve the
    # link, so the failure only shows up when a model actually runs. exec'ing
    # from libexec means the launch path is the real one.
    bin.write_exec_script libexec/"rcli"
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
    # Registration is not the same as working: MLX reports itself available
    # whenever the shader bundle exists anywhere it can see, and then fails at
    # load time if the launch path is wrong. Generating a token is what
    # separates the two, so the test does that rather than reading a table.
    if OS.mac?
      assert_match(/^mlx\s+\d+/, engines)
      system bin/"rcli", "pull", "mlx-qwen3-0.6b-4bit"
      assert_match(/\S/, shell_output("#{bin}/rcli run mlx-qwen3-0.6b-4bit 'say ok' 2>/dev/null"))
    end
  end
end
