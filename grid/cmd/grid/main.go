package main

import (
	"context"
	"database/sql"
	"errors"
	"flag"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/assetmeta"
	"github.com/homeworldz/homeworldz/grid/internal/config"
	"github.com/homeworldz/homeworldz/grid/internal/httpapi"
	"github.com/homeworldz/homeworldz/grid/internal/identity"
	"github.com/homeworldz/homeworldz/grid/internal/inventory"
	"github.com/homeworldz/homeworldz/grid/internal/presence"
	"github.com/homeworldz/homeworldz/grid/internal/regions"
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

	var db *sql.DB
	if settings.DatabaseURL != "" {
		db, err = sql.Open("pgx", settings.DatabaseURL)
		if err != nil {
			logger.Error("open database", "error", err)
			os.Exit(1)
		}
		defer db.Close()
	}

	server := &http.Server{
		Addr: settings.Address,
		Handler: httpapi.New(db, version, httpapi.Options{
			ServiceToken:  settings.ServiceToken,
			GridPublicURL: settings.PublicURL,
			Logger:        logger,
			Regions:       regionStore(db),
			Identity:      identityStore(db),
			Presence:      presenceStore(db),
			Inventory:     inventoryStore(db),
			Assets:        assetStore(db),
		}),
		ReadHeaderTimeout: 5 * time.Second,
	}
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	go func() {
		logger.Info("grid service listening", "address", settings.Address,
			"version", version, "configDirectory", settings.Directory)
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

func regionStore(db *sql.DB) regions.Store {
	if db == nil {
		return nil
	}
	return regions.NewPostgresStore(db)
}

func identityStore(db *sql.DB) identity.Store {
	if db == nil {
		return nil
	}
	return identity.NewPostgresStore(db)
}

func presenceStore(db *sql.DB) presence.Store {
	if db == nil {
		return nil
	}
	return presence.NewPostgresStore(db)
}

func inventoryStore(db *sql.DB) inventory.Store {
	if db == nil {
		return nil
	}
	return inventory.NewPostgresStore(db)
}

func assetStore(db *sql.DB) assetmeta.Store {
	if db == nil {
		return nil
	}
	return assetmeta.NewPostgresStore(db)
}
