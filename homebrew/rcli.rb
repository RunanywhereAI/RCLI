class Rcli < Formula
  desc "RunAnywhere RCLI — on-device voice AI for macOS"
  homepage "https://github.com/RunanywhereAI/RCLI"
  url "https://github.com/RunanywhereAI/RCLI/releases/download/v0.1.0/rcli-0.1.0-Darwin-arm64.tar.gz"
  sha256 "PLACEHOLDER_SHA256"
  license "MIT"
  version "0.1.0"

  depends_on :macos
  depends_on arch: :arm64

  def install
    bin.install "rcli"
    lib.install Dir["lib/*.dylib"] if Dir.exist?("lib")
  end

  def post_install
    ohai "Run 'rcli setup' to download AI models (~700MB)"
  end

  def caveats
    <<~EOS
      RunAnywhere RCLI requires Apple Silicon (M1+) and ~700MB of AI models.

      Get started:
        rcli setup              # download models (~700MB, one-time)
        rcli                    # interactive mode (push-to-talk + text)
        rcli actions            # see all available actions
        rcli ask "open Safari"  # one-shot voice command

      Voice mode:
        rcli listen             # continuous hands-free voice control

      Upgrade speech recognition (optional, +640MB):
        rcli upgrade-stt        # download Parakeet TDT (1.9% WER vs 5%)

      Upgrade language model (optional):
        rcli upgrade-llm        # choose LFM2 1.2B Tool or Qwen3 4B

      RAG (optional):
        rcli rag ingest ~/Documents  # index your docs
        rcli rag query "summarize the meeting notes"

      Benchmarks:
        rcli bench                  # run all benchmarks (STT, LLM, TTS, E2E)
        rcli bench --suite llm      # LLM benchmarks only
    EOS
  end

  test do
    assert_match "RCLI", shell_output("#{bin}/rcli --help")
  end
end
