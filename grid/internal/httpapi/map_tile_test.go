package httpapi

import (
	"context"
	"encoding/binary"
	"image/jpeg"
	"math"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/regions"
)

func encodedHeightmap(height float32) []byte {
	result := make([]byte, 256*256*4)
	bits := math.Float32bits(height)
	for offset := 0; offset < len(result); offset += 4 {
		binary.LittleEndian.PutUint32(result[offset:], bits)
	}
	return result
}

func TestTerrainHeightmapUsesNorthAtTop(t *testing.T) {
	data := encodedHeightmap(20)
	binary.LittleEndian.PutUint32(data[(255*256+10)*4:], math.Float32bits(30))
	tile := renderTerrainHeightmap(data)
	north := tile.At(10, 0)
	south := tile.At(10, 255)
	_, northGreen, _, _ := north.RGBA()
	_, southGreen, _, _ := south.RGBA()
	if northGreen <= southGreen {
		t.Fatalf("north green = %d, south green = %d; terrain row orientation was lost", northGreen, southGreen)
	}
}

func TestMapTileFetchesAndCachesLiveRegionTerrain(t *testing.T) {
	var requests atomic.Int32
	heightmap := encodedHeightmap(90)
	regionServer := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests.Add(1)
		w.Header().Set("Content-Type", "application/vnd.homeworldz.heightmap-f32le")
		_, _ = w.Write(heightmap)
	}))
	defer regionServer.Close()

	store := newMemoryRegionStore()
	_, err := store.RegisterProvisioned(context.Background(),
		"11111111-1111-4111-8111-111111111111", regions.Registration{
			Name: "Welcome", GridX: 1000, GridY: 1000, PublicEndpoint: regionServer.URL,
			ViewerPort: 42002, LeaseDuration: time.Minute,
		})
	if err != nil {
		t.Fatal(err)
	}
	handler := New(checker{}, "test", Options{
		ServiceToken: "secret", Regions: store, TerrainHTTPClient: regionServer.Client(),
	})
	for attempt := 0; attempt < 2; attempt++ {
		request := httptest.NewRequest(http.MethodGet, "/map/map-1-1000-1000-objects.jpg", nil)
		response := httptest.NewRecorder()
		handler.ServeHTTP(response, request)
		if response.Code != http.StatusOK {
			t.Fatalf("map status = %d", response.Code)
		}
		decoded, err := jpeg.Decode(response.Body)
		if err != nil {
			t.Fatal(err)
		}
		red, green, blue, _ := decoded.At(128, 128).RGBA()
		if red < 40000 || green < 40000 || blue < 40000 {
			t.Fatalf("high terrain pixel = (%d, %d, %d), want light mountain", red, green, blue)
		}
	}
	if requests.Load() != 1 {
		t.Fatalf("heightmap requests = %d, want one cached fetch", requests.Load())
	}
}
