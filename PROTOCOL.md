# Bridge protocol (version 1)

How a client — t7kb — talks to the T7RadiantBridge plugin inside a running Radiant. The plugin carries bytes and calls Radiant; it has no logic of its own. Filtering, shaping answers for an agent, building map text from an intent and checking an edit's target belong to the client.

## Transport

- One named pipe per Radiant: `\\.\pipe\t7_radiant_bridge_<pid>`, local only. Several clients may connect at once; each connection has its own thread in the plugin.
- A **frame** is a 4-byte little-endian length, then that many bytes of payload; a request is at most 64 MiB.
- A **payload** is a header line (UTF-8, ending in `\n`) followed by a body (any bytes, possibly empty: map text, pixels).
- A **request** header is `<id> <command> [args…]`. `<id>` is a positive integer the client picks, unique on its connection.
- A **reply** header is `<id> ok [summary]` or `<id> err <code> <message>`; it carries the request's id.
- An **event** header is `0 <event> [args…]`, sent only to a connection that asked for it with `subscribe`.
- Arguments are separated by single spaces. Where a command says "the rest", its last argument is the rest of the line and may hold spaces (a label, a menu path, a key's value).

## Radiant's main thread and dialogs

- Every command runs on Radiant's main thread. The pipe threads hand requests to a message-only window of the plugin's own, so the plugin answers even while a dialog runs its modal loop (the host's idle tick stops then).
- A connection's requests run one after the other, in order: a request starts once the one before it has replied (a capture waits for frames), so replies come in request order. While a capture runs, no other request starts.
- While a modal dialog is open — the user's, or one a `command` opened — or a map is loading, `hello`, `state` and `menus` answer at once, out of turn, and every other request fails with `err busy <dialog title>`. `state` names the dialog.
- A request is dropped, never run late, once its connection is closed.
- An edit never leaves an undo group open across a Radiant command: `import`, `delete`, `replace` and `setkey` are each one undo step; `command` makes Radiant's own.
- The user's selection is put back after `import`, `delete`, `replace` and `capture` (by GUID, without what was deleted); only `select`, `deselect` and `command` change it.

## Commands

| Command | Request body | Reply summary / body |
| --- | --- | --- |
| `hello <protocol>` | | first on a connection; `<protocol> plugin=<version> host=<version> pid=<pid>`; `err version` when the plugin cannot serve the client's protocol |
| `state` | | body: `key=value` lines — `map`, `modified`, `prefab_depth`, `entities`, `selected`, `minimised`, `dialog` (empty when none), `render` and `view2d` (see [Vision](#vision)) |
| `entities [<guid>…]` | | body: the entities' text as Radiant writes it, all of them without arguments; summary `missing=<guid>,…` for unknown GUIDs |
| `selection` | | body: one line per selected brush, `<brush guid> <entity guid> <min x> <min y> <min z> <max x> <max y> <max z>` |
| `import <label…>` | entity text, worldspawn block first | body: one line per entity that got brushes, `<entity guid> <brush guid>…`, in import order (world brushes under worldspawn's GUID; a point entity has one brush, its placeholder) |
| `delete <label…>` | one entity or brush GUID per line | summary `deleted=<brushes>`; an entity GUID deletes the entity; `err not-found` deletes nothing; worldspawn is refused |
| `replace <guid> <label…>` | the entity's new text | summary: the entity's GUID afterwards (a `misc_prefab` gets a new one); refused for worldspawn and `reflection_probe` |
| `setkey <guid> <key> [<value…>]` | | sets the key in place to the rest of the line (empty allowed), worldspawn included; `guid` and `classname` are refused, and so is a value Radiant will not take as text (`origin`, `angles`: change those with `replace`), leaving no undo step; removing a key is a `replace` |
| `select <guid>…` / `deselect` | | the selection, as `selection`; summary `missing=<guid>,…` |
| `command <menu path…>` | | runs a Radiant menu entry (`Selection/CSG/CSG Hollow`) on the current selection; `err missing`, `err disabled` |
| `menus [<menu path…>]` | | body: one `label<TAB>enabled<TAB>has_submenu` line per entry (`---` a separator) |
| `action <name> [trigger]` | | a named action of Radiant's (`actionTex_Unlit`), triggered when asked; summary `checkable=0\|1 checked=0\|1 enabled=0\|1` afterwards; `err not-found` |
| `camera [<x> <y> <z> <pitch> <yaw> <roll>]` | | summary: the camera, after setting it when given (degrees; positive pitch looks up, yaw 90 faces +y) |
| `capture 3d\|xy\|xz\|yz [<x> <y> <z> <pitch> <yaw> <roll>]` | optional: one `x y z` world point per line | a view's frame and its projection, see [Vision](#vision) |
| `bake` | | starts Radiant's lighting bake (sun and probes), which ends with the console event `All Probes Done (<s> s)`; refused unless the 3D view's render method is `Real Lighting` |
| `save` | | saves the open map (File > Save); refused for an unnamed map |
| `reload <path…>` | | opens a map without any prompt, unsaved changes lost; `err busy` while a mouse button is held, refused inside a prefab |
| `subscribe <event>…` | | starts sending these events on this connection |

## Vision

`capture` is how an agent sees its work: one view's frame, and what it needs to tie the image to the map.

- **The frame** is drawn after the request, with nothing selected (selected brushes draw tinted; the selection is put back after) and none of the bridge's own overlays. It shows what the view shows: its render method and filters, the bake's lighting once baked. `err minimised` when Radiant is minimised; covered or off screen is fine.
- **A pose** (3D only) shoots from that camera and then puts the user's camera back; `err busy` while a mouse button is held.
- **The 2D view** shows one axis at a time. `command View/Layout/Next (XY, YZ, XY)` turns it XY → XZ → YZ (the layout's other entries do not change it); a capture of an axis it does not show is `err not-found the 2D view shows <axis>` at once, and `err not-found` after 3 s when no 2D view is open (`command View/Toggle View Window/Active XY`). To frame a spot, put the camera over it, `action actionCenter_2D_On_Camera trigger`, and zoom with `View/Zoom/XY Zoom In`, `XY Zoom Out` or `XY 100%`; the summary gives the centre and scale reached.
- **The reply summary** carries the view's projection: `w=<w> h=<h> origin=<x>,<y>,<z> angles=<pitch>,<yaw>,<roll> game_view=0|1` for `3d`, `w=<w> h=<h> centre=<x>,<y>,<z> scale=<pixels per unit>` for a 2D view (`xy` looks down, `xz` from the front, `yz` from the side).
- **The reply body** is the pixels, `w × h × 4` bytes, BGRA, top row first; then, when the request listed world points, one `<px> <py>` line per point, in that frame's projection (`-` for a point behind the camera). The client places labels and boxes with these, so the image and the map agree without the client knowing Radiant's lens.
- **Render methods** are `View/Render Method/<method>` commands: `Wireframe`, `Fullbright`, `Normal-based Fake Lighting`, `View-Based Fake Lighting`, `Real Lighting`, `Tri Density`. An unbaked map is black under `Real Lighting`; a fake lighting shows a blockout. `state` gives the current one as `render=<method>` and the 2D view's axis as `view2d=XY (Top)|XZ|YZ` (the last it drew), so a client that changes either puts it back.
- **Belongs to the client:** scaling to the agent's pixel budget, encoding, the labels drawn on the image, framing a target from its bounds, several angles in one sheet.

## Events

| Event | Arguments |
| --- | --- |
| `console` | one entry of Radiant's console, without its timestamp: one print, whether or not it ends its line (a print of several lines is one event per line) |
| `map` | `loaded <path>`, `saved <path>` or `scope <prefab depth> <path>` |

## Errors

`err <code> <message>`, with `<code>` one of `bad-request` (unknown command, wrong arguments, a request before `hello`), `version`, `busy`, `not-found` (no such GUID, view or action), `refused` (Radiant refused, or the plugin does not do that), `missing` / `disabled` (menu entries), `minimised`.

## Versions

`hello` comes first on a connection. The protocol number changes only when a command changes meaning; adding a command, an argument with a default or an event does not change it, so a client checks the commands it needs against the plugin version `hello` reports.
