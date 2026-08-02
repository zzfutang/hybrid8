# R50 factory presets

One JSON patch document per factory preset, in bank order by filename. The
whole directory is bundled into the AUv3, and when it holds any documents it
**is** the factory bank; the Swift recipes in `R50FactoryPresets.swift` are
the origin of the content and the fallback when it is empty. Loading is all
or nothing — one unreadable document falls back to the recipes rather than
shipping a hole where a preset number used to be.

A document carries `schemaVersion`, the display `name`, every parameter by
its tree keyPath, and a `sampleAssets` map of partial index to persistent
sample instrument ID (samples are never referenced by library index). Values
missing from a document stay at their defaults; unknown keys are reported on
import rather than silently ignored, so a typo cannot quietly shape a
different sound.

Workflow:

- edit a factory patch in the app, export it with the browser's EXP button,
  and replace its file here (keep the ordering prefix); or
- change the Swift recipes and regenerate the whole set with
  `scripts/export-r50-presets.sh` (requires the current build installed via
  `scripts/install-r50.sh` **without** these files bundled, or with files
  matching the recipes — the exporter dumps whatever bank the installed AU
  reports).

Rebuild + install after either, since the appex bundles this directory.
