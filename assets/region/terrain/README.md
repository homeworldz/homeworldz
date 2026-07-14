# Default Region Heightmap

`default-heightmap.jpg` is the user-supplied `Image-island3.jpg` free resource
selected for the initial HomeWorldz region terrain on 2026-07-14. Its original
creator and upstream package are not identified in the supplied files.
HomeWorldz records the stable `HomeWorldz Library` service identity as the
importing creator/uploader and preserves this source note rather than inventing
an author attribution.

`default-heightmap.raw` is a deterministic 256-by-256, eight-bit grayscale
terrain derived from the 512-by-512 rendered source image. Blue-dominant water
pixels map to 18 metres, below the 20-metre region waterline. Land maps to a
gently scaled 23-to-40-metre range based on luminance, followed by a one-pixel
Gaussian blur; the encoded result ranges from 18 to 37 metres. This avoids
mistaking the source image's full 20-to-250 luminance range for metre heights.
Each raw byte is interpreted directly as terrain height in metres. The source
JPEG has SHA-256
`597f2532b19b243dfd2216d13d7d82936a968b5d89de4e2d01c082ed1981bc19`
and the raw output has SHA-256
`7aa0c5a5433e555ecb45496f737840e578fc90218e3aa31f3c34d41318a3f8f7`.

Set `HOMEWORLDZ_REGION_TERRAIN_PATH` to another raw 65,536-byte heightmap to
override this development default. A missing or invalid file falls back to the
former flat 25-metre terrain.
