# NOTICE

t7_radiant_bridge is licensed under the GNU GPL v3 (see [`LICENSE`](LICENSE)). It includes third-party code:

## MTPlugins

The standalone host under [`host/`](host/) is forked from [MTPlugins](https://github.com/Aidan601/MTPlugins) (commit `89423aa`, v1.1.2), by Figglebottom and contributors, licensed under the GNU GPL v3. Each forked file names the MTPlugins source it was modified from, at the top. The SDK header under `third_party/mtplugins_sdk/` is MTPlugins' plugin API, used to build the plugin against its host.

## MinHook

[`third_party/minhook/`](third_party/minhook/) is [MinHook](https://github.com/TsudaKageyu/minhook) v1.3.4 (commit `c3fcafdc10146beb5919319d0683e44e3c30d537`), Copyright (C) 2009-2017 Tsuda Kageyu and contributors, licensed under the 2-clause BSD License. Its full license and author list are in `third_party/minhook/LICENSE.txt` and `AUTHORS.txt`, kept as distributed.

## t7kb

[t7kb](https://github.com/McReaper/t7_companion), which drives this plugin, is a separate MIT-licensed program. It talks to the bridge over a named pipe only (see [`PROTOCOL.md`](PROTOCOL.md)); it does not link the bridge's code.
