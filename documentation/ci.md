# Project validation in GitHub Actions

The [workflow](../.github/workflows/validate-projects.yml) runs on every push and
pull request. It can also be started manually through **Actions → Validate
changed projects → Run workflow**.

For a push, it compares the previous and current commit, including all commits
in that push. For a pull request, it compares the branch with its merge base.
Changes in `projects/<name>/` select that project; moves between projects select
both surviving directories. Deleted projects are omitted. New project folders
are checked even if they have not yet been registered in the solution.

Changes to shared code, board support, submodules, schemas, scripts, CI, tool
configuration or Python dependencies put every current project in the work list.
A manual run, first push or unavailable comparison commit does the same. Each
project is still prepared and built separately. Changes limited to repository
documentation do not start firmware builds.

## Checks

For each affected project, `scripts/ci_projects.py` checks:

1. The firmware builds with LLVM Embedded (`cbuild --toolchain CLANG`).
2. `model/network.yml` and any `model/*benchmark*.yml` or `.yaml` files satisfy
   the existing shared schemas. Benchmark files are optional.
3. `README.md` starts with a closed YAML header containing nonempty, single-line
   `category` and `description` strings. Duplicate YAML keys are rejected.
4. The graph and tensor paths configured in `network.yml` name nonempty `.pt2`
   and `.safetensors` (or `.safetensor`) files inside `model/`. For a multi-method
   model, every entry in `methods` is checked, including CPU methods. Optional
   OSPI `assets` must be nonempty files inside `model/`, within the configured
   partition and mutually nonoverlapping. Generation additionally checks overlap
   with the actual PTE, then binds all asset hashes for programming.
5. `generated/`, `model/`, `nodes/`, `nodes_impl/` and `RTE/` exist.
6. `app.py` parses as Python and explicitly imports `cmsis_stream` or a submodule.
7. The cproject declares an existing linker script within its project directory.
8. The project's Stream integration and generated files exist: `stream_init.cpp`
   and `.hpp`, `hardware_params.cpp` and `.h`, `stream_types.h`, `datatypes.h`,
   the RTE Stream configuration/shims, generated scheduler, parameters, node
   includes, sizes and settings.

The cproject must also appear exactly once in `EthosCatalog.csolution.yml`, with
at least one target set that selects only that project. CI builds every such
target set, covering the existing Debug, Release and OSPI ProgramModel sets.

## Environment and build order

An unfinished port retained outside the catalog can explicitly declare this
README header policy:

```yaml
catalog: false
ci:
  mode: metadata_only
  reason: A concrete explanation referring to the port checkpoint.
```

This requires a nonempty project `CHECKPOINT.md`. CI continues checking source
structure, Python syntax/imports, schemas, model paths, linker scripts and target
sets. Generated Stream files, model binaries, binary asset layout, generation and firmware
builds are reported as **SKIP**, with the stated reason. They are not certified
by a metadata-only pass. Merely setting `catalog: false` does not skip builds.
Remove the explicit policy when a port is accepted and reproducible in CI.

The Arm setup follows CMSIS-DSP's `runtest.yaml`: Ubuntu 22.04, Python 3.12,
system dependencies, the CMSIS vcpkg action and Arm license activation.
System package downloads use APT's `Acquire::Retries=5` to tolerate temporary
package-server failures such as HTTP 503. Setup output is saved in the report.
The [CI manifest](../.github/ci/vcpkg-configuration.json) adds LLVM Embedded
22.1.0. It retains armclang to provide the license tooling used by the reference
setup. The license action uses its default evaluation configuration.

CI runs the repository's `python setup_venv.py`, which installs the root
requirements and the separate TOSA requirements into `.venv` and checks the
Arm quantizer import. Generation runs with `.venv/bin/python`.

The exporter needs the solution's pinned ExecuTorch runtime pack before it can
create a layer, so CI installs that pack first. `cbuild --packs` installs the
remaining required packs. Packs must be publicly downloadable on a fresh runner.
`CMSIS_PACK_ROOT` points to the runner's default Linux pack cache, overriding the
checked-in developer-machine environment path. `cpackget` initializes its default
cache automatically on a fresh runner.

There is one sequential build loop, with no compiler or project matrix:

1. Run the project's `app.py` to regenerate its Stream graph.
2. Run `scripts/create_ai_layer.py --project <cproject>` to generate its AI layer.
3. Build only that project's target sets with `--active <target>@<set>` and CLANG.
4. Move to the next project and generate its project-local AI layer. Previously
   generated project layers remain available; switching projects does not replace them.

A generation failure skips that project's builds. Other projects still get their
own generation attempt. A build failure does not stop the remaining target sets
or projects. Metadata errors are collected independently of build results.

The checked-in projects currently select AC6 and declare AC6 scatter files.
The CI command overrides the compiler selection, but this change does not migrate
startup, linker scripts or runtime support. CLANG incompatibilities fail the
build check and appear in the report. A CI build checks compilation and linking;
it does not run firmware on a board or simulator.

## Downloading errors

Open the Actions run and download **project-validation-report** from its
**Artifacts** section. It contains:

- `validation-errors.md`: the failed checks and diagnostics, grouped by project,
  including full output for failed generation and build commands.
- `command-*.log`: complete command output, including successful commands.
- `system-setup.log`: output from system repository setup and package installation.
- `python-setup.log`: output from `setup_venv.py`.
- `results.json`: the selected work list and check results.

The job summary lists failed checks. The report and artifact steps run even if
validation, tool activation, Python setup or compilation fails. Any recorded
failure makes the job fail. Reports are also uploaded for successful runs and
retained for 14 days. System package and ExecuTorch setup failures include their
captured output in `validation-errors.md`. Other setup action failures are
identified by step name, with their detailed output in the Actions job log.

## Local checks

After installing the Python dependencies, validate all project files without
building firmware:

```sh
python scripts/ci_projects.py select
python scripts/ci_projects.py validate
```

Outside Actions, `select` defaults to a manual run. Results go into the ignored
`ci-reports/` directory. Use a fresh `--report-dir <directory>` for an independent
run; pass the same directory to each command in that run.

Run the CI regression checks with:

```sh
python -m unittest discover -s tests -p test_ci_projects.py
```
