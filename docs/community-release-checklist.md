# Community Release Checklist

The current revision is a controlled prototype, not a redistribution-ready
community release. Complete every blocking item below before publishing source
archives, firmware binaries, screenshots, videos, or demo builds.

This checklist is an engineering release gate, not legal advice.

## 1. Replace restricted content

- [ ] Replace Bulbasaur, Charmander, and Squirtle with original City Spirits
      names, descriptions, stats, identifiers, and designs.
- [ ] Replace the three PNG files in `demo/assets/` with original artwork whose
      license permits the intended distribution.
- [ ] Regenerate all firmware sprite arrays from the approved original artwork.
- [ ] Replace the runtime Poke Ball drawing and any confusingly similar brand
      or interface elements with original visual language.
- [ ] Remove Pokemon and Pokedex branding from executable names, logs, build
      targets, documentation, metadata, repository name, and social copy.
- [ ] Recreate screenshots, cover images, videos, and firmware binaries after
      the replacement. Do not reuse media rendered from restricted content.

Use this audit as a starting point:

```sh
rg -n -i \
  'pokemon|pokémon|pokedex|pokédex|bulbasaur|charmander|squirtle|poke ball'
git ls-files demo/assets main/assets release-assets
```

Review every match manually. A zero-result text scan is necessary but not
sufficient because images and compiled firmware may still contain restricted
material.

## 2. Verify rights and attribution

- [ ] Record the author, source, and license for every shipped image, sound,
      font, video, and hardware design.
- [ ] Obtain explicit contributor permission for all original contributions.
- [ ] Preserve the FoloToy MIT notice for reused Bubu Passport foundation code.
- [ ] Generate dependency notices from the exact versions in
      `dependencies.lock` and include them with binary distributions.
- [ ] Have a qualified lawyer review the release if Pokemon-related content or
      confusingly similar branding will remain.

## 3. Package the release

- [ ] Include `LICENSE` and `THIRD_PARTY_NOTICES.md` in source archives.
- [ ] Include applicable dependency license notices with firmware binaries.
- [ ] Build from a clean, tagged commit rather than a local artifact.
- [ ] Confirm no credentials, raw SSIDs, BSSIDs, public BLE addresses, precise
      locations, device backups, or private logs are tracked.
- [ ] Run `./tools/test-host.sh`.
- [ ] Record firmware build and physical-device validation separately.

## 4. Final publication gate

Release only when:

1. no restricted prototype content is present in the distributed package;
2. every shipped asset has a documented redistribution right;
3. the package carries all required copyright and license notices; and
4. the public name and presentation do not imply official affiliation.
