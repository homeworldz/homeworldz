package main

import (
	"flag"
	"fmt"
	"image/png"
	"os"
	"path/filepath"

	"github.com/homeworldz/homeworldz/grid/internal/terrainimage"
)

func main() {
	output := flag.String("output", "assets/region/terrain", "terrain asset directory")
	flag.Parse()
	terrains := []struct {
		name    string
		heights []float32
	}{
		{"plateau-square", terrainimage.RoundedSquarePlateau()},
		{"plateau-round", terrainimage.RoundPlateau()},
	}
	for _, terrain := range terrains {
		if err := writeTerrain(*output, terrain.name, terrain.heights); err != nil {
			fmt.Fprintln(os.Stderr, "generate default terrain failed:", err)
			os.Exit(1)
		}
	}
}

func writeTerrain(directory, name string, heights []float32) error {
	if err := os.MkdirAll(directory, 0o755); err != nil {
		return fmt.Errorf("create output directory: %w", err)
	}
	image, err := terrainimage.GrayscaleImage(
		heights, terrainimage.DefaultSize, terrainimage.DefaultSize)
	if err != nil {
		return err
	}
	imagePath := filepath.Join(directory, name+".png")
	file, err := os.Create(imagePath)
	if err != nil {
		return fmt.Errorf("create %s: %w", imagePath, err)
	}
	if err := png.Encode(file, image); err != nil {
		file.Close()
		return fmt.Errorf("encode %s: %w", imagePath, err)
	}
	if err := file.Close(); err != nil {
		return fmt.Errorf("close %s: %w", imagePath, err)
	}
	compatibleHeights, err := terrainimage.Heights(
		image, terrainimage.DefaultSize, terrainimage.DefaultSize)
	if err != nil {
		return err
	}
	rawPath := filepath.Join(directory, name+".raw")
	if err := os.WriteFile(rawPath, terrainimage.ByteRaw(compatibleHeights), 0o644); err != nil {
		return fmt.Errorf("write %s: %w", rawPath, err)
	}
	fmt.Printf("Generated %s and %s.\n", imagePath, rawPath)
	return nil
}
