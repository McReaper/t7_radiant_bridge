package main

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"time"
	"unsafe"
)

var (
	user32                         = syscall.NewLazyDLL("user32.dll")
	kernel32                       = syscall.NewLazyDLL("kernel32.dll")
	pFindWindowExW                 = user32.NewProc("FindWindowExW")
	pGetWindowThreadProcessId      = user32.NewProc("GetWindowThreadProcessId")
	pSendMessageTimeoutW           = user32.NewProc("SendMessageTimeoutW")
	pQueryFullProcessImageNameW    = kernel32.NewProc("QueryFullProcessImageNameW")
	processQueryLimitedInformation = uint32(0x1000)
)

const (
	hwndMessage = ^uintptr(2) // HWND_MESSAGE, (HWND)-3
	wmCopyData  = 0x004A
	smtoAbort   = 0x0002
	pluginName  = "T7RadiantBridge"
)

type copyData struct {
	data uintptr
	size uint32
	ptr  uintptr
}

// controlWindows maps each process running MTPlugins' host to its control window (RadiantPlugins_Control).
func controlWindows() map[uint32]uintptr {
	class, _ := syscall.UTF16PtrFromString("RadiantPlugins_Control")
	out := map[uint32]uintptr{}
	var after uintptr
	for {
		h, _, _ := pFindWindowExW.Call(hwndMessage, after, uintptr(unsafe.Pointer(class)), 0)
		if h == 0 {
			return out
		}
		var pid uint32
		pGetWindowThreadProcessId.Call(h, uintptr(unsafe.Pointer(&pid)))
		out[pid] = h
		after = h
	}
}

func exePath(pid uint32) (string, error) {
	h, err := syscall.OpenProcess(processQueryLimitedInformation, false, pid)
	if err != nil {
		return "", err
	}
	defer syscall.CloseHandle(h)
	buf := make([]uint16, 1024)
	n := uint32(len(buf))
	if r, _, err := pQueryFullProcessImageNameW.Call(uintptr(h), 0, uintptr(unsafe.Pointer(&buf[0])), uintptr(unsafe.Pointer(&n))); r == 0 {
		return "", err
	}
	return syscall.UTF16ToString(buf[:n]), nil
}

// control sends one command to the host's control window and returns the reply it writes to RadiantPlugins.ctl.
func control(h uintptr, ctl, cmd string) (string, error) {
	before, _ := os.Stat(ctl)
	b := []byte(cmd)
	cds := copyData{size: uint32(len(b)), ptr: uintptr(unsafe.Pointer(&b[0]))}
	var result uintptr
	if r, _, err := pSendMessageTimeoutW.Call(h, wmCopyData, 0, uintptr(unsafe.Pointer(&cds)), smtoAbort, 10000, uintptr(unsafe.Pointer(&result))); r == 0 {
		return "", fmt.Errorf("%s: %v", cmd, err)
	}
	for i := 0; i < 40; i++ {
		after, err := os.Stat(ctl)
		if err == nil && (before == nil || after.ModTime() != before.ModTime() || after.Size() != before.Size()) {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	reply, err := os.ReadFile(ctl)
	return strings.TrimSpace(string(reply)), err
}

func copyFile(from, to string) error {
	in, err := os.Open(from)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.Create(to)
	if err != nil {
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		out.Close()
		return err
	}
	return out.Close()
}

// deploy swaps the plugin's DLL in a running Radiant: disable (the host unloads it), copy, enable.
func deploy(pid int, dll string) error {
	wins := controlWindows()
	var h uintptr
	var owner uint32
	for p, w := range wins {
		if pid == 0 || int(p) == pid {
			h, owner = w, p
		}
	}
	if h == 0 || (pid == 0 && len(wins) > 1) {
		return fmt.Errorf("found %d Radiant(s) running MTPlugins: pass -pid when there are several", len(wins))
	}
	exe, err := exePath(owner)
	if err != nil {
		return err
	}
	plugins := filepath.Join(filepath.Dir(exe), "plugins")
	ctl := filepath.Join(plugins, "RadiantPlugins.ctl")
	target := filepath.Join(plugins, pluginName, pluginName+".dll")
	if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
		return err
	}
	reply, err := control(h, ctl, "plugins disable "+pluginName)
	if err != nil {
		return err
	}
	fmt.Println("disable:", reply)
	if err := copyFile(dll, target); err != nil {
		return err
	}
	if reply, err = control(h, ctl, "plugins enable "+pluginName); err != nil {
		return err
	}
	fmt.Println("enable:", reply)
	return nil
}
