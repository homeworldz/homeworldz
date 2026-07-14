package terrainimage

import (
	"image"
	"image/color"
	"math"
	"testing"
)

func TestHeightsUsesHSLBrightnessAndFlipsRows(t *testing.T) {
	source := image.NewNRGBA(image.Rect(0, 0, 2, 2))
	source.Set(0, 0, color.NRGBA{R: 255, A: 255})                 // top: 64 m
	source.Set(1, 0, color.NRGBA{R: 255, G: 255, B: 255, A: 255}) // top: 128 m
	source.Set(0, 1, color.NRGBA{A: 255})                         // bottom: 0 m
	source.Set(1, 1, color.NRGBA{R: 64, G: 128, B: 192, A: 255})  // bottom: ~64.25 m

	heights, err := Heights(source, 2, 2)
	if err != nil {
		t.Fatal(err)
	}
	want := []float32{0, 64.25098, 64, 128}
	for index := range want {
		difference := heights[index] - want[index]
		if difference < -0.001 || difference > 0.001 {
			t.Fatalf("height %d = %f, want %f", index, heights[index], want[index])
		}
	}
	if got := ByteRaw(heights); string(got) != string([]byte{0, 64, 64, 128}) {
		t.Fatalf("byte raw = %v", got)
	}
}

func TestHeightsRequiresExactRegionDimensions(t *testing.T) {
	_, err := Heights(image.NewGray(image.Rect(0, 0, 4, 2)), 2, 2)
	if err == nil {
		t.Fatal("expected dimension error")
	}
}

func TestPlateauDimensionsAtWaterline(t *testing.T) {
	tests := []struct {
		name      string
		heights   []float32
		wantWidth int
	}{
		{"rounded square", RoundedSquarePlateau(), 250},
		{"round", RoundPlateau(), 200},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			centerY := DefaultSize / 2
			width := 0
			for x := 0; x < DefaultSize; x++ {
				if test.heights[centerY*DefaultSize+x] > DefaultWaterHeight {
					width++
				}
			}
			if width != test.wantWidth {
				t.Fatalf("above-water width = %d, want %d", width, test.wantWidth)
			}
			if center := test.heights[centerY*DefaultSize+DefaultSize/2]; center != DefaultPlateau {
				t.Fatalf("center height = %f, want %f", center, DefaultPlateau)
			}
		})
	}
}

func TestGrayscaleImageRoundTripsCompatibleHeights(t *testing.T) {
	source := RoundedSquarePlateau()
	encoded, err := GrayscaleImage(source, DefaultSize, DefaultSize)
	if err != nil {
		t.Fatal(err)
	}
	decoded, err := Heights(encoded, DefaultSize, DefaultSize)
	if err != nil {
		t.Fatal(err)
	}
	for index := range source {
		if difference := math.Abs(float64(source[index] - decoded[index])); difference > 0.26 {
			t.Fatalf("height %d differs by %f metres", index, difference)
		}
	}
}
