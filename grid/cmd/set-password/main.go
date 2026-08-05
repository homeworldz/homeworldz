// set-password replaces the password of a Homeworldz account.
//
// The account API's password change deliberately requires the current password,
// which is right for a user and leaves an operator with no way back into an
// account whose password is lost. This is that way back, and it is a console
// tool on the grid host on purpose: it needs the database URL and a terminal.
//
// The new password is only ever read from a prompt, never from a flag or an
// environment variable. A password on a command line is in the shell history and
// in every process listing on the machine for as long as the command runs, which
// turns a recovery into a disclosure.
package main

import (
	"bufio"
	"context"
	"database/sql"
	"errors"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"syscall"

	"github.com/homeworldz/server/grid/internal/config"
	"github.com/homeworldz/server/grid/internal/identity"
	_ "github.com/jackc/pgx/v5/stdlib"
	"golang.org/x/term"
)

func main() {
	root, _ := os.Getwd()
	configDirectory := flag.String("config", filepath.Join(root, "config"),
		"directory containing grid.ini and db.ini")
	username := flag.String("username", "", "account to set the password for, in first.last form, for example jim.tarber")
	flag.Parse()
	if err := run(context.Background(), *configDirectory, *username); err != nil {
		fmt.Fprintln(os.Stderr, "set password failed:", err)
		os.Exit(1)
	}
}

func run(ctx context.Context, configDirectory, username string) error {
	if strings.TrimSpace(username) == "" {
		return errors.New("-username is required; naming the account explicitly is what stops the wrong one being reset")
	}
	settings, err := config.LoadGrid(configDirectory)
	if err != nil {
		return err
	}
	if settings.DatabaseURL == "" {
		return errors.New("database URL is not configured")
	}
	password, err := readPassword("New password for " + username + ": ")
	if err != nil {
		return err
	}
	confirmation, err := readPassword("Confirm new password for " + username + ": ")
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
	user, err := identity.NewPostgresStore(database).SetPassword(ctx, username, password)
	if err != nil {
		return err
	}
	// The account, never the password.
	fmt.Printf("Password set for %s (%s).\n", user.Username, user.ID)
	return nil
}

// Shared so consecutive prompts read consecutive lines; see readPassword.
var pipedInput = bufio.NewReader(os.Stdin)

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
	// One reader for the whole process, not one per prompt. A fresh bufio.Reader
	// buffers everything available, so a second one created for the confirmation
	// prompt finds the stream already drained and returns EOF — which made the
	// piped path fail on every two-prompt run while the comment here claimed the
	// path was supported. Found by testing the claim rather than by reading it.
	value, err := pipedInput.ReadString('\n')
	if err != nil {
		return "", fmt.Errorf("read password: %w", err)
	}
	return strings.TrimSpace(value), nil
}
