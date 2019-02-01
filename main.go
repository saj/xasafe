package main

import (
	"fmt"
	"os"
	"os/exec"
	"os/signal"

	"golang.org/x/sys/unix"
)

func main() {
	args := os.Args[1:]
	c := exec.Command(args[0], args[1:]...)
	c.Stdout = os.Stdout
	c.Stderr = os.Stderr
	if err := runForwardSignals(c); err != nil {
		fmt.Fprintf(os.Stderr, "xasafe: child: %v\n", err)
		os.Exit(255)
	}
}

var forwardedSignals = []os.Signal{unix.SIGINT, unix.SIGTERM}

func runForwardSignals(c *exec.Cmd) error {
	sigs := make(chan os.Signal, 1)
	signal.Notify(sigs, forwardedSignals...)
	defer func() {
		signal.Stop(sigs)
		close(sigs)
	}()

	if err := c.Start(); err != nil {
		return err
	}
	result := make(chan error)
	go func() {
		result <- c.Wait()
		close(result)
	}()

	var err error
loop:
	for {
		select {
		case s := <-sigs:
			c.Process.Signal(s)
		case r := <-result:
			err = r
			break loop
		}
	}
	return err
}
