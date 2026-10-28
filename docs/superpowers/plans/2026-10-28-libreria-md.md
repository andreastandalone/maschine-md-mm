# Libreria Machinedrum (passo 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A LIBRERIA button in the combined MD-MM plug-in opens a full-window library that lists Machinedrum kits and patterns from pack files and lets Andrea audition them (samples included), restoring the machine on close.

**Architecture:** Pure byte-level pack parsing and dump rewriting live in `mdLib` (no JUCE, unit-tested on synthetic and real files). A JUCE-side scanner turns the Library folder (folders and zips) into packs. An audition session owns the machine snapshot and drives the existing `md::Device` user-SysEx import API plus small load commands. A JUCE view replaces the two machine panels while open, because the panels are native Metal views that would cover any sibling overlay.

**Tech Stack:** C++20, JUCE (`juce::ZipFile`, `juce::Component`), existing `md::Device` SysEx import (`mdsysexfile.h`, `mdsysextransfer.h`, `mddevice.h`).

**Spec:** `docs/superpowers/specs/2026-10-28-libreria-design.md` (Italian). Byte facts below were verified against the real packs in `_handoff/Kits`.

## Global Constraints

- Library folder: `<user documents>/Gearmulator Preview/Library` (sibling of `Machinedrum/` and `Monomachine/`); create it if missing.
- A pack = a directory under Library that directly contains `.syx` files, named by its relative path in upper case with ` / ` separators (e.g. `DEEP RHYTHMS / DUB1`), or a `.zip` under Library (each zip directory with `.syx` = one pack).
- Only MD (model byte `0x02`) kits/patterns appear in this step; MM items are parsed but hidden until passo 2. Globals and songs are never sent.
- Audition slots: kit 63, pattern 127.
- Closing the library (button, or closing the plug-in editor) without importing restores the snapshot. While the library is open, `CombinedProcessor::getStateInformation` returns the snapshot, not the audition state.
- Style: dark display look, light upper-case monospace text (`juce::Font::getDefaultMonospacedFontName()`), inverted selection. Colours: background `#0d0f0d`, text `#d8e6c8`, dividers `#3c423c`. Two columns: packs left, contents right; buttons bottom `SUONA KIT`, `SUONA PATTERN` (`IMPORTA` comes in passo 3); a status line above the buttons.
- Keyboard: Up/Down move within the focused column, Left/Right switch column, Enter = SUONA KIT on a kit or SUONA PATTERN on a pattern.
- SUONA only loads; it does not start the sequencer (deferred by agreement).
- Commit after each task on branch `dev` with the `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` trailer.

## Byte facts (verified)

- Elektron dump: `F0 00 20 3C <model> 00 <cmd> <ver> <rev> <pos> … chkHi chkLo lenHi lenLo F7`. Slot at `[9]`. Checksum = sum of bytes `[9 .. size-6]` `& 0x3FFF`, two 7-bit bytes MSB first at `[size-5]`,`[size-4]`. Length = `size-10`, same encoding at `[size-3]`,`[size-2]`.
- MD kit `0x52` (1233 bytes): name 16 ASCII bytes NUL-padded at `[0x0A..0x19]`; per-track machine IDs = 16 big-endian 32-bit values, Elektron 7-bit packed (one MSB byte + 7 data bytes per group), starting at `[0x1AA]`, 74 bytes.
- MD pattern `0x67` (5410 bytes): kit number at raw `[0xB5]` (< 64).
- Sample machine IDs: `0x80+n`, n 0..31 → sample slot n; n 48..63 → slot n-16. Others are not samples.
- SDS header `F0 7E dev 01 lo hi …` (21 bytes): sample slot = `b[4] | b[5]<<7`. Optional MD name message `F0 00 20 3C 02 00 73 <slot> c c c c F7` follows the header.
- Load commands: kit `F0 00 20 3C 02 00 58 kk F7`, pattern `F0 00 20 3C 02 00 57 pp F7`.
- `MD/Booster-Pack/SysEx/The Room.syx` ends with a truncated message: the scanner must keep the complete messages and drop the tail.

## Review Focus

- A pack whose kit uses sample slots with no matching SDS file in the pack: audition must still send the kit, and the status line names the missing slots (`CAMPIONI MANCANTI: 21 22`).
- A file that is not valid SysEx or is an OS update inside the Library: skipped, the list still loads.
- Pressing SUONA again while a sample transfer is running: the new request replaces the pending one after the current transfer ends; never two transfers at once.
- Closing the plug-in window with the library open: snapshot restored, and a DAW save during audition stores the snapshot.
- Library folder missing or empty: view shows `NESSUN PACK IN LIBRARY` and the folder path, no crash.

---

### Task 1: Pack content parser and dump rewriting (mdLib)

**Files:**
- Create: `source/elektron/md/mdLib/mdpacklibrary.h`, `source/elektron/md/mdLib/mdpacklibrary.cpp`
- Modify: `source/elektron/md/mdLib/CMakeLists.txt` (add sources)
- Test: `source/elektron/md/mdLibTest/mdPackLibraryTest.cpp`, register in `source/elektron/md/mdLibTest/CMakeLists.txt` like `mdUartRegisterTest` (LABELS `UnitTest;Library`)

**Interfaces:**
- Produces (namespace `md::packlib`):
  - `enum class ItemKind : uint8_t { Kit, Pattern };`
  - `struct Item { MachineModel model; ItemKind kind; uint8_t slot; std::string name; uint8_t kitSlot; std::vector<uint8_t> dump; std::vector<uint8_t> sampleSlots; };` (`name` trimmed; for patterns `name` = `PATTERN A01`-style label from slot: bank letter `A`+slot/16, number slot%16+1 two digits; `kitSlot` only for patterns; `sampleSlots` only for MD kits, sorted unique)
  - `struct Sample { uint8_t slot; std::vector<uint8_t> bytes; };` (SDS header, optional 0x73, all packets, contiguous)
  - `struct FileContents { std::vector<Item> items; std::vector<Sample> samples; };`
  - `FileContents readSyx(const std::vector<uint8_t>& _bytes);` (never throws; ignores invalid/truncated messages, songs, globals, other manufacturers)
  - `std::vector<uint8_t> retargetDump(std::vector<uint8_t> _dump, uint8_t _slot);` (sets `[9]`, recomputes checksum)
  - `std::vector<uint8_t> retargetMdPattern(std::vector<uint8_t> _dump, uint8_t _slot, uint8_t _kitSlot);` (also sets `[0xB5]`)
  - `std::vector<uint8_t> loadKitCommand(MachineModel, uint8_t _slot);` and `loadPatternCommand(MachineModel, uint8_t _slot);`

- [ ] **Step 1: Write failing tests** in `mdPackLibraryTest.cpp` (template: `mdJucePlugin/mdCombinedMidiRouterTest.cpp`, `expect(cond,msg)` + `std::exit(1)`):
  - `checksumRoundTrip`: build an MD kit dump of 1233 bytes with name `TEST KIT`, slot 5, machine IDs `0x80,0x81,0x1c,…` (helper packs 16×32-bit values into 74 bytes), valid checksum; `readSyx` returns one Kit, `slot==5`, `name=="TEST KIT"`, `sampleSlots=={0,1}`.
  - `retargetKeepsValid`: `retargetDump(kit, 63)` → `[9]==63` and `md::parseMidiSysexFile` on it returns `Valid`.
  - `retargetPattern`: 5410-byte pattern with `[0xB5]=3`, `retargetMdPattern(p, 127, 63)` → `[9]==127`, `[0xB5]==63`, `Valid`.
  - `truncatedTail`: kit dump followed by `F0 00 20 3C 02 00 52 04` (no F7) → one Kit.
  - `samplesGrouped`: SDS header slot 2 + 0x73 name + 2 data packets → one `Sample{slot 2}` whose bytes start with `F0 7E` and include the 0x73 message.
  - `loadCommands`: `loadKitCommand(Machinedrum, 63) == {F0,00,20,3C,02,00,58,3F,F7}`; pattern `57,7F`.
  - `realPacks` (returns 77 if `_handoff/Kits` missing, path via argv[1]): `MD909UW.syx` → 13 kits, 128 patterns, kits 0..6 `sampleSlots=={0,1,2,3}`; `09CH-SLOT1.syx` → one Sample slot 0; `The Room.syx` → items non-empty.
- [ ] **Step 2: Run, verify fail.** Configure a test build once: `cmake -S . -B build/test -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_TESTING=ON -Dgearmulator_SYNTH_ELEKTRON=ON -Dgearmulator_BUILD_JUCEPLUGIN=OFF` (plus the other `gearmulator_SYNTH_*=OFF` flags from `build/dev`), then `cmake --build build/test --target mdPackLibraryTest`. Expected: compile failure (header missing).
- [ ] **Step 3: Implement** `mdpacklibrary.h/.cpp`. Reuse `md::parseMidiSysexFile` per message where convenient; checksum code mirrors `mdsysexfile.h:142-146`.
- [ ] **Step 4: Run** `ctest --test-dir build/test -R '^mdPackLibraryTest$' --output-on-failure` (pass `_handoff/Kits` as argument in `add_test`). Expected: PASS.
- [ ] **Step 5: Commit** `Add MD pack parsing and dump retargeting`.

### Task 2: Library folder scanner (JUCE)

**Files:**
- Create: `source/elektron/md/mdJucePlugin/mdLibraryScanner.h`, `.cpp`
- Modify: `source/elektron/md/mdJucePlugin/CMakeLists.txt` (add to the plug-in sources list near `mdCombinedEditor.cpp`)
- Test: `source/elektron/md/mdJucePlugin/mdLibraryScannerTest.cpp` (links `mdLib` + `juce::juce_core`; register like `mdCombinedMidiRouterTest`)

**Interfaces:**
- Consumes: `md::packlib::readSyx`, `Item`, `Sample` (Task 1).
- Produces (namespace `mdJucePlugin::library`):
  - `struct Pack { juce::String name; std::vector<md::packlib::Item> items; std::vector<md::packlib::Sample> samples; };`
  - `juce::File libraryFolder();` (creates it)
  - `std::vector<Pack> scan(const juce::File& _root, md::MachineModel _model);` (sorted by name; items: kits first by slot, then patterns by slot; packs with no items for `_model` omitted; zips read with `juce::ZipFile`; files over 8 MiB skipped)

- [ ] **Step 1: Failing test** `scanFolderAndZip`: temp dir with `A/Sub/kit.syx` (synthetic kit from Task 1 helpers), `B.zip` containing `x/pat.syx`, `junk.syx` (random bytes) at root → `scan(root, Machinedrum)` names `{"A / SUB", "B / X"}`, junk ignored; MM model → empty.
- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, verify pass.**
- [ ] **Step 5: Commit** `Scan the pack library folder`.

### Task 3: Audition session (snapshot, samples, load)

**Files:**
- Create: `source/elektron/md/mdJucePlugin/mdLibrarySession.h`, `.cpp`
- Modify: `source/elektron/md/mdJucePlugin/mdCombinedProcessor.h/.cpp` (session ownership, state override)

**Interfaces:**
- Consumes: `library::Pack`, `md::packlib::*`; `md::Device` import API (`beginUserSysexImport`, `startUserSysexImport(ticket, prepared, true)`, `userSysexImportProgress`, `resumeUserSysexImport`, `retireUserSysexImport`) as used in `mdEditor.cpp:1468-1700`; `pluginLib::Processor::addMidiEvent` with `synthLib::MidiEventSource::Editor` for load commands (`mdEditor.cpp:1043-1052`).
- Produces (class `mdJucePlugin::LibrarySession`, message thread only):
  - `LibrarySession(AudioPluginAudioProcessor& _machine);` takes the snapshot (`getStateInformation`) in the constructor.
  - `void playKit(const library::Pack&, size_t _itemIndex);` → queue: SDS samples for `sampleSlots` not already sent this session, then `retargetDump(kit, 63)`, then `loadKitCommand(63)`.
  - `void playPattern(const library::Pack&, size_t _itemIndex);` → samples of its kit, kit to 63, `retargetMdPattern(p, 127, 63)`, then `loadPatternCommand(127)`.
  - `void service();` (call from a 30 ms timer: advances the queue, resumes `WaitingForReceiveMode`, retires terminal transfers)
  - `juce::String status() const;` (`INVIO CAMPIONI 2/4`, `KIT CARICATO`, `CAMPIONI MANCANTI: …`, errors)
  - `void restore();` (`setStateInformation(snapshot)`; idempotent)
  - `const juce::MemoryBlock& snapshot() const;`
- `CombinedProcessor`: `void openLibrarySession(); void closeLibrarySession();` (`close` calls `restore()` and destroys), `LibrarySession* librarySession();`. `getStateInformation` writes the MD child blob from `session->snapshot()` while a session exists.

- [ ] **Step 1: Implement** the session; one transfer at a time, a new play request replaces any not-yet-started queue entries.
- [ ] **Step 2: Build** `cmake --build build/dev --target mdmmJucePlugin_VST3` → succeeds.
- [ ] **Step 3: Commit** `Add library audition session`. (Behaviour is verified end to end in Task 5; firmware is not available to unit tests.)

### Task 4: Library view and LIBRERIA button

**Files:**
- Create: `source/elektron/md/mdJucePlugin/mdLibraryView.h`, `.cpp`
- Modify: `source/elektron/md/mdJucePlugin/mdCombinedEditor.h/.cpp`

**Interfaces:**
- Consumes: `library::scan`, `libraryFolder`, `CombinedProcessor::openLibrarySession/closeLibrarySession/librarySession`.
- Produces: `class LibraryView : public juce::Component, juce::Timer` with `explicit LibraryView(CombinedProcessor&)`; the editor gets a `juce::TextButton m_libraryButton` labelled `LIBRERIA` in a 28 px bar above the machines (`resized()` shrinks the machines' area by that bar).
- Behaviour: opening scans the folder, opens the session, hides both child editors (`setVisible(false)`) and shows the view over the machines' area; closing (button again, or `~CombinedEditor`) closes the session and shows the children. MM tab visible but disabled (`MM: PROSSIMAMENTE`).

- [ ] **Step 1: Implement** view per Global Constraints style and keyboard rules, status line from `session->status()`, empty-folder message with path.
- [ ] **Step 2: Build** VST3, AU, Standalone targets → succeed.
- [ ] **Step 3: Commit** `Add library view and button`.

### Task 5: Manual verification with Andrea

- [ ] **Step 1:** Install VST3 and AU (`ditto` into `~/Library/Audio/Plug-Ins/...`), copy `_handoff/Kits/MD/*` folders into the Library folder.
- [ ] **Step 2:** Andrea checks in Ableton: list shows `DEEP RHYTHMS / DUB1`, `MD808`, `MD909`; SUONA KIT on an MD909 kit sends 4 samples then sounds; SUONA PATTERN plays the pack groove; closing restores the previous kit, pattern and samples; measure and report restore silence duration.
- [ ] **Step 3:** Fix findings, then commit and push `dev`.
