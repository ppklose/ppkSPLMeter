# SPLMeter — REW Measurement Sequencer Specification

Status: **Implemented** (v0.3) · 2026-11-13 · release: **v3.6.0** · branch `feature/vibration` · user guide: [REW_SEQUENCER_GUIDE.md](REW_SEQUENCER_GUIDE.md)

## 1. Goals

Run a list of Room EQ Wizard sweep measurements one after the other, each step with its own settings, from SPLMeter. REW does the measuring; SPLMeter configures REW through its REST API, triggers each sweep, waits for the result and moves on.

| Decision | Value |
|---|---|
| Name | **REW Measurement Sequencer** |
| Access | **Tools → REW Measurement Sequencer…** opens a separate window, like the other tools |
| Transport | REW REST API, `http://127.0.0.1:4735` (port editable) |
| Targets | Standalone, VST3, AU on macOS / Windows / Linux; **not iOS** (REW only listens on the local machine) |
| Results | REW keeps the measurements; SPLMeter adds its own **acoustic and vibration values per step** (REW cannot measure vibration) and saves them with the run |
| Typical uses | multi-position room measurements (mic moves between steps), L / R / sub / L+R per output channel, level series, sweep-length or bandwidth comparisons, repeated runs for repeatability |

### Non-goals (v1)

- Starting or installing REW. The user starts REW with the API enabled (Preferences → API, or `-api`).
- Analysis, EQ or averaging of REW data inside SPLMeter. REW's results stay in REW; SPLMeter reads back only the title and UUID.
- RTA, stepped-sine, impedance and SPL-logger sequences. The data model allows other commands later, but v1 only uses `SPL` sweeps.
- REW on another computer. The API refuses non-local connections.

## 2. REW API facts (verified against REW 5.40 beta 147, API 0.9.9, `/doc.json`)

| Purpose | Endpoint | Notes |
|---|---|---|
| Alive / version | `GET /version` → `{"message":"5.40 Beta 147 API 0.9.9"}` | used for the connection status |
| Audio ready | `GET /audio/status` → `{enabled, ready}` | |
| Driver | `GET /audio/driver` → `Java` or `ASIO` | controls which channel endpoints are used |
| Output channel | Java: `POST /audio/java/output-channel {"channel":"L"}`, choices from `GET /audio/java/output-channels` (`L`, `R`, `L+R`, `3`…); ASIO: `/audio/asio/output`, `/audio/asio/outputs` | |
| Second output | ASIO: `POST /audio/asio/secondary-output {"output":…}` + `/audio/asio/secondary-output-enable {"enable":true}`; both drivers: `POST /measure/invert-second-output true` | REW has no secondary output for the Java driver; there the output `L+R` drives two outputs and only *invert* applies |
| Input channel | Java: `/audio/java/input-channel {"channel":1}`, `/audio/java/last-input-channel` (multi-input, Pro); ASIO: `/audio/asio/input`, `/audio/asio/last-input` | set *last* before *first*, otherwise REW clamps the range (VirtualEareality lesson) |
| Level | `POST /measure/level {"value":-12,"unit":"dBFS"}`; units `dBu`, `dBV`, `V`, `dBFS` | |
| Sweep | `POST /measure/sweep/configuration {startFrequency, endFrequency, length, fillSilenceWithDither}`; lengths `64k`…`4M` | |
| Repetitions | `POST /measure/sweep/repetitions` (sweeps averaged into one measurement) | |
| Timing reference | `POST /measure/timing/reference "Acoustic"`; choices `None`, `Loopback`, `Loopback cal`, `Acoustic`, `Wired` | |
| Name / notes | `POST /measure/naming {title, namingOption:"Use as entered"}`, `POST /measure/notes "…"` | apply to the *next* measurement |
| Protection | `/measure/protection-options` (abort on clipping / SPL limit) | |
| Trigger | `POST /measure/command {"command":"SPL"}` → 202 Accepted; `{"command":"Cancel"}` aborts | **automated sweeps need a REW Pro licence** |
| Blocking | `GET/POST /application/blocking` | must be **false**: blocking calls time out after 10 s, and the setting persists in REW |
| Results | `GET /measurements` → object keyed by index (`{"1":{…,"uuid":…}}`), `GET /measurements/{id}` | |
| Groups | `GET/POST /groups`, `POST /groups/{uuid}/measurements {"uuid":…}` | |
| Errors / warnings | `GET /application/errors`, `/application/warnings` (`{time,title,message}`) | |
| Save | `POST /measurements/command {"command":"Save all","parameters":["/path/file.mdat","note"]}` | overwrites existing files |
| Busy | a POST while a command is running → **400 Bad Request** | |

Push notifications (`/measure/subscribe`, `/measurements/subscribe`) need an HTTP server on SPLMeter's side, and REW drops the subscription after one failed delivery. v1 **polls** instead. See §5.3.

## 3. Data model

```
RewStep
  enabled          bool
  name             string        → REW measurement title ("" = sequence name + step number)
  notes            string
  output           string        e.g. "L", "R", "L+R", "3" (driver-specific choice list)
  secondOutput     enabled, output, invert   (ASIO secondary output; Java: invert only, with L+R)
  inputFirst/Last  int           last > first only with REW Pro multi-input
  level            double + unit dBFS | dBu | dBV | V
  sweepStart/End   int Hz
  sweepLength      string        "64k" … "4M"
  repetitions      int           1–16 (averaged by REW)
  timingRef        string        None | Loopback | Loopback cal | Acoustic | Wired
  group            string        REW group name ("" = none; created if missing)
  protection       clippingAbort bool, splLimitAbort bool, dBSPLLimit dB   (/measure/protection-options)
  waitForUser      bool + prompt text   pause before this step, e.g. "Move mic to position 3"
  preDelay         seconds       extra settle time before the trigger

RewSequence
  name, steps[], port
  loops            int           run the whole list N times (repeatability)
  betweenSteps     seconds
  onError          stop | skip step | retry once
  restoreRewSettings  bool       snapshot REW settings before the run, restore after
  saveMdatPath     string        optional "Save all" at the end
  resultsFolder    string        where the run results (§6.2) are written automatically
  addLogMarkers    bool          one SPLMeter timeline marker per step
```

- Fields left "unchanged" (a per-field `inherit` flag, shown as "—" in the table) are not sent. This keeps REW settings the user made by hand, such as the sweep length.
- Persistence: `UIState/RewSequencer` (last sequence + window state). **Save / Load Sequence…** as `*.rewseq.json`. The sequence is not part of the SPLMeter settings JSON (decided, Q3).
- **Add positions…** helper: generates N copies of the selected step with `waitForUser` set and names like `Pos 1`, `Pos 2`, ….

## 4. User interface

`RewSequencerWindow` (DocumentWindow, resizable, about 900×560):

```
┌ REW Measurement Sequencer ──────────────────────────────────────────────┐
│ ● REW 5.40 b147 · API 0.9.9 · Java · 48 kHz        Port [4735] [Connect] │
│ Sequence [Room L/R 9 pos ▾]  [Load…] [Save…]   Loops [1]  Gap [2 s]      │
├───┬───┬──────────┬──────┬─────┬────────┬──────────┬──────┬────┬────┬─────┤
│ # │ ✓ │ Name     │ Out  │ In  │ Level  │ Sweep    │ Len  │ Rep│ Ref│ Wait│
│ 1 │ ✓ │ L pos1   │ L    │ 1   │ −12 dBFS│ 20–20k  │ 512k │ 1  │ Ac │  ✓  │
│ 2 │ ✓ │ R pos1   │ R    │ 1   │   —    │   —      │  —   │ —  │ —  │     │
│ … │   │          │      │     │        │          │      │    │    │     │
├───┴───┴──────────┴──────┴─────┴────────┴──────────┴──────┴────┴────┴─────┤
│ [+ Add] [Duplicate] [Add positions…] [Delete] [▲] [▼]                     │
├──────────────────────────────────────────────────────────────────────────┤
│ [▶ Run] [❚❚ Pause] [■ Abort]  Step 3/18 · loop 1/1 · measuring… 0:07     │
│ ▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░                                                  │
│ 12:01:03  1 L pos1   ✓  "L pos1"  uuid 7ec…                               │
│ 12:01:15  2 R pos1   ✓  …                                                 │
└──────────────────────────────────────────────────────────────────────────┘
```

- Status dot: green when connected and audio is ready; amber when REW is reachable but `audio/status.ready` is false or blocking is on; red when REW is not reachable. Checked every 5 s while the window is open and idle.
- Choice columns (output, sweep length, timing reference, level unit) are filled from REW's `…/choices` endpoints after connecting, with built-in defaults offline. The sequence can be edited without REW running.
- A **wait-for-user** step shows a prompt banner with **Continue** / **Skip** / **Abort**, and the window comes to the front.
- While a sequence runs, the table is read-only and the current row is highlighted. Closing the window asks whether to abort.
- **Dry run** (checkbox next to Run): sends all settings but skips the trigger. Use it to check routing.

## 5. Engine

### 5.1 Components

| File | Purpose |
|---|---|
| `Source/Rew/RewTransport.h` | interface `request(method, path, body) → {status, body}`, plus `HttpRewTransport` (juce::URL, connection timeout 2 s, read timeout 5 s) |
| `Source/Rew/RewSequence.h` | `RewStep`, `RewSequence`, JSON (de)serialisation, validation |
| `Source/Rew/RewSequencer.h` | runner on its own `juce::Thread`; state machine; publishes a progress snapshot (mutex-protected, polled by the window at 10 Hz) |
| `Source/RewSequencerComponent.h` | window, table, controls, run log |

The engine never runs on the audio thread and does not touch the processor, apart from the optional log marker posted to the message thread.

### 5.2 Run sequence

```
Preflight:  GET /version · /audio/status (ready) · /audio/driver · GET /application/blocking
            → POST blocking=false (remember the old value)
            → snapshot settings if restoreRewSettings (output, inputs, level, sweep, reps, timing, naming, notes)
            → validate every enabled step against REW's choice lists (unknown output → error before starting)
Per step:   [waitForUser] → configure (POST only the non-inherited fields, check every response)
            → naming {title, namingOption:"Use as entered"} + notes
            → preDelay → remember the UUID set and the error/warning counts
            → POST /measure/command SPL (202 expected; 400 = busy → wait and retry once)
            → wait for the result (§5.3) → group assignment → log line → betweenSteps
End:        optional Save all → restore snapshot → restore blocking → summary
Abort:      POST /measure/command Cancel, then restore as above
```

### 5.3 Completion detection

Poll every 500 ms:

1. `GET /measurements`: a **UUID not in the set before the trigger** means a new measurement. With multi-input, wait for the expected count (`inputLast − inputFirst + 1`, plus one overall average unless `no-overall-average`), or until 2 s pass without another one. Comparing UUIDs instead of counting entries keeps working when the user deletes measurements or REW hits its 500-measurement limit.
2. `GET /application/errors` and `/warnings`: a new entry since the trigger is attached to the step. An error ends the step as **failed** (e.g. clipping abort, no Pro licence, input not found). A warning (e.g. low SNR) is logged and the step counts as ok.
3. Timeout: expected duration = start delay + repetitions × (sweep samples ÷ fs + 1 s) + 15 s margin. When it passes: Cancel, step **failed: timeout**.

`onError` then decides: stop the sequence, skip to the next step, or retry the step once.

### 5.4 Threading and robustness

- All HTTP runs on the sequencer thread; the UI thread never blocks.
- Abort and pause are atomics checked between every request and every poll.
- The plugin editor can be closed while a sequence runs: the runner is owned by the **processor** (decided, Q4), so the window can be reopened and the run continues. Destroying the processor aborts the run (Cancel + restore).
- JSON bodies are built with `juce::JSON`, never by string concatenation (quotes in names).

## 6. SPLMeter integration

### 6.1 Per-step measurement (decided, Q6; implemented)

REW measures the sweep; SPLMeter measures the same interval with its own microphone and accelerometer. REW cannot measure vibration, so these values are part of the result, not an extra.

- **Step integrator** in the processor (audio thread, lock-free handshake `stepState_`): the sequencer begins it just before the trigger and ends it when the measurement has arrived (the end waits up to 0.5 s for the audio thread; without audio the step has no levels).
  - Mic: mean square A, C, Z → **L_Aeq, L_Ceq, L_Zeq**; **L_AFmax** / **L_CSmax** from FAST-A and SLOW-C detectors that run continuously (independent of the meter's FAST/SLOW choice, so they are settled when a step starts); **L_Cpeak** from the C-weighted sample peak.
  - Vibration (if an accelerometer is active): per axis X/Y/Z and Σ the **RMS** over the step (mean of the 125 ms running mean squares) and the **peak**, with quantity and axes. The profile's main value is *not* reported per step: A(8), VDV, KB_FTr and the like accumulate over the user's whole measurement and are not meaningful for one sweep.
- It does not touch the user's running values: no reset of LAeq, log, peaks or vibration accumulators.
- Paused metering (Pause / DAW sync) is respected: paused time is counted (`paused_s`) but not measured.
- The step window runs from the trigger to the measurement's arrival. That includes REW's start delay and the silence before and after the sweep. The duration is stored, so values can be compared between steps.

### 6.2 Run results (saved)

At the end of every run that measured, and also after an abort or error with the steps done so far, SPLMeter writes to the results folder (default `Documents/SPLMeter/REW runs`). Dry runs write nothing.

- `<sequence>_<YYYY-MM-DD_HH-MM-SS>.csv`:
  - header lines (`#`): SPLMeter and REW versions, result, mic calibration offset, vibration profile, accelerometer calibration per axis, REW file;
  - one row per step result: step, loop, name, status, start, duration, messages, REW UUIDs, output, second output, input(s), level;
  - LAeq, LCeq, LZeq, LAFmax, LCSmax, LCpeak, metered and paused seconds;
  - vibration unit (the display unit), X/Y/Z/Σ RMS, X/Y/Z/Σ peak. The vibration columns are empty without an accelerometer.
- `<same name>.json`: the same data plus the full sequence (format `splmeter-rew-run`, version 1).
- REW's `Save all` writes to the .mdat path chosen in the window (unchanged by the results folder).

The run log in the window shows each step's LAeq, LAFmax, LCpeak and the vibration RMS. **Show results** opens the CSV in the file browser.

### 6.3 Timeline

- **Log markers** (optional, on by default): `REW 3/18 L pos1` at the trigger. SPLMeter's timeline then shows which sweep belongs to which SPL and vibration trace. The processor queues the markers (the run may go on without an editor); the editor collects them on its timer.

## 7. Testing

- Step integrator: sine at a known level → L_Aeq/L_Zeq correct; vibration RMS/peak per axis; user accumulators untouched; paused interval flagged.
- `Tests/RewSequenceTests.cpp`: JSON round-trip, validation, `Add positions…`, inherit flags.
- `Tests/RewSequencerTests.cpp` against a `FakeRewTransport`, which simulates REW state: settings store, measurement list, a busy flag, scripted errors. Cases:
  - happy path with N steps, checking the POST order (last-input before input) and that only non-inherited fields are sent;
  - completion detected by UUID;
  - multi-input count;
  - error → stop / skip / retry;
  - timeout → Cancel sent;
  - abort mid-sweep;
  - wait-for-user;
  - blocking restored and settings snapshot restored on success, on abort and on error;
  - REW unreachable at preflight;
  - 400 busy → retry.
- Manual test against real REW (Pro): 3-step L/R/L+R sequence, 2-position wait-for-user sequence, clipping abort, unplug-REW mid-run.

## 8. Implementation steps

| # | Step | Result |
|---|---|---|
| 1 | `RewTransport` + `HttpRewTransport`, `FakeRewTransport` for tests | GET/POST/PUT with status codes |
| 2 | `RewSequence` model, JSON, validation, persistence in `UIState` | unit tests |
| 3 | `RewSequencer` engine: preflight, configure, trigger, completion, errors, abort, restore | fake-REW tests |
| 4 | Window: table editing, toolbar, connection status, choice lists from REW, Tools menu entry | usable UI, offline editing |
| 5 | Run controls: progress, run log, wait-for-user banner, pause/abort, dry run | first real-REW run |
| 6 | Integration: step integrator (mic + vibration), run results CSV/JSON, log markers, `Save all`, Load/Save `.rewseq.json` | |
| 7 | Docs: `REW_SEQUENCER_GUIDE.md`, README, changelog in 3.6.0 | |

## 9. Decisions (2026-11-13)

| # | Question | Decision |
|---|---|---|
| Q1 | Per-step fields | §3 list **plus per-step protection** (clipping abort, SPL-limit abort with limit). Filtered SPL later. |
| Q2 | REW Pro | Available everywhere. If REW still rejects a trigger, show a clear message. |
| Q3 | Sequence storage | Separate `*.rewseq.json`, plus the last sequence in `UIState`. Not in the settings JSON. |
| Q4 | Run ownership | Processor. The run survives closing the window or editor. |
| Q5 | Drivers | Java **and** ASIO. |
| Q6 | SPLMeter values | Yes: acoustic **and vibration** values per step, measured and saved with the run (§6). |
| — | Branch | Built on `feature/vibration`; part of PR #1 and v3.6.0. |
