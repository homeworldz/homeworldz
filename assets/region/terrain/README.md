# Default Region Heightmap

`default-heightmap.jpg` is the user-supplied `Image-island3.jpg` free resource
selected for the initial HomeWorldz region terrain on 2026-07-14. Its original
creator and upstream package are not identified in the supplied files.
HomeWorldz records the stable `HomeWorldz Library` service identity as the
importing creator/uploader and preserves this source note rather than inventing
an author attribution.

`default-heightmap.raw` is a deterministic 256-by-256, eight-bit grayscale
terrain derived from the 512-by-512 rendered source image. The source is
flipped vertically to compensate for the viewer terrain coordinate convention.
Blue-dominant water pixels map to 18 metres, below the 20-metre region
waterline. Land maps to a gently scaled 23-to-40-metre range based on
luminance. A one-pixel Gaussian blur leaves the encoded result ranging from 18
to 37 metres. This avoids amplifying the rendered source image's texture and
shading into jagged terrain or mistaking its full 20-to-250 luminance range for
metre heights. Each raw byte is interpreted directly as terrain height in
metres. The source JPEG has SHA-256
`597f2532b19b243dfd2216d13d7d82936a968b5d89de4e2d01c082ed1981bc19`
and the raw output has SHA-256
`a854d2d2e5a7782495ad59ecb69a8f77455aef9af819f44b1ae85cd89c79cee6`.

Set `HOMEWORLDZ_REGION_TERRAIN_PATH` to another raw 65,536-byte heightmap to
override this development default. A missing or invalid file falls back to the
former flat 25-metre terrain.
