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
	"sort"
	"strconv"
	"strings"
	"syscall"

	"github.com/homeworldz/homeworldz/grid/internal/config"
	"github.com/jackc/pgx/v5"
	"golang.org/x/term"
)

type options struct {
	host        string
	port        int
	adminUser   string
	adminDB     string
	appUser     string
	appDB       string
	configDir   string
	migrations  string
	migrateOnly bool
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
	flag.StringVar(&opts.migrations, "migrations", filepath.Join(root, "db", "migrations"), "SQL migration directory")
	flag.BoolVar(&opts.migrateOnly, "migrations-only", false, "apply pending migrations using the configured database URL")
	flag.Parse()

	var err error
	if opts.migrateOnly {
		err = runConfiguredMigrations(context.Background(), opts.migrations)
	} else {
		err = run(context.Background(), opts)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "bootstrap failed:", err)
		os.Exit(1)
	}
}

func runConfiguredMigrations(ctx context.Context, migrations string) error {
	settings, err := config.LoadGrid()
	if err != nil {
		return err
	}
	if settings.DatabaseURL == "" {
		return errors.New("database URL is not configured")
	}
	connection, err := pgx.Connect(ctx, settings.DatabaseURL)
	if err != nil {
		return fmt.Errorf("connect to configured database: %w", err)
	}
	defer connection.Close(ctx)
	if err := applyMigrations(ctx, connection, migrations); err != nil {
		return err
	}
	fmt.Println("HomeWorldz database migrations completed.")
	return nil
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

	if err := applyMigrations(ctx, application, opts.migrations); err != nil {
		return err
	}

	if err := writeDatabaseConfig(opts.configDir, connectionURL(opts.host, opts.port, opts.appUser, appPassword, opts.appDB)); err != nil {
		return err
	}
	fmt.Println("HomeWorldz grid bootstrap completed.")
	fmt.Println("Database configuration written to", filepath.Join(opts.configDir, "db.ini"))
	return nil
}

func applyMigrations(ctx context.Context, connection *pgx.Conn, directory string) error {
	paths, err := filepath.Glob(filepath.Join(directory, "*.up.sql"))
	if err != nil {
		return fmt.Errorf("find migrations in %s: %w", directory, err)
	}
	if len(paths) == 0 {
		return fmt.Errorf("find migrations in %s: no .up.sql files", directory)
	}
	sort.Strings(paths)
	for _, path := range paths {
		prefix, _, found := strings.Cut(filepath.Base(path), "_")
		version, err := strconv.ParseInt(prefix, 10, 64)
		if !found || err != nil {
			return fmt.Errorf("parse migration version from %s", path)
		}
		var metadataExists bool
		if err := connection.QueryRow(ctx, "SELECT to_regclass('public.schema_metadata') IS NOT NULL").Scan(&metadataExists); err != nil {
			return fmt.Errorf("check migration metadata: %w", err)
		}
		if metadataExists {
			var applied bool
			if err := connection.QueryRow(ctx, "SELECT EXISTS (SELECT 1 FROM schema_metadata WHERE version=$1)", version).Scan(&applied); err != nil {
				return fmt.Errorf("check migration %d: %w", version, err)
			}
			if applied {
				continue
			}
		}
		migration, err := os.ReadFile(path)
		if err != nil {
			return fmt.Errorf("read migration %d: %w", version, err)
		}
		fmt.Printf("Applying migration %06d...\n", version)
		if _, err := connection.Exec(ctx, string(migration)); err != nil {
			return fmt.Errorf("apply migration %d: %w", version, err)
		}
	}
	return nil
}

func formattedSQL(ctx context.Context, conn *pgx.Conn, format string, args ...any) (string, error) {
	queryArgs := make([]any, 0, len(args)+1)
	queryArgs = append(queryArgs, format)
	queryArgs = append(queryArgs, args...)
	var result string
	if err := conn.QueryRow(ctx, formatQuery(len(queryArgs)), queryArgs...).Scan(&result); err != nil {
		return "", fmt.Errorf("format PostgreSQL command: %w", err)
	}
	return result, nil
}

func formatQuery(argumentCount int) string {
	placeholders := make([]string, argumentCount)
	for i := range placeholders {
		// PostgreSQL's format() accepts polymorphic arguments, so parameters
		// after the format string have no type context under the extended query
		// protocol. All bootstrap values are textual identifiers or literals.
		placeholders[i] = fmt.Sprintf("$%d::text", i+1)
	}
	return "SELECT format(" + strings.Join(placeholders, ",") + ")"
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
