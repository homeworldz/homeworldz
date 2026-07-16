package config

import (
	"fmt"
	"net/url"
	"path/filepath"
	"strings"

	ini "gopkg.in/ini.v1"
)

type Grid struct {
	Address      string
	PublicURL    string
	Name         string
	DatabaseURL  string
	ServiceToken string
	Directory    string
}

func LoadGrid(directory string) (Grid, error) {
	resolved, err := filepath.Abs(directory)
	if err != nil {
		return Grid{}, fmt.Errorf("resolve configuration directory: %w", err)
	}

	files := []string{
		filepath.Join(resolved, "grid.ini"),
		filepath.Join(resolved, "db.ini"),
	}
	parsed, err := ini.LoadSources(ini.LoadOptions{IgnoreInlineComment: true}, files[0], files[1])
	if err != nil {
		return Grid{}, fmt.Errorf("load configuration: %w", err)
	}

	result := Grid{
		Address:      parsed.Section("server").Key("address").MustString("127.0.0.1:42000"),
		PublicURL:    parsed.Section("server").Key("public_url").MustString("http://127.0.0.1:42000"),
		Name:         strings.TrimSpace(parsed.Section("grid").Key("name").MustString("HomeWorldz")),
		DatabaseURL:  parsed.Section("database").Key("url").String(),
		ServiceToken: parsed.Section("auth").Key("service_token").String(),
		Directory:    resolved,
	}
	if result.Name == "" || len(result.Name) > 128 {
		return Grid{}, fmt.Errorf("invalid grid name %q", result.Name)
	}
	result.PublicURL = strings.TrimRight(result.PublicURL, "/")
	publicURL, err := url.Parse(result.PublicURL)
	if err != nil || (publicURL.Scheme != "http" && publicURL.Scheme != "https") || publicURL.Host == "" ||
		publicURL.User != nil || publicURL.RawQuery != "" || publicURL.Fragment != "" {
		return Grid{}, fmt.Errorf("invalid grid public URL %q", result.PublicURL)
	}
	return result, nil
}
