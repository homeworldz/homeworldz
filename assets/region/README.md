# Bundled region assets

`default-avatar/` contains the four base body-part assets, a shirt and pants,
and five source textures needed to render HomeWorldz's initial system outfit.
They were derived from the Halcyon simulator asset set without changing their
wearable parameters and named by viewer UUID so the region can import them
deterministically.

`library/textures/` contains the canonical Blank, Plywood, Transparent, and
Media texture assets expected by Second Life-compatible viewers and scripts.
They retain their standard viewer UUIDs. Library inventory records attribute
their import to the `HomeWorldz Library` service identity, independently of
their upstream artwork provenance.

The Halcyon source is distributed under the 3-clause BSD license. The texture
set's provenance notice says that some included textures derive from Second
Life Viewer Artwork, copyright Linden Research, Inc., and are licensed under
Creative Commons Attribution-ShareAlike 3.0. The upstream notices are retained
in `HALCYON-ASSET-LICENSES.txt`.

Source: <https://github.com/HalcyonGrid/halcyon>
