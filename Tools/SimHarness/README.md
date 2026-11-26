# Rate for Rate - sim harness

Standalone test and balance harness for the engine-independent numeric core in `core/`
(copied unchanged into `Source/ShiftHappens/Sim/` for the Unreal module).

    Tools\build_harness.ps1                       build (MSVC via vcvars64.bat) into Tools\SimHarness\out
    Tools\build_harness.ps1 -Run test             build, then run a command

    harness.exe test                              unit / invariant / determinism tests
    harness.exe batch  [--seeds 200]              bots x jams: rival%, you%, DQ%, shares, rank
    harness.exe sweep  [--seeds 100]              Posts-regen sweep 0.25..0.9 (tipping point)
    harness.exe r2     [--seeds 200]              R^2 of final votes vs quality / followers
    harness.exe calib  [--bot greedy]             regen where the bot wins 20/50/80%
    harness.exe solve  [--bot greedy]             regen hitting the plan's 85/55/30% greedy targets
    harness.exe solve-interval                    rival interval hitting 85/55/30% at the jams' regen
    harness.exe init                              starting shares / weights per jam
    harness.exe trace --jam 2 --seed 7 --bot sock [--flips 40] [--bot-log 1]
    harness.exe export-data                       write struct defaults to Data/Tuning.csv + Data/Jams.csv
    harness.exe all                               test + batch + sweep + r2

Options: `--data <dir>`, `--threads N`, `--jam 123`, `--bots idle,random,greedy,sock`,
`--set Name=Value` (Params or stand-in policy field, repeatable),
`--jamset N.Field=Value` (N = 1-based jam, 0 = all; fields are the Jams.csv column names).

Single source of tuning: `Data/Tuning.csv` (`Name,Value`) and `Data/Jams.csv` (one row per jam)
are loaded at start-up (the harness walks up from the working directory / exe to find `Data/`).
The `Params` and `JamDef` structs in `core/ShiftParams.h` hold the same values as defaults;
`export-data` regenerates both CSVs from them. Unknown names in a CSV are an error.

Tuning.csv holds the core `Params` fields first, then the harness-only stand-in policy fields
(`Rival*`, `Mega*`, `Others*`, `ModFirstSweepMin`, `BotInterval`, `SockMaxHeat`, `SockWindup`).

## Files
- `core/` engine-independent core (plain C++17, no exceptions, no globals, own PRNG)
- `config.h` CSV loading / writing
- `policies.h` stand-in rival, other entries, Mod Eye and the player bots (NOT the StateTree layer)
- `runner.h` single run, threaded batch runner, R^2
- `tests.h` tests, `main.cpp` command line
- `out/` build output (not committed)
