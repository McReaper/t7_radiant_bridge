// What the plugin reaches past the SDK: Radiant's console, keys set in place, the bake, Qt's actions and modal
// dialogs, and a view's pixels. Radiant's functions are found by address in the one build the bridge knows
// (host->RadiantAddress gives NULL on any other), each feature degrading to a refusal when its address is missing.
#pragma once
#include <string>

namespace bridge
{
    std::string Utf8(const std::wstring& w);
    std::wstring Wide(const std::string& s);
}

namespace bridge::radiant
{
    // Resolves Radiant's and Qt's functions (MAIN THREAD, from OnLoad) and logs what this build lacks.
    void Bind();

    // Hooks Radiant's console print; fn gets each finished line without its timestamp, on whatever thread printed it.
    using LineFn = void (*)(const std::string& line);
    bool HookConsole(LineFn fn);
    void StopConsole();

    // Sets one key of an entity in place, as one undo step. "" on success, "busy" while an undo operation is open,
    // otherwise the reason.
    std::string SetKey(void* entity, const std::string& key, const std::string& value);

    // Starts the bake of the sun and the probes. "" or the reason it cannot.
    std::string Bake();

    // The 3D view's render method, Radiant's own number (CCamWnd+232), -1 when unknown.
    int RenderMethod();

    struct ActionState
    {
        bool checkable = false, checked = false, enabled = false;
    };
    // Reads, and triggers when asked, the QAction of that object name. "" on success, "not-found", "disabled", or the reason.
    std::string Action(const std::string& name, bool trigger, ActionState& out);

    // The title of the modal dialog the main thread is in, "" when there is none.
    std::string ModalTitle();

    // A view window's pixels, BGRA, top row first, alpha 255. "" or the reason.
    std::string PrintView(void* hwnd, std::string& pixels, int& w, int& h);

    // A mouse button is held anywhere on the desktop (Radiant's map load refuses then).
    bool MouseHeld();
}
