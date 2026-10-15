#include "editor/Editor.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

namespace cc {
namespace {

bool isHat(BlockKind kind) { return static_cast<uint32_t>(kind) <= static_cast<uint32_t>(BlockKind::WhenHurt); }

const char* blockTitle(BlockKind kind) {
    switch (kind) {
        case BlockKind::WhenSpawned: return "when spawned";
        case BlockKind::EveryFrame: return "every frame";
        case BlockKind::WhenButton1: return "when button 1 pressed";
        case BlockKind::WhenButton2: return "when button 2 pressed";
        case BlockKind::WhenTouchHero: return "when touching the hero";
        case BlockKind::WhenHurt: return "when hurt";
        case BlockKind::Move: return "move";
        case BlockKind::Turn: return "turn around";
        case BlockKind::Jump: return "jump";
        case BlockKind::SetVelocity: return "set velocity";
        case BlockKind::FacePlayer: return "face the hero";
        case BlockKind::Bounce: return "bounce";
        case BlockKind::Wait: return "wait";
        case BlockKind::If: return "if";
        case BlockKind::Repeat: return "repeat";
        case BlockKind::SetProp: return "set";
        case BlockKind::ChangeProp: return "change";
        case BlockKind::Shoot: return "shoot";
        case BlockKind::Sound: return "play sound";
        case BlockKind::Vanish: return "vanish";
        case BlockKind::AddScore: return "add score";
        case BlockKind::HurtHero: return "hurt the hero";
    }
    return "block";
}

ImVec4 blockColor(BlockKind kind) {
    const auto id = static_cast<uint32_t>(kind);
    if (id <= static_cast<uint32_t>(BlockKind::WhenHurt)) return ImVec4(0.90f, 0.50f, 0.12f, 1.f);
    if (id <= static_cast<uint32_t>(BlockKind::Bounce)) return ImVec4(0.20f, 0.45f, 0.90f, 1.f);
    if (id <= static_cast<uint32_t>(BlockKind::Repeat)) return ImVec4(0.86f, 0.66f, 0.14f, 1.f);
    if (id <= static_cast<uint32_t>(BlockKind::ChangeProp)) return ImVec4(0.16f, 0.62f, 0.32f, 1.f);
    return ImVec4(0.52f, 0.30f, 0.78f, 1.f);
}

Block makeBlock(BlockKind kind) {
    Block block;
    block.kind = kind;
    switch (kind) {
        case BlockKind::Move: block.num = 80.f; break;
        case BlockKind::SetVelocity: block.num = 60.f; break;
        case BlockKind::Wait: block.num = 0.4f; break;
        case BlockKind::Repeat: block.num = 3.f; break;
        case BlockKind::ChangeProp: block.num = 1.f; break;
        case BlockKind::AddScore: block.num = 50.f; break;
        case BlockKind::HurtHero: block.num = 1.f; break;
        case BlockKind::SetProp: block.name = "speed"; break;
        default: break;
    }
    return block;
}

const char* kBuiltins[] = {"speed", "jump", "health", "lives", "score"};

bool samePath(const std::vector<int>& a, const std::vector<int>& b) { return a == b; }

struct FoundBlock {
    std::vector<Block>* list = nullptr;
    int index = -1;
};

FoundBlock findBlock(ActorDef& actor, int script, const std::vector<int>& path) {
    if (script < 0 || script >= static_cast<int>(actor.scripts.size()) || path.empty()) return {};
    std::vector<Block>* list = &actor.scripts[static_cast<size_t>(script)].body;
    for (size_t depth = 0; depth < path.size(); ++depth) {
        const int index = path[depth];
        if (!list || index < 0 || index >= static_cast<int>(list->size())) return {};
        if (depth + 1 == path.size()) return {list, index};
        list = &(*list)[static_cast<size_t>(index)].inner;
    }
    return {};
}

bool propCombo(const char* id, std::string& name, const ActorDef& actor) {
    const char* shown = name.empty() ? "property" : name.c_str();
    bool changed = false;
    if (ImGui::BeginCombo(id, shown)) {
        for (const char* builtin : kBuiltins) {
            if (ImGui::Selectable(builtin, name == builtin)) {
                name = builtin;
                changed = true;
            }
        }
        for (const auto& prop : actor.props) {
            if (prop.name.empty()) continue;
            if (ImGui::Selectable(prop.name.c_str(), name == prop.name)) {
                name = prop.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void paletteButton(BlockKind kind, const std::function<void(BlockKind)>& add) {
    ImGui::PushStyleColor(ImGuiCol_Button, blockColor(kind));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, blockColor(kind));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, blockColor(kind));
    ImGui::PushID(static_cast<int>(kind));
    if (ImGui::Button(blockTitle(kind), ImVec2(-1, 0))) add(kind);
    if (ImGui::BeginDragDropSource()) {
        int payload = static_cast<int>(kind);
        ImGui::SetDragDropPayload("CCBLOCK", &payload, sizeof(payload));
        ImGui::TextUnformatted(blockTitle(kind));
        ImGui::EndDragDropSource();
    }
    ImGui::PopID();
    ImGui::PopStyleColor(3);
}

struct Drop {
    int kind = -1;
    int script = -1;
    std::vector<int> path;
    bool inside = false;
    bool append = false;
};

void sameLineFit(float width) {
    const ImVec2 item = ImGui::GetItemRectMax();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (item.x + ImGui::GetStyle().ItemSpacing.x + width <= right - 2.f) ImGui::SameLine();
}

void acceptDrop(Drop& drop, int script, const std::vector<int>& path, bool inside, bool append) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CCBLOCK")) {
        drop.kind = *static_cast<const int*>(payload->Data);
        drop.path = path;
        drop.inside = inside;
        drop.append = append;
        drop.script = script;
    }
    ImGui::EndDragDropTarget();
}

}  // namespace

void Editor::drawBlockEditor() {
    if (!blocksOpen_) return;
    if (blockActor_ < 0 || blockActor_ >= static_cast<int>(project_.actors.size())) {
        blocksOpen_ = false;
        return;
    }
    ActorDef& actor = project_.actors[static_cast<size_t>(blockActor_)];
    const bool hero = actor.kind == ActorKind::Hero;
    const std::string title = (hero ? "Hero properties — " : "Actor blocks — ") + actor.name;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 work = viewport->WorkSize;
    const ImVec2 minSize(std::min(280.f, std::max(160.f, work.x)), std::min(200.f, std::max(120.f, work.y)));
    ImGui::SetNextWindowSizeConstraints(minSize, work);
    ImGui::SetNextWindowSize(ImVec2(std::min(1040.f, work.x), std::min(700.f, work.y)), ImGuiCond_FirstUseEver);
    const ImVec2 placed(viewport->WorkPos.x + std::max(0.f, (work.x - 1040.f) * 0.5f),
                        viewport->WorkPos.y + std::max(0.f, (work.y - 700.f) * 0.5f));
    ImGui::SetNextWindowPos(placed, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &blocksOpen_)) {
        ImGui::End();
        return;
    }

    Drop drop;
    auto addKind = [&](BlockKind kind) {
        Block block = makeBlock(kind);
        if (block.name.empty() && (kind == BlockKind::SetProp || kind == BlockKind::ChangeProp) && !actor.props.empty())
            block.name = actor.props[0].name;
        if (isHat(kind)) {
            Script script;
            script.hat = block;
            actor.scripts.push_back(std::move(script));
            blockScript_ = static_cast<int>(actor.scripts.size()) - 1;
            blockPath_.clear();
            return;
        }
        if (actor.scripts.empty()) {
            Script script;
            script.hat.kind = BlockKind::EveryFrame;
            actor.scripts.push_back(std::move(script));
            blockScript_ = 0;
        }
        if (blockScript_ < 0 || blockScript_ >= static_cast<int>(actor.scripts.size())) blockScript_ = 0;
        FoundBlock found = findBlock(actor, blockScript_, blockPath_);
        if (found.list && (found.list->at(static_cast<size_t>(found.index)).kind == BlockKind::If ||
                           found.list->at(static_cast<size_t>(found.index)).kind == BlockKind::Repeat)) {
            Block& parent = found.list->at(static_cast<size_t>(found.index));
            parent.inner.push_back(std::move(block));
            blockPath_.push_back(static_cast<int>(parent.inner.size()) - 1);
            return;
        }
        if (found.list) {
            const int at = found.index + 1;
            found.list->insert(found.list->begin() + at, std::move(block));
            blockPath_.back() = at;
            return;
        }
        Script& script = actor.scripts[static_cast<size_t>(blockScript_)];
        script.body.push_back(std::move(block));
        blockPath_ = {static_cast<int>(script.body.size()) - 1};
    };

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float longest = ImGui::CalcTextSize("when touching the hero").x + ImGui::GetStyle().FramePadding.x * 2.f + 16.f;
    const float minScript = 200.f;
    const bool stack = avail.x < longest + 160.f + minScript + spacing * 2.f;
    float propsW = stack ? avail.x : std::clamp(avail.x * 0.22f, 160.f, 280.f);
    float paletteW = stack ? avail.x : std::clamp(std::max(longest, avail.x * 0.30f), longest, 340.f);
    if (!stack && propsW + paletteW + minScript + spacing * 2.f > avail.x) {
        const float room = std::max(80.f, avail.x - minScript - spacing * 2.f);
        const float scale = room / std::max(1.f, propsW + paletteW);
        propsW *= scale;
        paletteW *= scale;
    }
    float propsH = avail.y;
    float paletteH = avail.y;
    float scriptH = avail.y;
    if (stack) {
        const float gap = ImGui::GetStyle().ItemSpacing.y * 2.f;
        const float budget = std::max(48.f, avail.y - gap);
        propsH = budget * 0.24f;
        paletteH = budget * 0.36f;
        scriptH = budget - propsH - paletteH;
    }

    ImGui::BeginChild("props", ImVec2(stack ? 0.f : propsW, propsH), true);
        ImGui::TextUnformatted(hero ? "Hero properties" : "Properties");
        ImGui::TextWrapped("speed, jump, health, lives, and score are live. Scripts can read and change them.");
        int removeProp = -1;
        for (int i = 0; i < static_cast<int>(actor.props.size()); ++i) {
            ImGui::PushID(i);
            PropDef& prop = actor.props[static_cast<size_t>(i)];
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%s", prop.name.c_str());
            const float row = ImGui::GetContentRegionAvail().x;
            const float closeW = ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.f;
            const float valueW = std::clamp(row * 0.32f, 56.f, 84.f);
            const float nameW = row - valueW - closeW - spacing * 2.f;
            if (nameW < 72.f) {
                ImGui::SetNextItemWidth(-1.f);
                if (ImGui::InputText("##name", buffer, sizeof(buffer))) prop.name = buffer;
                ImGui::SetNextItemWidth(std::max(40.f, row - closeW - spacing));
                ImGui::InputFloat("##value", &prop.initial, 0, 0, "%.1f");
                sameLineFit(closeW);
            } else {
                ImGui::SetNextItemWidth(nameW);
                if (ImGui::InputText("##name", buffer, sizeof(buffer))) prop.name = buffer;
                sameLineFit(valueW);
                ImGui::SetNextItemWidth(valueW);
                ImGui::InputFloat("##value", &prop.initial, 0, 0, "%.1f");
                sameLineFit(closeW);
            }
            if (ImGui::SmallButton("X")) removeProp = i;
            ImGui::PopID();
        }
        if (removeProp >= 0) actor.props.erase(actor.props.begin() + removeProp);
        if (ImGui::Button("Add property")) {
            PropDef prop;
            prop.name = "prop" + std::to_string(actor.props.size() + 1);
            actor.props.push_back(prop);
        }
    ImGui::EndChild();
    if (!stack) ImGui::SameLine();

    ImGui::BeginChild("palette", ImVec2(stack ? 0.f : paletteW, paletteH), true);
        ImGui::TextUnformatted("Blocks");
        ImGui::TextWrapped("Click a block to snap it on. Drag one onto the script too.");
        if (ImGui::CollapsingHeader("Events", ImGuiTreeNodeFlags_DefaultOpen)) {
            paletteButton(BlockKind::WhenSpawned, addKind);
            paletteButton(BlockKind::EveryFrame, addKind);
            paletteButton(BlockKind::WhenButton1, addKind);
            paletteButton(BlockKind::WhenButton2, addKind);
            paletteButton(BlockKind::WhenTouchHero, addKind);
            paletteButton(BlockKind::WhenHurt, addKind);
        }
        if (ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen)) {
            paletteButton(BlockKind::Move, addKind);
            paletteButton(BlockKind::Turn, addKind);
            paletteButton(BlockKind::Jump, addKind);
            paletteButton(BlockKind::SetVelocity, addKind);
            paletteButton(BlockKind::FacePlayer, addKind);
            paletteButton(BlockKind::Bounce, addKind);
        }
        if (ImGui::CollapsingHeader("Control", ImGuiTreeNodeFlags_DefaultOpen)) {
            paletteButton(BlockKind::Wait, addKind);
            paletteButton(BlockKind::If, addKind);
            paletteButton(BlockKind::Repeat, addKind);
        }
        if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            paletteButton(BlockKind::SetProp, addKind);
            paletteButton(BlockKind::ChangeProp, addKind);
        }
        if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
            paletteButton(BlockKind::Shoot, addKind);
            paletteButton(BlockKind::Sound, addKind);
            paletteButton(BlockKind::Vanish, addKind);
            paletteButton(BlockKind::AddScore, addKind);
            paletteButton(BlockKind::HurtHero, addKind);
        }
    ImGui::EndChild();
    if (!stack) ImGui::SameLine();

    ImGui::BeginChild("scriptpane", ImVec2(stack ? 0.f : 0.f, scriptH), true);
        if (ImGui::Button("Delete block")) blockEdit_ = 1;
        sameLineFit(ImGui::CalcTextSize("Up").x + 24.f);
        if (ImGui::Button("Up")) blockEdit_ = 2;
        sameLineFit(ImGui::CalcTextSize("Down").x + 24.f);
        if (ImGui::Button("Down")) blockEdit_ = 3;
        FoundBlock selected = findBlock(actor, blockScript_, blockPath_);
        if (selected.list && (selected.list->at(static_cast<size_t>(selected.index)).kind == BlockKind::If ||
                              selected.list->at(static_cast<size_t>(selected.index)).kind == BlockKind::Repeat)) {
            ImGui::TextWrapped("New blocks go inside the selected if / repeat.");
        } else if (actor.scripts.empty()) {
            ImGui::TextWrapped("Start with an event block, such as every frame or when button 1 pressed.");
        } else {
            ImGui::TextWrapped("New blocks snap under the selected block. Select an if or repeat to put blocks inside it.");
        }
        ImGui::TextWrapped(hero ? "The hero still moves with the joystick. A motion block overrides that for the frame it runs."
                                : "Scripts replace the built-in behavior for this actor.");

        int shown = blockScript_;
        std::function<void(std::vector<Block>&, std::vector<int>, int)> drawList;
        drawList = [&](std::vector<Block>& list, std::vector<int> path, int depth) {
            if (depth > 6) return;
            for (int i = 0; i < static_cast<int>(list.size()); ++i) {
                Block& block = list[static_cast<size_t>(i)];
                std::vector<int> here = path;
                here.push_back(i);
                ImGui::PushID(i);
                ImGui::Indent(depth * 16.f);
                const bool on = samePath(here, blockPath_);
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.f, 0.86f, 0.45f, 1.f));
                else ImGui::PushStyleColor(ImGuiCol_Button, blockColor(block.kind));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, blockColor(block.kind));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, blockColor(block.kind));
                if (ImGui::Button(blockTitle(block.kind), ImVec2(-1.f, 0))) {
                    blockScript_ = shown;
                    blockPath_ = here;
                }
                acceptDrop(drop, shown, here, false, false);
                ImGui::PopStyleColor(3);
                switch (block.kind) {
                    case BlockKind::Move:
                        ImGui::SetNextItemWidth(std::min(88.f, ImGui::GetContentRegionAvail().x));
                        ImGui::InputFloat("##n", &block.num, 0, 0, "%.0f");
                        sameLineFit(ImGui::CalcTextSize("pixels / second").x);
                        ImGui::TextUnformatted("pixels / second");
                        break;
                    case BlockKind::SetVelocity:
                        ImGui::SetNextItemWidth(std::min(88.f, ImGui::GetContentRegionAvail().x));
                        ImGui::InputFloat("##x", &block.num, 0, 0, "%.0f");
                        sameLineFit(88.f);
                        ImGui::SetNextItemWidth(std::min(88.f, ImGui::GetContentRegionAvail().x));
                        ImGui::InputFloat("##y", &block.num2, 0, 0, "%.0f");
                        break;
                    case BlockKind::Wait:
                        ImGui::SetNextItemWidth(std::min(88.f, ImGui::GetContentRegionAvail().x));
                        ImGui::InputFloat("##w", &block.num, 0, 0, "%.2f");
                        sameLineFit(ImGui::CalcTextSize("seconds").x);
                        ImGui::TextUnformatted("seconds");
                        break;
                    case BlockKind::Repeat:
                        ImGui::SetNextItemWidth(std::min(88.f, ImGui::GetContentRegionAvail().x));
                        ImGui::InputFloat("##r", &block.num, 0, 0, "%.0f");
                        sameLineFit(ImGui::CalcTextSize("times").x);
                        ImGui::TextUnformatted("times");
                        break;
                    case BlockKind::SetProp:
                        ImGui::SetNextItemWidth(std::min(140.f, std::max(70.f, ImGui::GetContentRegionAvail().x * 0.45f)));
                        propCombo("##p", block.name, actor);
                        sameLineFit(24.f);
                        ImGui::TextUnformatted("to");
                        sameLineFit(88.f);
                        ImGui::SetNextItemWidth(std::min(88.f, ImGui::GetContentRegionAvail().x));
                        ImGui::InputFloat("##v", &block.num, 0, 0, "%.1f");
                        break;
                    case BlockKind::ChangeProp:
                        ImGui::SetNextItemWidth(std::min(140.f, std::max(70.f, ImGui::GetContentRegionAvail().x * 0.45f)));
                        propCombo("##p", block.name, actor);
                        sameLineFit(24.f);
                        ImGui::TextUnformatted("by");
                        sameLineFit(88.f);
                        ImGui::SetNextItemWidth(std::min(88.f, ImGui::GetContentRegionAvail().x));
                        ImGui::InputFloat("##v", &block.num, 0, 0, "%.1f");
                        break;
                    case BlockKind::AddScore:
                    case BlockKind::HurtHero:
                        ImGui::SetNextItemWidth(std::min(88.f, ImGui::GetContentRegionAvail().x));
                        ImGui::InputFloat("##n", &block.num, 0, 0, "%.0f");
                        break;
                    case BlockKind::Sound:
                        ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x));
                        if (ImGui::BeginCombo("##snd", block.name.empty() ? "sound" : block.name.c_str())) {
                            for (const auto& sound : project_.sounds) {
                                if (ImGui::Selectable(sound.id.c_str(), block.name == sound.id)) block.name = sound.id;
                            }
                            ImGui::EndCombo();
                        }
                        break;
                    case BlockKind::If: {
                        const char* tests[] = {"property", "button 1 held", "button 2 held", "on the ground",
                                               "touching the hero", "wall ahead"};
                        const int test = std::clamp(block.pick, 0, 5);
                        ImGui::SetNextItemWidth(std::min(160.f, std::max(90.f, ImGui::GetContentRegionAvail().x)));
                        if (ImGui::BeginCombo("##if", tests[test])) {
                            for (int t = 0; t < 6; ++t) {
                                if (ImGui::Selectable(tests[t], test == t)) block.pick = t;
                            }
                            ImGui::EndCombo();
                        }
                        if (block.pick == static_cast<int>(BlockTest::Prop)) {
                            sameLineFit(120.f);
                            ImGui::SetNextItemWidth(std::min(130.f, std::max(70.f, ImGui::GetContentRegionAvail().x * 0.4f)));
                            propCombo("##pn", block.name, actor);
                            sameLineFit(48.f);
                            const char* ops[] = {">", "<", "="};
                            const int op = std::clamp(static_cast<int>(block.num2), 0, 2);
                            ImGui::SetNextItemWidth(46.f);
                            if (ImGui::BeginCombo("##op", ops[op])) {
                                for (int o = 0; o < 3; ++o) {
                                    if (ImGui::Selectable(ops[o], op == o)) block.num2 = static_cast<float>(o);
                                }
                                ImGui::EndCombo();
                            }
                            sameLineFit(72.f);
                            ImGui::SetNextItemWidth(std::min(80.f, ImGui::GetContentRegionAvail().x));
                            ImGui::InputFloat("##cv", &block.num, 0, 0, "%.1f");
                        }
                        break;
                    }
                    default: break;
                }
                if (block.kind == BlockKind::If || block.kind == BlockKind::Repeat) {
                    ImGui::Indent(12.f);
                    ImGui::BeginChild("inner", ImVec2(-1, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
                    acceptDrop(drop, shown, here, true, true);
                    drawList(block.inner, here, depth + 1);
                    if (block.inner.empty()) ImGui::TextUnformatted("Drop blocks inside");
                    ImGui::EndChild();
                    ImGui::Unindent(12.f);
                }
                ImGui::Unindent(depth * 16.f);
                ImGui::PopID();
            }
        };

        if (ImGui::BeginChild("workspace", ImVec2(0, 0), true)) {
            acceptDrop(drop, blockScript_, {}, false, true);
            if (actor.scripts.empty()) ImGui::TextUnformatted("This actor has no scripts yet.");
            for (int s = 0; s < static_cast<int>(actor.scripts.size()); ++s) {
                ImGui::PushID(s);
                Script& script = actor.scripts[static_cast<size_t>(s)];
                ImGui::PushStyleColor(ImGuiCol_Button, blockColor(script.hat.kind));
                const float removeW = ImGui::CalcTextSize("Remove script").x + ImGui::GetStyle().FramePadding.x * 2.f;
                const float hatRoom = ImGui::GetContentRegionAvail().x - removeW - ImGui::GetStyle().ItemSpacing.x;
                if (ImGui::Button(blockTitle(script.hat.kind), ImVec2(hatRoom > 80.f ? hatRoom : -1.f, 0))) {
                    blockScript_ = s;
                    blockPath_.clear();
                }
                ImGui::PopStyleColor();
                if (hatRoom > 80.f) ImGui::SameLine();
                if (ImGui::SmallButton("Remove script")) {
                    actor.scripts.erase(actor.scripts.begin() + s);
                    blockPath_.clear();
                    blockScript_ = 0;
                    ImGui::PopID();
                    break;
                }
                shown = s;
                drawList(script.body, {}, 0);
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    ImGui::EndChild();

    if (drop.kind >= 0) {
        Block block = makeBlock(static_cast<BlockKind>(drop.kind));
        const int scriptIndex = drop.script >= 0 ? drop.script : blockScript_;
        if (isHat(block.kind)) {
            addKind(block.kind);
        } else if (drop.append && drop.inside && !drop.path.empty()) {
            FoundBlock found = findBlock(actor, scriptIndex, drop.path);
            if (found.list) found.list->at(static_cast<size_t>(found.index)).inner.push_back(std::move(block));
        } else if (drop.append) {
            if (actor.scripts.empty()) addKind(BlockKind::EveryFrame);
            const int dest = scriptIndex >= 0 && scriptIndex < static_cast<int>(actor.scripts.size()) ? scriptIndex : 0;
            if (!actor.scripts.empty()) actor.scripts[static_cast<size_t>(dest)].body.push_back(std::move(block));
        } else if (!drop.path.empty()) {
            FoundBlock found = findBlock(actor, scriptIndex, drop.path);
            if (found.list) found.list->insert(found.list->begin() + found.index, std::move(block));
        }
    }

    if (blockEdit_ != 0) {
        FoundBlock found = findBlock(actor, blockScript_, blockPath_);
        if (found.list && found.index >= 0 && found.index < static_cast<int>(found.list->size())) {
            if (blockEdit_ == 1) {
                found.list->erase(found.list->begin() + found.index);
                blockPath_.clear();
            } else if (blockEdit_ == 2 && found.index > 0) {
                std::swap(found.list->at(static_cast<size_t>(found.index)),
                          found.list->at(static_cast<size_t>(found.index - 1)));
                blockPath_.back() = found.index - 1;
            } else if (blockEdit_ == 3 && found.index + 1 < static_cast<int>(found.list->size())) {
                std::swap(found.list->at(static_cast<size_t>(found.index)),
                          found.list->at(static_cast<size_t>(found.index + 1)));
                blockPath_.back() = found.index + 1;
            }
        }
        blockEdit_ = 0;
    }

    ImGui::End();
}

}  // namespace cc
