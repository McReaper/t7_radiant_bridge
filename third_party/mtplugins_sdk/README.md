# MTPlugins SDK headers

`modtools_plugin.h` and `modtools_plugin.hpp`, copied unchanged from [Aidan601/MTPlugins](https://github.com/Aidan601/MTPlugins) `sdk/` at commit `89423aa0850a968df19bdc626f347bc73d2b463e` (release 1.1.2). They are licensed under the GNU GPL v3, like the rest of MTPlugins and like this repository.

Every Monday, `.github/workflows/mtplugins-sdk.yml` runs `tools/mtplugins-sdk.sh` against MTPlugins' latest release: when its headers differ, it builds and tests the plugin against them on a branch, then opens a PR (or, where the repository doesn't let workflows open one, an issue linking the branch). Review it before merging: a member of `ModToolsHostAPI` that moved builds a plugin that crashes in Radiant.
