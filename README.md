# QuestTrackerNG

> ⚠️ **Back up your save first.** This is a development/debugging tool that edits quest state (stages and objectives) in your running game. Forcing stages can have irreversible effects on a playthrough — make a hard save before changing anything.

> 🤖 **Built with AI.** This plugin — code, tests, CI, and documentation — was written by [Claude](https://claude.com) (Anthropic's AI assistant), directed and tested in-game by Will Tarking. We're not hiding it; if that matters to you, now you know. Bug reports are welcome regardless of who typed the semicolons.

SKSE plugin for Skyrim SE/AE (1.6.x) built on **CommonLibSSE-NG**. Renders an ImGui overlay from a D3D11 present hook that lists every running `TESQuest` (name, editor ID, form ID, current stage) and lets you set a quest's stage through the engine's own `SetCurrentStageID` Papyrus native, with guards.

Press **F6** in game to toggle the overlay (configurable). While it is open, game input is suppressed and the mouse drives the UI.

## How it works

| Hook | Address Library ID (SE / AE) | Purpose |
|---|---|---|
| `BSGraphics::InitD3D` | 75595+0x9 / 77226+0x275 | Grab the game's `ID3D11Device`/context/window from `BSGraphics::Renderer::GetRendererData()` and initialise ImGui (DX11 backend) |
| Present | 75461+0x9 / 77246+0x9 | Build and render the ImGui frame each game frame on the render thread |
| Input dispatch | 67315+0x7B / 68617+0x7B | Watch for the toggle key; while open, translate `RE::InputEvent`s into ImGui IO and forward an **empty** event chain so the game ignores input |

Quest data is snapshotted on the game's **main thread** via the SKSE task interface (never touched from the render thread). Setting a stage re-resolves the quest by form ID, requires the quest to be running (unless *Force* is ticked), warns on stage regressions, then dispatches `Quest.SetCurrentStageID(int)` through the Papyrus VM so all engine bookkeeping (fragments, objectives, story manager) runs normally.

CommonLibSSE-NG is fetched at configure time, pinned to commit `b93280e8` on `main` — no third-party vcpkg registry needed. Its dependencies (`spdlog`, `rapidcsv`) and `imgui[dx11-binding]` come from the project's vcpkg manifest (baseline pinned in `vcpkg.json`).

## Building the plugin (Windows)

Requirements: Visual Studio 2022 (C++ workload), CMake ≥ 3.24, [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set.

```powershell
cmake --preset vs2022-windows
cmake --build --preset release
```

Output is staged as an installable mod in `build/dist/Data/SKSE/Plugins/` (DLL + PDB + ini). `python scripts/package.py` zips it. Set the env var `QUEST_TRACKER_OUTPUT_DIR` to an MO2 mod folder to auto-deploy after each build.

Runtime requirements: SKSE64 and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).

## Tests

The UI's engine-independent logic (`src/Core/StageLogic.h`: stage parsing, filtering, form ID formatting, sort order, regression detection) builds and runs anywhere:

```bash
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

CI (`.github/workflows/build.yml`) runs these tests on Linux, then builds the plugin with MSVC on `windows-latest` and uploads the packaged mod as an artifact.

## Layout

```
CMakeLists.txt          plugin build (MSVC only, FetchContent + vcpkg manifest)
CMakePresets.json       vs2022-windows preset, x64-windows-static-md triplet
vcpkg.json              dependency manifest (pinned baseline)
contrib/                default QuestTrackerNG.ini (toggle key, auto-refresh)
src/
  main.cpp              SKSE entry point: SKSEPluginInfo / SKSEPluginLoad
  Settings.*            ini loading
  Core/StageLogic.h     pure logic, unit tested
  Hooks/                D3D init, present, input dispatch trampoline hooks
  Input/                DIK -> ImGui translation, toggle/escape detection
  UI/                   quest table + stage editor, SKSE main-thread tasks
tests/                  cross-platform unit tests for Core
scripts/package.py      zips build/dist into an installable archive
```

## Verification & security review

Beyond the unit tests, every translation unit has been cross-checked with clang 21 (`-Wall -Wextra`, plus the clang static analyzer) against the exact pinned CommonLibSSE-NG commit and real imgui/spdlog headers — zero errors and zero warnings in this codebase. A security pass hardened three things: all ImGui IO access is confined to the render thread (input events are recorded into a bounded, mutex-guarded queue on the game thread and drained inside the present hook — ImGui's IO API is not thread-safe), the Papyrus `IFunctionArguments` object is handed to the VM without a manual `delete` (matching CommonLibSSE's own dispatch code; freeing it would risk a double free), and ini values are range-clamped. Quest data is only read/written on the game's main thread via SKSE tasks; stage requests re-resolve the quest by form ID and are parsed/range-checked before dispatch. All user-controlled strings are rendered with `TextUnformatted`/literal format strings, so no printf-style format injection.

## Credits

This plugin stands on the shoulders of the Skyrim modding community's tooling: [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) by CharmedBaryon and the CommonLibSSE contributors (the reverse-engineered game API this is built on), [SKSE](https://skse.silverlock.org/) by the SKSE team, [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) by meh321 (version-independent addresses), [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut (the overlay UI), and the hook patterns established by open-source ImGui-based SKSE mods. Licensed MIT — see [LICENSE](LICENSE).

## Notes & caveats

The in-game hook offsets are the community-established call sites used by ImGui-based SKSE mods; they cover SE 1.5.97 and AE 1.6.x via Address Library. VR is not targeted (`ENABLE_SKYRIM_VR=OFF`). Forcing a stage on a non-running quest starts it implicitly via the engine, exactly as the console `setstage` would; regressions can break quest scripts that don't expect repeated stages — that's why both paths sit behind the *Force* checkbox.
