# Repository instructions

- Playback compatibility targets retail 3D Movie Maker. Prefer sharing playback
  policy between the SDL port and Marcus, and verify behavior against retail
  captures. V3dmm is an additional comparison baseline.

- Commit completed code and documentation changes as part of finishing each task.
  Include only task-related changes; preserve unrelated existing edits and files.
- Do not add Marcus UI tests unless the user explicitly asks for them.
- Before rebuilding Marcus, always stop the running copy from this workspace so
  the build can replace `marcus.exe`. Relaunch it after the rebuild.
- New Marcus code belongs to this project. Do not copy Microsoft copyright
  headers into new Marcus files. Preserve notices in original engine code and
  third-party sources.
