// Whether MTPlugins already hosts plugins in this Radiant. Two hosts would hook
// the same functions, so the standalone host stays passive when it does, and
// a client loads the MTPlugins plugin build instead.
#pragma once

namespace host::coexist
{
    // MTPlugins is hosting here when its RadiantPlugins.dll is loaded, or its
    // loader is installed and able to load it: bin\d3d9.dll beside the running
    // Radiant exe with a RadiantLoader.ini that isn't `enabled=0`. The loader's
    // delay_ms can load the host after us, so the caller rechecks for a while.
    bool MTPluginsPresent();
}
