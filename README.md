# t7_radiant_bridge

A Radiant plugin that lets an AI agent drive Black Ops III's map editor the way a mapper does: read the open map, edit it with undo, run Radiant's own commands, move the camera and capture the views. Its client is [t7kb](https://github.com/McReaper/t7_companion), which exposes it to agents as MCP tools: [Radiant integration](https://github.com/McReaper/t7_companion/blob/main/docs/radiant.md) has what an agent does with it and what stays in your hands.

## Install

Nothing to install by hand: t7kb ships the bridge and loads it into Radiant when it opens a map.

By hand, in a Radiant that [MTPlugins](https://github.com/Aidan601/MTPlugins) loads into: copy `mtplugins/T7RadiantBridge/` from `T7RadiantBridge-<version>.zip` ([latest release](https://github.com/McReaper/t7_radiant_bridge/releases/latest)) into `<BO3 root>/bin/plugins/` and start Radiant. MTPlugins loads a new plugin on its own unless its `autoload_new` setting (`bin/plugins/RadiantPlugins.ini`) is 0; then tick it in MTPlugins' plugin list.

## How it fits

- The plugin's code (`plugin/src/`) builds twice: `T7RadiantBridge.dll`, an MTPlugins plugin, and the standalone `T7RadiantBridge-<version>.dll`, which carries its own minimal host (`host/`, forked from MTPlugins) and hooks Radiant itself. The standalone one steps aside in a Radiant MTPlugins loads into: one host only ever runs in a Radiant.
- The plugin serves a versioned named-pipe protocol, [PROTOCOL.md](PROTOCOL.md): map text, edits, selection, commands, camera, captures, console, events. It carries bytes and calls Radiant; the logic is the client's.
- t7kb talks to it from its own process, shapes the answers for the agent, and owns the `.map` text model.

## Build

`build.bat` builds both DLLs into `build/Release/` with Visual Studio's C++ tools and CMake, and runs the tests: the protocol's text, and the host's logic that needs no Radiant (menu paths, view projection, the exe check). CI does the same on every pull request and keeps the packed archive. `tools/t7rb` is a development client: it sends one request and prints the reply, follows events, and `deploy` swaps a new DLL into a running Radiant.

## Release

Set `kPluginVersion` in `plugin/src/version.h` (the standalone host reports the same version), then push the tag `v<version>`: the release workflow checks the two match, builds and tests, and publishes `T7RadiantBridge-<version>.zip`, laid out by `tools/package.ps1`. A command added to the protocol is marked in PROTOCOL.md with the version that brought it, so a client can require it.

## License

GPLv3, like MTPlugins, whose SDK header the plugin compiles and whose host the standalone build forks; MinHook, which the standalone host hooks with, is BSD-2. [NOTICE.md](NOTICE.md) has the details. t7kb, a separate program talking over the pipe, keeps its MIT license.
