package config

import (
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"strings"

	ini "gopkg.in/ini.v1"
)

type Grid struct {
	Address      string
	PublicURL    string
	DatabaseURL  string
	ServiceToken string
	Directory    string
}

func LoadGrid() (Grid, error) {
	directory, err := resolveDirectory()
	if err != nil {
		return Grid{}, err
	}

	files := []string{
		filepath.Join(directory, "grid.ini"),
		filepath.Join(directory, "db.ini"),
	}
	parsed, err := ini.LoadSources(ini.LoadOptions{Loose: true, IgnoreInlineComment: true}, files[0], files[1])
	if err != nil {
		return Grid{}, fmt.Errorf("load configuration: %w", err)
	}

	result := Grid{
		Address:      parsed.Section("server").Key("address").MustString("127.0.0.1:42000"),
		PublicURL:    parsed.Section("server").Key("public_url").MustString("http://127.0.0.1:42000"),
		DatabaseURL:  parsed.Section("database").Key("url").String(),
		ServiceToken: parsed.Section("auth").Key("service_token").String(),
		Directory:    directory,
	}
	result.Address = environmentOr("HOMEWORLDZ_GRID_ADDR", result.Address)
	result.PublicURL = strings.TrimRight(environmentOr("HOMEWORLDZ_GRID_PUBLIC_URL", result.PublicURL), "/")
	result.DatabaseURL = environmentOr("HOMEWORLDZ_DATABASE_URL", result.DatabaseURL)
	result.ServiceToken = environmentOr("HOMEWORLDZ_GRID_SERVICE_TOKEN", result.ServiceToken)
	publicURL, err := url.Parse(result.PublicURL)
	if err != nil || (publicURL.Scheme != "http" && publicURL.Scheme != "https") || publicURL.Host == "" ||
		publicURL.User != nil || publicURL.RawQuery != "" || publicURL.Fragment != "" {
		return Grid{}, fmt.Errorf("invalid grid public URL %q", result.PublicURL)
	}
	return result, nil
}

func resolveDirectory() (string, error) {
	if configured := os.Getenv("HOMEWORLDZ_CONFIG_DIR"); configured != "" {
		return filepath.Abs(configured)
	}

	for _, candidate := range []string{"config", filepath.Join("..", "config")} {
		info, err := os.Stat(candidate)
		if err == nil && info.IsDir() {
			return filepath.Abs(candidate)
		}
		if err != nil && !os.IsNotExist(err) {
			return "", fmt.Errorf("inspect config directory %q: %w", candidate, err)
		}
	}
	return filepath.Abs("config")
}

func environmentOr(name, fallback string) string {
	if value, ok := os.LookupEnv(name); ok {
		return value
	}
	return fallback
}
