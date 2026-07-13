//go:build !windows

package main

import (
	"context"
	"os/exec"
)

func configureBackground(_ *exec.Cmd) {}

func prepareViewerLaunch(_ string) error { return nil }

func waitForViewer(ctx context.Context, command *exec.Cmd, _ string) error {
	done := make(chan error, 1)
	go func() { done <- command.Wait() }()
	select {
	case err := <-done:
		return err
	case <-ctx.Done():
		_ = command.Process.Kill()
		<-done
		return ctx.Err()
	}
}
