# Pokémon cries

Verified on 2026-09-12 against the [PokéAPI Pokémon endpoint](https://pokeapi.co/docs/v2#pokemon)
and the upstream [PokéAPI cry repository](https://github.com/PokeAPI/cries).
The repository documents that cry filenames map to Pokémon IDs, and that its
audio was obtained from Pokémon Showdown and Veekun.

`manifest.json` records the ID, name, API URL, immutable source URL and SHA-256
for every asset. All 15 records were checked for matching `id`, `name`,
`is_default` and `cries.latest`. Standard forms use their species IDs; regional
or other alternate-form IDs must not be substituted. The selected variant is
`latest`, as supplied by that repository, pinned to commit
`ef687b18f0ce17169b4b4c09175819f7ade92f0f`. This is a consistent game-cry
collection; it is not a claim that every title or animated adaptation uses
the same vocalization.

The `.ogg` downloads are retained unchanged. Pikachu's upstream `25.ogg`
actually contains MP3 audio; FFmpeg detects its content rather than trusting
the extension. All files were decoded with FFmpeg 7.1 to 16,000 Hz, mono,
signed 16-bit little-endian PCM. No speed, pitch, trimming or normalization
effect is applied. Resampling reduces bandwidth for this small speaker.
The stored PCM hashes and sample counts make offline validation reproducible.

`tools/generate_cries.py --ffmpeg /path/to/ffmpeg` generates the PCM and C table.
`tools/generate_cries.py --check` validates the roster, source/PCM hashes,
sample counts, non-silence, flash budget and exact generated C table without
network access or a decoder. Assets total 426,000 PCM bytes. The firmware uses
only the C table in flash, not the source Ogg/MP3 or PCM files at runtime.

Audio and Pokémon characters remain the property of their respective rights
holders. The community repository is a provenance source, not a grant of
rights to Pokémon audio.
