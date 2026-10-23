// The ModToolsHostApi the host hands the bridge: the services it calls back on.
// Milestone 2a fills what hello and state need (map, entities, selection,
// camera, main window, RadiantAddress, Detour, log); the rest is null, which the
// bridge null-checks.
#pragma once

struct ModToolsHostApi;

namespace host::hostapi
{
    const ModToolsHostApi* Table();
}
