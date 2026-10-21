---
license: apache-2.0
language:
  - en
pipeline_tag: text-to-speech
tags:
  - text-to-speech
  - tts
  - streaming
  - on-device
  - phonon
---

# phonon-31m English

**High-quality Text-to-Speech, on your device.**

phonon-31m is a text-to-speech model from the Phonon family.
It turns text into natural 24 kHz speech in English.
It streams audio while it is still generating, and it is small enough to run
in a browser tab, on a phone, or on a laptop CPU.

Phonon builds on [Pocket TTS](https://github.com/kyutai-labs/pocket-tts) and
the Mimi codec, developed by Kyutai.

[Try it in your browser](https://huggingface.co/spaces/gradium/phonon) · [All Phonon models](https://huggingface.co/collections/gradium/phonon) · [Runtime on GitHub](https://github.com/gradium-ai/xn-ptts) · [Python](https://pypi.org/project/ptts/) · [Browser](https://www.npmjs.com/package/phonon-tts) · [Discord](https://discord.gg/T85yt8kD33)


## Model details

- **Model type:** text-to-speech. A flow-matching transformer predicts audio latents from the text one 80 ms frame at a time, and the Mimi codec's decoder turns them into audio.
- **Parameters:** 31M (excluding the audio tokenizer)
- **Weights file:** `model.q8.gguf`, 64.5 MB (8-bit quantized)
- **Languages:** English
- **Output:** mono, 24 kHz
- **License:** [Apache 2.0](https://www.apache.org/licenses/LICENSE-2.0)

## The Phonon family

| Model | Parameters | q8 weights | Languages | License |
|---|---|---:|---|---|
| [phonon-31m-en](https://huggingface.co/gradium/phonon-31m-en) | 31m | 64.5 MB | English | [Apache 2.0](https://www.apache.org/licenses/LICENSE-2.0) |
| [phonon-40m-en](https://huggingface.co/gradium/phonon-40m-en) | 40m | 74.6 MB | English | [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) |
| [phonon-40m-fr](https://huggingface.co/gradium/phonon-40m-fr) | 40m | 74.6 MB | French | [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) |
| [phonon-40m-de](https://huggingface.co/gradium/phonon-40m-de) | 40m | 74.6 MB | German | [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) |
| [phonon-40m-es](https://huggingface.co/gradium/phonon-40m-es) | 40m | 74.6 MB | Spanish | [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) |
| [phonon-40m-pt](https://huggingface.co/gradium/phonon-40m-pt) | 40m | 74.6 MB | Portuguese | [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) |
| [phonon-40m-multilingual](https://huggingface.co/gradium/phonon-40m-multilingual) | 40m | 74.6 MB | All 5 languages | [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) |

All of them run on the same runtime and use the same file layout, so switching models means changing the repo ID.

## Quickstart

Every runtime names this model as `hf://gradium/phonon-31m-en`, and takes `lang`, which selects how text is normalized (numbers, symbols, dates). This model speaks English, so use `en`.

### Command line

```sh
uvx ptts --lang en --quant q8 \
  --voice en_us_Easton "Hello from Phonon." -o speech.wav
```

`uvx` runs the Python package's command without installing it. The standalone `ptts` binary from [GitHub Releases](https://github.com/gradium-ai/xn-ptts/releases) or Homebrew (`brew install gradium-ai/tap/ptts`) takes the same flags.

### Python

```sh
pip install ptts
```

```python
import ptts

tts = ptts.TTS("hf://gradium/phonon-31m-en", lang="en", quant="q8")
tts.save("speech.wav", "Hello from Phonon.", voice="en_us_Easton")

# Or stream: chunks arrive while the rest is still being generated.
with tts.stream("Speech can start playing right away.", voice="en_us_Easton") as audio:
    for pcm in audio:
        ...  # float32 chunks at audio.sample_rate
```


### Other platforms

| Platform | Guide |
|---|---|
| Python scripts and backends | [`ptts` on PyPI](https://pypi.org/project/ptts/) · [Python guide](https://github.com/gradium-ai/xn-ptts/blob/main/ptts-pyo3/README.md) |
| Web apps, in the browser | [`phonon-tts` on npm](https://www.npmjs.com/package/phonon-tts) · [Browser guide](https://github.com/gradium-ai/xn-ptts/blob/main/ptts-wasm/js/README.md) |
| iPhone, iPad and Mac apps (Core ML) | [Swift guide](https://github.com/gradium-ai/xn-ptts/blob/main/ios/PhononTTS/README.md) |
| Android apps | [Android guide](https://github.com/gradium-ai/xn-ptts/blob/main/android/README.md) · [Snapdragon NPU preview](https://github.com/gradium-ai/xn-ptts/blob/main/qnn/android/README.md) |
| Android system voice, for TalkBack and every app that uses `TextToSpeech` | [Android TTS engine](https://github.com/gradium-ai/xn-ptts/blob/main/android/tts-engine/README.md) |
| Server with an OpenAI-compatible API (Docker) | [Server guide](https://github.com/gradium-ai/xn-ptts/blob/main/ptts-openai-server/README.md) |
| Home Assistant | [Home Assistant setup](https://github.com/gradium-ai/xn-ptts/blob/main/ptts-openai-server/README.md#home-assistant) |
| Streaming over WebSocket | [WebSocket server](https://github.com/gradium-ai/xn-ptts/blob/main/ptts-ws-server/README.md) |
| Rust | [`ptts` on crates.io](https://crates.io/crates/ptts) · [Rust guide](https://github.com/gradium-ai/xn-ptts/blob/main/ptts/README.md) |
| C and C++ | [`ptts.h`](https://github.com/gradium-ai/xn-ptts/blob/main/ptts-ffi/include/ptts.h) |

## Voices

Phonon comes with a catalog of ~100 voices available in the [gradium/phonon-voices](https://huggingface.co/gradium/phonon-voices) repo.
Instant voice cloning and voice design are available through the [Gradium studio](https://studio.gradium.ai).

## Benchmarks

Phonon was compared with other open text-to-speech models on three measures, per language:

- **Elo**: human preference from blind pairwise listening tests (higher is better).
- **Hard set LLM WER**: word error rate on hard sentences full of structured data (numbers, dates, codes, units), scored with an LLM-assisted ASR comparison (lower is better).
- **Seed-TTS LLM WER**: the same metric on the Seed-TTS evaluation sentences (lower is better).

In every language, a Phonon model is first on all three measures.

**English (US)** (Elo from 468 raters, 8,290 votes)

| Model | Elo | Hard set LLM WER | Seed-TTS LLM WER |
|---|---:|---:|---:|
| **phonon-31m-en** | 2098 | **1.5%** | 0.51% |
| **phonon-40m-en** | **2117** | 2.3% | **0.45%** |
| **phonon-40m-multilingual** | 2097 | 2.0% | 0.48% |
| Kokoro | 2043 | 3.5% | **0.45%** |
| Magpie | 1939 | 28% | 0.46% |
| NeuTTS | 2034 | 8.7% | 0.62% |

**French** (Elo from 172 raters, 2,270 votes)

| Model | Elo | Hard set LLM WER | Seed-TTS LLM WER |
|---|---:|---:|---:|
| **phonon-40m-fr** | **2072** | **2.4%** | **1.3%** |
| **phonon-40m-multilingual** | 2016 | 2.7% | 1.5% |
| Kokoro | 1578 | 5.5% | 1.8% |
| Magpie | 1608 | 30% | 2.0% |
| NeuTTS | 1853 | 9.9% | 2.7% |

**German** (Elo from 168 raters, 2,220 votes)

| Model | Elo | Hard set LLM WER | Seed-TTS LLM WER |
|---|---:|---:|---:|
| **phonon-40m-de** | **2097** | **2.2%** | **0.83%** |
| **phonon-40m-multilingual** | 2046 | 2.9% | 0.90% |
| Magpie | 1640 | 29% | 1.6% |
| NeuTTS | 1768 | 9.3% | 2.6% |

**Spanish (Spain)** (Elo from 201 raters, 2,290 votes)

| Model | Elo | Hard set LLM WER | Seed-TTS LLM WER |
|---|---:|---:|---:|
| **phonon-40m-es** | **2103** | **2.1%** | **0.85%** |
| **phonon-40m-multilingual** | 2076 | 2.6% | 1.0% |
| Kokoro | 1629 | 3.8% | 0.93% |
| Magpie | 1870 | 25% | 0.92% |
| NeuTTS | 1778 | 16% | 5.1% |

**Portuguese (Brazil)** (Elo from 171 raters, 2,000 votes)

| Model | Elo | Hard set LLM WER | Seed-TTS LLM WER |
|---|---:|---:|---:|
| **phonon-40m-pt** | **2144** | **2.9%** | **1.1%** |
| **phonon-40m-multilingual** | 2048 | 3.2% | **1.1%** |
| Kokoro | 1633 | 4.1% | 1.3% |

## Intended use

Phonon is made for applications that speak on the user's device: offline and private
assistants, screen readers and other accessibility tools, games, and interactive apps that
cannot wait for a cloud round trip. It also runs as a local server with an OpenAI-compatible
API and as a voice for Home Assistant.

### Limitations

- The model speaks only the languages above. Text in other languages is read with the wrong
  pronunciation.
- Text is normalized before synthesis (numbers, currency, dates, emails and so on), and that
  step needs the right language. Choose `lang` to match the text.
- Very long inputs are split into sentences and spoken one after the other, so prosody does not
  carry across the whole text.

### Responsible use

Do not use Phonon to impersonate a real person, to deceive listeners about who is speaking, or
to clone a voice without the speaker's consent. Tell listeners when speech is synthetic where
that matters to them.

## License

The weights, tokenizer and voices in this repository are released under [Apache 2.0](https://www.apache.org/licenses/LICENSE-2.0), which allows commercial use. The runtime is a separate project, licensed MIT OR Apache-2.0.
