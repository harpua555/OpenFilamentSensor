# Filament Flow Renderer

Use these assets to watch the pulse simulator output as a flowing filament bar.

## Workflow

1. From the `test` directory run:
   ```
   ./pulse_simulator --log render/filament_log.csv
   ```
   This emits a CSV with per-check snapshots.

2. Serve the `render/` folder so the browser can access the files:
   ```
   cd render
   python3 -m http.server 8000
   ```

3. Open `http://localhost:8000` in your browser, load `filament_log.csv`, and press Play.

### Selecting a single test

The player now shows a **Test** dropdown below the speed slider. Pick one of the listed tests to see only that scenario’s frames—this is handy for replaying a specific corner case without wading through the entire log. Choose **All tests** to revert to the full log.

## What is rendered

Each row in the CSV contains:

- `test`: the test name
- `label`: the label passed to `printState`
- `timestamp`: simulated millis()
- `expected`: expected filament (mm)
- `actual`: measured filament (mm)
- `deficit`: `expected - actual`
- `ratio`: flow ratio
- `jammed`: `1` if the check reported a jam

The page draws expected vs actual bars, indicates jams with red, and shows the textual state for each row while letting you control playback speed.
