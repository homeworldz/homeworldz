package main

import (
	"flag"
	"fmt"
	"image"
	_ "image/png"
	"os"
	"path/filepath"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/terrainimage"
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, "convert terrain image failed:", err)
		os.Exit(1)
	}
}

func run() error {
	input := flag.String("input", "", "source PNG heightmap")
	output := flag.String("output", "", "destination eight-bit raw heightmap")
	width := flag.Int("width", 256, "region width in metres")
	height := flag.Int("height", 256, "region height in metres")
	flag.Parse()
	if *input == "" || *output == "" {
		return fmt.Errorf("both -input and -output are required")
	}
	if !strings.EqualFold(filepath.Ext(*input), ".png") {
		return fmt.Errorf("terrain images must use lossless PNG format")
	}
	file, err := os.Open(*input)
	if err != nil {
		return fmt.Errorf("open input: %w", err)
	}
	defer file.Close()
	source, format, err := image.Decode(file)
	if err != nil {
		return fmt.Errorf("decode input: %w", err)
	}
	if format != "png" {
		return fmt.Errorf("terrain images must decode as PNG, got %s", format)
	}
	heights, err := terrainimage.Heights(source, *width, *height)
	if err != nil {
		return err
	}
	if err := os.WriteFile(*output, terrainimage.ByteRaw(heights), 0o644); err != nil {
		return fmt.Errorf("write output: %w", err)
	}
	fmt.Printf("Converted %s %dx%d terrain to %s using OpenSimulator/Halcyon image semantics.\n",
		format, *width, *height, *output)
	return nil
}
