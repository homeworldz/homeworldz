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

	// Website API ([website] and [mail] sections). These configure the
	// separate browser-facing homeworldz-api binary; the grid binary ignores them.
	WebsiteAddress        string
	WebsiteAllowedOrigins []string
	WebsiteJWTSecret      string
	WebsiteJWTIssuer      string
	WebsiteJWTAudience    string
	WebsiteTokenTTL       time.Duration
	WebsiteRatePerMinute  int
	WebsiteRateBurst      int
	MailTransport         string
	MailFrom              string
	MailVerificationURL   string
	SMTPHost              string
	SMTPPort              int
	SMTPUsername          string
	SMTPPassword          string
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

	website := parsed.Section("website")
	result.WebsiteAddress = website.Key("address").MustString("127.0.0.1:42010")
	result.WebsiteAllowedOrigins = splitList(website.Key("allowed_origins").
		MustString("https://homeworldz.com,https://www.homeworldz.com"))
	result.WebsiteJWTSecret = website.Key("jwt_secret").String()
	result.WebsiteJWTIssuer = website.Key("jwt_issuer").MustString("https://api.homeworldz.com")
	result.WebsiteJWTAudience = website.Key("jwt_audience").MustString("https://homeworldz.com")
	result.WebsiteTokenTTL = time.Duration(website.Key("token_ttl_seconds").MustInt(3600)) * time.Second
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
