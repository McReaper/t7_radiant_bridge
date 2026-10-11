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

## Reload and modal dialogs

- `ReloadMap` refuses while a mouse button is held anywhere on the desktop (the host reads the global mouse state): retry later.
- With a selection, Radiant's map load asks "Copy the current selection?" in a modal box. A modal box blocks the main thread, so the pipe stalls until a human answers. The plugin now deselects before reloading.
- A request whose client timed out stayed queued and ran later (five reloads in a row once the box was answered). Fixed: a timed-out request is abandoned and never runs.

## Open

Capture after a camera move; a patch imported as text; an imported `reflection_probe` at bake time; whether the host's tick runs while a modal dialog opened by a menu command is up; two Radiants side by side; prefab enter/leave re-reading the file; `Save Custom Screenshot...`; `build` on a map the plugin produced.
