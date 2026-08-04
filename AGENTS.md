# AGENTS.md

## Project overview

ECMSim: single-binary C++17 electronic-countermeasures simulator. Calculates radar/jammer SINR from a JSON scene config.

## Build & run

All commands execute from the **`test/` directory** (the Makefile lives there):

```bash
make       # build bin/sim_test
make run   # build + run with config/sence_config.json
make clean # delete build/ and bin/

Output is written to the project root:
- build/ — intermediate .o files (preserves source tree layout)
- bin/sim_test — the final binary
- result_output.json — generated after each run
The binary accepts an optional config path as first argument (defaults to config/sence_config.json).

Repo structure
config/            — scene JSON configs
include/
  algo/            — ECMAlgo namespace: physics constants (math_const.h) + radar equations (radar_eq.h)
  entity/          — ECMSim namespace: Radar, Jammer classes
  sence/           — ECMSim namespace: SimScene coordinator
  jsoncpp/json/    — bundled jsoncpp headers
src/               — .cpp mirrors of include/ layout
  jsoncpp/         — jsoncpp.cpp directly here (NOT in src/jsoncpp/json/)
test/              — Makefile + main_test.cpp (the only entrypoint)
third_party/       — jsoncpp source tarball (already extracted into include/ + src/)

Architecture notes
- Two namespaces: ECMAlgo (physics/math, no state) and ECMSim (entities + simulation).
- Include style: all #include use paths relative to include/, e.g. <entity/radar.h>, <algo/radar_eq.h>. The Makefile sets -I../include.
- Unit convention: all physical quantities in linear SI (W, Hz, m) internally. Only constructors/JSON accept dBm values.
- dB helpers (db2lin, lin2db) are inline in include/algo/math_const.h — no corresponding .cpp file.
- jsoncpp is statically compiled in via src/jsoncpp/jsoncpp.cpp; its headers are in include/jsoncpp/json/.
- Radar constructor takes Pt_dBm (dBm, not dBW) — converts internally to linear Watts.
- Jam types (parsed from JSON strings): NOISE_JAM, RANGE_DECEPT.

Testing
The only test is test/main_test.cpp. It loads a scene config, runs one simulation step, and prints JSON results. No test framework is in use — verification is manual via make run and inspecting result_output.json.
