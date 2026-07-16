# Default Region Map Tile

`00000000-0000-1111-9999-000000000100.j2c` is the initial HomeWorldz map
tile. It is a deterministic color rendering of the project-created
`terrain/plateau-square.png` heightmap: blue below the standard 20-metre
waterline, a narrow sand transition, and green plateau land.

The Region imports and registers this JPEG-2000 asset like other packaged
assets and advertises its stable UUID in `MapBlockReply`. It prevents viewers
from displaying unrelated cached tiles for null map-image UUIDs. All Regions
temporarily share this image; later terrain-aware map generation will assign a
distinct regenerated asset to every Region.

The source heightmap and this derived tile are project-created HomeWorldz
assets. Regeneration requires ImageMagick with JPEG-2000 support and maps the
heightmap's 36–44 grayscale range to the documented water, shore, and land
palette.
