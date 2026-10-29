# NOTICE

t7_radiant_bridge is licensed under the GNU GPL v3 (see [`LICENSE`](LICENSE)). It includes third-party code:

## MTPlugins

The SDK header under [`third_party/mtplugins_sdk/`](third_party/mtplugins_sdk/) is the plugin API of [MTPlugins](https://github.com/Aidan601/MTPlugins), by Figglebottom and contributors, licensed under the GNU GPL v3: the plugin is built against it, and MTPlugins' host loads it into Radiant.

## Clients

A client talks to the bridge over a named pipe only (see [`PROTOCOL.md`](PROTOCOL.md)) and does not link the bridge's code: it is a separate program, under its own license.
