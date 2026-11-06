# Maschine MD-MM manual

## Library

Click **LIBRARY**. Packs go in `~/Documents/Gearmulator Preview/Library`: each
folder or zip with `.syx` files in it is one pack.

- **PLAY KIT** / **PLAY PATTERN**: audition in the last kit and pattern slot.
  Closing the library undoes it and restarts the machine.
- **IMPORT**: copies the selected kits and patterns, with their kits and MD
  samples, into free slots, then plays the first imported pattern.

## MIDI

The **MIDI** button switches between:

- **FOCUS**: MIDI goes to the machine you clicked last.
- **CHANNELS**:

| Channel | Goes to |
| --- | --- |
| 1 | MD notes: C1 to D3, white keys, play tracks 1-16 |
| 1-4 | MD parameter CCs, four tracks per channel |
| 5-10 | MM tracks 1-6 |
| 11 | MM selected track |

MD notes from E3 up select patterns. The note map is set on the MD in
GLOBAL > MIDI > MAP EDITOR.

## Outputs

Besides the main mix: **Machinedrum A-F** (mono) and **Main**, **Monomachine
Main**, **C-D** and **E-F** (stereo).

- MD: GLOBAL ([FUNCTION] + [PATTERN/SONG]) > ROUTING.
- MM: KIT > EDIT KIT, MIX column, OUT BUS.

In Ableton, set an audio track's **Audio From** to the plug-in and pick the
output, monitoring **In**.

## Panels

- Click a machine to give it the MIDI focus.
- Shift-click buttons to hold them (chords, p-locks).
- Click a secondary function's label instead of FUNCTION + button.
- Alt/Option-click an encoder to press it, Alt/Option-drag to press and turn.
- Drag and wheel sensitivity are in the settings.

## SysEx

Standalone app, **File** menu:

- **Load SysEx File to Machinedrum / Monomachine...**, then follow the prompt.
- **Save ... SysEx Dump...**, send from the machine's SYSEX SEND screen, then
  **Finish Saving SysEx Dump**.
- **Factory Reset** restores a machine's factory data.

## Maschine MK3

The left screen shows the MD, the right the MM. Display buttons **1** and **5**
choose which one the pads, knobs and buttons control. The MK3 firmware is not
touched.

| MK3 control or shortcut | Action in Maschine MD-MM |
| --- | --- |
| Display button 1 / 5 | Focus MD / MM. |
| Display button 2 / 6 | Advance the MD synthesis/effects/routing page / MM data page. |
| Display button 3 / 7 | Advance that instrument's `1:4`-`4:4` scale/page selector. |
| Display button 4 / 8 | Toggle MD Classic/Extended mode / MM trig-select mode. |
| Eight knobs below the screens | Turn DATA ENTRY encoders A-H. |
| NOTE REPEAT + touch a knob | Press that DATA ENTRY encoder while touched; release to let go. With a trig held, this toggles its parameter lock. |
| 5D encoder turn | Turn MD SOUND SELECTION (also used on its tempo screen) or MM LEVEL. |
| 5D encoder press / directional buttons | ENTER / navigate up, right, down and left. |
| Dedicated `<` / `>` buttons | Browse edit pages backward / forward. |
| PAD MODE + pad | Select MD track 1-16 or MM track 1-6. |
| MUTE + pad | Toggle mute for MD track 1-16 or MM track 1-6. |
| Pad 1-16 | Press the corresponding trig/step; when a pattern bank is held, select that pattern slot. |
| Group A-H + pad | Select a pattern in bank A-H. A-D and E-H also switch the instrument's bank group as needed. |
| SHIFT | Hold the instrument's FUNCTION key for its firmware shortcuts. |
| SHIFT + PLAY / STOP | Start / stop **both** instruments together. |
| PLAY + RECORD | Send the instrument's real-time recording gesture. |
| ERASE / DUPLICATE | Send FUNCTION + PLAY (clear) / FUNCTION + STOP (paste) to the focused instrument. |
| PLAY / RECORD / STOP | Use the focused instrument's transport controls. |
| MIXER + knob 1 / knob 8 | Adjust the selected track's LEVEL/DATA / the app's master output level for the focused instrument. |
| SAMPLING + turn 5D encoder | Cycle the MK3 lighting effects. |
| SHIFT + SAMPLING | Toggle decorative lighting on the MK3. |
| TEMPO | Open the instrument's tempo control. |
| STEP | Send SCALE on either instrument. |
| SCENE | Send SONG ENABLE on MM. |
| PATTERN or ARRANGER | Send PATTERN-SONG on MD. |
| PLUG-IN or CHANNEL | Send SYNTHESIS-EFFECTS-ROUTING on MD. |
| BROWSER | Send KIT on MD, or KIT/SONG SETUP on MM. |
| SELECT or PITCH / MOD | Send ENTER or YES / EXIT or NO. |

## Building

Needs Xcode command line tools, CMake, Python 3 and Git.

```sh
git clone --recurse-submodules https://github.com/andreastandalone/maschine-md-mm.git
cd maschine-md-mm
GEARMULATOR_REQUIRE_FIRMWARE_TESTS=0 ./scripts/macos/build_mdmm.sh
```

The universal ZIP lands in `artifacts/macos-mdmm-universal/`. To also run the
firmware tests, set `GEARMULATOR_MD_FIRMWARE_BIN` and `GEARMULATOR_MM_FIRMWARE_BIN`
to your firmware images. Signed DMGs: `scripts/macos/build_installer.sh` and
[doc/mdmm_release.md](mdmm_release.md).
