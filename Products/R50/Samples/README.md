# R50 Samples

R50 supports single-sample and multisampled instruments. The engine and factory
loader carry multiple zones: a note selects the matching key/velocity zone,
starts that zone's WAV at its configured start position, applies the zone's
root and fine tuning, and follows its one-shot or loop mode.

Drop an individual WAV file in here and it becomes a single-zone instrument.
Group several WAV files as `zones` of one entry in
`factory_samples/factory_samples.json` to make a multisampled instrument. The
files are part of the repository and are built into the plugin, so working on
a sample is the same loop as working on code: edit, rebuild, run.

## Directory instruments

The low-friction multisample layout is one immediate child directory per
instrument:

```text
factory_samples/
  factory_samples.json       # existing ordered catalog remains supported
  Flute/
    instrument.json          # optional; required for explicit/complex maps
    flute-C3.wav
    flute-F3.wav
    flute-A3.wav
    flute-C4.wav
```

A directory without `instrument.json` is an **automatic root-zoned
instrument**. Every WAV must have a valid `smpl` unity note. The loader sorts
the files by `(rootKey, filename)` and places the key boundary halfway between
adjacent roots. The lower zone wins an exact half-semitone tie. The first and
last zones extend to MIDI keys 0 and 127. This makes adding a folder of properly
tagged samples enough to create an instrument without encoding note names in
filenames.

Automatic zoning is intentionally narrow:

- one WAV per root and one velocity layer;
- no recursive directory scan;
- root, fine tuning and loop behavior come from each WAV's `smpl` chunk;
- duplicate roots, missing/invalid roots, unsupported loop types, unreadable
  WAVs, or more than the engine's region limit reject the whole directory;
- filenames affect only deterministic tie-breaking and diagnostics, never
  pitch.

Use `instrument.json` when the samples need explicit key ranges, velocity
layers, gaps, overlaps, metadata overrides, or a stable display name:

```json
{
  "schemaVersion": 1,
  "id": "factory.concert_flute",
  "name": "Concert Flute",
  "zones": [
    { "id": "c3", "file": "flute-C3.wav", "lowKey": 0,  "highKey": 50 },
    { "id": "f3", "file": "flute-F3.wav", "lowKey": 51, "highKey": 56 },
    { "id": "a3", "file": "flute-A3.wav", "lowKey": 57, "highKey": 62 },
    { "id": "c4", "file": "flute-C4.wav", "lowKey": 63, "highKey": 127 }
  ]
}
```

Zone fields omitted from `instrument.json` inherit root, fine tuning and loop
metadata from the WAV. Explicit fields use the same override rules as
`factory_samples.json`. Paths must be relative filenames contained directly in
the instrument directory; absolute paths and `..` are rejected.

For deterministic discovery, the existing top-level manifest is loaded first,
then immediate child instrument directories are appended in bytewise directory
name order. Hidden directories are ignored. An `id` must be globally unique.
Automatic directories receive an ID derived from their relative directory path
for now; factory content intended for presets should add `instrument.json` and
an explicit ID. AU automation continues to use the numeric selector, while
saved documents and user presets carry an `R50SampleAssetIDs` sidecar and
resolve those persistent IDs back to runtime indices when restored.

## Names and asset IDs

The synth's sample selector names an **instrument**, not each underlying WAV.
The mapping has three distinct values:

| Purpose | Flute example | Source |
|---|---|---|
| Browser label | `Concert Flute` | `instrument.json.name`; otherwise directory name |
| Persistent instrument ID | `factory.concert_flute` | `instrument.json.id`; stored by presets |
| Zone asset ID | `factory.concert_flute/c4` | instrument ID + zone `id`; resolves to `Flute C4 -45.wav` |

The WAV filename is storage, not the user-facing asset name. A preset stores
only `factory.concert_flute`; note-on region selection resolves that instrument
to a zone such as `factory.concert_flute/c4`, and the loader has already
resolved that zone asset to an immutable sample slot. Runtime DSP never
compares names or file paths.

Explicit `id` values use lowercase ASCII letters, digits, dots, hyphens, and
underscores. Instrument IDs are globally unique. Zone IDs are unique within
their instrument. They are opaque identifiers: changing a display name or WAV
filename does not change sound identity as long as the IDs and file mapping in
`instrument.json` remain unchanged.

Defaults exist for quick drop-in use:

- automatic directory browser label: the directory basename;
- automatic instrument ID: `factory.auto.` plus a canonical escaped relative
  directory path;
- automatic zone ID: a canonical escaped WAV filename stem;
- loose-WAV browser label: its filename stem;
- loose-WAV ID: `factory.loose.` plus its canonical escaped filename.

These derived IDs intentionally change when a directory or file is renamed.
They are convenient for auditioning, but released factory content and anything
referenced by a preset must use explicit IDs. Duplicate IDs, two filenames
which canonicalize to the same derived ID, or an ID mapped to more than one
file reject that instrument with a diagnostic.

The existing top-level `factory_samples.json` follows the same model during
migration: its instrument `name` remains the browser label, it gains an
explicit instrument `id`, and each zone gains an `id`. Until those fields have
been added, the compatibility loader derives temporary IDs from manifest order
and filename; released presets should use entries with explicit IDs.

Loose files are loaded in filename order and named in the browser by their
filename without the extension. Their derived IDs change when renamed.
Automation still exposes a numeric selector, but saved state and current
factory presets resolve persistent IDs before playback; released sounds must
therefore reference explicitly identified directory instruments rather than a
loose file's position.

## Current factory multisamples

The factory tree packages 23 directory instruments plus 11 loose manifest
instruments (Glass Pad, Spectrum 1–9, Anvil):

| Directory | Display name | Persistent ID | Zones |
|---|---|---|---:|
| `AcousticBass` | Acoustic Bass | `factory.acoustic_bass` | 9 |
| `AhhChoir` | Ahh Choir | `factory.ahh_choir` | 15 |
| `BrassSection` | Brass Section | `factory.brass_section` | 10 |
| `Cello` | Cello | `factory.cello` | 16 |
| `ChurchOrgan` | Church Organ | `factory.church_organ` | 16 |
| `Drums` | Drum Kit | `factory.drum_kit` | 16 |
| `FrenchHorn` | French Horn | `factory.french_horn` | 7 |
| `GrandPiano` | Grand Piano | `factory.piano_multisample` | 16 |
| `Harp` | Harp | `factory.harp` | 14 |
| `NylonGuitar` | Nylon Guitar | `factory.nylon_guitar_multisample` | 9 |
| `Oboe` | Oboe | `factory.oboe` | 7 |
| `OhhChoir` | Ohh Choir | `factory.ooh_choir` | 7 |
| `PanFlute` | Pan Flute | `factory.pan_flute` | 8 |
| `PercussiveOrgan` | Percussive Organ | `factory.percussive_organ` | 16 |
| `PickedBass` | Picked Bass | `factory.picked_bass` | 9 |
| `PizzicatoSection` | Pizzicato Section | `factory.pizzicato_celli` | 14 |
| `SlapBass` | Slap Bass | `factory.slap_bass_multisample` | 9 |
| `SteelDrum` | Steel Drum | `factory.steel_drum` | 4 |
| `SteelGuitar` | Steel Guitar | `factory.acoustic_guitar` | 9 |
| `Strings` | Strings | `factory.strings` | 16 |
| `Trumpet` | Trumpet | `factory.trumpet` | 6 |
| `Vibraphone` | Vibraphone | `factory.vibraphone` | 13 |
| `Violin` | Violin | `factory.solo_violin` | 14 |

That is 260 directory zones: 244 looped sustains and the 16 one-shot drums,
271 sample slots including the loose manifest. The test suite verifies that
every directory ingests atomically, every MIDI key resolves, every persistent
ID resolves, playback is finite, held notes remain in their declared loops,
and no shipped loop joins with a gross discontinuity.

Every `instrument.json` states its roots explicitly, and those roots are
**measured, not read from the filenames**: every WAV in these banks carries a
useless `smpl` root of 60, and the filename conventions vary per folder (most
are written one octave above sounding pitch, `GrandPiano` and the organs
differ, the bass folders are string+fret labels — "E2" is the E string at
fret 2, F#1 — and a handful of files are plainly mislabeled, e.g. `Harp
B5.wav` sounds B4). Re-deriving a root from a filename is therefore never
safe; correct the JSON instead.

Files on disk that are deliberately not mapped: the guitars' `Extension.wav`,
the `Strings` right-channel files (the engine is mono per zone; the L side is
mapped), 70 of the 86 drum hits (the kit maps a GM-style 16 on keys 36–51,
edge zones stretched across the keyboard), and a few zones dropped from
folders that exceed the 16-region limit or measured badly out of tune
(`Cello C#7`, the piano's top tinkle octave, duplicate-pitch organ pipes).

Preset documents reference samples **only** through a `partial -> persistent
instrument ID` map; `R50AudioUnit` resolves it after the ordinary numeric
parameter reset, so no document's numeric selector value can repoint a sound
when discovery order changes. `factory.arco_basses` and `factory.concert_flute`
were retired with the old sample set; presets now name `factory.cello` and
`factory.pan_flute`. The retired generated instruments (warm pad, the attack
transients, gong, …) map to their closest sampled successors in the preset
vocabulary — see `Instrument` in `R50FactoryPresets.swift`.

WAV ingestion:

- 16- and 24-bit integer and 32-bit float PCM, any sample rate. Stereo is
  summed to mono, because a Partial plays one channel.
- A RIFF `smpl` chunk, if present, for MIDI unity note (root), MIDI pitch
  fraction (fine-tuning adjustment), and the first valid loop's start, inclusive
  end, and type. R50 converts the loop end to its internal exclusive convention.
  Forward and alternating loops map to R50's forward and ping-pong modes.
- Without a `smpl` chunk the root key is detected from the audio, and anything
  under half a second is treated as a one-shot while anything longer loops over
  its whole length.

Detection is good but not infallible on unpitched material; the browser's ROOT
column shows what was decided and lets you correct it.

For a manifest zone, values stated in JSON override the WAV metadata. Omitted
`rootKey`, `tuneCents`, `loop`, `loopStart`, `loopEnd`, and `loopMode` inherit
the decoded `smpl` values where available. Key and velocity bounds still belong
to the zone (`lowKey`, `highKey`, and, when implemented, velocity bounds);
`smpl` does not contain a keyboard map.

At note-on, region selection is performed once and the selected immutable
sample stays attached to that voice until it ends. Playback begins at the
Partial's sample-start position. A one-shot stops at the sample end; a sustain
wraps or reverses between its loop points using loop-aware interpolation.
