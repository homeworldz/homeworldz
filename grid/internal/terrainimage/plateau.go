package terrainimage

import "math"

const (
	DefaultSize        = 256
	DefaultWaterHeight = float32(20)
	DefaultSeabed      = float32(18)
	DefaultPlateau     = float32(22)
)

// RoundedSquarePlateau returns a calm 1x1-region heightmap whose 20-metre
// shoreline is a 250-by-250-metre rounded square.
func RoundedSquarePlateau() []float32 {
	const (
		halfExtent   = 125.0
		cornerRadius = 12.0
		transition   = 4.0
	)
	return generatePlateau(func(x, y float64) float64 {
		qx := math.Abs(x) - (halfExtent - cornerRadius)
		qy := math.Abs(y) - (halfExtent - cornerRadius)
		outside := math.Hypot(math.Max(qx, 0), math.Max(qy, 0))
		inside := math.Min(math.Max(qx, qy), 0)
		return -(outside + inside - cornerRadius)
	}, transition)
}

// RoundPlateau returns a calm 1x1-region heightmap with a 200-metre-diameter
// circular shoreline at the standard water height.
func RoundPlateau() []float32 {
	const (
		radius     = 100.0
		transition = 4.0
	)
	return generatePlateau(func(x, y float64) float64 {
		return radius - math.Hypot(x, y)
	}, transition)
}

func generatePlateau(signedDistance func(float64, float64) float64, transition float64) []float32 {
	result := make([]float32, DefaultSize*DefaultSize)
	center := float64(DefaultSize-1) / 2
	for y := 0; y < DefaultSize; y++ {
		for x := 0; x < DefaultSize; x++ {
			distance := signedDistance(float64(x)-center, float64(y)-center)
			amount := smoothstep(-transition/2, transition/2, distance)
			result[y*DefaultSize+x] = DefaultSeabed +
				(DefaultPlateau-DefaultSeabed)*float32(amount)
		}
	}
	return result
}

func smoothstep(low, high, value float64) float64 {
	amount := math.Max(0, math.Min(1, (value-low)/(high-low)))
	return amount * amount * (3 - 2*amount)
}
