# Bridge protocol (version 1)

How a client — t7kb — talks to the T7RadiantBridge plugin inside a running Radiant. The plugin carries bytes and calls Radiant; it has no logic of its own. Filtering, shaping answers for an agent, building map text from an intent and checking an edit's target belong to the client.

## Transport

- One named pipe per Radiant: `\\.\pipe\t7_radiant_bridge_<pid>`, local only. Several clients may connect at once; each connection has its own thread in the plugin.
- A **frame** is a 4-byte little-endian length, then that many bytes of payload.
- A **payload** is a header line (UTF-8, ending in `\n`) followed by a body (any bytes, possibly empty: map text, pixels).
- A **request** header is `<id> <command> [args…]`. `<id>` is a positive integer the client picks, unique on its connection.
- A **reply** header is `<id> ok [summary]` or `<id> err <code> <message>`; it carries the request's id. Replies to one connection come in request order.
- An **event** header is `0 <event> [args…]`, sent only to a connection that asked for it with `subscribe`.
- Arguments are separated by single spaces. Where a command says "the rest", its last argument is the rest of the line and may hold spaces (a label, a menu path, a key's value).

## Radiant's main thread and dialogs

- Every command runs on Radiant's main thread. The pipe threads hand requests to a message-only window of the plugin's own, so a command is served even while a dialog runs its modal loop (the host's idle tick stops then).
- While a modal dialog is open, a command that changes the map, the selection, the camera or the views fails with `err busy <dialog title>`; reads still answer.
- A request is dropped, never run late, once its connection is closed.
- An edit never leaves an undo group open across a Radiant command: `import`, `delete` and `replace` are each one undo step labelled as the client asks; `command` makes Radiant's own step.
- The user's selection is put back after `import`, `delete`, `replace`, `setkey` and `capture`; only `select`, `deselect` and `command` change it.

## Commands

| Command | Request body | Reply summary / body |
| --- | --- | --- |
| `hello <protocol>` | | `<protocol> plugin=<version> host=<version> pid=<pid>`; `err version` when the plugin cannot serve the client's protocol |
| `state` | | body: `key=value` lines — `map`, `modified`, `prefab_depth`, `entities`, `minimised` |
| `entities [<guid>…]` | | body: the entities' text as Radiant writes it, all of them without arguments; summary `missing=<guid,…>` for unknown GUIDs |
| `selection` | | body: one line per selected brush, `<brush guid> <entity guid> <min x y z> <max x y z>` |
| `import <label…>` | entity text, worldspawn block first | body: one line per new entity, `<entity guid> <brush guid>…` |
| `delete <label> <guid>…` | | summary `deleted=<n>`; a GUID names an entity (all its brushes) or one brush |
| `replace <guid> <label…>` | the entity's new text | summary: the entity's GUID afterwards (a `misc_prefab` gets a new one) |
| `setkey <guid> <key> [value…]` | | sets the key in place, worldspawn included; no value removes it |
| `select <guid>…` / `deselect` | | the selection, as `selection` |
| `command <menu path…>` | | runs a Radiant menu entry (`Selection/CSG/CSG Hollow`) on the current selection; `err missing`, `err disabled` |
| `menus [<menu path…>]` | | body: one `label<TAB>enabled<TAB>has_submenu` line per entry |
| `camera [<x> <y> <z> <pitch> <yaw> <roll>]` | | the camera, after setting it when given (degrees; positive pitch looks up) |
| `capture 3d\|xy\|xz\|yz` | | summary `w=<w> h=<h>`; body: the view's pixels, BGRA, top row first, from a frame drawn after the request; `err minimised` |
| `bake` | | starts the lighting bake; its end comes as the `bake` event |
| `save` | | saves the open map (File > Save) |
| `reload <path…>` | | opens a map without any prompt, unsaved changes lost; `err busy` while a mouse button is held |
| `subscribe <event>…` | | starts sending these events on this connection |

## Events

| Event | Arguments |
| --- | --- |
| `console` | one line Radiant printed to its console, without its timestamp |
| `map` | `loaded <path>`, `saved <path>` or `scope <prefab depth> <path>` |
| `bake` | `done <seconds>` |

## Errors

`err <code> <message>`, with `<code>` one of `bad-request` (unknown command, wrong arguments), `version`, `busy`, `not-found` (no such GUID or view), `refused` (Radiant refused the edit), `missing` / `disabled` (menu entries), `minimised`.

## Versions

`hello` comes first on a connection. The protocol number changes only when a command changes meaning; adding a command, an argument with a default or an event does not change it, so a client checks the commands it needs against the plugin version `hello` reports.
