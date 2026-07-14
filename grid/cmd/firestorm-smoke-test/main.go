package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"time"

	"golang.org/x/term"
	ini "gopkg.in/ini.v1"
)

const (
	gridURL   = "http://127.0.0.1:42000"
	regionURL = "http://127.0.0.1:42001"
)

type options struct {
	firstName     string
	lastName      string
	firestormPath string
	configPath    string
	validateOnly  bool
}

type smokeConfig struct {
	firstName string
	lastName  string
	password  string
}

type childProcess struct {
	command *exec.Cmd
	done    chan struct{}
	stdout  *os.File
	stderr  *os.File
	mu      sync.Mutex
	err     error
}

func main() {
	opts := options{}
	flag.StringVar(&opts.firstName, "first", "", "Firestorm development user's first name (overrides smoke config)")
	flag.StringVar(&opts.lastName, "last", "", "Firestorm development user's last name (overrides smoke config)")
	flag.StringVar(&opts.firestormPath, "firestorm", "", "path to the Firestorm executable")
	flag.StringVar(&opts.configPath, "config", "", "path to smoke-test user config (default config/smoke-test.ini)")
	flag.BoolVar(&opts.validateOnly, "validate-only", false, "verify service startup without prompting or launching Firestorm")
	flag.Parse()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	if err := run(ctx, opts); err != nil {
		fmt.Fprintln(os.Stderr, "smoke test failed:", err)
		os.Exit(1)
	}
}

func run(ctx context.Context, opts options) error {
	root, err := repositoryRoot()
	if err != nil {
		return err
	}
	configPath := opts.configPath
	if configPath == "" {
		configPath = filepath.Join(root, "config", "smoke-test.ini")
	} else if !filepath.IsAbs(configPath) {
		configPath = filepath.Join(root, configPath)
	}
	smoke, err := loadSmokeConfig(configPath)
	if err != nil {
		return err
	}
	if opts.firstName == "" {
		opts.firstName = smoke.firstName
	}
	if opts.firstName == "" {
		opts.firstName = "Smoke"
	}
	if opts.lastName == "" {
		opts.lastName = smoke.lastName
	}
	if opts.lastName == "" {
		opts.lastName = "User"
	}
	gridExecutable := filepath.Join(root, "build", "windows-vcpkg", "grid", "homeworldz-grid.exe")
	regionExecutable := filepath.Join(root, "build", "windows-vcpkg", "region", "Debug", "homeworldz-region.exe")
	if err := buildGrid(ctx, root, gridExecutable); err != nil {
		return err
	}
	for _, executable := range []string{regionExecutable} {
		if info, err := os.Stat(executable); err != nil || info.IsDir() {
			return fmt.Errorf("required executable %q was not found; build the grid and run scripts\\build-region.ps1 -Test first", executable)
		}
	}

	databaseURL, err := iniValue(filepath.Join(root, "config", "db.ini"), "database", "url")
	if err != nil {
		return err
	}
	serviceToken, err := iniValue(filepath.Join(root, "config", "grid.ini"), "auth", "service_token")
	if err != nil {
		return err
	}
	if serviceToken == "" {
		return errors.New("grid service token is empty")
	}

	if opts.firestormPath == "" {
		opts.firestormPath = defaultFirestormPath()
	}
	if !opts.validateOnly {
		if info, err := os.Stat(opts.firestormPath); err != nil || info.IsDir() {
			return fmt.Errorf("Firestorm was not found at %q; install it or pass -firestorm", opts.firestormPath)
		}
	}

	logDirectory := filepath.Join(root, "var", "smoke-test")
	if err := os.MkdirAll(logDirectory, 0o700); err != nil {
		return fmt.Errorf("create smoke-test log directory: %w", err)
	}
	stamp := time.Now().Format("20060102-150405")
	environment := environmentWith(map[string]string{
		"HOMEWORLDZ_CONFIG_DIR":             filepath.Join(root, "config"),
		"HOMEWORLDZ_DATABASE_URL":           databaseURL,
		"HOMEWORLDZ_GRID_PUBLIC_URL":        gridURL,
		"HOMEWORLDZ_GRID_SERVICE_TOKEN":     serviceToken,
		"HOMEWORLDZ_GRID_URL":               gridURL,
		"HOMEWORLDZ_REGION_PUBLIC_ENDPOINT": regionURL,
		"HOMEWORLDZ_REGION_DATA_PATH":       filepath.Join(root, "var", "region"),
		"HOMEWORLDZ_REGION_ASSET_PATH":      filepath.Join(root, "assets", "region"),
	})

	grid, err := startChild(gridExecutable, root, environment,
		filepath.Join(logDirectory, stamp+"-grid.stdout.log"),
		filepath.Join(logDirectory, stamp+"-grid.stderr.log"))
	if err != nil {
		return fmt.Errorf("start grid: %w", err)
	}
	defer grid.stop()
	if err := waitReady(ctx, gridURL+"/ready", grid); err != nil {
		return fmt.Errorf("grid did not become ready (inspect %s): %w", logDirectory, err)
	}

	region, err := startChild(regionExecutable, root, environment,
		filepath.Join(logDirectory, stamp+"-region.stdout.log"),
		filepath.Join(logDirectory, stamp+"-region.stderr.log"))
	if err != nil {
		return fmt.Errorf("start region: %w", err)
	}
	defer region.stop()
	if err := waitReady(ctx, regionURL+"/ready", region); err != nil {
		return fmt.Errorf("region did not become ready (inspect %s): %w", logDirectory, err)
	}

	fmt.Println("HomeWorldz grid and region are ready on loopback.")
	if opts.validateOnly {
		fmt.Println("Smoke-test launcher validation completed.")
		return nil
	}

	username := strings.ToLower(opts.firstName + "." + opts.lastName)
	if !validUsername(username) {
		return fmt.Errorf("combined development username %q is invalid", username)
	}
	password := []byte(smoke.password)
	if len(password) == 0 {
		password, err = readPassword(fmt.Sprintf("Password for Firestorm user %q (8-128 characters): ", opts.firstName+" "+opts.lastName))
		if err != nil {
			return err
		}
	}
	defer clear(password)
	if len(password) < 8 || len(password) > 128 {
		return errors.New("development-user password must contain 8 to 128 characters")
	}
	if err := ensureDevelopmentUser(ctx, serviceToken, username, password); err != nil {
		return err
	}

	fmt.Printf("Launching Firestorm for %q.\n", opts.firstName+" "+opts.lastName)
	fmt.Println("Keep this terminal open. Exit Firestorm after the login, disconnect, and reconnect checks are complete.")
	if err := prepareViewerLaunch(opts.firestormPath); err != nil {
		return err
	}
	viewer := exec.Command(opts.firestormPath, viewerArguments(gridURL)...)
	viewer.Dir = root
	if err := viewer.Start(); err != nil {
		return fmt.Errorf("launch Firestorm: %w", err)
	}
	if err := waitForViewer(ctx, viewer, opts.firestormPath); err != nil {
		return fmt.Errorf("Firestorm exited: %w", err)
	}
	return nil
}

func buildGrid(ctx context.Context, root, executable string) error {
	if err := os.MkdirAll(filepath.Dir(executable), 0o755); err != nil {
		return fmt.Errorf("create grid build directory: %w", err)
	}
	command := exec.CommandContext(ctx, "go", "build", "-o", executable, "./grid/cmd/grid")
	command.Dir = root
	output, err := command.CombinedOutput()
	if err != nil {
		return fmt.Errorf("build grid executable: %w: %s", err, strings.TrimSpace(string(output)))
	}
	return nil
}

func viewerArguments(gridURL string) []string {
	return []string{"--grid", gridURL, "--novoice"}
}

func iniValue(path, section, key string) (string, error) {
	parsed, err := ini.LoadSources(ini.LoadOptions{IgnoreInlineComment: true}, path)
	if err != nil {
		return "", fmt.Errorf("load %s: %w", path, err)
	}
	value := parsed.Section(section).Key(key).Value()
	if value == "" {
		return "", fmt.Errorf("missing [%s] %s in %s", section, key, path)
	}
	return value, nil
}

func loadSmokeConfig(path string) (smokeConfig, error) {
	parsed, err := ini.LoadSources(ini.LoadOptions{IgnoreInlineComment: true}, path)
	if errors.Is(err, os.ErrNotExist) {
		return smokeConfig{}, nil
	}
	if err != nil {
		return smokeConfig{}, fmt.Errorf("load %s: %w", path, err)
	}
	user := parsed.Section("user")
	return smokeConfig{
		firstName: strings.TrimSpace(user.Key("first_name").Value()),
		lastName:  strings.TrimSpace(user.Key("last_name").Value()),
		password:  user.Key("password").Value(),
	}, nil
}

func startChild(executable, directory string, environment []string, stdoutPath, stderrPath string) (*childProcess, error) {
	stdout, err := os.Create(stdoutPath)
	if err != nil {
		return nil, err
	}
	stderr, err := os.Create(stderrPath)
	if err != nil {
		stdout.Close()
		return nil, err
	}
	command := exec.Command(executable)
	command.Dir = directory
	command.Env = environment
	command.Stdout = stdout
	command.Stderr = stderr
	configureBackground(command)
	if err := command.Start(); err != nil {
		stdout.Close()
		stderr.Close()
		return nil, err
	}
	child := &childProcess{command: command, done: make(chan struct{}), stdout: stdout, stderr: stderr}
	go func() {
		err := command.Wait()
		child.mu.Lock()
		child.err = err
		child.mu.Unlock()
		close(child.done)
	}()
	return child, nil
}

func (child *childProcess) exited() (bool, error) {
	select {
	case <-child.done:
		child.mu.Lock()
		defer child.mu.Unlock()
		return true, child.err
	default:
		return false, nil
	}
}

func (child *childProcess) stop() {
	select {
	case <-child.done:
	default:
		_ = child.command.Process.Kill()
		<-child.done
	}
	_ = child.stdout.Close()
	_ = child.stderr.Close()
}

func waitReady(ctx context.Context, uri string, child *childProcess) error {
	client := &http.Client{Timeout: time.Second}
	deadline := time.NewTimer(10 * time.Second)
	defer deadline.Stop()
	ticker := time.NewTicker(200 * time.Millisecond)
	defer ticker.Stop()
	for {
		if exited, err := child.exited(); exited {
			if err == nil {
				err = errors.New("process exited")
			}
			return err
		}
		request, _ := http.NewRequestWithContext(ctx, http.MethodGet, uri, nil)
		if response, err := client.Do(request); err == nil {
			io.Copy(io.Discard, response.Body)
			response.Body.Close()
			if response.StatusCode == http.StatusOK {
				return nil
			}
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-deadline.C:
			return errors.New("readiness timeout")
		case <-ticker.C:
		}
	}
}

func ensureDevelopmentUser(ctx context.Context, token, username string, password []byte) error {
	requestBody, err := json.Marshal(map[string]string{"username": username, "password": string(password)})
	if err != nil {
		return err
	}
	status, responseBody, err := apiRequest(ctx, http.MethodPost, gridURL+"/api/v1/users", token, requestBody)
	clear(requestBody)
	if err != nil {
		return err
	}
	if status == http.StatusCreated {
		return nil
	}
	if status != http.StatusConflict {
		return fmt.Errorf("development-user creation returned HTTP %d: %s", status, responseBody)
	}

	sessionBody, err := json.Marshal(map[string]any{
		"username": username, "password": string(password), "sessionSeconds": 300,
	})
	if err != nil {
		return err
	}
	status, responseBody, err = apiRequest(ctx, http.MethodPost, gridURL+"/api/v1/sessions", token, sessionBody)
	clear(sessionBody)
	if err != nil {
		return err
	}
	if status != http.StatusCreated {
		return fmt.Errorf("development user %q exists, but the supplied password is not valid", username)
	}
	var session struct {
		ID string `json:"id"`
	}
	if err := json.Unmarshal(responseBody, &session); err != nil || session.ID == "" {
		return errors.New("decode development-user validation session")
	}
	_, _, err = apiRequest(ctx, http.MethodDelete, gridURL+"/api/v1/sessions/"+session.ID, token, nil)
	return err
}

func apiRequest(ctx context.Context, method, uri, token string, body []byte) (int, []byte, error) {
	request, err := http.NewRequestWithContext(ctx, method, uri, bytes.NewReader(body))
	if err != nil {
		return 0, nil, err
	}
	request.Header.Set("Authorization", "Bearer "+token)
	if body != nil {
		request.Header.Set("Content-Type", "application/json")
	}
	response, err := (&http.Client{Timeout: 5 * time.Second}).Do(request)
	if err != nil {
		return 0, nil, err
	}
	defer response.Body.Close()
	responseBody, err := io.ReadAll(io.LimitReader(response.Body, 1<<20))
	return response.StatusCode, responseBody, err
}

func readPassword(prompt string) ([]byte, error) {
	fmt.Print(prompt)
	descriptor := int(os.Stdin.Fd())
	if !term.IsTerminal(descriptor) {
		return nil, errors.New("password input requires an interactive terminal")
	}
	password, err := term.ReadPassword(descriptor)
	fmt.Println()
	if err != nil {
		return nil, fmt.Errorf("read password: %w", err)
	}
	return password, nil
}

func validUsername(value string) bool {
	if len(value) < 3 || len(value) > 32 {
		return false
	}
	for _, character := range value {
		if character >= 'a' && character <= 'z' || character >= '0' && character <= '9' ||
			character == '.' || character == '_' || character == '-' {
			continue
		}
		return false
	}
	return true
}

func environmentWith(overrides map[string]string) []string {
	result := make([]string, 0, len(os.Environ())+len(overrides))
	for _, entry := range os.Environ() {
		name, _, found := strings.Cut(entry, "=")
		if !found {
			result = append(result, entry)
			continue
		}
		overridden := false
		for key := range overrides {
			if strings.EqualFold(name, key) {
				overridden = true
				break
			}
		}
		if !overridden {
			result = append(result, entry)
		}
	}
	for key, value := range overrides {
		result = append(result, key+"="+value)
	}
	return result
}

func defaultFirestormPath() string {
	if runtime.GOOS != "windows" {
		return ""
	}
	return filepath.Join(os.Getenv("ProgramFiles"), "FirestormOS-Releasex64", "FirestormOS-Releasex64.exe")
}

func repositoryRoot() (string, error) {
	workingDirectory, err := os.Getwd()
	if err != nil {
		return "", err
	}
	for _, candidate := range []string{workingDirectory, filepath.Dir(workingDirectory), filepath.Dir(filepath.Dir(workingDirectory))} {
		if _, err := os.Stat(filepath.Join(candidate, "docs", "PLAN.md")); err == nil {
			return candidate, nil
		}
	}
	return "", errors.New("repository root was not found")
}

func clear(value []byte) {
	for index := range value {
		value[index] = 0
	}
}
