// The bridge's version: hello reports it, a release's tag must match it (.github/workflows/release.yml), and the
// standalone host reports it as its own.
#pragma once

namespace bridge
{
    constexpr const char* kPluginVersion = "1.2.2";
}
