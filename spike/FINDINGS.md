# M0 spike findings

Measured on the user's install (BO3 mod tools, Steam build), MTPlugins 1.0.0 core only (host + loader), plugin `T7RadiantBridge` 0.0.1 from `spike/plugin`, test map `map_source/test_claude.map`. Issue: [#1](https://github.com/McReaper/t7_radiant_bridge/issues/1).

## Loading and the pipe

- The `d3d9.dll` loader starts the host in Radiant launched normally. The host reports `hostVersion` 1.1.0 although the release is named 1.0.0, so imports keep `target`/`linkto`.
- A plugin dropped into `bin\plugins\<Name>\<Name>.dll` loads without restarting Radiant through the host's control window (`WM_COPYDATA` "plugins rescan"); the reply lands in `bin\plugins\RadiantPlugins.ctl`.
- The named pipe (`\\.\pipe\t7_radiant_bridge_<pid>`) with a main-thread job queue drained in `OnTick` works; the Go client opens it with `os.OpenFile`, no cgo.
- Reads are live, from Radiant's memory. `WriteEntityText` orders keys differently from the saved `.map`, and worldspawn's GUID differs from the file's after a load: in-memory text is not a byte-identical copy of the file.

## Commands and menus

- `MenuEntries` gives every menu's labels with their enabled state (19 top-level menus; `Selection/CSG/CSG Hollow`, `Patch/Primitives/...`, `Selection/Plant/Use Random Scale and Rotation`...). The lighting bake is not in a menu: it is the 3D view's toolbar bolt.
- Importing generated brush text works: the brush gets fresh GUIDs and is left selected.
- `TriggerMenuAction("Selection/CSG/CSG Hollow")` on a selection the plugin set works: six 8-unit walls.

## Undo

- **A menu command triggered inside `BeginUndo`/`EndUndo` breaks the group.** Radiant logs `Undo_Start: WARNING last undo ('<our label>') not finished`, pushes its own operation (`hollow`) on top, and our operation stays open: Edit > Undo is disabled and the following imports and deletes are swallowed into it, with no undo entry of their own. Rule: never run a Radiant command inside an undo group; an action mixing our edits and Radiant commands is several undo steps.
- Radiant logs every undo operation to `Documents\Radiant.log` (`'hollow' added as operation # 2 on top of the undo stack.`, `hollow undone.  There is now 1 operation left`): that is the ground truth for undo tests, not the Edit menu's state.

## Captures

- After `SetCamera` + `RequestRedraw`, waiting for two `OnDraw` frames of that view gives the new view; `PrintWindow(PW_CLIENTONLY | PW_RENDERFULLCONTENT)` on `CamWindow` works from Radiant's main thread and takes 94–125 ms for a 1711×869 view.
- Positive pitch looks up (the `ModToolsCamParams` comment is right; the `MapEditExample` comment is not).
- Radiant keeps rendering, and the capture shows a camera move made just before it, in every case where it is not minimised, checked with another application in the foreground the whole time: **covered** by another window (25 of 25 sampled points of the 3D view belonged to that window) and **off screen** (the main window un-maximised with `SW_SHOWNOACTIVATE`, then moved left of every monitor with `SetWindowPos(SWP_NOACTIVATE)`; `SetWindowPlacement` alone pulls a window back onto a monitor). **Minimised**, `PrintWindow` fails. A sandbox Radiant for the agent can therefore live off screen, never minimised; on the user's Radiant the plugin must report a minimised window rather than return an empty image.
- Radiant has several top-level windows (the main one and floating panes such as "FX Browser | Textures | Models"): `Process.MainWindowHandle` can name a pane, so find the main window by its title, which carries the map path.
- Tool volumes (`volume_sun`'s brush) hide what is behind them, and an unbaked map renders its floor black: a capture needs a clean-view preset (hide volumes, clips, tool textures) and, for a blockout, the fake lighting mode rather than a bake.

## Patches, probes and the bake

- A curve patch imported as text (`curve { toolFlags; <material> lightmap_gray 3 3 16 1 ( v x y z t u v lu lv ... ) }`, the most common header in the install: 1,967 patches) is accepted, renders, and gets its own undo entry.
- A `reflection_probe` imported through `ImportMapText` bakes without an assert on host 1.1.0: the bake logs one `Probe:` line per probe (three for three). The SDK docs' warning did not reproduce.
- The bake is the 3D view's bolt, the third `QToolButton` under the `QToolBar` named `Exposure`; `ui->ClickButton` on it starts a full bake, which ends with `All Probes Done (<s> s)` in `Documents\Radiant.log`. Finding the button by position is fragile: the durable path is Radiant's own `CCamWnd_BuildLighting`.

## Reload and modal dialogs

- `ReloadMap` refuses while a mouse button is held anywhere on the desktop (the host reads the global mouse state): retry later.
- With a selection, Radiant's map load asks "Copy the current selection?" in a modal box. A modal box blocks the main thread, so the pipe stalls until a human answers. The plugin now deselects before reloading.
- **While a modal dialog is up, the host's tick stops** (our tick counter froze), so a command run from `OnTick` cannot answer; but the host's `WM_COPYDATA` control window is still served, because the modal loop dispatches window messages. Design consequence: take commands through a message-only window of our own (the pipe thread sends it a message) rather than the tick, so the plugin stays responsive during a dialog and can fill it. Also guard against reentrancy: a command that opens a modal dialog is still inside our tick.
- `Misc/Reports/Basic Map Stats` opens a modal "Choose path..." file dialog; `Edit/Map Info...` opens a dock pane (not modal) whose text goes to no log.
- Hot swap: `plugins disable <Name>`, copy the new DLL, `plugins enable <Name>`, without restarting Radiant.
- A request whose client timed out stayed queued and ran later (five reloads in a row once the box was answered). Fixed: a timed-out request is abandoned and never runs.

## Open

Two Radiants side by side; prefab enter/leave re-reading the file; `Save Custom Screenshot...`; `build` on a map the plugin produced.
