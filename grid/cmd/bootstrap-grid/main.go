package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"syscall"

	"github.com/jackc/pgx/v5"
	"golang.org/x/term"
)

type options struct {
	host      string
	port      int
	adminUser string
	adminDB   string
	appUser   string
	appDB     string
	configDir string
	migration string
}

func main() {
	root := repositoryRoot()
	opts := options{}
	flag.StringVar(&opts.host, "host", "localhost", "PostgreSQL server host")
	flag.IntVar(&opts.port, "port", 5432, "PostgreSQL server port")
	flag.StringVar(&opts.adminUser, "admin-user", "postgres", "PostgreSQL administrator role")
	flag.StringVar(&opts.adminDB, "admin-database", "postgres", "administrator connection database")
	flag.StringVar(&opts.appUser, "application-user", "homeworldz", "HomeWorldz application role")
	flag.StringVar(&opts.appDB, "application-database", "homeworldz", "HomeWorldz database")
	flag.StringVar(&opts.configDir, "config-dir", filepath.Join(root, "config"), "HomeWorldz configuration directory")
	flag.StringVar(&opts.migration, "migration", filepath.Join(root, "db", "migrations", "000001_initial.up.sql"), "initial SQL migration")
	flag.Parse()

	if err := run(context.Background(), opts); err != nil {
		fmt.Fprintln(os.Stderr, "bootstrap failed:", err)
		os.Exit(1)
	}
}

func run(ctx context.Context, opts options) error {
	adminPassword, err := readPassword("PostgreSQL password for '" + opts.adminUser + "': ")
	if err != nil {
		return err
	}
	appPassword, err := readPassword("Password to assign to '" + opts.appUser + "': ")
	if err != nil {
		return err
	}
	confirmation, err := readPassword("Confirm password for '" + opts.appUser + "': ")
	if err != nil {
		return err
	}
	if appPassword == "" {
		return errors.New("application password cannot be empty")
	}
	if appPassword != confirmation {
		return errors.New("application passwords do not match")
	}

	fmt.Println("Checking PostgreSQL connection...")
	admin, err := pgx.Connect(ctx, connectionURL(opts.host, opts.port, opts.adminUser, adminPassword, opts.adminDB))
	if err != nil {
		return fmt.Errorf("connect as administrator: %w", err)
	}
	defer admin.Close(ctx)

	var roleExists bool
	if err := admin.QueryRow(ctx, "SELECT EXISTS (SELECT 1 FROM pg_roles WHERE rolname=$1)", opts.appUser).Scan(&roleExists); err != nil {
		return fmt.Errorf("check application role: %w", err)
	}
	roleSQL, err := formattedSQL(ctx, admin, map[bool]string{true: "ALTER ROLE %I LOGIN PASSWORD %L", false: "CREATE ROLE %I LOGIN PASSWORD %L"}[roleExists], opts.appUser, appPassword)
	if err != nil {
		return err
	}
	action := "Creating"
	if roleExists {
		action = "Updating"
	}
	fmt.Printf("%s role %q...\n", action, opts.appUser)
	if _, err := admin.Exec(ctx, roleSQL); err != nil {
		return fmt.Errorf("configure application role: %w", err)
	}

	var databaseExists bool
	if err := admin.QueryRow(ctx, "SELECT EXISTS (SELECT 1 FROM pg_database WHERE datname=$1)", opts.appDB).Scan(&databaseExists); err != nil {
		return fmt.Errorf("check application database: %w", err)
	}
	if !databaseExists {
		createSQL, err := formattedSQL(ctx, admin, "CREATE DATABASE %I OWNER %I", opts.appDB, opts.appUser)
		if err != nil {
			return err
		}
		fmt.Printf("Creating database %q...\n", opts.appDB)
		if _, err := admin.Exec(ctx, createSQL); err != nil {
			return fmt.Errorf("create application database: %w", err)
		}
	} else {
		fmt.Printf("Database %q already exists.\n", opts.appDB)
	}

	appConfig, err := pgx.ParseConfig(connectionURL(opts.host, opts.port, opts.appUser, appPassword, opts.appDB))
	if err != nil {
		return fmt.Errorf("parse application connection: %w", err)
	}
	appConfig.DefaultQueryExecMode = pgx.QueryExecModeSimpleProtocol
	application, err := pgx.ConnectConfig(ctx, appConfig)
	if err != nil {
		return fmt.Errorf("connect as application role: %w", err)
	}
	defer application.Close(ctx)

	var migrationApplied bool
	if err := application.QueryRow(ctx, "SELECT to_regclass('public.schema_metadata') IS NOT NULL").Scan(&migrationApplied); err != nil {
		return fmt.Errorf("check migration state: %w", err)
	}
	if migrationApplied {
		fmt.Println("Initial migration is already applied.")
	} else {
		migration, err := os.ReadFile(opts.migration)
		if err != nil {
			return fmt.Errorf("read migration: %w", err)
		}
		fmt.Println("Applying initial migration...")
		if _, err := application.Exec(ctx, string(migration)); err != nil {
			return fmt.Errorf("apply initial migration: %w", err)
		}
	}

	if err := writeDatabaseConfig(opts.configDir, connectionURL(opts.host, opts.port, opts.appUser, appPassword, opts.appDB)); err != nil {
		return err
	}
	fmt.Println("HomeWorldz grid bootstrap completed.")
	fmt.Println("Database configuration written to", filepath.Join(opts.configDir, "db.ini"))
	return nil
}

func formattedSQL(ctx context.Context, conn *pgx.Conn, format string, args ...any) (string, error) {
	queryArgs := make([]any, 0, len(args)+1)
	queryArgs = append(queryArgs, format)
	queryArgs = append(queryArgs, args...)
	placeholders := make([]string, len(queryArgs))
	for i := range queryArgs {
		placeholders[i] = fmt.Sprintf("$%d", i+1)
	}
	var result string
	if err := conn.QueryRow(ctx, "SELECT format("+strings.Join(placeholders, ",")+")", queryArgs...).Scan(&result); err != nil {
		return "", fmt.Errorf("format PostgreSQL command: %w", err)
	}
	return result, nil
}

func connectionURL(host string, port int, user, password, database string) string {
	u := &url.URL{Scheme: "postgres", Host: fmt.Sprintf("%s:%d", host, port), Path: "/" + database}
	u.User = url.UserPassword(user, password)
	query := u.Query()
	query.Set("sslmode", "disable")
	u.RawQuery = query.Encode()
	return u.String()
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

func writeDatabaseConfig(directory, databaseURL string) error {
	if err := os.MkdirAll(directory, 0o700); err != nil {
		return fmt.Errorf("create config directory: %w", err)
	}
	contents := "[database]\nurl = " + databaseURL + "\n"
	if err := os.WriteFile(filepath.Join(directory, "db.ini"), []byte(contents), 0o600); err != nil {
		return fmt.Errorf("write database config: %w", err)
	}
	return nil
}

func repositoryRoot() string {
	workingDirectory, err := os.Getwd()
	if err != nil {
		return "."
	}
	for _, candidate := range []string{workingDirectory, filepath.Dir(workingDirectory)} {
		if _, err := os.Stat(filepath.Join(candidate, "db", "migrations", "000001_initial.up.sql")); err == nil {
			return candidate
		}
	}
	return workingDirectory
}
