#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cc {

enum class PhysicsMode : uint32_t {
    Platform = 0,
    RunAndGun = 1,
    ShooterVert = 2,
    ShooterHorz = 3,
    Maze = 4,
    TopDown = 5
};

enum class ScrollMode : uint32_t {
    Follow = 0,
    Locked = 1,
    AutoUp = 2,
    AutoDown = 3,
    AutoLeft = 4,
    AutoRight = 5
};

enum class CompleteMode : uint32_t {
    TouchExit = 0,
    ReachEnd = 1,
    CollectAll = 2,
    BossDead = 3
};

enum class ActorKind : uint32_t {
    Hero = 0,
    Enemy = 1,
    Gun = 2,
    Item = 3,
    Check = 4,
    Boss = 5,
    Decoration = 6
};

enum class Pickup : uint32_t {
    None = 0,
    Coin = 1,
    Heal = 2,
    Life = 3,
    Weapon = 4,
    Invuln = 5,
    Key = 6,
    PowerPellet = 7,
    Special = 8
};

enum class AiPattern : uint32_t {
    None = 0,
    Idle = 1,
    Patrol = 2,
    Chase = 3,
    Shooter = 4,
    Sine = 5,
    Bounce = 6,
    Hopper = 7,
    Dive = 8,
    MazeGhost = 9,
    Boss = 10,
    Turret = 11,
    Fly = 12,
    Platform = 13
};

enum class AimMode : uint32_t {
    Facing = 0,
    AtPlayer = 1,
    Fixed = 2,
    Up = 3,
    Down = 4
};

enum class BlockKind : uint32_t {
    WhenSpawned = 1,
    EveryFrame = 2,
    WhenButton1 = 3,
    WhenButton2 = 4,
    WhenTouchHero = 5,
    WhenHurt = 6,
    Move = 10,
    Turn = 11,
    Jump = 12,
    SetVelocity = 13,
    FacePlayer = 14,
    Bounce = 15,
    Wait = 20,
    If = 21,
    Repeat = 22,
    SetProp = 30,
    ChangeProp = 31,
    Shoot = 40,
    Sound = 41,
    Vanish = 42,
    AddScore = 43,
    HurtHero = 44,
    GoToStage = 45,
    SpawnActor = 46,
    ShowFrame = 47,
    SendMessage = 48,
    WhenMessage = 49,
    Plugin = 50
};

enum class BlockTest : uint32_t {
    Prop = 0,
    Button1 = 1,
    Button2 = 2,
    OnGround = 3,
    TouchHero = 4,
    WallAhead = 5
};

struct Block {
    BlockKind kind = BlockKind::EveryFrame;
    float num = 0.f;
    float num2 = 0.f;
    int pick = 0;
    std::string name;
    std::vector<Block> inner;
};

struct PropDef {
    std::string name;
    float initial = 0.f;
};

struct Script {
    Block hat;
    std::vector<Block> body;
};

enum TileFlags : uint32_t {
    TileSolid = 1u << 0,
    TileOneWay = 1u << 1,
    TileLadder = 1u << 2,
    TileHazard = 1u << 3,
    TileWater = 1u << 4,
    TileIce = 1u << 5,
    TileDestructible = 1u << 6,
    TileCrumble = 1u << 7
};

struct Input {
    float axisX = 0.f;
    float axisY = 0.f;
    bool jump = false;
    bool shoot = false;
    bool start = false;
    bool pause = false;
    bool jumpEdge = false;
    bool shootEdge = false;
    bool startEdge = false;
    bool pauseEdge = false;
    bool quitEdge = false;
    bool restartEdge = false;
    bool coinEdge = false;
};

struct Behavior {
    AiPattern pattern = AiPattern::Idle;
    AimMode aim = AimMode::AtPlayer;
    float speed = 40.f;
    float fireInterval = 1.4f;
    float shotSpeed = 140.f;
    float shotAngle = 0.f;
    float spread = 0.35f;
    float amplitude = 28.f;
    float period = 2.f;
    float lifetime = 2.f;
    int hp = 1;
    int touchDamage = 1;
    int score = 100;
    int burst = 1;
    bool gravity = true;
    bool turnAtEdge = true;
    bool turnAtWall = true;
    bool stomp = false;
};

struct Sprite {
    std::string id;
    std::string name;
    int frameW = 16;
    int frameH = 16;
    int frames = 1;
    int directions = 1;
    float fps = 8.f;
    int imgW = 0;
    int imgH = 0;
    std::vector<uint8_t> rgba;
};

struct TileDef {
    uint32_t flags = 0;
    int damage = 1;
    int frames = 1;
    float fps = 6.f;
};

struct Tileset {
    std::string id;
    std::string name;
    int tileW = 16;
    int tileH = 16;
    int columns = 8;
    int imgW = 0;
    int imgH = 0;
    std::vector<uint8_t> rgba;
    std::vector<TileDef> tiles;
};

struct ActorDef {
    std::string id;
    std::string name;
    std::string spriteId;
    std::string projectileId;
    std::string dropActorId;
    ActorKind kind = ActorKind::Enemy;
    Pickup pickup = Pickup::None;
    Behavior behavior;
    float hitW = 12.f;
    float hitH = 14.f;
    float dropChance = 0.f;
    int pickupAmount = 1;
    bool needsKey = false;
    bool checkpoint = false;
    std::vector<PropDef> props;
    std::vector<Script> scripts;
};

struct Sound {
    std::string id;
    std::string name;
    int sampleRate = 44100;
    int channels = 1;
    bool music = false;
    std::vector<int16_t> samples;
};

struct Placement {
    std::string uid;
    std::string actorId;
    float x = 0.f;
    float y = 0.f;
};

struct Stage {
    std::string id;
    std::string name;
    std::string tilesetId;
    std::string musicId;
    std::string message;
    int width = 20;
    int height = 15;
    PhysicsMode physics = PhysicsMode::Platform;
    ScrollMode scroll = ScrollMode::Follow;
    CompleteMode complete = CompleteMode::TouchExit;
    float autoScroll = 28.f;
    float gravity = 920.f;
    float jumpVel = 290.f;
    float playerSpeed = 92.f;
    bool intermission = false;
    std::vector<uint16_t> main;
    std::vector<uint16_t> back;
    std::vector<uint16_t> front;
    std::vector<Placement> placements;

    void resize(int w, int h);
    uint16_t get(const std::vector<uint16_t>& layer, int x, int y) const;
    void set(std::vector<uint16_t>& layer, int x, int y, uint16_t v);
};

struct Settings {
    std::string title = "Untitled Cabinet";
    std::string author;
    int viewW = 320;
    int viewH = 240;
    int scale = 3;
    int lives = 3;
    int maxHealth = 3;
    bool crt = true;
    bool screenShake = true;
    bool freePlay = true;
    std::string startStage;
};

struct Project {
    Settings settings;
    int nextUid = 1;
    std::vector<Sprite> sprites;
    std::vector<Tileset> tilesets;
    std::vector<ActorDef> actors;
    std::vector<Sound> sounds;
    std::vector<Stage> stages;

    Sprite* findSprite(const std::string& id);
    const Sprite* findSprite(const std::string& id) const;
    Tileset* findTileset(const std::string& id);
    const Tileset* findTileset(const std::string& id) const;
    ActorDef* findActor(const std::string& id);
    const ActorDef* findActor(const std::string& id) const;
    Sound* findSound(const std::string& id);
    const Sound* findSound(const std::string& id) const;
    Stage* findStage(const std::string& id);
    std::string freshUid();
    std::string validate() const;
    bool save(const std::string& path, std::string& err) const;
    static bool load(const std::string& path, Project& out, std::string& err);
};

const char* physicsName(PhysicsMode m);
const char* scrollName(ScrollMode m);
const char* completeName(CompleteMode m);
const char* kindName(ActorKind k);
const char* pickupName(Pickup p);
const char* patternName(AiPattern p);
const char* aimName(AimMode a);

}  // namespace cc
