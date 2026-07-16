# Bundled Region Heightmaps

HomeWorldz terrain image import follows the OpenSimulator and Halcyon
`GenericSystemDrawing` convention: a lossless image is flipped vertically into
terrain coordinates and each pixel's HSL lightness is multiplied by 128
metres. Imported images must exactly match the region dimensions. HomeWorldz
accepts PNG and intentionally rejects JPEG because lossy compression artifacts
become terrain height spikes.

`plateau-square.png` and `plateau-round.png` are deterministic, project-created
256-by-256 grayscale heightmaps. Both have an 18-metre seabed, a smooth shore
transition through the standard 20-metre waterline, and a calm 22-metre
plateau. The default square shoreline is approximately 250 by 250 metres, with
slightly softened corners. The separate alternate round shoreline is
approximately 200 metres in diameter.
Their corresponding `.raw` files are the current region service's rounded
eight-bit metre representation. Regenerate all four files with:

```cmd
go run ./grid/cmd/generate-default-terrain
```

`island3-smoothed.raw` is the earlier experimental Island 3 terrain and remains
available as an alternate raw heightmap. `sources/island3-preview.png` is the
user-supplied PNG conversion of the `Image-island3` free resource from which
that terrain was authored. It is retained only for provenance and visual
reference; its rendered water, ground texture, lighting, and pre-existing
compression artifacts mean it is not a terrain heightmap merely because its
container is now lossless PNG. Its original creator and upstream package are
not identified in the supplied files. HomeWorldz records the stable
`HomeWorldz Library` service identity as its importing creator/uploader rather
than inventing an author attribution. The source PNG has SHA-256
`f13dd19bf0c0be1cd9deb84fbb990ca3e6f8a219cc2aa06d6727eea946ca6acb`.

Set `region.terrain_path` in `region.ini` to another raw 65,536-byte heightmap
to override the development default. A missing or invalid file falls back to
the former flat 25-metre terrain.
