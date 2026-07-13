//go:build !windows

package main

import "os/exec"

func configureBackground(_ *exec.Cmd) {}
