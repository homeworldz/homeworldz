# Default Region Heightmap

`default-heightmap.png` is the user-supplied `terrain-island5.png` free
resource selected for the initial HomeWorldz region terrain on 2026-07-14.
Its original creator and upstream package are not identified in the supplied
files. HomeWorldz records the stable `HomeWorldz Library` service identity as
the importing creator/uploader and preserves this source note rather than
inventing an author attribution.

`default-heightmap.raw` is a deterministic 256-by-256, eight-bit grayscale
conversion used by the C++ region service. Each byte is interpreted directly
as terrain height in metres. The source PNG has SHA-256
`01aae9528f68d66313e5f0641ddcedc0c00cc9a1d68b1bccdcf3283ff6dd5f83`.

Set `HOMEWORLDZ_REGION_TERRAIN_PATH` to another raw 65,536-byte heightmap to
override this development default. A missing or invalid file falls back to the
former flat 25-metre terrain.
