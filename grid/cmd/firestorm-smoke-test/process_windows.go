//go:build windows

package main

import (
	"bytes"
	"context"
	"encoding/csv"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"
)

func configureBackground(command *exec.Cmd) {
	command.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
}

func prepareViewerLaunch(executable string) error {
	pids, err := viewerProcessIDs(executable)
	if err != nil {
		return fmt.Errorf("inspect running Firestorm processes: %w", err)
	}
	if len(pids) != 0 {
		return errors.New("Firestorm is already running; exit it before starting the smoke test")
	}
	return nil
}

func waitForViewer(ctx context.Context, command *exec.Cmd, executable string) error {
	starterDone := make(chan error, 1)
	go func() { starterDone <- command.Wait() }()
	select {
	case err := <-starterDone:
		return waitForRelaunchedViewer(ctx, executable, err)
	case <-ctx.Done():
		_ = command.Process.Kill()
		<-starterDone
		killViewerProcesses(executable)
		return ctx.Err()
	}
}

func waitForRelaunchedViewer(ctx context.Context, executable string, starterError error) error {
	grace := time.NewTimer(3 * time.Second)
	defer grace.Stop()
	graceC := grace.C
	ticker := time.NewTicker(250 * time.Millisecond)
	defer ticker.Stop()
	viewerSeen := false
	for {
		pids, err := viewerProcessIDs(executable)
		if err != nil {
			return err
		}
		if len(pids) != 0 {
			viewerSeen = true
		} else if viewerSeen {
			return starterError
		}
		select {
		case <-ctx.Done():
			killViewerProcesses(executable)
			return ctx.Err()
		case <-graceC:
			if !viewerSeen {
				return starterError
			}
			graceC = nil
		case <-ticker.C:
		}
	}
}

func viewerProcessIDs(executable string) ([]int, error) {
	name := filepath.Base(executable)
	command := exec.Command("tasklist.exe", "/FI", "IMAGENAME eq "+name, "/FO", "CSV", "/NH")
	configureBackground(command)
	output, err := command.Output()
	if err != nil {
		return nil, err
	}
	return parseTasklist(output, name)
}

func parseTasklist(output []byte, name string) ([]int, error) {
	reader := csv.NewReader(bytes.NewReader(output))
	records, err := reader.ReadAll()
	if err != nil {
		return nil, err
	}
	var pids []int
	for _, record := range records {
		if len(record) < 2 || !strings.EqualFold(record[0], name) {
			continue
		}
		pid, err := strconv.Atoi(record[1])
		if err == nil {
			pids = append(pids, pid)
		}
	}
	return pids, nil
}

func killViewerProcesses(executable string) {
	pids, _ := viewerProcessIDs(executable)
	for _, pid := range pids {
		if process, err := os.FindProcess(pid); err == nil {
			_ = process.Kill()
		}
	}
}
