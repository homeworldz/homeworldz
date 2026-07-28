// Package assetrefs extracts the asset references embedded inside asset
// bytes, so the durability keeper can gather an inventory item's whole
// reference closure (ADR 0026, "Completeness is transitive").
//
// Inventory-to-asset is 1:N. An object asset names the textures on its faces
// and the assets in its task inventory; a nested object is itself an asset
// with a closure of its own; wearables name textures; gestures name
// animations and sounds; notecards can embed items. These parsers run
// grid-side against the vault's own copy of the bytes — ADR 0028 forbids
// trusting a region-supplied reference list, and verifying one would mean
// parsing anyway.
//
// Parsers are deliberately lenient: they extract what they can recognize and
// never fail. Malformed content yields fewer references, not an error —
// refusing an inventory commit because a notecard is oddly formatted would
// hold user data hostage to a parser, while a missed reference only means
// that asset waits for another reference to make it durable.
package assetrefs

import (
	"encoding/hex"
	"encoding/json"
	"strings"
)

// Asset type codes, as viewers and regions use them.
const (
	TypeUnknown   = -1
	TypeTexture   = 0
	TypeSound     = 1
	TypeClothing  = 5
	TypeObject    = 6
	TypeNotecard  = 7
	TypeBodypart  = 13
	TypeAnimation = 20
	TypeGesture   = 21
)

// Reference is one asset named inside another asset's bytes. Type is the
// referenced asset's type when the format carries it, TypeUnknown otherwise.
type Reference struct {
	ID   string
	Type int
}

const zeroUUID = "00000000-0000-0000-0000-000000000000"

// Bearing reports whether an asset type can reference other assets — whether
// its bytes are worth parsing at all.
func Bearing(assetType int) bool {
	switch assetType {
	case TypeObject, TypeClothing, TypeBodypart, TypeGesture, TypeNotecard:
		return true
	}
	return false
}

// Gather extracts the references inside content, given its asset type.
// Non-bearing types, unknown types, and unparseable content yield nil.
func Gather(assetType int, content []byte) []Reference {
	switch assetType {
	case TypeObject:
		return objectReferences(content)
	case TypeClothing, TypeBodypart:
		return wearableReferences(string(content))
	case TypeGesture:
		return gestureReferences(string(content))
	case TypeNotecard:
		return notecardReferences(string(content))
	}
	return nil
}

// collector deduplicates and drops the zero UUID, which formats use for
// "none" and which never names an asset.
type collector struct {
	seen       map[string]bool
	references []Reference
}

func (c *collector) add(id string, assetType int) {
	id = strings.ToLower(id)
	if !validUUID(id) || id == zeroUUID || c.seen[id] {
		return
	}
	if c.seen == nil {
		c.seen = make(map[string]bool)
	}
	c.seen[id] = true
	c.references = append(c.references, Reference{ID: id, Type: assetType})
}

// --- objects ---------------------------------------------------------------

// objectPart mirrors the fields of homeworldz-object-v1 that carry
// references: the texture entry (hex-encoded viewer TextureEntry bytes) and
// the task inventory. Everything else in the serialization is shape and
// physics, reference-free.
type objectPart struct {
	Format        string `json:"format"`
	TextureEntry  string `json:"textureEntry"`
	TaskInventory []struct {
		AssetID   string `json:"assetId"`
		AssetType int    `json:"assetType"`
	} `json:"taskInventory"`
}

type linksetAsset struct {
	Format string       `json:"format"`
	Parts  []objectPart `json:"parts"`
}

func objectReferences(content []byte) []Reference {
	var parts []objectPart
	var linkset linksetAsset
	if err := json.Unmarshal(content, &linkset); err == nil &&
		linkset.Format == "homeworldz-linkset-v1" {
		parts = linkset.Parts
	} else {
		var single objectPart
		if err := json.Unmarshal(content, &single); err != nil ||
			single.Format != "homeworldz-object-v1" {
			return nil
		}
		parts = []objectPart{single}
	}
	var refs collector
	for _, part := range parts {
		if entry, err := hex.DecodeString(part.TextureEntry); err == nil {
			for _, id := range textureEntryTextures(entry) {
				refs.add(id, TypeTexture)
			}
		}
		for _, item := range part.TaskInventory {
			refs.add(item.AssetID, item.AssetType)
		}
	}
	return refs.references
}

// textureEntryTextures reads the texture-UUID section of a viewer
// TextureEntry: a 16-byte default texture, then {face-bitfield varint,
// 16-byte texture} exceptions until a zero bitfield. The sections after it
// (colors, repeats, bump, glow) carry no asset references and are not read.
// The varint uses the high bit as a continuation flag, viewer convention.
func textureEntryTextures(entry []byte) []string {
	if len(entry) < 16 {
		return nil
	}
	ids := []string{uuidFromBytes(entry[:16])}
	offset := 16
	for {
		bits, next, ok := faceBits(entry, offset)
		if !ok || bits == 0 {
			break
		}
		offset = next
		if offset+16 > len(entry) {
			break
		}
		ids = append(ids, uuidFromBytes(entry[offset:offset+16]))
		offset += 16
	}
	return ids
}

func faceBits(entry []byte, offset int) (bits uint64, next int, ok bool) {
	for {
		if offset >= len(entry) || offset-16 > 64 {
			return 0, 0, false
		}
		b := entry[offset]
		offset++
		bits = bits<<7 | uint64(b&0x7f)
		if b&0x80 == 0 {
			return bits, offset, true
		}
	}
}

// --- wearables --------------------------------------------------------------

// wearableReferences reads the textures section of the LLWearable text
// format: a "textures <count>" line followed by "<te-index> <uuid>" lines.
// The visual parameters above it are numbers, never assets.
func wearableReferences(text string) []Reference {
	if !strings.HasPrefix(strings.TrimSpace(text), "LLWearable") {
		return nil
	}
	var refs collector
	inTextures := false
	for _, line := range strings.Split(text, "\n") {
		fields := strings.Fields(line)
		if len(fields) == 0 {
			continue
		}
		if fields[0] == "textures" {
			inTextures = true
			continue
		}
		if !inTextures {
			continue
		}
		if len(fields) != 2 || !allDigits(fields[0]) {
			break // past the textures section
		}
		refs.add(fields[1], TypeTexture)
	}
	return refs.references
}

// --- gestures ---------------------------------------------------------------

// gestureReferences walks the viewer gesture text format sequentially:
// version, key, mask, trigger, replace, step count, then typed steps.
// Animation (0) and sound (1) steps carry a name line, a UUID line, and a
// flags line; chat (2) and wait (3) steps carry no assets. Sequential rather
// than scanning, because a chat step's text may look like anything.
func gestureReferences(text string) []Reference {
	lines := strings.Split(text, "\n")
	position := 0
	next := func() (string, bool) {
		if position >= len(lines) {
			return "", false
		}
		line := strings.TrimRight(lines[position], "\r")
		position++
		return line, true
	}
	version, ok := next()
	if !ok || strings.TrimSpace(version) != "2" {
		return nil
	}
	// key, mask, trigger, replace
	for range 4 {
		if _, ok := next(); !ok {
			return nil
		}
	}
	countLine, ok := next()
	if !ok {
		return nil
	}
	count := parseSmallInt(strings.TrimSpace(countLine))
	var refs collector
	for step := 0; step < count && count >= 0; step++ {
		kind, ok := next()
		if !ok {
			break
		}
		switch strings.TrimSpace(kind) {
		case "0", "1": // animation, sound: name, UUID, flags
			assetType := TypeAnimation
			if strings.TrimSpace(kind) == "1" {
				assetType = TypeSound
			}
			if _, ok := next(); !ok {
				return refs.references
			}
			id, ok := next()
			if !ok {
				return refs.references
			}
			refs.add(strings.TrimSpace(id), assetType)
			if _, ok := next(); !ok {
				return refs.references
			}
		case "2", "3": // chat: text, flags; wait: time, flags
			if _, ok := next(); !ok {
				return refs.references
			}
			if _, ok := next(); !ok {
				return refs.references
			}
		default:
			return refs.references // unrecognized step: stop, keep what we have
		}
	}
	return refs.references
}

// --- notecards ---------------------------------------------------------------

// notecardReferences finds embedded inventory items in the Linden text
// format: "asset_id <uuid>" lines inside the LLEmbeddedItems header, each
// followed (within its block) by a "type <word>" line. Only the header is
// scanned — the body after "Text length" is the user's prose, where an
// asset_id-looking line is just text.
func notecardReferences(text string) []Reference {
	if !strings.HasPrefix(strings.TrimSpace(text), "Linden text") {
		return nil
	}
	if marker := strings.Index(text, "Text length"); marker >= 0 {
		text = text[:marker]
	}
	var refs collector
	remaining := text
	for {
		position := strings.Index(remaining, "asset_id")
		if position < 0 {
			break
		}
		block := remaining[position:]
		fields := strings.Fields(block)
		if len(fields) < 2 {
			break
		}
		assetType := TypeUnknown
		// The type line follows within the same item block; 400 bytes spans it.
		window := block[:min(len(block), 400)]
		if typePosition := strings.Index(window, "\ttype\t"); typePosition < 0 {
			if typePosition = strings.Index(window, "type\t"); typePosition >= 0 {
				assetType = notecardType(window[typePosition:])
			}
		} else {
			assetType = notecardType(window[typePosition+1:])
		}
		refs.add(fields[1], assetType)
		remaining = remaining[position+len("asset_id"):]
	}
	return refs.references
}

func notecardType(block string) int {
	fields := strings.Fields(block)
	if len(fields) < 2 || fields[0] != "type" {
		return TypeUnknown
	}
	switch fields[1] {
	case "texture":
		return TypeTexture
	case "sound":
		return TypeSound
	case "clothing":
		return TypeClothing
	case "object":
		return TypeObject
	case "notecard":
		return TypeNotecard
	case "bodypart":
		return TypeBodypart
	case "animatn":
		return TypeAnimation
	case "gesture":
		return TypeGesture
	}
	return TypeUnknown
}

// --- helpers -----------------------------------------------------------------

func uuidFromBytes(raw []byte) string {
	encoded := hex.EncodeToString(raw)
	return encoded[0:8] + "-" + encoded[8:12] + "-" + encoded[12:16] + "-" +
		encoded[16:20] + "-" + encoded[20:32]
}

func allDigits(value string) bool {
	if value == "" {
		return false
	}
	for _, character := range value {
		if character < '0' || character > '9' {
			return false
		}
	}
	return true
}

func parseSmallInt(value string) int {
	if !allDigits(value) || len(value) > 4 {
		return -1
	}
	result := 0
	for _, character := range value {
		result = result*10 + int(character-'0')
	}
	return result
}

func validUUID(value string) bool {
	if len(value) != 36 {
		return false
	}
	for index, character := range value {
		switch index {
		case 8, 13, 18, 23:
			if character != '-' {
				return false
			}
		default:
			if !((character >= '0' && character <= '9') ||
				(character >= 'a' && character <= 'f')) {
				return false
			}
		}
	}
	return true
}
