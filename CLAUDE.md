# hardware-playground

Multi-app firmware for the FreeWili OG (two RP2040s), consuming the BSP in
`external/wiliOGbsp` (git submodule) via `add_subdirectory(<bsp>/bsp)`.
README.md covers layout and workflow.

- Build/flash only through `tools/fw.py` (`fw build|flash|run <app>`). It
  delegates flashing, CPU identification and the console to the BSP's
  `tools/fw.py` with `--uf2`. Never flash with `picotool load`, the Pico
  extension's default Run task, or a manual copy to a BOOTSEL drive of unknown CPU.
- Apps are auto-discovered from `apps/*/CMakeLists.txt`; the template for
  `fw new-app` is `tools/app_template/`.
- The top-level CMakeLists.txt must keep defining `FWOG_GIT_DESCRIBE` and
  `FWOG_GIT_COMMIT_TS`: the BSP's app helpers read them but only its own
  top-level file sets them.
- SDK/toolchain versions live in the Pico extension block at the top of
  CMakeLists.txt; ninja's path is in CMakePresets.json.
- On Windows use `py`, not `python` (it can resolve to the Store stub).

Hardware invariants and the FwOGapp contract (read before changing app or
driver code):

@external/wiliOGbsp/AGENTS.md
