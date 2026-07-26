package config

import (
	"fmt"
	"net/url"
	"path/filepath"
	"strings"
	"time"

	ini "gopkg.in/ini.v1"
)

type Grid struct {
	Address      string
	PublicURL    string
	Name         string
	DatabaseURL  string
	ServiceToken string
	Directory    string
	// WelcomeLocations is the ordered new-arrival list ([grid]
	// welcome_locations): comma-separated Region/x/y/z entries, first entry
	// preferred. Parsing into structured points happens at the consumer so a
	// malformed entry fails that binary's startup with a specific error.
	WelcomeLocations []string
	// VaultPath is the filesystem root of the asset vault (ADR 0026), holding the
	// durable bytes behind inventory-referenced assets. A relative value resolves
	// against the process working directory, as the region's data_path does.
	VaultPath string

	// Website API ([website] and [mail] sections). These configure the
	// separate browser-facing homeworldz-api binary; the grid binary ignores them.
	WebsiteAddress        string
	WebsiteAllowedOrigins []string
	WebsiteJWTSecret      string
	WebsiteJWTIssuer      string
	WebsiteJWTAudience    string
	WebsiteTokenTTL       time.Duration
	// RegionTicketTTL is the short lifetime of region-scoped tickets minted at
	// world entry ([website] region_ticket_ttl_seconds, default 300).
	RegionTicketTTL time.Duration
	WebsiteRatePerMinute  int
	WebsiteRateBurst      int
	MailTransport         string
	MailFrom              string
	MailVerificationURL   string
	SMTPHost              string
	SMTPPort              int
	SMTPUsername          string
	SMTPPassword          string
	SMTPImplicitTLS       bool
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
		Name:         strings.TrimSpace(parsed.Section("grid").Key("name").MustString("Homeworldz")),
		DatabaseURL:  parsed.Section("database").Key("url").String(),
		ServiceToken: parsed.Section("auth").Key("service_token").String(),
		Directory:    resolved,
		VaultPath: strings.TrimSpace(parsed.Section("vault").Key("path").
			MustString(filepath.Join("var", "vault"))),
		WelcomeLocations: splitList(parsed.Section("grid").Key("welcome_locations").String()),
	}
	if result.VaultPath == "" {
		return Grid{}, fmt.Errorf("invalid asset vault path %q", result.VaultPath)
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

	website := parsed.Section("website")
	result.WebsiteAddress = website.Key("address").MustString("127.0.0.1:42010")
	result.WebsiteAllowedOrigins = splitList(website.Key("allowed_origins").
		MustString("https://homeworldz.com,https://www.homeworldz.com"))
	result.WebsiteJWTSecret = website.Key("jwt_secret").String()
	result.WebsiteJWTIssuer = website.Key("jwt_issuer").MustString("https://api.homeworldz.com")
	result.WebsiteJWTAudience = website.Key("jwt_audience").MustString("https://homeworldz.com")
	result.WebsiteTokenTTL = time.Duration(website.Key("token_ttl_seconds").MustInt(3600)) * time.Second
	result.RegionTicketTTL = time.Duration(website.Key("region_ticket_ttl_seconds").MustInt(300)) * time.Second
	result.WebsiteRatePerMinute = website.Key("rate_per_minute").MustInt(30)
	result.WebsiteRateBurst = website.Key("rate_burst").MustInt(10)

	mail := parsed.Section("mail")
	result.MailTransport = strings.ToLower(strings.TrimSpace(mail.Key("transport").MustString("log")))
	result.MailFrom = mail.Key("from").MustString("no-reply@homeworldz.com")
	result.MailVerificationURL = mail.Key("verification_url").MustString("https://homeworldz.com/verify")
	result.SMTPHost = mail.Key("smtp_host").String()
	result.SMTPPort = mail.Key("smtp_port").MustInt(587)
	result.SMTPUsername = mail.Key("smtp_username").String()
	result.SMTPPassword = mail.Key("smtp_password").String()
	// Implicit TLS (SMTPS) defaults on for the conventional port 465; a relay on
	// another port can force it with smtp_implicit_tls = true.
	result.SMTPImplicitTLS = mail.Key("smtp_implicit_tls").MustBool(result.SMTPPort == 465)

	return result, nil
}

// splitList parses a comma-separated INI value into trimmed, non-empty entries.
func splitList(value string) []string {
	parts := strings.Split(value, ",")
	items := make([]string, 0, len(parts))
	for _, part := range parts {
		if trimmed := strings.TrimSpace(part); trimmed != "" {
			items = append(items, trimmed)
		}
	}
	return items
}
