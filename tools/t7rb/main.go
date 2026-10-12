// Command t7rb is the bridge's development client: one request to a running Radiant's T7RadiantBridge plugin, its
// events, or a hot swap of the plugin's DLL. t7kb is the real client; this one exercises PROTOCOL.md by hand.
//
//	t7rb [-pid N] [-body file|-] [-png file] <command> [args…]   one request after hello; prints the reply
//	t7rb [-pid N] listen <event>…                                 prints the events until interrupted
//	t7rb [-pid N] deploy <dll>                                     disables the plugin, copies the DLL in, enables it
//
// Without -pid it uses the only \\.\pipe\t7_radiant_bridge_<pid> there is. A capture's pixels go to -png, and its
// projected points are printed.
package main

import (
	"bufio"
	"encoding/binary"
	"flag"
	"fmt"
	"image"
	"image/png"
	"io"
	"os"
	"strconv"
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

type conn struct {
	f    *os.File
	next uint64
}

func dial(pid int) (*conn, error) {
	path, err := findPipe(pid)
	if err != nil {
		return nil, err
	}
	f, err := os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		return nil, err
	}
	c := &conn{f: f}
	if _, _, err := c.call("hello 1", nil); err != nil {
		f.Close()
		return nil, err
	}
	return c, nil
}

func (c *conn) send(payload []byte) error {
	msg := make([]byte, 4+len(payload))
	binary.LittleEndian.PutUint32(msg, uint32(len(payload)))
	copy(msg[4:], payload)
	_, err := c.f.Write(msg)
	return err
}

// read returns one frame's header line and body.
func (c *conn) read() (string, []byte, error) {
	var n [4]byte
	if _, err := io.ReadFull(c.f, n[:]); err != nil {
		return "", nil, err
	}
	b := make([]byte, binary.LittleEndian.Uint32(n[:]))
	if _, err := io.ReadFull(c.f, b); err != nil {
		return "", nil, err
	}
	head, body, _ := strings.Cut(string(b), "\n")
	return head, []byte(body), nil
}

// call sends one request and returns its reply's summary and body; an err reply is an error.
func (c *conn) call(line string, body []byte) (string, []byte, error) {
	c.next++
	id := strconv.FormatUint(c.next, 10)
	payload := []byte(id + " " + line + "\n")
	if err := c.send(append(payload, body...)); err != nil {
		return "", nil, err
	}
	for {
		head, b, err := c.read()
		if err != nil {
			return "", nil, err
		}
		gotID, rest, _ := strings.Cut(head, " ")
		if gotID == "0" {
			fmt.Fprintln(os.Stderr, "event:", rest)
			continue
		}
		if gotID != id {
			return "", nil, fmt.Errorf("reply to %s while waiting for %s: %s", gotID, id, head)
		}
		if s, ok := strings.CutPrefix(rest, "ok"); ok {
			return strings.TrimPrefix(s, " "), b, nil
		}
		return "", nil, fmt.Errorf("%s", strings.TrimPrefix(rest, "err "))
	}
}

// writeCapture saves a capture's BGRA pixels as a PNG and prints the projected points that follow them.
func writeCapture(summary string, body []byte, path string) error {
	var w, h int
	for _, kv := range strings.Fields(summary) {
		k, v, _ := strings.Cut(kv, "=")
		switch k {
		case "w":
			w, _ = strconv.Atoi(v)
		case "h":
			h, _ = strconv.Atoi(v)
		}
	}
	if w <= 0 || h <= 0 || len(body) < w*h*4 {
		return fmt.Errorf("capture: %d bytes for %dx%d", len(body), w, h)
	}
	img := image.NewRGBA(image.Rect(0, 0, w, h))
	for i := 0; i < w*h; i++ {
		img.Pix[i*4+0], img.Pix[i*4+1], img.Pix[i*4+2], img.Pix[i*4+3] = body[i*4+2], body[i*4+1], body[i*4+0], 255
	}
	if points := strings.TrimSpace(string(body[w*h*4:])); points != "" {
		fmt.Println(points)
	}
	if path == "" {
		return nil
	}
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	return png.Encode(f, img)
}

func readBody(path string) ([]byte, error) {
	switch path {
	case "":
		return nil, nil
	case "-":
		return io.ReadAll(os.Stdin)
	}
	return os.ReadFile(path)
}

func run() error {
	pid := flag.Int("pid", 0, "Radiant's process id (default: the only bridge pipe)")
	bodyPath := flag.String("body", "", "file whose content is the request body, - for stdin")
	pngPath := flag.String("png", "", "where a capture's image goes")
	flag.Parse()
	args := flag.Args()
	if len(args) == 0 {
		return fmt.Errorf("usage: t7rb [-pid N] [-body file|-] [-png file] <command> [args…] | listen <event>… | deploy <dll>")
	}
	if args[0] == "deploy" {
		if len(args) != 2 {
			return fmt.Errorf("usage: t7rb [-pid N] deploy <dll>")
		}
		return deploy(*pid, args[1])
	}
	c, err := dial(*pid)
	if err != nil {
		return err
	}
	defer c.f.Close()
	if args[0] == "listen" {
		if _, _, err := c.call("subscribe "+strings.Join(args[1:], " "), nil); err != nil {
			return err
		}
		out := bufio.NewWriter(os.Stdout)
		for {
			head, _, err := c.read()
			if err != nil {
				return err
			}
			fmt.Fprintln(out, strings.TrimPrefix(head, "0 "))
			out.Flush()
		}
	}
	body, err := readBody(*bodyPath)
	if err != nil {
		return err
	}
	summary, reply, err := c.call(strings.Join(args, " "), body)
	if err != nil {
		return err
	}
	if summary != "" {
		fmt.Println(summary)
	}
	if args[0] == "capture" {
		return writeCapture(summary, reply, *pngPath)
	}
	os.Stdout.Write(reply)
	return nil
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
