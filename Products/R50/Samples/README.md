# R50 Samples

Drop WAV files in here and they become instruments. They are part of the
repository and are built into the plugin, so working on a sample is the same
loop as working on code: edit, rebuild, run.

Files are loaded in filename order, after all generated factory content, and
they are named in the browser by their filename without the extension. Sorting
is what keeps instrument indices stable, and a preset stores an index — so
**renaming or removing a file shifts every sample after it**. Add new material
at the end of the alphabet, or accept that presets referring to later files
need repointing.

What is read:

- 16- and 24-bit integer and 32-bit float PCM, any sample rate. Stereo is
  summed to mono, because a Partial plays one channel.
- A `smpl` chunk, if present, for the root key and the loop. This is what most
  editors write, and it is how a sustain keeps a seamless loop.
- Without a `smpl` chunk the root key is detected from the audio, and anything
  under half a second is treated as a one-shot while anything longer loops over
  its whole length.

Detection is good but not infallible on unpitched material; the browser's ROOT
column shows what was decided and lets you correct it.
