package main

import (
	"bufio"
	"context"
	"database/sql"
	"errors"
	"fmt"
	"os"
	"strings"
	"syscall"

	"github.com/homeworldz/homeworldz/grid/internal/config"
	"github.com/homeworldz/homeworldz/grid/internal/identity"
	"github.com/homeworldz/homeworldz/grid/internal/inventory"
	_ "github.com/jackc/pgx/v5/stdlib"
	"golang.org/x/term"
)

const libraryUsername = "homeworldz.library"

func main() {
	if err := run(context.Background()); err != nil {
		fmt.Fprintln(os.Stderr, "configure library failed:", err)
		os.Exit(1)
	}
}

func run(ctx context.Context) error {
	settings, err := config.LoadGrid()
	if err != nil {
		return err
	}
	if settings.DatabaseURL == "" {
		return errors.New("database URL is not configured")
	}
	password, err := readPassword("Password for 'HomeWorldz Library': ")
	if err != nil {
		return err
	}
	confirmation, err := readPassword("Confirm password for 'HomeWorldz Library': ")
	if err != nil {
		return err
	}
	if password == "" || password != confirmation {
		return errors.New("passwords are empty or do not match")
	}
	database, err := sql.Open("pgx", settings.DatabaseURL)
	if err != nil {
		return fmt.Errorf("open database: %w", err)
	}
	defer database.Close()
	user, err := identity.NewPostgresStore(database).ConfigureSystemUser(
		ctx, inventory.LibraryOwnerID, libraryUsername, password)
	if err != nil {
		return err
	}
	fmt.Printf("Configured %s (%s).\n", user.Username, user.ID)
	return nil
}

func readPassword(prompt string) (string, error) {
	fmt.Print(prompt)
	if term.IsTerminal(int(syscall.Stdin)) {
		value, err := term.ReadPassword(int(syscall.Stdin))
		fmt.Println()
		if err != nil {
			return "", fmt.Errorf("read password: %w", err)
		}
		return string(value), nil
	}
	value, err := bufio.NewReader(os.Stdin).ReadString('\n')
	if err != nil {
		return "", fmt.Errorf("read password: %w", err)
	}
	return strings.TrimSpace(value), nil
}
