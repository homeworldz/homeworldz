-- A modern rendition for a legacy texture, closing an asymmetry in the pipeline.
--
-- Mesh converts both ways: a GLB canonical derives sl-mesh for viewers, and a
-- stored Second Life mesh derives gltf for the first-party client. Textures only
-- converted one way. A texture uploaded through Firestorm is canonically
-- JPEG2000 — and the viewer downsizes it to 1024 and encodes it lossily on the
-- way — while the first-party client refuses JPEG2000 by rule, so every texture
-- a viewer creates is invisible to it. That is the same gap that made five of
-- seven meshes unreadable before the gltf rendition existed (ADR 0033 M2).
--
-- png-texture is the reverse direction for images: decode the canonical
-- JPEG2000, re-encode as PNG. It recovers the pixels the viewer stored, not the
-- detail its uploader already discarded, which is a reason to prefer uploading
-- through the client rather than a defect in the conversion.

ALTER TABLE asset_renditions DROP CONSTRAINT asset_renditions_kind_check;
ALTER TABLE asset_renditions ADD CONSTRAINT asset_renditions_kind_check
    CHECK (kind IN ('gltf', 'sl-mesh', 'sl-material', 'j2c-texture', 'png-texture'));

ALTER TABLE rendition_jobs DROP CONSTRAINT rendition_jobs_kind_check;
ALTER TABLE rendition_jobs ADD CONSTRAINT rendition_jobs_kind_check
    CHECK (kind IN ('gltf', 'sl-mesh', 'sl-material', 'j2c-texture', 'png-texture'));
