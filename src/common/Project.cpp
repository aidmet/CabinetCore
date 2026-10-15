#include "common/Project.hpp"

#include "common/BinaryIO.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace cc {
namespace {

constexpr char kMagic[8] = {'C', 'C', 'O', 'R', 'E', 'D', 'A', 'T'};
constexpr uint32_t kVersion = 3;

void writeBlock(ByteWriter& w, const Block& block) {
    w.u32(static_cast<uint32_t>(block.kind));
    w.f32(block.num);
    w.f32(block.num2);
    w.i32(block.pick);
    w.str(block.name);
    w.u32(static_cast<uint32_t>(block.inner.size()));
    for (const auto& child : block.inner) writeBlock(w, child);
}

Block readBlock(ByteReader& r, int depth) {
    Block block;
    block.kind = static_cast<BlockKind>(r.u32());
    block.num = r.f32();
    block.num2 = r.f32();
    block.pick = r.i32();
    block.name = r.str();
    const uint32_t count = r.u32();
    if (count > 256 || depth > 8) {
        r.ok = false;
        return block;
    }
    for (uint32_t i = 0; r.ok && i < count; ++i) block.inner.push_back(readBlock(r, depth + 1));
    return block;
}

void writeActorScript(ByteWriter& w, const ActorDef& actor) {
    w.u32(static_cast<uint32_t>(actor.props.size()));
    for (const auto& prop : actor.props) {
        w.str(prop.name);
        w.f32(prop.initial);
    }
    w.u32(static_cast<uint32_t>(actor.scripts.size()));
    for (const auto& script : actor.scripts) {
        writeBlock(w, script.hat);
        w.u32(static_cast<uint32_t>(script.body.size()));
        for (const auto& block : script.body) writeBlock(w, block);
    }
}

void readActorScript(ByteReader& r, ActorDef& actor) {
    const uint32_t propCount = r.u32();
    if (propCount > 64) {
        r.ok = false;
        return;
    }
    for (uint32_t i = 0; r.ok && i < propCount; ++i) {
        PropDef prop;
        prop.name = r.str();
        prop.initial = r.f32();
        actor.props.push_back(std::move(prop));
    }
    const uint32_t scriptCount = r.u32();
    if (scriptCount > 32) {
        r.ok = false;
        return;
    }
    for (uint32_t i = 0; r.ok && i < scriptCount; ++i) {
        Script script;
        script.hat = readBlock(r, 0);
        const uint32_t bodyCount = r.u32();
        if (bodyCount > 256) {
            r.ok = false;
            return;
        }
        for (uint32_t b = 0; r.ok && b < bodyCount; ++b) script.body.push_back(readBlock(r, 0));
        actor.scripts.push_back(std::move(script));
    }
}

void writeBehavior(ByteWriter& w, const Behavior& b) {
    w.u32(static_cast<uint32_t>(b.pattern));
    w.u32(static_cast<uint32_t>(b.aim));
    w.f32(b.speed);
    w.f32(b.fireInterval);
    w.f32(b.shotSpeed);
    w.f32(b.shotAngle);
    w.f32(b.spread);
    w.f32(b.amplitude);
    w.f32(b.period);
    w.f32(b.lifetime);
    w.i32(b.hp);
    w.i32(b.touchDamage);
    w.i32(b.score);
    w.i32(b.burst);
    w.boolean(b.gravity);
    w.boolean(b.turnAtEdge);
    w.boolean(b.turnAtWall);
    w.boolean(b.stomp);
}

Behavior readBehavior(ByteReader& r, uint32_t version) {
    Behavior b;
    b.pattern = static_cast<AiPattern>(r.u32());
    b.aim = static_cast<AimMode>(r.u32());
    b.speed = r.f32();
    b.fireInterval = r.f32();
    b.shotSpeed = r.f32();
    b.shotAngle = r.f32();
    b.spread = r.f32();
    b.amplitude = r.f32();
    b.period = r.f32();
    b.lifetime = r.f32();
    b.hp = r.i32();
    b.touchDamage = r.i32();
    b.score = r.i32();
    b.burst = r.i32();
    b.gravity = r.boolean();
    b.turnAtEdge = r.boolean();
    b.turnAtWall = r.boolean();
    if (version >= 3) b.stomp = r.boolean();
    return b;
}

template <typename T>
T* findId(std::vector<T>& items, const std::string& id) {
    for (auto& item : items)
        if (item.id == id) return &item;
    return nullptr;
}

template <typename T>
const T* findIdConst(const std::vector<T>& items, const std::string& id) {
    for (const auto& item : items)
        if (item.id == id) return &item;
    return nullptr;
}

}  // namespace

void Stage::resize(int w, int h) {
    w = std::max(1, w);
    h = std::max(1, h);
    auto copyLayer = [&](const std::vector<uint16_t>& src) {
        std::vector<uint16_t> dst(static_cast<size_t>(w) * h, 0);
        const int cw = std::min(w, width);
        const int ch = std::min(h, height);
        if (width > 0 && static_cast<int>(src.size()) >= width * height) {
            for (int y = 0; y < ch; ++y)
                for (int x = 0; x < cw; ++x)
                    dst[static_cast<size_t>(y) * w + x] = src[static_cast<size_t>(y) * width + x];
        }
        return dst;
    };
    main = copyLayer(main);
    back = copyLayer(back);
    front = copyLayer(front);
    width = w;
    height = h;
}

uint16_t Stage::get(const std::vector<uint16_t>& layer, int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    const size_t i = static_cast<size_t>(y) * width + x;
    if (i >= layer.size()) return 0;
    return layer[i];
}

void Stage::set(std::vector<uint16_t>& layer, int x, int y, uint16_t v) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const size_t i = static_cast<size_t>(y) * width + x;
    if (layer.size() < static_cast<size_t>(width) * height)
        layer.resize(static_cast<size_t>(width) * height, 0);
    layer[i] = v;
}

Sprite* Project::findSprite(const std::string& id) { return findId(sprites, id); }
const Sprite* Project::findSprite(const std::string& id) const { return findIdConst(sprites, id); }
Tileset* Project::findTileset(const std::string& id) { return findId(tilesets, id); }
const Tileset* Project::findTileset(const std::string& id) const { return findIdConst(tilesets, id); }
ActorDef* Project::findActor(const std::string& id) { return findId(actors, id); }
const ActorDef* Project::findActor(const std::string& id) const { return findIdConst(actors, id); }
Sound* Project::findSound(const std::string& id) { return findId(sounds, id); }
const Sound* Project::findSound(const std::string& id) const { return findIdConst(sounds, id); }
Stage* Project::findStage(const std::string& id) { return findId(stages, id); }

std::string Project::freshUid() { return "e" + std::to_string(nextUid++); }

std::string Project::validate() const {
    if (stages.empty()) return "Add at least one stage.";
    bool hero = false;
    for (const auto& actor : actors)
        if (actor.kind == ActorKind::Hero) hero = true;
    if (!hero) return "Add a hero actor.";
    for (const auto& stage : stages) {
        if (stage.intermission) continue;
        if (stage.main.size() != static_cast<size_t>(stage.width) * stage.height)
            return "Stage '" + stage.name + "' has a broken tile map.";
        bool placed = false;
        for (const auto& placement : stage.placements) {
            const ActorDef* actor = findActor(placement.actorId);
            if (actor && actor->kind == ActorKind::Hero) placed = true;
        }
        if (!placed) return "Place a hero on stage '" + stage.name + "'.";
    }
    return {};
}

bool Project::save(const std::string& path, std::string& err) const {
    ByteWriter w;
    w.bytes(reinterpret_cast<const uint8_t*>(kMagic), 8);
    w.u32(kVersion);
    w.str(settings.title);
    w.str(settings.author);
    w.i32(settings.viewW);
    w.i32(settings.viewH);
    w.i32(settings.scale);
    w.i32(settings.lives);
    w.i32(settings.maxHealth);
    w.boolean(settings.crt);
    w.boolean(settings.screenShake);
    w.str(settings.startStage);
    w.i32(nextUid);

    w.u32(static_cast<uint32_t>(sprites.size()));
    for (const auto& sprite : sprites) {
        w.str(sprite.id);
        w.str(sprite.name);
        w.i32(sprite.frameW);
        w.i32(sprite.frameH);
        w.i32(sprite.frames);
        w.i32(sprite.directions);
        w.f32(sprite.fps);
        w.i32(sprite.imgW);
        w.i32(sprite.imgH);
        w.u32(static_cast<uint32_t>(sprite.rgba.size()));
        if (!sprite.rgba.empty()) w.bytes(sprite.rgba.data(), sprite.rgba.size());
    }

    w.u32(static_cast<uint32_t>(tilesets.size()));
    for (const auto& tileset : tilesets) {
        w.str(tileset.id);
        w.str(tileset.name);
        w.i32(tileset.tileW);
        w.i32(tileset.tileH);
        w.i32(tileset.columns);
        w.i32(tileset.imgW);
        w.i32(tileset.imgH);
        w.u32(static_cast<uint32_t>(tileset.rgba.size()));
        if (!tileset.rgba.empty()) w.bytes(tileset.rgba.data(), tileset.rgba.size());
        w.u32(static_cast<uint32_t>(tileset.tiles.size()));
        for (const auto& tile : tileset.tiles) {
            w.u32(tile.flags);
            w.i32(tile.damage);
            w.i32(tile.frames);
            w.f32(tile.fps);
        }
    }

    w.u32(static_cast<uint32_t>(actors.size()));
    for (const auto& actor : actors) {
        w.str(actor.id);
        w.str(actor.name);
        w.str(actor.spriteId);
        w.str(actor.projectileId);
        w.str(actor.dropActorId);
        w.u32(static_cast<uint32_t>(actor.kind));
        w.u32(static_cast<uint32_t>(actor.pickup));
        w.f32(actor.hitW);
        w.f32(actor.hitH);
        w.f32(actor.dropChance);
        w.i32(actor.pickupAmount);
        w.boolean(actor.needsKey);
        w.boolean(actor.checkpoint);
        writeBehavior(w, actor.behavior);
        writeActorScript(w, actor);
    }

    w.u32(static_cast<uint32_t>(sounds.size()));
    for (const auto& sound : sounds) {
        w.str(sound.id);
        w.str(sound.name);
        w.i32(sound.sampleRate);
        w.i32(sound.channels);
        w.boolean(sound.music);
        w.i16s(sound.samples);
    }

    w.u32(static_cast<uint32_t>(stages.size()));
    for (const auto& stage : stages) {
        w.str(stage.id);
        w.str(stage.name);
        w.str(stage.tilesetId);
        w.str(stage.musicId);
        w.str(stage.message);
        w.i32(stage.width);
        w.i32(stage.height);
        w.u32(static_cast<uint32_t>(stage.physics));
        w.u32(static_cast<uint32_t>(stage.scroll));
        w.u32(static_cast<uint32_t>(stage.complete));
        w.f32(stage.autoScroll);
        w.f32(stage.gravity);
        w.f32(stage.jumpVel);
        w.f32(stage.playerSpeed);
        w.boolean(stage.intermission);
        auto writeLayer = [&](const std::vector<uint16_t>& layer) {
            w.u32(static_cast<uint32_t>(layer.size()));
            for (uint16_t cell : layer) w.u16(cell);
        };
        writeLayer(stage.main);
        writeLayer(stage.back);
        writeLayer(stage.front);
        w.u32(static_cast<uint32_t>(stage.placements.size()));
        for (const auto& placement : stage.placements) {
            w.str(placement.uid);
            w.str(placement.actorId);
            w.f32(placement.x);
            w.f32(placement.y);
        }
    }

    auto bytes = w.data();
    const uint32_t sum = crc32(bytes.data(), bytes.size());
    ByteWriter tail;
    tail.u32(sum);
    bytes.insert(bytes.end(), tail.data().begin(), tail.data().end());

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        err = "Could not write " + path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        err = "Failed while writing " + path;
        return false;
    }
    return true;
}

bool Project::load(const std::string& path, Project& project, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "Could not open " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 16) {
        err = "File is too small to be a CabinetCore game.";
        return false;
    }
    if (bytes.size() > 64u * 1024u * 1024u) {
        err = "File is larger than 64 MB.";
        return false;
    }
    const uint32_t stored = bytes[bytes.size() - 4] | (uint32_t(bytes[bytes.size() - 3]) << 8) |
                            (uint32_t(bytes[bytes.size() - 2]) << 16) | (uint32_t(bytes[bytes.size() - 1]) << 24);
    const uint32_t sum = crc32(bytes.data(), bytes.size() - 4);
    if (sum != stored) {
        err = "Checksum mismatch. game.dat is damaged.";
        return false;
    }
    ByteReader r(bytes.data(), bytes.size() - 4);
    const auto magic = r.bytes(8);
    if (!r.ok || magic.size() != 8 || std::memcmp(magic.data(), kMagic, 8) != 0) {
        err = "Not a CabinetCore game.dat.";
        return false;
    }
    const uint32_t version = r.u32();
    if (version < 1 || version > kVersion) {
        err = "Unsupported game.dat version.";
        return false;
    }

    Project loaded;
    loaded.settings.title = r.str();
    loaded.settings.author = r.str();
    loaded.settings.viewW = r.i32();
    loaded.settings.viewH = r.i32();
    loaded.settings.scale = r.i32();
    loaded.settings.lives = r.i32();
    loaded.settings.maxHealth = r.i32();
    loaded.settings.crt = r.boolean();
    loaded.settings.screenShake = r.boolean();
    loaded.settings.startStage = r.str();
    loaded.nextUid = r.i32();

    const uint32_t spriteCount = r.u32();
    for (uint32_t i = 0; r.ok && i < spriteCount; ++i) {
        Sprite sprite;
        sprite.id = r.str();
        sprite.name = r.str();
        sprite.frameW = r.i32();
        sprite.frameH = r.i32();
        sprite.frames = r.i32();
        sprite.directions = r.i32();
        sprite.fps = r.f32();
        sprite.imgW = r.i32();
        sprite.imgH = r.i32();
        sprite.rgba = r.bytes(r.u32());
        loaded.sprites.push_back(std::move(sprite));
    }

    const uint32_t tilesetCount = r.u32();
    for (uint32_t i = 0; r.ok && i < tilesetCount; ++i) {
        Tileset tileset;
        tileset.id = r.str();
        tileset.name = r.str();
        tileset.tileW = r.i32();
        tileset.tileH = r.i32();
        tileset.columns = r.i32();
        tileset.imgW = r.i32();
        tileset.imgH = r.i32();
        tileset.rgba = r.bytes(r.u32());
        const uint32_t tileCount = r.u32();
        for (uint32_t t = 0; r.ok && t < tileCount; ++t) {
            TileDef tile;
            tile.flags = r.u32();
            tile.damage = r.i32();
            tile.frames = r.i32();
            tile.fps = r.f32();
            tileset.tiles.push_back(tile);
        }
        loaded.tilesets.push_back(std::move(tileset));
    }

    const uint32_t actorCount = r.u32();
    for (uint32_t i = 0; r.ok && i < actorCount; ++i) {
        ActorDef actor;
        actor.id = r.str();
        actor.name = r.str();
        actor.spriteId = r.str();
        actor.projectileId = r.str();
        actor.dropActorId = r.str();
        actor.kind = static_cast<ActorKind>(r.u32());
        actor.pickup = static_cast<Pickup>(r.u32());
        actor.hitW = r.f32();
        actor.hitH = r.f32();
        actor.dropChance = r.f32();
        actor.pickupAmount = r.i32();
        actor.needsKey = r.boolean();
        actor.checkpoint = r.boolean();
        actor.behavior = readBehavior(r, version);
        if (version >= 2) readActorScript(r, actor);
        loaded.actors.push_back(std::move(actor));
    }

    const uint32_t soundCount = r.u32();
    for (uint32_t i = 0; r.ok && i < soundCount; ++i) {
        Sound sound;
        sound.id = r.str();
        sound.name = r.str();
        sound.sampleRate = r.i32();
        sound.channels = r.i32();
        sound.music = r.boolean();
        sound.samples = r.i16s();
        loaded.sounds.push_back(std::move(sound));
    }

    const uint32_t stageCount = r.u32();
    for (uint32_t i = 0; r.ok && i < stageCount; ++i) {
        Stage stage;
        stage.id = r.str();
        stage.name = r.str();
        stage.tilesetId = r.str();
        stage.musicId = r.str();
        stage.message = r.str();
        stage.width = r.i32();
        stage.height = r.i32();
        stage.physics = static_cast<PhysicsMode>(r.u32());
        stage.scroll = static_cast<ScrollMode>(r.u32());
        stage.complete = static_cast<CompleteMode>(r.u32());
        stage.autoScroll = r.f32();
        stage.gravity = r.f32();
        stage.jumpVel = r.f32();
        stage.playerSpeed = r.f32();
        stage.intermission = r.boolean();
        auto readLayer = [&](std::vector<uint16_t>& layer) {
            const uint32_t n = r.u32();
            layer.resize(n);
            for (uint32_t c = 0; c < n; ++c) layer[c] = r.u16();
        };
        readLayer(stage.main);
        readLayer(stage.back);
        readLayer(stage.front);
        const uint32_t placeCount = r.u32();
        for (uint32_t p = 0; r.ok && p < placeCount; ++p) {
            Placement placement;
            placement.uid = r.str();
            placement.actorId = r.str();
            placement.x = r.f32();
            placement.y = r.f32();
            stage.placements.push_back(std::move(placement));
        }
        loaded.stages.push_back(std::move(stage));
    }

    if (!r.ok) {
        err = "game.dat ended early or is corrupt.";
        return false;
    }
    if (loaded.settings.viewW < 160 || loaded.settings.viewH < 120 || loaded.settings.viewW > 640 ||
        loaded.settings.viewH > 480) {
        err = "View size is out of range.";
        return false;
    }
    project = std::move(loaded);
    return true;
}

const char* physicsName(PhysicsMode mode) {
    switch (mode) {
        case PhysicsMode::Platform: return "Platform";
        case PhysicsMode::RunAndGun: return "Run and gun";
        case PhysicsMode::ShooterVert: return "Vertical shooter";
        case PhysicsMode::ShooterHorz: return "Horizontal shooter";
        case PhysicsMode::Maze: return "Maze";
        case PhysicsMode::TopDown: return "Top-down";
    }
    return "Platform";
}

const char* scrollName(ScrollMode mode) {
    switch (mode) {
        case ScrollMode::Follow: return "Follow";
        case ScrollMode::Locked: return "Locked";
        case ScrollMode::AutoUp: return "Auto up";
        case ScrollMode::AutoDown: return "Auto down";
        case ScrollMode::AutoLeft: return "Auto left";
        case ScrollMode::AutoRight: return "Auto right";
    }
    return "Follow";
}

const char* completeName(CompleteMode mode) {
    switch (mode) {
        case CompleteMode::TouchExit: return "Touch exit";
        case CompleteMode::ReachEnd: return "Reach end";
        case CompleteMode::CollectAll: return "Collect all";
        case CompleteMode::BossDead: return "Defeat boss";
    }
    return "Touch exit";
}

const char* kindName(ActorKind kind) {
    switch (kind) {
        case ActorKind::Hero: return "Hero";
        case ActorKind::Enemy: return "Enemy";
        case ActorKind::Gun: return "Gun";
        case ActorKind::Item: return "Item";
        case ActorKind::Check: return "Check";
        case ActorKind::Boss: return "Boss";
        case ActorKind::Decoration: return "Decoration";
    }
    return "Enemy";
}

const char* pickupName(Pickup pickup) {
    switch (pickup) {
        case Pickup::None: return "None";
        case Pickup::Coin: return "Coin";
        case Pickup::Heal: return "Heal";
        case Pickup::Life: return "Extra life";
        case Pickup::Weapon: return "Weapon";
        case Pickup::Invuln: return "Invincibility";
        case Pickup::Key: return "Key";
        case Pickup::PowerPellet: return "Power pellet";
        case Pickup::Special: return "Special";
    }
    return "None";
}

const char* patternName(AiPattern pattern) {
    switch (pattern) {
        case AiPattern::None: return "None";
        case AiPattern::Idle: return "Idle";
        case AiPattern::Patrol: return "Patrol";
        case AiPattern::Chase: return "Chase";
        case AiPattern::Shooter: return "Shooter";
        case AiPattern::Sine: return "Sine";
        case AiPattern::Bounce: return "Bounce";
        case AiPattern::Hopper: return "Hopper";
        case AiPattern::Dive: return "Dive";
        case AiPattern::MazeGhost: return "Maze ghost";
        case AiPattern::Boss: return "Boss";
        case AiPattern::Turret: return "Turret";
        case AiPattern::Fly: return "Fly";
        case AiPattern::Platform: return "Platform";
    }
    return "Idle";
}

const char* aimName(AimMode aim) {
    switch (aim) {
        case AimMode::Facing: return "Facing";
        case AimMode::AtPlayer: return "At player";
        case AimMode::Fixed: return "Fixed angle";
        case AimMode::Up: return "Up";
        case AimMode::Down: return "Down";
    }
    return "Facing";
}

}  // namespace cc
