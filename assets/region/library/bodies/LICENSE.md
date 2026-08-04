# Library Bodies — sources and licences

Two whole-body meshes, bundled as Library objects "Female" and "Male". They are
here **to be looked at**, not to be worn; see *What these are not* below.

| Asset | File | Source | Licence |
| --- | --- | --- | --- |
| Female body mesh | `3f1d9c84-6b52-4a17-9d0e-2c7a5b81e640.glb` | MakeHuman / MPFB 2.0.17 | CC0 1.0 |
| Male body mesh | `7c4b1e58-2d96-4f03-8a5e-1b9d7c60af32.glb` | MakeHuman / MPFB 2.0.17 | CC0 1.0 |
| Female object wrapper | `5a2e7f10-8c34-4b96-a1d7-6e3f92b45c08.object` | authored here | CC0 1.0 |
| Male object wrapper | `9e6a3d72-4f18-4c85-b23f-8d51e07ba946.object` | authored here | CC0 1.0 |

Generated with MakeHuman Plugin For Blender (MPFB) 2.0.17 under Blender 4.5, one
macro axis apart — gender 0.0 and 1.0 — with every other parameter left neutral,
because a default body should be unremarkable rather than a character. 36,972
triangles each.

## Why the licence chain is trusted here, and was not for Ruth2

The previous candidate (Ruth2/Roth2) was rejected because its rig was attributed
CC-BY-3.0 to Machinimatrix by a *downstream* file, and Machinimatrix's own current
licence page does not say so — it puts the source under GPL and states that parts
not explicitly marked GPL "may not be redistributed". Unverifiable rather than
contradicted, but unverifiable is enough when the rig is the part you need.

MakeHuman meets the standard Ruth2 failed:

- **The skeleton declares its own licence.** `data/rigs/default.mhskel` carries
  `license: "CC0"` and `copyright: "(c) 2020 Data Collection AB, Joel Palmius,
  Jonas Hauquier"` in the file's own metadata. Read from the upstream repository,
  not from a summary of it. The asset speaks for itself.
- **The project states a blanket CC0 over assets**, with the source separately
  under AGPL.
- **The AGPL is on the tool, not the output.** MakeHuman's `LICENSE.md` says
  outright that "no output from MakeHuman contains any trace of program logic"
  and that there is "no limitation on what you can do with this combined output".

One residual, recorded rather than smoothed over because omitting it is how the
last candidate went wrong: MakeHuman's `LICENSE.md` enumerates CC0 asset
categories — base mesh and proxies, targets and modifiers, textures, clothes,
poses and expressions — and **does not name skeletons**. The file's own
declaration and the project's blanket statement both cover them, so two sources
agree and the third is silent rather than contrary.

## What these are not

**Not wearable, and not a step toward wearing.** Second Life rigged mesh must be
weighted to the viewer's own skeleton by joint name — 71 named joints in
Firestorm's `character/avatar_skeleton.xml` (`mPelvis`, `mChest`, `mAnkleLeft`, …),
and the viewer knows no other. These bodies carry MakeHuman's 163 bones under
different names, so their weights would land on joints no viewer has.

The skinning was therefore **removed** rather than shipped: the GLBs here are
static bind-pose copies, with `skins`, `JOINTS_n` and `WEIGHTS_n` stripped from
the originals. A skinned glTF stores POSITION in bind pose, so what remains is
exactly the bind-pose geometry and nothing was reconstructed. That is also what
the region's own acceptance policy asks for while `rigged` is false.

Making a wearable body means re-rigging to the viewer skeleton, which is content
work in Blender rather than a code change. When one exists it will be a different
asset with different ids, because canonical bytes are never rewritten
([ADR 0026](../../../../docs/adr/0026-asset-durability.md)) — these two ids were
spent knowingly on a preview.

**Not the default avatar.** Nothing selects these; they are Library items an
estate manager can rez and look at.

## Provenance note

The exported GLBs carry no `asset.copyright`, so their licence lives in this file
rather than in the bytes. That is worth fixing at export for anything adopted
long-term: the reason the MakeHuman skeleton's claim is strong is precisely that
the file speaks for itself, and an asset in a content-addressed vault is
identified by its bytes alone.
