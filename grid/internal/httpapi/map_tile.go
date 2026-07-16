package httpapi

import (
	"bytes"
	_ "embed"
	"image"
	"image/color"
	"image/draw"
	"image/jpeg"
	"net/http"
	"strconv"
	"strings"
	"sync"

	"github.com/homeworldz/homeworldz/grid/internal/regions"
)

//go:embed assets/default-map.jpg
var defaultMapTile []byte

var (
	decodedMapTile     image.Image
	decodedMapTileErr  error
	decodedMapTileOnce sync.Once
)

func (a *API) mapTile(w http.ResponseWriter, r *http.Request) {
	if a.regions == nil {
		a.notFound(w, r)
		return
	}
	name := strings.TrimPrefix(r.URL.Path, "/map/")
	const prefix = "map-"
	const suffix = "-objects.jpg"
	if !strings.HasPrefix(name, prefix) || !strings.HasSuffix(name, suffix) {
		a.notFound(w, r)
		return
	}
	coordinates := strings.Split(strings.TrimSuffix(strings.TrimPrefix(name, prefix), suffix), "-")
	if len(coordinates) != 3 {
		a.notFound(w, r)
		return
	}
	level, levelErr := strconv.Atoi(coordinates[0])
	x, xErr := strconv.Atoi(coordinates[1])
	y, yErr := strconv.Atoi(coordinates[2])
	if levelErr != nil || level < 1 || level > 8 || xErr != nil || yErr != nil ||
		x < 0 || x > 65535 || y < 0 || y > 65535 {
		a.notFound(w, r)
		return
	}
	regions, err := a.regions.List(r.Context())
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "region_store_error", Message: "map tile lookup failed"})
		return
	}
	tile, found, err := renderMapTile(level, x, y, regions)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "map_tile_error", Message: "map tile rendering failed"})
		return
	}
	if !found {
		a.notFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "image/jpeg")
	w.Header().Set("Cache-Control", "public, max-age=60")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write(tile)
}

func renderMapTile(level, tileX, tileY int, online []regions.Region) ([]byte, bool, error) {
	span := 1 << (level - 1)
	matching := make([]regions.Region, 0)
	for _, region := range online {
		if region.GridX >= tileX && region.GridX < tileX+span &&
			region.GridY >= tileY && region.GridY < tileY+span {
			matching = append(matching, region)
		}
	}
	if len(matching) == 0 {
		return nil, false, nil
	}
	if level == 1 {
		return defaultMapTile, true, nil
	}

	decodedMapTileOnce.Do(func() {
		decodedMapTile, decodedMapTileErr = jpeg.Decode(bytes.NewReader(defaultMapTile))
	})
	if decodedMapTileErr != nil {
		return nil, false, decodedMapTileErr
	}
	const size = 256
	result := image.NewRGBA(image.Rect(0, 0, size, size))
	draw.Draw(result, result.Bounds(), &image.Uniform{C: color.RGBA{R: 36, G: 87, B: 122, A: 255}}, image.Point{}, draw.Src)
	regionSize := size / span
	sourceBounds := decodedMapTile.Bounds()
	for _, region := range matching {
		left := (region.GridX - tileX) * regionSize
		top := (span - 1 - (region.GridY - tileY)) * regionSize
		for pixelY := 0; pixelY < regionSize; pixelY++ {
			for pixelX := 0; pixelX < regionSize; pixelX++ {
				sourceX := sourceBounds.Min.X + pixelX*sourceBounds.Dx()/regionSize
				sourceY := sourceBounds.Min.Y + pixelY*sourceBounds.Dy()/regionSize
				result.Set(left+pixelX, top+pixelY, decodedMapTile.At(sourceX, sourceY))
			}
		}
	}
	var encoded bytes.Buffer
	if err := jpeg.Encode(&encoded, result, &jpeg.Options{Quality: 90}); err != nil {
		return nil, false, err
	}
	return encoded.Bytes(), true, nil
}
