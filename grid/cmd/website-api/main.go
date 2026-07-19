// Command website-api serves the browser-facing HomeWorldz website API
// (homeworldz.com/api/openapi.yaml): email-verified avatar registration,
// stateless website authentication, self-service account management, and
// privileged administration. It runs as its own binary on its own port,
// separate from the grid's service-token internal API.
package main

import (
	"context"
	"database/sql"
	"errors"
	"flag"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/config"
	"github.com/homeworldz/homeworldz/grid/internal/mailer"
	"github.com/homeworldz/homeworldz/grid/internal/provisioning"
	"github.com/homeworldz/homeworldz/grid/internal/regions"
	"github.com/homeworldz/homeworldz/grid/internal/webaccount"
	"github.com/homeworldz/homeworldz/grid/internal/webapi"
	"github.com/homeworldz/homeworldz/grid/internal/webtoken"
	_ "github.com/jackc/pgx/v5/stdlib"
)

var version = "dev"

func main() {
	configDirectory := flag.String("config", "config", "directory containing grid.ini and db.ini")
	flag.Parse()

	logger := slog.New(slog.NewJSONHandler(os.Stdout, nil))
	settings, err := config.LoadGrid(*configDirectory)
	if err != nil {
		logger.Error("load configuration", "error", err)
		os.Exit(1)
	}
	if settings.DatabaseURL == "" {
		logger.Error("website api requires a database url ([database] url in db.ini)")
		os.Exit(1)
	}

	db, err := sql.Open("pgx", settings.DatabaseURL)
	if err != nil {
		logger.Error("open database", "error", err)
		os.Exit(1)
	}
	defer db.Close()

	signer, err := webtoken.NewSigner([]byte(settings.WebsiteJWTSecret),
		settings.WebsiteJWTIssuer, settings.WebsiteJWTAudience, settings.WebsiteTokenTTL)
	if err != nil {
		logger.Error("configure token signer (set [website] jwt_secret)", "error", err)
		os.Exit(1)
	}

	mail, err := buildMailer(settings, logger)
	if err != nil {
		logger.Error("configure mailer", "error", err)
		os.Exit(1)
	}

	handler, err := webapi.New(webapi.Options{
		Accounts:        webaccount.NewPostgresStore(db),
		Regions:         provisioning.NewPostgresStore(db),
		Leases:          regions.NewPostgresStore(db),
		Signer:          signer,
		Mailer:          mail,
		Logger:          logger,
		AllowedOrigins:  settings.WebsiteAllowedOrigins,
		VerificationURL: settings.MailVerificationURL,
		RatePerMinute:   settings.WebsiteRatePerMinute,
		RateBurst:       settings.WebsiteRateBurst,
	})
	if err != nil {
		logger.Error("build website api", "error", err)
		os.Exit(1)
	}

	server := &http.Server{
		Addr:              settings.WebsiteAddress,
		Handler:           handler,
		ReadHeaderTimeout: 5 * time.Second,
	}
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	go func() {
		logger.Info("website api listening", "address", settings.WebsiteAddress,
			"version", version, "mailTransport", settings.MailTransport)
		if err := server.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			logger.Error("serve", "error", err)
			os.Exit(1)
		}
	}()

	<-stop
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := server.Shutdown(ctx); err != nil {
		logger.Error("shutdown", "error", err)
	}
}

// buildMailer selects the mail transport. "log" (the default) writes the
// verification link to the log instead of sending email, which is what local
// development uses; "smtp" sends through the configured relay.
func buildMailer(settings config.Grid, logger *slog.Logger) (mailer.Mailer, error) {
	switch settings.MailTransport {
	case "smtp":
		return mailer.NewSMTPMailer(mailer.SMTPConfig{
			Host:     settings.SMTPHost,
			Port:     settings.SMTPPort,
			Username: settings.SMTPUsername,
			Password: settings.SMTPPassword,
			From:     settings.MailFrom,
		})
	case "log", "":
		return mailer.NewLogMailer(logger, settings.MailFrom), nil
	default:
		return nil, fmt.Errorf("unknown mail transport %q", settings.MailTransport)
	}
}
