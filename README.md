# Maschine MD-MM (andreastandalone fork)

Machinedrum and Monomachine emulation in one AU/VST3 plug-in, with Maschine MK3
support.

This is a fork of David Whiting's great work
[mo0kid/maschine-md-mm](https://github.com/mo0kid/maschine-md-mm), which builds on
[joelanders/gearmulator-md-mm](https://github.com/joelanders/gearmulator-md-mm)
and The Usual Suspects' [Gearmulator](https://github.com/dsp56300/gearmulator).
Everything those projects do is still here; this fork adds the features listed
below.

This fork is not affiliated with Elektron, Native Instruments, The Usual
Suspects or the upstream authors: please report its problems here, not to them.

[Downloads](https://github.com/andreastandalone/maschine-md-mm/releases) ·
[Report a bug](https://github.com/andreastandalone/maschine-md-mm/issues)

## Added in this fork

- The combined plug-in works in a DAW with host tempo and transport.
- Library: audition and import `.syx` kits and patterns, with MD samples.
- MIDI by channel: one channel per MM track, MD on channels 1-4.
- Separate audio outputs for both machines.
- Fixes for lost MD pattern dumps and for the Maschine agent crash.

## Install

macOS only. Download a [release](https://github.com/andreastandalone/maschine-md-mm/releases)
(versions end in `-as`, for example `0.2.0-as`) and copy the plug-ins to `~/Library/Audio/Plug-Ins/VST3` and `.../Components`.
Builds are not signed by Apple, so unblock them once:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Maschine MD-MM.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Maschine MD-MM.component"
```

Firmware is not included: on first launch, copy your own MD and MM images into
the folders shown. The plug-in replaces an installed upstream version and opens
its projects.

## Documentation

[doc/manual.md](doc/manual.md)

## Credits

Thanks to David Whiting, joelanders, The Usual Suspects and all Gearmulator
contributors. See [README.upstream.md](README.upstream.md).
Licence: [GPLv3](LICENSE.md).
