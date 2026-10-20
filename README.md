# t7_radiant_bridge

A Radiant plugin, built on the [MTPlugins](https://github.com/Aidan601/MTPlugins) SDK, that lets an AI agent drive Black Ops III's map editor the way a mapper does: read the open map, edit it with undo, run Radiant's own commands, move the camera and capture the views. Its client is [t7kb](https://github.com/McReaper/t7_companion), which exposes it to agents as MCP tools: [Radiant integration](https://github.com/McReaper/t7_companion/blob/main/docs/radiant.md) has what an agent does with it and what stays in your hands.

## Install

1. Install [MTPlugins](https://github.com/Aidan601/MTPlugins) with its setup: it loads plugins into Radiant.
2. Download `T7RadiantBridge.zip` from the [latest release](https://github.com/McReaper/t7_radiant_bridge/releases/latest) and unzip it into `<BO3 root>/bin/plugins/`, so the DLL is at `bin/plugins/T7RadiantBridge/T7RadiantBridge.dll`.
3. Start Radiant. MTPlugins loads a new plugin on its own unless its `autoload_new` setting (`bin/plugins/RadiantPlugins.ini`) is 0; then tick it in MTPlugins' plugin list.

To update, close Radiant and unzip the new release over the old one.

## How it fits

- MTPlugins loads the plugin into Radiant (through its `d3d9.dll` proxy or its launcher) and provides the SDK.
- This plugin serves a versioned named-pipe protocol, [PROTOCOL.md](PROTOCOL.md): map text, edits, selection, commands, camera, captures, console, events. It carries bytes and calls Radiant; the logic is the client's.
- t7kb talks to it from its own process, shapes the answers for the agent, and owns the `.map` text model.

## Build

`build.bat` builds `build/Release/T7RadiantBridge.dll` with Visual Studio's C++ tools and CMake, and runs the tests; CI does the same on every pull request. `tools/t7rb` is a development client: it sends one request and prints the reply, follows events, and `deploy` swaps a new DLL into a running Radiant.

## Release

Set `kPluginVersion` in `plugin/src/commands.h`, then push the tag `v<version>`: the release workflow checks the two match, builds and tests the plugin, and publishes `T7RadiantBridge.zip`. A command added to the protocol is marked in PROTOCOL.md with the version that brought it, so a client can require it.

## License

GPLv3, like MTPlugins, whose SDK header this plugin compiles. t7kb, a separate program talking over the pipe, keeps its MIT license.
