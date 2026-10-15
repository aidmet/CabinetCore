#pragma once

#include "common/Canvas.hpp"
#include "common/Types.hpp"

#include <string>
#include <vector>

namespace cc {

enum class Phase { Title, Playing, Paused, Intermission, GameOver, Victory, NameEntry };

class Engine {
public:
    void start(const Project& project, int stageIndex = 0, const std::string& scorePath = {});
    void update(float dt, const Input& input);
    void render();

    const std::vector<uint8_t>& pixels() const { return canvas_.pixels(); }
    int width() const { return canvas_.width(); }
    int height() const { return canvas_.height(); }
    std::vector<std::string> takeSounds();
    bool takeMusic(std::string& id);

    Phase phase() const { return phase_; }
    int score() const { return score_; }
    int lives() const { return lives_; }
    int hp() const { return hp_; }
    int stageIndex() const { return stageIndex_; }
    float playerX() const { return px_; }
    float playerY() const { return py_; }
    bool quitRequested() const { return quit_; }
    bool debugBlocked(float x, float y) const;
    float debugBulletX() const;
    float debugProp(const std::string& name) const;
    const std::string& musicId() const { return musicId_; }

private:
    struct ScriptRt {
        float wait = 0.f;
        int pc = 0;
        bool done = false;
    };

    struct Ent {
        const ActorDef* def = nullptr;
        float x = 0.f;
        float y = 0.f;
        float vx = 0.f;
        float vy = 0.f;
        float originX = 0.f;
        float originY = 0.f;
        float anim = 0.f;
        float timer = 0.f;
        float fireCd = 0.f;
        float life = 0.f;
        float respawn = 0.f;
        float phase = 0.f;
        int hp = 1;
        int facing = 1;
        int dirX = 0;
        int dirY = 0;
        int homeTx = 0;
        int homeTy = 0;
        int ghostIndex = 0;
        bool alive = true;
        bool awake = false;
        bool fromPlayer = false;
        bool projectile = false;
        bool waiting = false;
        bool used = false;
        bool onGround = false;
        bool blockedX = false;
        bool touchedHero = false;
        std::vector<float> props;
        std::vector<ScriptRt> scriptRt;
    };

    struct Particle {
        float x = 0, y = 0, vx = 0, vy = 0, life = 0, maxLife = 1;
        uint8_t r = 255, g = 255, b = 255;
    };

    struct ScoreRow {
        char name[4] = {'-', '-', '-', 0};
        int score = 0;
    };

    struct Box {
        float x, y, w, h;
    };

    const Stage* stage() const;
    const Tileset* tileset() const;
    void loadStage();
    void beginGame();
    void goNext();
    void completeStage();
    void endGame(bool victory);
    void updatePlaying(float dt, const Input& input);
    void updatePlatform(float dt, const Input& input);
    void updateShooter(float dt, const Input& input);
    void updateTopDown(float dt, const Input& input);
    void updateMaze(float dt, const Input& input);
    void updateEnt(size_t index, float dt);
    void updateProjectile(Ent& ent, float dt);
    void moveBody(Ent& ent, float dt, bool gravity, bool passLadders = false);
    void advanceGrid(float& x, float& y, int& dirX, int& dirY, int wantX, int wantY, float dt, float speed, Ent* ghost);
    void chooseGhostDir(Ent& ghost, int cx, int cy);
    void tryPlayerFire(const Input& input);
    void spawnShot(const ActorDef& gun, float x, float y, float vx, float vy, bool fromPlayer);
    void hurtPlayer(int amount);
    void loseLife();
    void killEnt(Ent& ent);
    void burst(float x, float y, int count, uint8_t r, uint8_t g, uint8_t b);
    void sfx(const std::string& id);
    void updateCamera(float dt);
    void clampShooter();
    Box playerBox() const;
    Box entBox(const Ent& ent) const;
    bool overlap(Box a, Box b) const;
    bool solidTile(int tx, int ty) const;
    bool boxHits(float x, float y, float w, float h, bool oneWay, float prevBottom, bool passLadders = false) const;
    uint32_t flagsAt(int tx, int ty) const;
    bool ladderAt(int tx, int ty) const;
    bool overlapsLadder(Box box) const;
    bool overlapsFlag(Box box, uint32_t flag) const;
    void grabLadder();
    bool stepOffLadder(const Input& input);
    bool groundAhead(const Ent& ent) const;
    void runHeroScripts(float dt, uint32_t mask);
    void runActorScripts(Ent& ent, float dt, uint32_t mask, bool tickWaits);
    bool runBody(Ent* ent, ScriptRt* rt, const std::vector<Block>& body, int& pc);
    bool blockTest(const Ent* ent, const Block& block) const;
    void applyStatement(Ent* ent, const Block& block);
    bool readProp(const Ent* ent, const std::string& name, float& out) const;
    void writeProp(Ent* ent, const std::string& name, float value);
    void drawTile(uint16_t id, int dx, int dy);
    void drawSprite(const Sprite* sprite, int frame, int dx, int dy, bool flip, uint8_t tr = 255, uint8_t tg = 255,
                    uint8_t tb = 255);
    void drawEnt(const Ent& ent, int ox, int oy);
    void drawPlayer(int ox, int oy);
    void drawHud();
    void drawOverlay();
    void center(const std::string& text, int y, uint8_t r, uint8_t g, uint8_t b, int scale);
    void loadScores();
    void saveScores();
    int spriteW(const ActorDef* def) const;
    int spriteH(const ActorDef* def) const;

    const Project* proj_ = nullptr;
    Canvas canvas_;
    std::vector<uint16_t> tiles_;
    std::vector<Ent> ents_;
    std::vector<Particle> parts_;
    std::vector<std::string> sounds_;
    std::string scorePath_;
    std::string musicId_;
    std::string weaponId_;
    ScoreRow board_[5]{};
    Phase phase_ = Phase::Title;
    int stageIndex_ = 0;
    int score_ = 0;
    int lives_ = 3;
    int hp_ = 3;
    int tileW_ = 16;
    int tileH_ = 16;
    int psW_ = 16;
    int psH_ = 16;
    int hitW_ = 10;
    int hitH_ = 14;
    int facing_ = 1;
    int collectTotal_ = 0;
    int bossTotal_ = 0;
    int namePos_ = 0;
    int ghostCount_ = 0;
    float px_ = 0, py_ = 0, vx_ = 0, vy_ = 0;
    float spawnX_ = 0, spawnY_ = 0;
    float camX_ = 0, camY_ = 0;
    float time_ = 0, invuln_ = 0, shake_ = 0, flash_ = 0, pending_ = 0, endHold_ = 0;
    float fireCd_ = 0, playerAnim_ = 0, frightened_ = 0, ghostClock_ = 0, nameCool_ = 0;
    bool quit_ = false;
    bool hasKey_ = false;
    bool onGround_ = false;
    bool climbing_ = false;
    bool ladderCool_ = false;
    bool wasJump_ = false;
    bool musicDirty_ = false;
    bool victory_ = false;
    bool camReady_ = false;
    bool ghostScatter_ = false;
    bool reverseGhosts_ = false;
    bool blockedX_ = false;
    bool lockX_ = false;
    bool lockY_ = false;
    bool hasSpeed_ = false;
    bool hasJump_ = false;
    bool heroTouching_ = false;
    int scriptSteps_ = 0;
    int hpMark_ = 0;
    float speedVar_ = 0.f;
    float jumpVar_ = 0.f;
    Input frameInput_;
    std::vector<float> heroProps_;
    std::vector<ScriptRt> heroRt_;
    char name_[4] = {'A', 'A', 'A', 0};
    const ActorDef* hero_ = nullptr;
};

}  // namespace cc
