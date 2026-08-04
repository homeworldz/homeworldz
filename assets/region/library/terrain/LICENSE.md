# Default Terrain Texture License

Two unrelated sets live here. The four **PNGs are the ground every region
renders**; the four **JPEG2000 files are Library inventory content** and are no
longer used as terrain layers.

## The terrain layers (CC0 1.0, public domain)

Lowest elevation to highest:

- `e0a13caa-d4dc-4efe-8605-bfbf8411177b.png` — layer 1, dirt — ambientCG `Ground079L`
- `150cbea9-6ecf-4d31-aeae-0fddbda311b5.png` — layer 2, grass — ambientCG `Grass005`
- `97a0e146-6773-4d69-8cf9-263758a430ca.png` — layer 3, mountain — ambientCG `Rock060`
- `d992da50-328f-4052-89e5-dc08a1b67a2b.png` — layer 4, rock — ambientCG `Rock002`

Each is the `Color` map of its 1K PNG set from <https://ambientcg.com>, released
under Creative Commons CC0 1.0 Universal:
<https://creativecommons.org/publicdomain/zero/1.0/>. Unmodified — copied and
renamed to their asset ids, nothing else. Everything on ambientCG is CC0.

**CC0 means no attribution is required and no share-alike follows.** That is the
point of choosing it: the grid's default ground can be redistributed inside a
server anyone runs, with no obligation travelling with it. The attribution above
is courtesy, not a condition.

Wrap continuity was measured rather than assumed. Each tile's wrap edge differs
from its neighbouring column by this multiple of a typical interior step, where
1.0 would be a perfect tile and a non-tiling texture scores 5 to 20:

| layer | horizontal | vertical |
| --- | --- | --- |
| 1 dirt (Ground079L) | 1.25 | 1.12 |
| 2 grass (Grass005) | 1.16 | 1.19 |
| 3 mountain (Rock060) | 1.11 | 0.97 |
| 4 rock (Rock002) | 1.35 | 1.13 |

All four are 1024x1024. Square matters: the terrain shader maps textures to
world space at a fixed scale, so a 1024x512 texture repeats twice as often on one
axis and reads as stretched. Two candidates were rejected for that alone
(`Rock041` and `Rock056`, both 1024x512), which is worth recording because
nothing about their appearance suggested it.

1024 is also the largest usable size today, not merely a reasonable one: it is
Firestorm's texture ceiling, and the `j2c-texture` rendition encodes at the
source resolution without resampling, so a larger canonical would derive a
JPEG2000 no viewer would accept.

## What was here before

The previous four layers were generative-AI upscales of the Second Life viewer
artwork below, and Creative Commons Attribution-ShareAlike 3.0 followed them as
adaptations — so the grid's default ground carried a share-alike obligation.
Replacing them with CC0 art removed it. Their asset ids are dead and nothing
references them.

## The Library JPEG2000 textures (CC BY-SA 3.0)

Still shipped, still referenced — as **Library inventory items**, by
`grid/internal/inventory/library.go`, not as terrain layers:

- `b8d3965a-ad78-bf43-699b-bff8eca6c975.j2c` (`Terrain Dirt`)
- `abb783e6-3e93-26c0-248a-247666855da3.j2c` (`Terrain Grass`)
- `179cdabd-398a-9b6b-1391-4dc333ba321f.j2c` (`Terrain Mountain`)
- `beb169c7-11ea-fff2-efe5-0f24dc881df2.j2c` (`Terrain Rock`)

These four are unmodified Second Life Viewer artwork from the Halcyon
`TexturesAssetSet`, 128x128 each. Homeworldz renamed the source files to their
existing asset UUIDs but did not change their contents.

Second Life(TM) Viewer Artwork. Copyright (C) 2008 Linden Research, Inc.

Linden Research, Inc. licenses these works under the Creative Commons
Attribution-ShareAlike 3.0 License:
<https://creativecommons.org/licenses/by-sa/3.0/legalcode>.

Linden Lab trademarks remain subject to its trademark policy.
