// Crash isolation: a plugin callback runs under SEH, so a fault in it becomes a
// logged refusal, not a Radiant crash that loses the mapper's work.
#pragma once

namespace host::seh
{
    // Runs fn(arg) guarded. Returns true when it returned, false when it
    // faulted (the fault is logged with its code and module+offset).
    bool Run(void (*fn)(void*), void* arg, const char* what);
}
