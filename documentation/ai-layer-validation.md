# Project-local AI layer validation

Validated on 2026-10-22 with the physical Alif DevKit-E8 M55_HP / Ethos-U85-256,
AC6 6.24.0 and ExecuTorch 1.4.1. Builds, programming and board inspection used
the CMSIS Developer Assistant tools. [Recorded snapshots](ai-layer-validation.json)
contain the build job IDs and exact debugger values.

Each application now generates `projects/<project>/ai_layer/` and references
`ai_layer/ai_layer.clayer.yml` in its cproject. The global layer is removed.
All five layers were regenerated from the existing project caches. PTE hashes,
configuration/cache hashes, lengths, header CRCs and layer descriptions passed
integrity checks. All five Release build descriptions reference their own
project-local layers. Switching contexts preserved previously generated layers.

| Application | PTE bytes | Release build | Board result |
| --- | ---: | --- | --- |
| template_mram | 2,816 | PASS b-1 | Gain and benchmark PASS; live spectrum advances |
| template_ospi | 2,816 | PASS b-8 | Full model CRC and gain PASS; live spectrum advances |
| kws_micronet_l | 559,680 | PASS b-4 | 270 network cases, MFCC and speech PASS; live stream advances |
| object_detection | 407,440 | PASS b-12 | Four network cases, preprocessing and decoder PASS; live camera/display advances |
| phonon_31m_en | 44,678,800 | PASS b-10 | Excluded from runtime testing as requested |

The OSPI preparation context also built (b-6), reached readiness values
`{model_programming_ready, app_stage, app_error, app_service_error} = {1,5,0,0}`,
and CMSIS flash successfully programmed the project-local model. Runtime verified
the complete PTE CRC `0xcbf6a854`. The flash-loader utility built successfully
as `DevKit-E8@FlashLoader` (b-11).

Two debugger snapshots confirmed progress:

| Application | Source/processing counters | Display/inference counters |
| --- | --- | --- |
| MRAM | Audio and spectrum 9,304 → 11,776 | Display 1,996 → 2,527 |
| OSPI | Audio and spectrum 1,760 → 5,236 | Display 378 → 1,124 |
| KWS | Audio and MFCC 2,240 → 3,236 | Inference 84 → 122; display 895 → 1,294 |
| Detection | Capture/crop/grayscale/inference/decode/NMS/display 162 → 355 each | Startup flags all 1; four fixtures completed |

Application, NPU and pipeline errors and the recorded deadline/drop counters
remained zero. Microphone overflow/discontinuity counters were nonzero and
increased around debugger halts; these checks establish startup correctness and
pipeline progress, not uninterrupted audio-loss performance. Phonon was not
loaded or runtime-tested.

Host validation passed 23 project-script tests, 23 CI tests, three flash-chunk
tests and all network schemas. The KWS and detection isolated generator checks
also passed the real CLI including `--quantize`, unchanged source graph/weights,
project-local temporary output and absence of a temporary global layer.

Regression checks cover preservation of another project's layer, project-local
cproject references, ignoring a legacy global model during flash preparation,
and explicit selection between overlapping flash algorithms. The latter was
needed because the board run description contains both the pack's 32 MiB loader
and the project's 64 MiB loader. `--algorithm` selects the matching existing entry
and preserves its RAM/range settings.

The `Detection_Release` target set was corrected to select
`object_detection.Release`. Object detection was resumed after inspection and
left running; the serial port was closed.
