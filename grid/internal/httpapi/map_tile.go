package httpapi

import (
	"bytes"
	"context"
	_ "embed"
	"encoding/binary"
	"image"
	"image/color"
	"image/draw"
	"image/jpeg"
	"io"
	"math"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/regions"
)

//go:embed assets/default-map.jpg
var defaultMapTile []byte

var (
	decodedMapTile     image.Image
	decodedMapTileErr  error
	decodedMapTileOnce sync.Once
)

type cachedTerrainTile struct {
	image     image.Image
	expiresAt time.Time
}

type terrainTileCache struct {
	mu      sync.Mutex
	entries map[string]cachedTerrainTile
}

func newTerrainTileCache() terrainTileCache {
	return terrainTileCache{entries: make(map[string]cachedTerrainTile)}
}

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
	terrain := make(map[string]image.Image)
	for _, region := range regions {
		if region.GridX < x || region.GridX >= x+(1<<(level-1)) ||
			region.GridY < y || region.GridY >= y+(1<<(level-1)) {
			continue
		}
		if tile, ok := a.regionTerrainTile(r.Context(), region); ok {
			terrain[region.ID] = tile
		}
	}
	tile, found, err := renderMapTile(level, x, y, regions, terrain)
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

func renderMapTile(level, tileX, tileY int, online []regions.Region, terrain map[string]image.Image) ([]byte, bool, error) {
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
		if tile := terrain[matching[0].ID]; tile != nil {
			return encodeJPEG(tile)
		}
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
	for _, region := range matching {
		source := decodedMapTile
		if tile := terrain[region.ID]; tile != nil {
			source = tile
		}
		left := (region.GridX - tileX) * regionSize
		top := (span - 1 - (region.GridY - tileY)) * regionSize
		sourceBounds := source.Bounds()
		for pixelY := 0; pixelY < regionSize; pixelY++ {
			for pixelX := 0; pixelX < regionSize; pixelX++ {
				sourceX := sourceBounds.Min.X + pixelX*sourceBounds.Dx()/regionSize
				sourceY := sourceBounds.Min.Y + pixelY*sourceBounds.Dy()/regionSize
				result.Set(left+pixelX, top+pixelY, source.At(sourceX, sourceY))
			}
		}
	}
	return encodeJPEG(result)
}

func encodeJPEG(source image.Image) ([]byte, bool, error) {
	var encoded bytes.Buffer
	if err := jpeg.Encode(&encoded, source, &jpeg.Options{Quality: 90}); err != nil {
		return nil, false, err
	}
	return encoded.Bytes(), true, nil
}

func (a *API) regionTerrainTile(ctx context.Context, region regions.Region) (image.Image, bool) {
	if a.terrainHTTP == nil || region.PublicEndpoint == "" {
		return nil, false
	}
	endpoint := strings.TrimRight(region.PublicEndpoint, "/") + "/map/terrain.raw"
	now := time.Now()
	a.terrainCache.mu.Lock()
	if cached, ok := a.terrainCache.entries[endpoint]; ok && cached.expiresAt.After(now) {
		a.terrainCache.mu.Unlock()
		return cached.image, cached.image != nil
	}
	a.terrainCache.mu.Unlock()

	request, err := http.NewRequestWithContext(ctx, http.MethodGet, endpoint, nil)
	if err != nil {
		return nil, false
	}
	response, err := a.terrainHTTP.Do(request)
	if err != nil {
		a.cacheTerrainTile(endpoint, nil, now.Add(5*time.Second))
		return nil, false
	}
	defer response.Body.Close()
	const byteCount = 256 * 256 * 4
	body, err := io.ReadAll(io.LimitReader(response.Body, byteCount+1))
	if err != nil || response.StatusCode != http.StatusOK || len(body) != byteCount {
		a.cacheTerrainTile(endpoint, nil, now.Add(5*time.Second))
		return nil, false
	}
	tile := renderTerrainHeightmap(body)
	a.cacheTerrainTile(endpoint, tile, now.Add(60*time.Second))
	return tile, true
}

func (a *API) cacheTerrainTile(endpoint string, tile image.Image, expiresAt time.Time) {
	a.terrainCache.mu.Lock()
	defer a.terrainCache.mu.Unlock()
	a.terrainCache.entries[endpoint] = cachedTerrainTile{image: tile, expiresAt: expiresAt}
}

func renderTerrainHeightmap(data []byte) image.Image {
	heights := make([]float64, 256*256)
	for index := range heights {
		heights[index] = float64(math.Float32frombits(binary.LittleEndian.Uint32(data[index*4:])))
	}
	result := image.NewRGBA(image.Rect(0, 0, 256, 256))
	for outputY := 0; outputY < 256; outputY++ {
		y := 255 - outputY
		for x := 0; x < 256; x++ {
			height := heights[y*256+x]
			base := terrainColor(height)
			left, right := max(0, x-1), min(255, x+1)
			down, up := max(0, y-1), min(255, y+1)
			dx := heights[y*256+right] - heights[y*256+left]
			dy := heights[up*256+x] - heights[down*256+x]
			shade := 0.9 + 0.18*(-dx+dy)/math.Sqrt(dx*dx+dy*dy+4)
			result.SetRGBA(x, outputY, color.RGBA{
				R: shadeChannel(base.R, shade), G: shadeChannel(base.G, shade),
				B: shadeChannel(base.B, shade), A: 255})
		}
	}
	return result
}

func terrainColor(height float64) color.RGBA {
	if height <= 20 {
		depth := min(1, max(0, (20-height)/20))
		return color.RGBA{R: uint8(35 - 12*depth), G: uint8(105 - 35*depth), B: uint8(145 - 25*depth), A: 255}
	}
	if height < 22 {
		return color.RGBA{R: 194, G: 178, B: 128, A: 255}
	}
	if height < 45 {
		return color.RGBA{R: 78, G: 132, B: 70, A: 255}
	}
	if height < 80 {
		return color.RGBA{R: 112, G: 108, B: 91, A: 255}
	}
	return color.RGBA{R: 205, G: 207, B: 201, A: 255}
}

func shadeChannel(value uint8, shade float64) uint8 {
	return uint8(min(255, max(0, math.Round(float64(value)*shade))))
}
