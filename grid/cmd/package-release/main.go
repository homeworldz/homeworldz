package main

import (
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"
)

type options struct {
	version          string
	outputDirectory  string
	regionExecutable string
	gridOnly         bool
	targetOS         string
}

type archiveEntry struct {
	source string
	name   string
}

func main() {
	var opts options
	flag.StringVar(&opts.version, "version", "", "override the repository VERSION for a preview package")
	flag.StringVar(&opts.outputDirectory, "output", "dist", "release archive output directory")
	flag.StringVar(&opts.regionExecutable, "region-executable", "", "path to the native homeworldz-region executable")
	flag.BoolVar(&opts.gridOnly, "grid-only", false, "build only the grid-owner archive")
	flag.StringVar(&opts.targetOS, "target-os", runtime.GOOS, "target operating system: windows or linux")
	flag.Parse()
	if err := run(context.Background(), opts); err != nil {
		fmt.Fprintln(os.Stderr, "package release:", err)
		os.Exit(1)
	}
}

func run(ctx context.Context, opts options) error {
	if opts.targetOS != "windows" && opts.targetOS != "linux" {
		return errors.New("target OS must be windows or linux")
	}
	if runtime.GOARCH != "amd64" {
		return errors.New("release packaging requires an amd64 build host")
	}
	if !opts.gridOnly && opts.targetOS != runtime.GOOS {
		return errors.New("a full release must target the native host OS; use -grid-only for cross-compilation")
	}
	isWindows := opts.targetOS == "windows"
	binarySuffix := ""
	if isWindows {
		binarySuffix = ".exe"
	}
	root, err := repositoryRoot()
	if err != nil {
		return err
	}
	requestedVersion := opts.version
	if requestedVersion == "" {
		content, readErr := os.ReadFile(filepath.Join(root, "VERSION"))
		if readErr != nil {
			return fmt.Errorf("read repository VERSION: %w", readErr)
		}
		requestedVersion = strings.TrimSpace(string(content))
	}
	version, err := safeVersion(requestedVersion)
	if err != nil {
		return err
	}
	output := opts.outputDirectory
	if !filepath.IsAbs(output) {
		output = filepath.Join(root, output)
	}
	if err := os.MkdirAll(output, 0o755); err != nil {
		return fmt.Errorf("create output directory: %w", err)
	}
	work, err := os.MkdirTemp("", "homeworldz-release-")
	if err != nil {
		return fmt.Errorf("create release workspace: %w", err)
	}
	defer os.RemoveAll(work)
	versionFile := filepath.Join(work, "VERSION")
	if err := os.WriteFile(versionFile, []byte(version+"\n"), 0o644); err != nil {
		return fmt.Errorf("write packaged version: %w", err)
	}

	gridBins := []struct{ command, name string }{
		{"./grid/cmd/grid", "homeworldz-grid" + binarySuffix},
		{"./grid/cmd/bootstrap-grid", "bootstrap-grid" + binarySuffix},
		{"./grid/cmd/configure-library", "configure-library" + binarySuffix},
	}
	gridEntries := make([]archiveEntry, 0, len(gridBins)+8)
	for _, binary := range gridBins {
		destination := filepath.Join(work, binary.name)
		if err := buildGoCommand(ctx, root, binary.command, destination, version, opts.targetOS); err != nil {
			return err
		}
		gridEntries = append(gridEntries, archiveEntry{destination, binary.name})
	}
	gridEntries = append(gridEntries,
		archiveEntry{versionFile, "VERSION"},
		archiveEntry{filepath.Join(root, "config", "examples", "grid.ini"), "config/examples/grid.ini"},
		archiveEntry{filepath.Join(root, "config", "examples", "grid-personal.ini"), "config/examples/grid-personal.ini"},
		archiveEntry{filepath.Join(root, "config", "examples", "grid-cloud.ini"), "config/examples/grid-cloud.ini"},
		archiveEntry{filepath.Join(root, "config", "examples", "regions.json"), "config/examples/regions.json"},
		archiveEntry{filepath.Join(root, "docs", "INSTALL-GRID.md"), "INSTALL-GRID.md"},
		archiveEntry{filepath.Join(root, "docs", "INSTALL-REGION.md"), "INSTALL-REGION.md"},
		archiveEntry{filepath.Join(root, "docs", "FEATURES.md"), "docs/FEATURES.md"},
		archiveEntry{filepath.Join(root, "docs", "ROADMAP.md"), "docs/ROADMAP.md"},
		archiveEntry{filepath.Join(root, "deploy", "linux", "Caddyfile.grid"), "deploy/linux/Caddyfile.grid"},
		archiveEntry{filepath.Join(root, "deploy", "linux", "homeworldz-grid.service"), "deploy/linux/homeworldz-grid.service"},
	)
	gridEntries, err = appendTree(gridEntries, filepath.Join(root, "db", "migrations"), "db/migrations", func(path string) bool {
		return strings.HasSuffix(path, ".up.sql")
	})
	if err != nil {
		return err
	}
	if opts.gridOnly {
		platform := opts.targetOS + "-x64"
		extension := ".tar.gz"
		writer := writeTarGZ
		if isWindows {
			extension = ".zip"
			writer = writeZIP
		}
		gridArchive := filepath.Join(output, "homeworldz-grid-"+version+"-"+platform+extension)
		if err := writer(gridArchive, "homeworldz-grid", gridEntries); err != nil {
			return err
		}
		if err := writeChecksums(filepath.Join(output, "SHA256SUMS"), []string{gridArchive}); err != nil {
			return err
		}
		fmt.Println(gridArchive)
		return nil
	}

	regionExecutable, err := findRegionExecutable(root, opts.regionExecutable)
	if err != nil {
		return err
	}
	regionEntries := []archiveEntry{
		{versionFile, "VERSION"},
		{regionExecutable, "homeworldz-region" + binarySuffix},
		{filepath.Join(root, "config", "examples", "region.ini"), "config/examples/region.ini"},
		{filepath.Join(root, "config", "examples", "region-personal.ini"), "config/examples/region-personal.ini"},
		{filepath.Join(root, "config", "examples", "region-cloud.ini"), "config/examples/region-cloud.ini"},
		{filepath.Join(root, "docs", "INSTALL-REGION.md"), "INSTALL-REGION.md"},
		{filepath.Join(root, "docs", "INSTALL-GRID.md"), "INSTALL-GRID.md"},
		{filepath.Join(root, "docs", "FEATURES.md"), "docs/FEATURES.md"},
		{filepath.Join(root, "docs", "ROADMAP.md"), "docs/ROADMAP.md"},
		{filepath.Join(root, "deploy", "linux", "homeworldz-region@.service"), "deploy/linux/homeworldz-region@.service"},
		{filepath.Join(root, "deploy", "linux", "region.env.example"), "deploy/linux/region.env.example"},
	}
	if isWindows {
		for _, dll := range siblingDLLs(regionExecutable) {
			regionEntries = append(regionEntries, archiveEntry{dll, filepath.Base(dll)})
		}
		runtimeEntries, runtimeErr := visualCRuntimeEntries(ctx)
		if runtimeErr != nil {
			return runtimeErr
		}
		regionEntries = append(regionEntries, runtimeEntries...)
	} else if err := validateLinuxDependencies(ctx, regionExecutable); err != nil {
		return err
	}
	regionEntries, err = appendTree(regionEntries, filepath.Join(root, "assets", "region"), "assets/region", nil)
	if err != nil {
		return err
	}

	platform := opts.targetOS + "-x64"
	extension := ".tar.gz"
	writer := writeTarGZ
	if isWindows {
		extension = ".zip"
		writer = writeZIP
	}
	gridArchive := filepath.Join(output, "homeworldz-grid-"+version+"-"+platform+extension)
	regionArchive := filepath.Join(output, "homeworldz-region-"+version+"-"+platform+extension)
	if err := writer(gridArchive, "homeworldz-grid", gridEntries); err != nil {
		return err
	}
	if err := writer(regionArchive, "homeworldz-region", regionEntries); err != nil {
		return err
	}
	if err := writeChecksums(filepath.Join(output, "SHA256SUMS"), []string{gridArchive, regionArchive}); err != nil {
		return err
	}
	fmt.Println(gridArchive)
	fmt.Println(regionArchive)
	return nil
}

func safeVersion(value string) (string, error) {
	value = strings.TrimSpace(value)
	if value == "" {
		return "", errors.New("version must not be empty")
	}
	for _, r := range value {
		if (r < 'a' || r > 'z') && (r < 'A' || r > 'Z') && (r < '0' || r > '9') && r != '.' && r != '-' && r != '_' {
			return "", fmt.Errorf("version %q contains an unsafe archive-name character", value)
		}
	}
	return value, nil
}

func repositoryRoot() (string, error) {
	directory, err := os.Getwd()
	if err != nil {
		return "", err
	}
	for {
		if _, err := os.Stat(filepath.Join(directory, "go.work")); err == nil {
			return directory, nil
		}
		parent := filepath.Dir(directory)
		if parent == directory {
			return "", errors.New("repository root containing go.work was not found")
		}
		directory = parent
	}
}

func buildGoCommand(ctx context.Context, root, commandPath, destination, version, targetOS string) error {
	args := []string{"build", "-trimpath", "-o", destination}
	if commandPath == "./grid/cmd/grid" {
		args = append(args, "-ldflags", "-s -w -X main.version="+version)
	} else {
		args = append(args, "-ldflags", "-s -w")
	}
	args = append(args, commandPath)
	command := exec.CommandContext(ctx, "go", args...)
	command.Dir = root
	command.Env = append(os.Environ(), "GOOS="+targetOS, "GOARCH=amd64", "CGO_ENABLED=0")
	output, err := command.CombinedOutput()
	if err != nil {
		return fmt.Errorf("build %s: %w: %s", commandPath, err, strings.TrimSpace(string(output)))
	}
	return nil
}

func findRegionExecutable(root, supplied string) (string, error) {
	if supplied != "" {
		if !filepath.IsAbs(supplied) {
			supplied = filepath.Join(root, supplied)
		}
		if info, err := os.Stat(supplied); err == nil && !info.IsDir() {
			return supplied, nil
		}
		return "", fmt.Errorf("region executable %s was not found", supplied)
	}
	candidates := []string{
		filepath.Join(root, "build", "release", "region", "homeworldz-region"),
		filepath.Join(root, "build", "default", "region", "homeworldz-region"),
	}
	if runtime.GOOS == "windows" {
		candidates = []string{
			filepath.Join(root, "build", "windows-vcpkg", "region", "Release", "homeworldz-region.exe"),
			filepath.Join(root, "build", "windows-vcpkg", "region", "Debug", "homeworldz-region.exe"),
			filepath.Join(root, "build", "default", "region", "homeworldz-region.exe"),
		}
	}
	for _, candidate := range candidates {
		if info, err := os.Stat(candidate); err == nil && !info.IsDir() {
			return candidate, nil
		}
	}
	return "", errors.New("homeworldz-region executable was not found; build it or pass -region-executable")
}

func validateLinuxDependencies(ctx context.Context, executable string) error {
	command := exec.CommandContext(ctx, "ldd", executable)
	output, err := command.CombinedOutput()
	if err != nil {
		return fmt.Errorf("inspect Linux region dependencies: %w: %s", err, strings.TrimSpace(string(output)))
	}
	if strings.Contains(string(output), "not found") {
		return fmt.Errorf("Linux region has unresolved shared dependencies: %s", strings.TrimSpace(string(output)))
	}
	return nil
}

func siblingDLLs(executable string) []string {
	paths, _ := filepath.Glob(filepath.Join(filepath.Dir(executable), "*.dll"))
	sort.Strings(paths)
	return paths
}

func visualCRuntimeEntries(ctx context.Context) ([]archiveEntry, error) {
	programFiles := os.Getenv("ProgramFiles(x86)")
	if programFiles == "" {
		return nil, errors.New("ProgramFiles(x86) is unavailable; cannot locate the Visual C++ runtime")
	}
	vswhere := filepath.Join(programFiles, "Microsoft Visual Studio", "Installer", "vswhere.exe")
	command := exec.CommandContext(ctx, vswhere, "-latest", "-products", "*", "-property", "installationPath")
	output, err := command.Output()
	if err != nil {
		return nil, fmt.Errorf("locate Visual Studio runtime: %w", err)
	}
	installation := strings.TrimSpace(string(output))
	if installation == "" {
		return nil, errors.New("Visual Studio installation was not found; cannot package its C++ runtime")
	}
	return visualCRuntimeEntriesUnder(installation)
}

func visualCRuntimeEntriesUnder(installation string) ([]archiveEntry, error) {
	directories, err := filepath.Glob(filepath.Join(
		installation, "VC", "Redist", "MSVC", "*", "x64", "Microsoft.VC*.CRT"))
	if err != nil {
		return nil, fmt.Errorf("locate Visual C++ runtime directories: %w", err)
	}
	sort.Sort(sort.Reverse(sort.StringSlice(directories)))
	required := []string{"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}
	for _, directory := range directories {
		entries := make([]archiveEntry, 0, len(required))
		for _, name := range required {
			path := filepath.Join(directory, name)
			if info, statErr := os.Stat(path); statErr != nil || info.IsDir() {
				entries = nil
				break
			}
			entries = append(entries, archiveEntry{path, name})
		}
		if entries != nil {
			return entries, nil
		}
	}
	return nil, errors.New("complete x64 Visual C++ redistributable runtime was not found")
}

func appendTree(entries []archiveEntry, sourceRoot, archiveRoot string, include func(string) bool) ([]archiveEntry, error) {
	err := filepath.WalkDir(sourceRoot, func(path string, entry fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if entry.IsDir() || (include != nil && !include(path)) {
			return nil
		}
		relative, err := filepath.Rel(sourceRoot, path)
		if err != nil {
			return err
		}
		entries = append(entries, archiveEntry{path, filepath.ToSlash(filepath.Join(archiveRoot, relative))})
		return nil
	})
	if err != nil {
		return nil, fmt.Errorf("collect %s: %w", sourceRoot, err)
	}
	return entries, nil
}

func writeZIP(path, root string, entries []archiveEntry) error {
	sort.Slice(entries, func(i, j int) bool { return entries[i].name < entries[j].name })
	file, err := os.Create(path)
	if err != nil {
		return fmt.Errorf("create %s: %w", path, err)
	}
	archive := zip.NewWriter(file)
	for _, entry := range entries {
		if err := addZIPFile(archive, entry.source, root+"/"+entry.name); err != nil {
			archive.Close()
			file.Close()
			return err
		}
	}
	if err := archive.Close(); err != nil {
		file.Close()
		return fmt.Errorf("finish %s: %w", path, err)
	}
	if err := file.Close(); err != nil {
		return fmt.Errorf("close %s: %w", path, err)
	}
	return nil
}

func writeTarGZ(path, root string, entries []archiveEntry) error {
	sort.Slice(entries, func(i, j int) bool { return entries[i].name < entries[j].name })
	file, err := os.Create(path)
	if err != nil {
		return fmt.Errorf("create %s: %w", path, err)
	}
	compressed := gzip.NewWriter(file)
	compressed.Header.ModTime = time.Unix(0, 0).UTC()
	archive := tar.NewWriter(compressed)
	for _, entry := range entries {
		if err := addTarFile(archive, entry.source, root+"/"+entry.name); err != nil {
			archive.Close()
			compressed.Close()
			file.Close()
			return err
		}
	}
	if err := archive.Close(); err != nil {
		compressed.Close()
		file.Close()
		return fmt.Errorf("finish %s: %w", path, err)
	}
	if err := compressed.Close(); err != nil {
		file.Close()
		return fmt.Errorf("compress %s: %w", path, err)
	}
	if err := file.Close(); err != nil {
		return fmt.Errorf("close %s: %w", path, err)
	}
	return nil
}

func addTarFile(archive *tar.Writer, source, name string) error {
	input, err := os.Open(source)
	if err != nil {
		return fmt.Errorf("open %s: %w", source, err)
	}
	defer input.Close()
	info, err := input.Stat()
	if err != nil {
		return fmt.Errorf("stat %s: %w", source, err)
	}
	mode := int64(0o644)
	base := filepath.Base(name)
	if base == "homeworldz-grid" || base == "bootstrap-grid" || base == "configure-library" || base == "homeworldz-region" {
		mode = 0o755
	}
	header := &tar.Header{Name: filepath.ToSlash(name), Mode: mode, Size: info.Size(), ModTime: time.Unix(0, 0).UTC(), Typeflag: tar.TypeReg}
	if err := archive.WriteHeader(header); err != nil {
		return fmt.Errorf("create archive entry %s: %w", name, err)
	}
	if _, err := io.Copy(archive, input); err != nil {
		return fmt.Errorf("write archive entry %s: %w", name, err)
	}
	return nil
}

func addZIPFile(archive *zip.Writer, source, name string) error {
	input, err := os.Open(source)
	if err != nil {
		return fmt.Errorf("open %s: %w", source, err)
	}
	defer input.Close()
	header := &zip.FileHeader{Name: filepath.ToSlash(name), Method: zip.Deflate}
	header.SetModTime(time.Unix(0, 0).UTC())
	header.SetMode(0o644)
	if strings.HasSuffix(strings.ToLower(name), ".exe") {
		header.SetMode(0o755)
	}
	output, err := archive.CreateHeader(header)
	if err != nil {
		return fmt.Errorf("create archive entry %s: %w", name, err)
	}
	if _, err := io.Copy(output, input); err != nil {
		return fmt.Errorf("write archive entry %s: %w", name, err)
	}
	return nil
}

func writeChecksums(path string, archives []string) error {
	sort.Strings(archives)
	var content strings.Builder
	for _, archive := range archives {
		file, err := os.Open(archive)
		if err != nil {
			return err
		}
		hash := sha256.New()
		_, copyErr := io.Copy(hash, file)
		closeErr := file.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeErr != nil {
			return closeErr
		}
		content.WriteString(hex.EncodeToString(hash.Sum(nil)))
		content.WriteString("  ")
		content.WriteString(filepath.Base(archive))
		content.WriteByte('\n')
	}
	return os.WriteFile(path, []byte(content.String()), 0o644)
}
