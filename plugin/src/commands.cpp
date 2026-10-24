#include "commands.h"
#include "modtools_plugin.hpp"
#include "radiant.h"
#include <windows.h>
#include <array>
#include <set>
#include <unordered_map>
#include <vector>

namespace bridge
{
    namespace
    {
        struct Call
        {
            const std::shared_ptr<Conn>& conn;
            const Header& h;
            const std::string& body;
            std::vector<std::string> w;   // h.args in words
            bool blocked;
            const std::string& dialog;
            bool later = false;           // the reply comes through Complete
        };

        using Handler = Reply (*)(Call& c);

        std::string Label(const std::string& args, size_t skip, const char* fallback)
        {
            std::string l = Trim(Rest(args, skip));
            return l.empty() ? std::string(fallback) : l;
        }

        HWND MainWindow() { return static_cast<HWND>(rp::host->MainWindow(rp::self)); }

        // What `state` reports as entries of Radiant's menus, so a client puts the user's back with one `command`:
        // View/Render Method/<render>, View/Layout/<view2d>.
        std::string RenderLabel(int method)
        {
            static const char* const kMethods[] = { "Wireframe", "Fullbright", "Normal-based Fake Lighting", "View-Based Fake Lighting", "Real Lighting", nullptr, "Tri Density" };
            if (method >= 0 && method < int(std::size(kMethods)) && kMethods[method]) return kMethods[method];
            return method < 0 ? std::string() : std::to_string(method);
        }
        const char* const kLayouts[] = { "YZ", "XZ", "XY (Top)" };   // by ModToolsXYParams::type
        int g_lastView2D = -1;                                       // the type the 2D view last drew

        // ---- the selection, kept by GUID so that it survives any edit in between ------------------------------
        struct Picked
        {
            std::string brush, entity;
        };

        std::vector<Picked> SaveSelection()
        {
            std::vector<Picked> out;
            for (const ModToolsSelectedBrush& s : rp::GetSelection()) out.push_back({ s.brushGuid, s.entityGuid });
            return out;
        }

        void Dedupe(std::vector<ModToolsBrush>& v)
        {
            std::set<ModToolsBrush> seen;
            std::vector<ModToolsBrush> out;
            for (ModToolsBrush b : v)
                if (b && seen.insert(b).second) out.push_back(b);
            v.swap(out);
        }

        std::vector<ModToolsBrushInst> InstsOf(const std::vector<ModToolsBrush>& brushes)
        {
            std::vector<ModToolsBrushInst> out;
            for (ModToolsBrush b : brushes)
                if (ModToolsBrushInst i = rp::host->BrushInstOf(rp::self, b)) out.push_back(i);
            return out;
        }

        // The brushes that GUIDs name, everything deselected first (a selected brush cannot be found): an entity GUID
        // gives all its brushes. `missing` gets the GUIDs that name nothing.
        std::vector<ModToolsBrush> Resolve(const std::vector<std::string>& guids, std::vector<std::string>& missing)
        {
            rp::host->DeselectAll(rp::self);
            std::vector<ModToolsEntity> ents = rp::FindEntitiesByGuid(guids);
            std::vector<ModToolsBrush> found = rp::FindBrushesByGuid(guids), out;
            for (size_t i = 0; i < guids.size(); ++i)
            {
                if (ents[i]) rp::ForEachEntityBrush(ents[i], [&](ModToolsBrush b) { out.push_back(b); });
                else if (found[i]) out.push_back(found[i]);
                else missing.push_back(guids[i]);
            }
            Dedupe(out);
            return out;
        }

        // Selects what was saved and still exists: a brush by its GUID, a point entity (whose one brush may have no
        // GUID of its own) by the entity's.
        void RestoreSelection(const std::vector<Picked>& saved)
        {
            rp::host->DeselectAll(rp::self);
            if (saved.empty()) return;
            std::vector<std::string> guids, points;
            for (const Picked& p : saved) guids.push_back(p.brush);
            std::vector<ModToolsBrush> found = rp::FindBrushesByGuid(guids), brushes;
            for (size_t i = 0; i < saved.size(); ++i)
                if (found[i]) brushes.push_back(found[i]);
                else points.push_back(saved[i].entity);
            if (!points.empty())
                for (ModToolsEntity e : rp::FindEntitiesByGuid(points))
                    if (e && rp::EntityFixedSize(e)) rp::ForEachEntityBrush(e, [&](ModToolsBrush b) { brushes.push_back(b); });
            Dedupe(brushes);
            std::vector<ModToolsBrushInst> insts = InstsOf(brushes);
            if (!insts.empty()) rp::host->SelectBrushInsts(rp::self, insts.data(), insts.size());
        }

        std::string SelectionText()
        {
            std::string out;
            for (const ModToolsSelectedBrush& s : rp::GetSelection())
            {
                out += std::string(s.brushGuid) + " " + s.entityGuid;
                for (float v : s.mins) out += " " + Num(v);
                for (float v : s.maxs) out += " " + Num(v);
                out += "\n";
            }
            return out;
        }

        std::string WorldGuid()
        {
            ModToolsEntity w = rp::host->WorldEntity(rp::self);
            return w ? rp::EntityGuid(w) : std::string();
        }

        // The selected brushes by owner: Radiant unlinks a selected brush from its entity, so its text needs them back.
        std::unordered_map<ModToolsEntity, std::vector<ModToolsBrush>> SelectedByEntity()
        {
            std::unordered_map<ModToolsEntity, std::vector<ModToolsBrush>> out;
            for (ModToolsBrushInst i : rp::SelectedBrushInsts())
                if (ModToolsBrush b = rp::BrushOfInst(i))
                {
                    ModToolsEntity owner = rp::host->BrushOwner(rp::self, b);
                    out[owner].push_back(b);
                }
            return out;
        }

        std::string EntityText(ModToolsEntity e, const std::unordered_map<ModToolsEntity, std::vector<ModToolsBrush>>& selected)
        {
            std::string t;
            auto it = selected.find(e);
            bool ok = it != selected.end() ? rp::WriteEntityTextWith(e, it->second, t) : rp::WriteEntityText(e, t);
            return ok ? t : std::string();
        }

        // ---- commands -----------------------------------------------------------------------------------------
        Reply Hello(Call& c)
        {
            if (c.w.size() != 1) return Err("bad-request", "hello <protocol>");
            if (c.w[0] != "1") return Err("version", "this plugin serves protocol 1");
            c.conn->hello = true;
            unsigned v = rp::host->hostVersion;
            char host[32];
            snprintf(host, sizeof host, "%u.%u.%u", v >> 16, (v >> 8) & 0xff, v & 0xff);
            return Ok(std::string("1 plugin=") + kPluginVersion + " host=" + host + " pid=" + std::to_string(GetCurrentProcessId()));
        }

        Reply State(Call& c)
        {
            std::string b = "map=" + Utf8(rp::CurrentMapPath()) + "\n";
            b += "modified=" + std::to_string(rp::host->IsMapModified(rp::self) ? 1 : 0) + "\n";
            b += "prefab_depth=" + std::to_string(rp::host->PrefabDepth(rp::self)) + "\n";
            if (c.blocked) b += "entities=\nselected=\n";    // the map may be half loaded under a dialog: no walk
            else
            {
                size_t n = 0;
                rp::ForEachEntity([&](ModToolsEntity) { ++n; });
                b += "entities=" + std::to_string(n) + "\n";
                b += "selected=" + std::to_string(rp::host->GetSelection(rp::self, nullptr, 0)) + "\n";
            }
            HWND main = MainWindow();
            b += "minimised=" + std::to_string(main && IsIconic(main) ? 1 : 0) + "\n";
            b += "dialog=" + (c.blocked ? c.dialog : std::string()) + "\n";
            b += "render=" + RenderLabel(radiant::RenderMethod()) + "\n";
            b += "view2d=" + std::string(g_lastView2D >= 0 ? kLayouts[g_lastView2D] : "") + "\n";
            return Ok(std::string(), b);
        }

        Reply Entities(Call& c)
        {
            auto selected = SelectedByEntity();
            std::string out;
            if (c.w.empty())
            {
                rp::ForEachEntity([&](ModToolsEntity e) { out += EntityText(e, selected); });
                return Ok(std::string(), out);
            }
            std::vector<ModToolsEntity> ents = rp::FindEntitiesByGuid(c.w);
            std::vector<std::string> missing;
            for (size_t i = 0; i < ents.size(); ++i)
                if (ents[i]) out += EntityText(ents[i], selected);
                else missing.push_back(c.w[i]);
            return Ok(missing.empty() ? std::string() : "missing=" + Join(missing, ','), out);
        }

        Reply Selection(Call&) { return Ok(std::string(), SelectionText()); }

        // What an import added, which it leaves selected: one "<entity guid> <brush guid>…" line per entity, in order.
        std::string ImportedRows()
        {
            std::vector<std::string> order;
            std::unordered_map<std::string, std::string> rows;
            for (const ModToolsSelectedBrush& s : rp::GetSelection())
            {
                if (!rows.count(s.entityGuid)) order.push_back(s.entityGuid);
                rows[s.entityGuid] += std::string(" ") + s.brushGuid;
            }
            std::string out;
            for (const std::string& e : order) out += e + rows[e] + "\n";
            return out;
        }

        Reply Import(Call& c)
        {
            if (c.body.empty()) return Err("bad-request", "import needs entity text in its body, worldspawn block first");
            std::string bad;
            if (!CheckMapText(c.body, bad)) return Err("bad-request", bad);
            std::string label = Label(c.h.args, 0, "bridge import");
            std::vector<Picked> saved = SaveSelection();
            rp::host->DeselectAll(rp::self);
            if (!rp::host->ImportMapText(rp::self, c.body.c_str(), label.c_str()))
            {
                RestoreSelection(saved);
                return Err("refused", "Radiant refused the text");
            }
            std::string out = ImportedRows();
            RestoreSelection(saved);
            return Ok(std::string(), out);
        }

        // Why a point entity cannot be replaced in place, "" when it can: Radiant's undo brings an entity that owns
        // brushes back as world brushes, and a reflection_probe cannot be rebuilt.
        std::string Unreplaceable(ModToolsEntity e, const std::string& guid)
        {
            if (!e) return "not-found";
            if (e == rp::host->WorldEntity(rp::self)) return "worldspawn cannot be replaced: set its keys with setkey";
            if (!rp::EntityFixedSize(e)) return guid + " owns brushes, which Radiant's undo would bring back as world brushes: delete it and import its new text";
            if (_stricmp(rp::GetKey(e, "classname").c_str(), "reflection_probe") == 0) return "a reflection_probe cannot be replaced in place: delete it and import its new text";
            return std::string();
        }

        // Deletes, point entities replaced in place and new objects, as one undo step. Everything goes first, then one
        // import of the new text followed by the replaced entities', the last entities imported, which get their GUIDs
        // back. A failure after the first change undoes the whole step.
        Reply Edit(Call& c)
        {
            EditPlan plan;
            std::string why;
            if (!ParseEdit(c.body, plan, why)) return Err("bad-request", why);
            if (!Trim(plan.import).empty() && !CheckMapText(plan.import, why)) return Err("bad-request", "import: " + why);
            for (const auto& rp : plan.replaces)
                if (!CheckMapText(rp.second, why)) return Err("bad-request", "replace " + rp.first + ": " + why);
            if (!rp::Has(&rp::host->EndUndo) || !rp::host->BeginUndo || !rp::host->EndUndo) return Err("refused", "this MTPlugins cannot make one undo step of several edits: update it");
            if (!plan.replaces.empty() && !(rp::Has(&rp::host->RestampImportedEntities) && rp::host->RestampImportedEntities))
                return Err("refused", "this MTPlugins cannot give a replaced entity its GUID back: update it");
            std::string world = WorldGuid();
            for (const std::string& g : plan.deletes)
                if (_stricmp(g.c_str(), world.c_str()) == 0) return Err("refused", "worldspawn cannot be deleted");
            std::vector<std::string> targets = plan.deletes, replaced;
            for (const auto& r : plan.replaces) replaced.push_back(r.first);
            std::vector<ModToolsEntity> ents = rp::FindEntitiesByGuid(replaced);
            for (size_t i = 0; i < ents.size(); ++i)
            {
                std::string no = Unreplaceable(ents[i], replaced[i]);
                if (no == "not-found") return Err("not-found", replaced[i]);
                if (!no.empty()) return Err("refused", no);
            }
            targets.insert(targets.end(), replaced.begin(), replaced.end());
            std::string label = Label(c.h.args, 0, "bridge edit");
            std::vector<Picked> saved = SaveSelection();
            std::vector<std::string> missing;
            std::vector<ModToolsBrush> brushes = Resolve(targets, missing);    // deselects everything
            if (!missing.empty())
            {
                RestoreSelection(saved);
                return Err("not-found", Join(missing, ','));
            }
            std::string text = Trim(plan.import).empty() ? std::string(rp::detail::EmptyWorldBlock()) : plan.import;
            std::vector<std::string> brushGuids;
            std::vector<size_t> counts;
            for (const auto& r : plan.replaces)
            {
                text += r.second;
                std::vector<std::string> bg = rp::BrushGuidsInText(r.second);
                counts.push_back(bg.size());
                brushGuids.insert(brushGuids.end(), bg.begin(), bg.end());
            }
            bool importing = !Trim(plan.import).empty() || !plan.replaces.empty();
            Reply r = Ok("deleted=" + std::to_string(brushes.size()));
            bool changed = false;
            rp::host->BeginUndo(rp::self, label.c_str());
            if (!brushes.empty())
            {
                if (rp::host->DeleteBrushes(rp::self, brushes.data(), brushes.size(), label.c_str())) changed = true;
                else r = Err("refused", "Radiant refused the delete");
            }
            if (r.ok && importing)
            {
                if (!rp::host->ImportMapText(rp::self, text.c_str(), label.c_str())) r = Err("refused", "Radiant refused the text to import");
                else
                {
                    changed = true;
                    std::vector<const char*> eg, bg;
                    for (const std::string& g : replaced) eg.push_back(g.c_str());
                    for (const std::string& g : brushGuids) bg.push_back(g.c_str());
                    if (!eg.empty() && rp::host->RestampImportedEntities(rp::self, eg.data(), eg.size(), bg.data(), counts.data()) != eg.size())
                        r = Err("refused", "Radiant could not give the replaced entities their GUIDs back");
                    else r.body = ImportedRows();
                }
            }
            rp::host->EndUndo(rp::self);
            if (!r.ok && changed) rp::host->TriggerMenuAction(rp::self, "Edit/Undo");
            RestoreSelection(saved);
            return r;
        }

        Reply Delete(Call& c)
        {
            std::vector<std::string> guids = Lines(c.body);
            if (guids.empty()) return Err("bad-request", "delete needs one GUID per line in its body");
            std::string world = WorldGuid();
            for (const std::string& g : guids)
                if (g == world) return Err("refused", "worldspawn cannot be deleted");
            std::string label = Label(c.h.args, 0, "bridge delete");
            std::vector<Picked> saved = SaveSelection();
            std::vector<std::string> missing;
            std::vector<ModToolsBrush> brushes = Resolve(guids, missing);
            Reply r = Ok("deleted=" + std::to_string(brushes.size()));
            if (!missing.empty()) r = Err("not-found", Join(missing, ','));
            else if (!rp::host->DeleteBrushes(rp::self, brushes.data(), brushes.size(), label.c_str())) r = Err("refused", "Radiant refused the delete");
            RestoreSelection(saved);
            return r;
        }

        // Delete then import as one undo step, the old GUIDs written back (MTPlugins' rp::ReplaceEntity, with its
        // reasons returned and a failed import undone).
        Reply Replace(Call& c)
        {
            if (c.w.empty() || c.body.empty()) return Err("bad-request", "replace <guid> <label…>, with the entity's new text in its body");
            std::string bad;
            if (!CheckMapText(c.body, bad)) return Err("bad-request", bad);
            const std::string guid = c.w[0];
            std::string label = Label(c.h.args, 1, "bridge replace");
            ModToolsEntity e = rp::FindEntitiesByGuid({ guid })[0];
            if (!e) return Err("not-found", guid);
            if (e == rp::host->WorldEntity(rp::self)) return Err("refused", "worldspawn cannot be replaced: use setkey");
            std::string cls = rp::GetKey(e, "classname");
            if (_stricmp(cls.c_str(), "reflection_probe") == 0) return Err("refused", "a reflection_probe cannot be replaced in place");
            std::vector<Picked> saved = SaveSelection();
            rp::host->DeselectAll(rp::self);    // links its selected brushes back
            std::vector<ModToolsBrush> brushes;
            rp::ForEachEntityBrush(e, [&](ModToolsBrush b) { brushes.push_back(b); });
            if (brushes.empty())
            {
                RestoreSelection(saved);
                return Err("refused", "the entity has no brush to replace");
            }
            std::vector<std::string> brushGuids = rp::BrushGuidsInText(c.body);
            std::vector<const char*> bg;
            for (const std::string& g : brushGuids) bg.push_back(g.c_str());
            std::string text = std::string(rp::detail::EmptyWorldBlock()) + c.body;
            Reply r;
            bool deleted = false;
            rp::host->BeginUndo(rp::self, label.c_str());
            if (!rp::host->DeleteBrushes(rp::self, brushes.data(), brushes.size(), label.c_str())) r = Err("refused", "Radiant refused to delete the old entity");
            else
            {
                deleted = true;
                if (!rp::host->ImportMapText(rp::self, text.c_str(), label.c_str())) r = Err("refused", "Radiant refused the new text; the old entity is back");
                else
                {
                    std::vector<std::string> fresh = rp::SelectedEntityGuids();    // the import leaves the new entity selected
                    bool restamp = _stricmp(cls.c_str(), "misc_prefab") != 0 && rp::host->RestampImportedGuids(rp::self, guid.c_str(), bg.data(), bg.size());
                    r = Ok(restamp ? guid : fresh.empty() ? std::string() : fresh.front());
                }
            }
            rp::host->EndUndo(rp::self);
            if (!r.ok && deleted) rp::host->TriggerMenuAction(rp::self, "Edit/Undo");
            RestoreSelection(saved);
            return r;
        }

        Reply SetKey(Call& c)
        {
            if (c.w.size() < 2) return Err("bad-request", "setkey <guid> <key> [<value…>]");
            const std::string& key = c.w[1];
            std::string value = Rest(c.h.args, 2);
            if (key == "guid" || key == "classname") return Err("refused", key + " cannot be set in place");
            if (!CleanValue(key) || !CleanValue(value)) return Err("bad-request", "a key or value cannot hold a double quote or a line break");
            ModToolsEntity e = rp::FindEntitiesByGuid({ c.w[0] })[0];
            if (!e) return Err("not-found", c.w[0]);
            std::string why = radiant::SetKey(e, key, value);
            if (why == "busy") return Err("busy", "an undo operation is still open");
            return why.empty() ? Ok() : Err("refused", why);
        }

        Reply Select(Call& c)
        {
            if (c.w.empty()) return Err("bad-request", "select <guid>…");
            std::vector<std::string> missing;
            std::vector<ModToolsBrushInst> insts = InstsOf(Resolve(c.w, missing));
            if (!insts.empty()) rp::host->SelectBrushInsts(rp::self, insts.data(), insts.size());
            return Ok(missing.empty() ? std::string() : "missing=" + Join(missing, ','), SelectionText());
        }

        Reply Deselect(Call&)
        {
            rp::host->DeselectAll(rp::self);
            return Ok();
        }

        Reply Command(Call& c)
        {
            std::string path = Trim(c.h.args);
            if (path.empty()) return Err("bad-request", "command <menu path…>");
            int r = rp::host->TriggerMenuAction(rp::self, path.c_str());
            return r == 1 ? Ok() : r == 0 ? Err("missing", "no menu entry " + path) : Err("disabled", path + " is disabled");
        }

        Reply Menus(Call& c)
        {
            std::string path = Trim(c.h.args);
            size_t n = rp::host->MenuEntries(rp::self, path.c_str(), nullptr, 0);
            std::string s(n, '\0');
            if (n) rp::host->MenuEntries(rp::self, path.c_str(), &s[0], n + 1);
            return Ok(std::string(), s);
        }

        Reply Action(Call& c)
        {
            bool trigger = c.w.size() == 2 && c.w[1] == "trigger";
            if (c.w.empty() || c.w.size() > 2 || (c.w.size() == 2 && !trigger)) return Err("bad-request", "action <name> [trigger]");
            radiant::ActionState s;
            std::string why = radiant::Action(c.w[0], trigger, s);
            if (why == "not-found") return Err("not-found", "no action named " + c.w[0]);
            if (why == "disabled") return Err("disabled", c.w[0] + " is disabled");
            if (!why.empty()) return Err("refused", why);
            return Ok(std::string("checkable=") + (s.checkable ? "1" : "0") + " checked=" + (s.checked ? "1" : "0") + " enabled=" + (s.enabled ? "1" : "0"));
        }

        Reply Camera(Call& c)
        {
            if (c.w.size() == 6)
            {
                float v[6];
                if (!ParseFloats(c.w, 0, v, 6)) return Err("bad-request", "camera <x> <y> <z> <pitch> <yaw> <roll>");
                if (!rp::host->SetCamera(rp::self, v, v + 3)) return Err("refused", "Radiant refused the camera");
                rp::host->RequestRedraw(rp::self, MODTOOLS_VIEW_3D | MODTOOLS_VIEW_2D);
            }
            else if (!c.w.empty()) return Err("bad-request", "camera [<x> <y> <z> <pitch> <yaw> <roll>]");
            float o[3], a[3];
            if (!rp::host->GetCamera(rp::self, o, a)) return Err("refused", "the camera is not there yet");
            return Ok(Num(o[0]) + " " + Num(o[1]) + " " + Num(o[2]) + " " + Num(a[0]) + " " + Num(a[1]) + " " + Num(a[2]));
        }

        Reply Bake(Call&)
        {
            std::string why = radiant::Bake();
            return why.empty() ? Ok() : Err("refused", why);
        }

        Reply Save(Call&)
        {
            std::wstring path = rp::CurrentMapPath();
            std::wstring tail = path.size() >= 11 ? path.substr(path.size() - 11) : path;
            if (path.empty() || _wcsicmp(tail.c_str(), L"unnamed.map") == 0) return Err("refused", "the map has no name yet: save it once in Radiant");
            if (rp::host->TriggerMenuAction(rp::self, "File/Save") != 1) return Err("refused", "File > Save is unavailable");
            return Ok(Utf8(path));
        }

        Reply Reload(Call& c)
        {
            std::string path = Trim(c.h.args);
            if (path.empty()) return Err("bad-request", "reload <path…>");
            std::wstring wp = Wide(path);
            if (GetFileAttributesW(wp.c_str()) == INVALID_FILE_ATTRIBUTES) return Err("not-found", path);
            if (rp::host->PrefabDepth(rp::self) > 0) return Err("refused", "leave the prefab first");
            if (radiant::MouseHeld()) return Err("busy", "a mouse button is held");
            rp::host->DeselectAll(rp::self);    // a selection makes the load ask "Copy the current selection?"
            if (!rp::host->ReloadMap(rp::self, wp.c_str(), 1)) return Err("refused", "Radiant did not open " + path);
            return Ok();
        }

        Reply Subscribe(Call& c)
        {
            if (c.w.empty()) return Err("bad-request", "subscribe <event>… (console, map)");
            uint32_t mask = 0;
            for (const std::string& e : c.w)
                if (e == "console") mask |= kEventConsole;
                else if (e == "map") mask |= kEventMap;
                else return Err("bad-request", "no event " + e + " (console, map)");
            c.conn->events |= mask;
            return Ok();
        }

        // ---- capture ------------------------------------------------------------------------------------------
        struct CaptureJob
        {
            std::shared_ptr<Conn> conn;
            uint64_t id = 0;
            int view = 3;                     // 0 YZ, 1 XZ, 2 XY (ModToolsXYParams::type), 3 the 3D view
            bool pose = false;
            float origin[3] = {}, angles[3] = {};   // the user's camera, put back after a pose
            std::vector<Picked> selection;
            std::vector<std::array<float, 3>> points;
            unsigned target = 0;
            ULONGLONG deadline = 0;
            bool drawn = false;
            int others = 0, otherView = -1;   // 2D frames of another axis since the request
            ModToolsDrawContext ctx{};
            std::vector<std::array<float, 2>> projected;
            std::vector<bool> inFront;
        };

        std::unique_ptr<CaptureJob> g_job;
        unsigned g_frames[4] = {};

        int ViewIndex(const std::string& v) { return v == "3d" ? 3 : v == "xy" ? 2 : v == "xz" ? 1 : v == "yz" ? 0 : -1; }

        HWND ViewWindow(int view) { return static_cast<HWND>(view == 3 ? rp::host->CamWindow(rp::self) : rp::host->XYWindow(rp::self, view)); }

        Reply Capture(Call& c)
        {
            int view = c.w.empty() ? -1 : ViewIndex(c.w[0]);
            if (view < 0 || (c.w.size() != 1 && c.w.size() != 7) || (c.w.size() == 7 && view != 3))
                return Err("bad-request", "capture 3d|xy|xz|yz [<x> <y> <z> <pitch> <yaw> <roll>] (a pose for 3d only)");
            auto job = std::make_unique<CaptureJob>();
            float pose[6];
            job->pose = c.w.size() == 7;
            if (job->pose && !ParseFloats(c.w, 1, pose, 6)) return Err("bad-request", "a pose is six numbers");
            for (const std::string& l : Lines(c.body))
            {
                std::vector<std::string> p = Words(l);
                std::array<float, 3> xyz{};
                if (p.size() != 3 || !ParseFloats(p, 0, xyz.data(), 3)) return Err("bad-request", "a point is \"x y z\", not \"" + l + "\"");
                job->points.push_back(xyz);
            }
            HWND main = MainWindow();
            if (main && IsIconic(main)) return Err("minimised", "Radiant is minimised: it draws nothing");
            // a 2D view gets its window once it draws as that axis, which the redraw below may be the first time
            if (view == 3 && !ViewWindow(view)) return Err("not-found", "the 3D view has not drawn yet");
            if (job->pose && radiant::MouseHeld()) return Err("busy", "a mouse button is held");
            job->conn = c.conn;
            job->id = c.h.id;
            job->view = view;
            job->selection = SaveSelection();
            rp::host->DeselectAll(rp::self);    // selected brushes draw tinted
            if (job->pose)
            {
                rp::host->GetCamera(rp::self, job->origin, job->angles);
                rp::host->SetCamera(rp::self, pose, pose + 3);
            }
            // the second frame after the request: the first may have been under way already
            job->target = g_frames[view] + 2;
            job->deadline = GetTickCount64() + 3000;
            rp::host->RequestRedraw(rp::self, view == 3 ? MODTOOLS_VIEW_3D : MODTOOLS_VIEW_2D);
            g_job = std::move(job);
            c.later = true;
            return Ok();
        }

        Reply Shoot(const CaptureJob& j)
        {
            std::string pixels;
            int w = 0, h = 0;
            std::string why = radiant::PrintView(ViewWindow(j.view), pixels, w, h);
            if (!why.empty()) return Err("refused", why);
            std::string summary = "w=" + std::to_string(w) + " h=" + std::to_string(h);
            if (j.view == 3)
                summary += " origin=" + Num3(j.ctx.cam.origin) + " angles=" + Num3(j.ctx.cam.angles) + " game_view=" + (j.ctx.cam.gameView ? "1" : "0");
            else
                summary += " centre=" + Num3(j.ctx.xy.origin) + " scale=" + Num(j.ctx.xy.scale);
            // the points were projected for the frame's size, the pixels have the window's
            float kx = j.ctx.width > 0 ? float(w) / float(j.ctx.width) : 1.0f, ky = j.ctx.height > 0 ? float(h) / float(j.ctx.height) : 1.0f;
            std::string body = std::move(pixels);
            for (size_t i = 0; i < j.projected.size(); ++i)
                body += j.inFront[i] ? Num(j.projected[i][0] * kx) + " " + Num(j.projected[i][1] * ky) + "\n" : std::string("-\n");
            return Ok(summary, body);
        }

        void Restore(const CaptureJob& j)
        {
            if (j.pose)
            {
                rp::host->SetCamera(rp::self, j.origin, j.angles);
                rp::host->RequestRedraw(rp::self, MODTOOLS_VIEW_3D | MODTOOLS_VIEW_2D);
            }
            RestoreSelection(j.selection);
        }

        struct Entry
        {
            const char* name;
            Handler fn;
            bool fast;
        };

        const Entry kCommands[] = {
            { "hello", Hello, true },
            { "state", State, true },
            { "menus", Menus, true },
            { "entities", Entities, false },
            { "selection", Selection, false },
            { "import", Import, false },
            { "delete", Delete, false },
            { "replace", Replace, false },
            { "edit", Edit, false },
            { "setkey", SetKey, false },
            { "select", Select, false },
            { "deselect", Deselect, false },
            { "command", Command, false },
            { "action", Action, false },
            { "camera", Camera, false },
            { "capture", Capture, false },
            { "bake", Bake, false },
            { "save", Save, false },
            { "reload", Reload, false },
            { "subscribe", Subscribe, false },
        };

        const Entry* Find(const std::string& name)
        {
            for (const Entry& e : kCommands)
                if (name == e.name) return &e;
            return nullptr;
        }
    }

    bool IsFast(const std::string& command)
    {
        const Entry* e = Find(command);
        return e && e->fast;
    }

    bool Run(const std::shared_ptr<Conn>& c, const Pending& p, Reply& out, bool blocked, const std::string& dialog)
    {
        const Entry* e = Find(p.h.command);
        if (!e)
        {
            out = Err("bad-request", "no command " + p.h.command);
            return true;
        }
        if (!c->hello && p.h.command != "hello")
        {
            out = Err("bad-request", "hello first");
            return true;
        }
        Call call{ c, p.h, p.body, Words(p.h.args), blocked, dialog };
        try
        {
            out = e->fn(call);
        }
        catch (const std::exception& ex)
        {
            out = Err("refused", std::string("the plugin failed: ") + ex.what());
            return true;
        }
        return !call.later;
    }

    bool CaptureActive() { return g_job != nullptr; }

    void CaptureDraw(const ModToolsDrawContext* ctx)
    {
        if (!ctx) return;
        int view = ctx->view == MODTOOLS_VIEW_3D ? 3 : ctx->xy.type >= 0 && ctx->xy.type < 3 ? ctx->xy.type : -1;
        if (view < 0) return;
        ++g_frames[view];
        if (view < 3) g_lastView2D = view;
        CaptureJob* j = g_job.get();
        if (j && !j->drawn && j->view < 3 && view < 3 && view != j->view)
        {
            ++j->others;
            j->otherView = view;
        }
        if (!j || j->drawn || j->view != view || g_frames[view] < j->target) return;
        j->ctx = *ctx;
        for (const auto& p : j->points)
        {
            float sx = 0, sy = 0;
            bool front = rp::host->Project(rp::self, p.data(), &sx, &sy) != 0;
            j->projected.push_back({ sx, sy });
            j->inFront.push_back(front);
        }
        j->drawn = true;
    }

    void CaptureTick()
    {
        CaptureJob* j = g_job.get();
        if (j && !j->drawn && j->view < 3) rp::host->RequestRedraw(rp::self, MODTOOLS_VIEW_2D);   // a 2D view draws only while dirty
        // the 2D views drew twice, none of them as this axis: it is not shown, no need to wait
        bool elsewhere = j && !j->drawn && j->others >= 2;
        if (!j || (!j->drawn && !elsewhere && GetTickCount64() < j->deadline)) return;
        std::unique_ptr<CaptureJob> job = std::move(g_job);
        const char* next = ": command View/Layout/Next (XY, YZ, XY) turns it XY, XZ, YZ";
        Reply r = job->drawn ? Shoot(*job)
                  : job->view == 3 ? Err("refused", "the 3D view drew no frame within 3 s")
                  : elsewhere ? Err("not-found", std::string("the 2D view shows ") + kLayouts[job->otherView] + next)
                  : Err("not-found", std::string("no 2D view drew within 3 s (is one open? command View/Toggle View Window/Active XY)"));
        Restore(*job);
        Complete(job->conn, job->id, r);
    }

    void CaptureCancel()
    {
        if (!g_job) return;
        std::unique_ptr<CaptureJob> job = std::move(g_job);
        Restore(*job);
    }
}
