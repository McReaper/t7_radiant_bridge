// Command t7rb is the M0 spike's client: it sends one command to the T7RadiantBridge plugin's named pipe and prints the reply.
//
//	t7rb [-pid N] [-body file|-] <command> [args...]
//
// Without -pid it uses the only \\.\pipe\t7_radiant_bridge_<pid> it finds.
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"
)

const prefix = "t7_radiant_bridge_"

func findPipe(pid int) (string, error) {
	if pid > 0 {
		return fmt.Sprintf(`\\.\pipe\%s%d`, prefix, pid), nil
	}
	entries, err := os.ReadDir(`\\.\pipe\`)
	if err != nil {
		return "", err
	}
	var found []string
	for _, e := range entries {
		if strings.HasPrefix(e.Name(), prefix) {
			found = append(found, `\\.\pipe\`+e.Name())
		}
	}
	switch len(found) {
	case 0:
		return "", fmt.Errorf("no %s* pipe: is Radiant running with the plugin loaded?", prefix)
	case 1:
		return found[0], nil
	}
	return "", fmt.Errorf("several Radiants: pass -pid (%s)", strings.Join(found, ", "))
}

func call(path, request string) (string, error) {
	f, err := os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		return "", err
	}
	defer f.Close()
	msg := make([]byte, 4+len(request))
	binary.LittleEndian.PutUint32(msg, uint32(len(request)))
	copy(msg[4:], request)
	if _, err := f.Write(msg); err != nil {
		return "", err
	}
	var n [4]byte
	if _, err := io.ReadFull(f, n[:]); err != nil {
		return "", err
	}
	reply := make([]byte, binary.LittleEndian.Uint32(n[:]))
	if _, err := io.ReadFull(f, reply); err != nil {
		return "", err
	}
	return string(reply), nil
}

func main() {
	pid := flag.Int("pid", 0, "Radiant's process id (default: the only bridge pipe)")
	bodyPath := flag.String("body", "", "file whose content is the request body, - for stdin")
	flag.Parse()
	if flag.NArg() == 0 {
		fmt.Fprintln(os.Stderr, "usage: t7rb [-pid N] [-body file|-] <command> [args...]")
		os.Exit(2)
	}
	request := strings.Join(flag.Args(), " ")
	if *bodyPath != "" {
		var body []byte
		var err error
		if *bodyPath == "-" {
			body, err = io.ReadAll(os.Stdin)
		} else {
			body, err = os.ReadFile(*bodyPath)
		}
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		request += "\n" + string(body)
	}
	path, err := findPipe(*pid)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	reply, err := call(path, request)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Print(reply)
	if strings.HasPrefix(reply, "err") {
		os.Exit(1)
	}
}
