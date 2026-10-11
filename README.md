# t7_radiant_bridge

A Radiant plugin, built on the [MTPlugins](https://github.com/Aidan601/MTPlugins) SDK, that lets an AI agent drive Black Ops III's map editor the way a mapper does: read the open map, edit it with undo, run Radiant's own commands, move the camera and capture the views. Its first client is [t7kb](https://github.com/McReaper/t7_companion), which exposes it to agents as MCP tools.

**Status: planning.** The plan and its milestones are in [McReaper/t7_companion#51](https://github.com/McReaper/t7_companion/issues/51); the first step is the feasibility spike, [#1](https://github.com/McReaper/t7_radiant_bridge/issues/1).

## How it fits

- MTPlugins loads the plugin into Radiant (through its `d3d9.dll` proxy or its launcher) and provides the SDK.
- This plugin serves a versioned named-pipe protocol: map text, edits, selection, commands, camera, captures, console, events.
- t7kb talks to it from its own process, shapes the answers for the agent, and owns the `.map` text model.

## License

GPLv3, like MTPlugins, whose SDK header this plugin compiles. t7kb, a separate program talking over the pipe, keeps its MIT license.
