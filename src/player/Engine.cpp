#include "player/Engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>

namespace cc {
namespace {

constexpr float kPi = 3.14159265f;
constexpr uint32_t kHatSpawn = 1u;
constexpr uint32_t kHatFrame = 2u;
constexpr uint32_t kHatBtn1 = 4u;
constexpr uint32_t kHatBtn2 = 8u;
constexpr uint32_t kHatTouch = 16u;
constexpr uint32_t kHatHurt = 32u;
constexpr uint32_t kHatLive = kHatSpawn | kHatFrame | kHatBtn1 | kHatBtn2;

float clampf(float v, float a, float b) { return std::max(a, std::min(b, v)); }

int tileIndex(float v, int tile) { return static_cast<int>(std::floor(v / static_cast<float>(tile))); }

}  // namespace

void Engine::start(const Project& project, int stageIndex, const std::string& scorePath) {
    proj_ = &project;
    scorePath_ = scorePath;
    canvas_.resize(std::max(160, project.settings.viewW), std::max(120, project.settings.viewH));
    stageIndex_ = std::clamp(stageIndex, 0, std::max(0, static_cast<int>(project.stages.size()) - 1));
    score_ = 0;
    lives_ = std::max(1, project.settings.lives);
    hp_ = std::max(1, project.settings.maxHealth);
    time_ = 0;
    quit_ = false;
    hasKey_ = false;
    frightened_ = 0;
    pending_ = 0;
    phase_ = Phase::Title;
    ents_.clear();
    parts_.clear();
    sounds_.clear();
    tiles_.clear();
    hero_ = nullptr;
    musicId_.clear();
    musicDirty_ = false;
    loadScores();
    if (!project.stages.empty() && stageIndex > 0) {
        phase_ = Phase::Playing;
        loadStage();
    }
}

const Stage* Engine::stage() const {
    if (!proj_ || stageIndex_ < 0 || stageIndex_ >= static_cast<int>(proj_->stages.size())) return nullptr;
    return &proj_->stages[stageIndex_];
}

const Tileset* Engine::tileset() const {
    const Stage* st = stage();
    if (!proj_ || !st) return nullptr;
    return proj_->findTileset(st->tilesetId);
}

void Engine::sfx(const std::string& id) {
    if (!id.empty()) sounds_.push_back(id);
}

std::vector<std::string> Engine::takeSounds() {
    std::vector<std::string> out;
    out.swap(sounds_);
    return out;
}

bool Engine::takeMusic(std::string& id) {
    if (!musicDirty_) return false;
    musicDirty_ = false;
    id = musicId_;
    return true;
}

void Engine::loadScores() {
    for (auto& row : board_) {
        row.name[0] = row.name[1] = row.name[2] = '-';
        row.name[3] = 0;
        row.score = 0;
    }
    if (scorePath_.empty()) return;
    std::ifstream in(scorePath_);
    std::string header;
    if (!std::getline(in, header) || header != "CCSCORE1") return;
    for (auto& row : board_) {
        std::string name;
        int score = 0;
        if (!(in >> name >> score)) break;
        for (int i = 0; i < 3; ++i) row.name[i] = i < static_cast<int>(name.size()) ? name[i] : 'A';
        row.name[3] = 0;
        row.score = score;
    }
}

void Engine::saveScores() {
    if (scorePath_.empty()) return;
    std::ofstream out(scorePath_);
    if (!out) return;
    out << "CCSCORE1\n";
    for (const auto& row : board_) out << row.name << ' ' << row.score << '\n';
}

void Engine::beginGame() {
    if (!proj_ || proj_->stages.empty()) return;
    score_ = 0;
    lives_ = std::max(1, proj_->settings.lives);
    hp_ = std::max(1, proj_->settings.maxHealth);
    hasKey_ = false;
    frightened_ = 0;
    pending_ = 0;
    stageIndex_ = 0;
    if (!proj_->settings.startStage.empty()) {
        for (int i = 0; i < static_cast<int>(proj_->stages.size()); ++i) {
            if (proj_->stages[i].id == proj_->settings.startStage) stageIndex_ = i;
        }
    }
    phase_ = Phase::Playing;
    loadStage();
}

void Engine::loadStage() {
    const Stage* st = stage();
    ents_.clear();
    ents_.reserve(1024);
    parts_.clear();
    tiles_.clear();
    hero_ = nullptr;
    collectTotal_ = 0;
    bossTotal_ = 0;
    ghostCount_ = 0;
    pending_ = 0;
    endHold_ = 0;
    climbing_ = false;
    ladderCool_ = false;
    wasJump_ = false;
    fireCd_ = 0;
    vx_ = vy_ = 0;
    onGround_ = false;
    camReady_ = false;
    frightened_ = 0;
    if (!st || !proj_) return;

    const Tileset* set = tileset();
    tileW_ = set && set->tileW > 0 ? set->tileW : 16;
    tileH_ = set && set->tileH > 0 ? set->tileH : 16;
    tiles_ = st->main;
    if (tiles_.size() != static_cast<size_t>(st->width) * st->height)
        tiles_.assign(static_cast<size_t>(st->width) * st->height, 0);

    for (const auto& placement : st->placements) {
        const ActorDef* def = proj_->findActor(placement.actorId);
        if (!def) continue;
        if (def->kind == ActorKind::Hero) {
            hero_ = def;
            px_ = placement.x;
            py_ = placement.y;
            spawnX_ = px_;
            spawnY_ = py_;
            weaponId_ = def->projectileId;
            heroProps_.clear();
            hasSpeed_ = false;
            hasJump_ = false;
            for (const auto& prop : def->props) {
                heroProps_.push_back(prop.initial);
                if (prop.name == "speed") {
                    hasSpeed_ = true;
                    speedVar_ = prop.initial;
                }
                if (prop.name == "jump") {
                    hasJump_ = true;
                    jumpVar_ = prop.initial;
                }
            }
            heroRt_.assign(def->scripts.size(), {});
            psW_ = spriteW(def);
            psH_ = spriteH(def);
            hitW_ = std::max(4, static_cast<int>(def->hitW));
            hitH_ = std::max(4, static_cast<int>(def->hitH));
            hp_ = std::max(1, proj_->settings.maxHealth);
            hpMark_ = hp_;
            continue;
        }
        if (def->kind == ActorKind::Gun) continue;
        Ent ent;
        ent.def = def;
        ent.x = placement.x;
        ent.y = placement.y;
        ent.originX = placement.x;
        ent.originY = placement.y;
        ent.hp = std::max(1, def->behavior.hp);
        ent.props.resize(def->props.size());
        for (size_t p = 0; p < def->props.size(); ++p) ent.props[p] = def->props[p].initial;
        ent.scriptRt.assign(def->scripts.size(), {});
        ent.facing = 1;
        ent.life = def->behavior.lifetime;
        ent.homeTx = tileIndex(placement.x + 1.f, tileW_);
        ent.homeTy = tileIndex(placement.y + 1.f, tileH_);
        ent.awake = def->kind == ActorKind::Item || def->kind == ActorKind::Check ||
                    def->behavior.pattern == AiPattern::MazeGhost;
        if (def->kind == ActorKind::Boss) bossTotal_++;
        if (def->kind == ActorKind::Item && (def->pickup == Pickup::Coin || def->pickup == Pickup::PowerPellet))
            collectTotal_++;
        if (def->behavior.pattern == AiPattern::MazeGhost) ent.ghostIndex = ghostCount_++;
        if (def->behavior.pattern == AiPattern::Bounce) {
            ent.vx = def->behavior.speed * 0.7f;
            ent.vy = def->behavior.speed * 0.7f;
        }
        ents_.push_back(ent);
    }

    if (st->intermission) phase_ = Phase::Intermission;
    else if (phase_ != Phase::Title) phase_ = Phase::Playing;

    if (musicId_ != st->musicId) {
        musicId_ = st->musicId;
        musicDirty_ = true;
    }
    updateCamera(0);
    camReady_ = true;
}

void Engine::goNext() {
    pending_ = 0;
    if (!proj_) return;
    stageIndex_++;
    if (stageIndex_ >= static_cast<int>(proj_->stages.size())) {
        endGame(true);
        return;
    }
    loadStage();
}

void Engine::completeStage() {
    if (pending_ > 0 || phase_ != Phase::Playing) return;
    pending_ = 0.7f;
    flash_ = 0.4f;
    sfx("goal");
}

void Engine::endGame(bool victory) {
    if (phase_ == Phase::NameEntry || phase_ == Phase::GameOver || phase_ == Phase::Victory) return;
    victory_ = victory;
    if (score_ > 0 && score_ >= board_[4].score) {
        phase_ = Phase::NameEntry;
        name_[0] = name_[1] = name_[2] = 'A';
        name_[3] = 0;
        namePos_ = 0;
        nameCool_ = 0;
    } else {
        phase_ = victory ? Phase::Victory : Phase::GameOver;
    }
}

void Engine::update(float dt, const Input& input) {
    dt = clampf(dt, 0.f, 0.05f);
    time_ += dt;
    shake_ = std::max(0.f, shake_ - dt);
    flash_ = std::max(0.f, flash_ - dt);
    invuln_ = std::max(0.f, invuln_ - dt);
    frightened_ = std::max(0.f, frightened_ - dt);

    switch (phase_) {
        case Phase::Title:
            if (input.startEdge) beginGame();
            if (input.quitEdge) quit_ = true;
            return;
        case Phase::Paused:
            if (input.pauseEdge || input.startEdge) phase_ = Phase::Playing;
            if (input.quitEdge || input.shootEdge) phase_ = Phase::Title;
            if (input.restartEdge || input.jumpEdge) loadStage();
            return;
        case Phase::Intermission:
            if (input.startEdge || input.jumpEdge) goNext();
            return;
        case Phase::GameOver:
        case Phase::Victory:
            if (input.startEdge || input.jumpEdge) phase_ = Phase::Title;
            if (input.quitEdge) quit_ = true;
            return;
        case Phase::NameEntry: {
            nameCool_ -= dt;
            if (nameCool_ <= 0.f) {
                if (input.axisY < -0.5f) {
                    name_[namePos_] = name_[namePos_] <= 'A' ? 'Z' : static_cast<char>(name_[namePos_] - 1);
                    nameCool_ = 0.14f;
                } else if (input.axisY > 0.5f) {
                    name_[namePos_] = name_[namePos_] >= 'Z' ? 'A' : static_cast<char>(name_[namePos_] + 1);
                    nameCool_ = 0.14f;
                } else if (input.axisX > 0.5f && namePos_ < 2) {
                    namePos_++;
                    nameCool_ = 0.16f;
                } else if (input.axisX < -0.5f && namePos_ > 0) {
                    namePos_--;
                    nameCool_ = 0.16f;
                }
            }
            if (input.startEdge || input.jumpEdge || input.shootEdge) {
                ScoreRow row;
                row.name[0] = name_[0];
                row.name[1] = name_[1];
                row.name[2] = name_[2];
                row.name[3] = 0;
                row.score = score_;
                board_[4] = row;
                std::sort(std::begin(board_), std::end(board_),
                          [](const ScoreRow& a, const ScoreRow& b) { return a.score > b.score; });
                saveScores();
                phase_ = victory_ ? Phase::Victory : Phase::GameOver;
            }
            return;
        }
        case Phase::Playing:
            break;
    }

    if (input.pauseEdge) {
        phase_ = Phase::Paused;
        sfx("pickup");
        return;
    }
    if (pending_ > 0.f) {
        pending_ -= dt;
        if (pending_ <= 0.f) goNext();
        return;
    }
    updatePlaying(dt, input);
}

void Engine::updatePlaying(float dt, const Input& input) {
    const Stage* st = stage();
    if (!st) return;
    frameInput_ = input;
    const int hpBefore = hp_;
    playerAnim_ += dt;
    for (auto& ent : ents_) ent.anim += dt;
    updateCamera(dt);

    if (st->physics == PhysicsMode::Maze) updateMaze(dt, input);
    else if (st->physics == PhysicsMode::ShooterVert || st->physics == PhysicsMode::ShooterHorz)
        updateShooter(dt, input);
    else if (st->physics == PhysicsMode::TopDown)
        updateTopDown(dt, input);
    else
        updatePlatform(dt, input);

    fireCd_ = std::max(0.f, fireCd_ - dt);
    if (st->physics != PhysicsMode::Maze) tryPlayerFire(input);

    const size_t count = ents_.size();
    for (size_t i = 0; i < count; ++i) {
        if (!ents_[i].alive || ents_[i].waiting) {
            if (ents_[i].waiting) {
                ents_[i].respawn -= dt;
                if (ents_[i].respawn <= 0.f) {
                    ents_[i].waiting = false;
                    ents_[i].alive = true;
                    ents_[i].hp = std::max(1, ents_[i].def->behavior.hp);
                    ents_[i].x = ents_[i].homeTx * tileW_;
                    ents_[i].y = ents_[i].homeTy * tileH_;
                    ents_[i].dirX = 0;
                    ents_[i].dirY = 0;
                }
            }
            continue;
        }
        if (ents_[i].projectile) updateProjectile(ents_[i], dt);
        else updateEnt(i, dt);
    }

    Box player = playerBox();
    if (py_ > st->height * tileH_ + 24.f) loseLife();

    heroTouching_ = false;
    for (auto& ent : ents_) ent.touchedHero = false;

    for (size_t ei = 0; ei < ents_.size(); ++ei) {
        Ent& ent = ents_[ei];
        if (!ent.alive || ent.projectile || !ent.def) continue;
        if (ent.def->kind == ActorKind::Item || ent.def->kind == ActorKind::Check) {
            if (!overlap(player, entBox(ent))) continue;
            if (ent.def->checkpoint || (ent.def->kind == ActorKind::Check && ent.def->checkpoint)) {
                spawnX_ = ent.x;
                spawnY_ = ent.y;
                if (!ent.used) {
                    ent.used = true;
                    sfx("pickup");
                }
                continue;
            }
            if (ent.def->kind == ActorKind::Check) {
                if (ent.def->needsKey && !hasKey_) continue;
                if (!ent.used) {
                    ent.used = true;
                    completeStage();
                }
                continue;
            }
            switch (ent.def->pickup) {
                case Pickup::Coin:
                    score_ += std::max(1, ent.def->pickupAmount);
                    sfx("coin");
                    break;
                case Pickup::Heal:
                    hp_ = std::min(proj_->settings.maxHealth, hp_ + std::max(1, ent.def->pickupAmount));
                    sfx("pickup");
                    break;
                case Pickup::Life:
                    lives_++;
                    sfx("pickup");
                    break;
                case Pickup::Weapon:
                    if (!ent.def->projectileId.empty()) weaponId_ = ent.def->projectileId;
                    sfx("pickup");
                    break;
                case Pickup::Invuln:
                    invuln_ = std::max(invuln_, static_cast<float>(std::max(1, ent.def->pickupAmount)));
                    sfx("pickup");
                    break;
                case Pickup::Key:
                    hasKey_ = true;
                    sfx("pickup");
                    break;
                case Pickup::PowerPellet:
                    frightened_ = 6.f;
                    reverseGhosts_ = true;
                    score_ += std::max(1, ent.def->pickupAmount);
                    sfx("coin");
                    break;
                case Pickup::None:
                    break;
            }
            burst(ent.x, ent.y, 6, 255, 230, 120);
            ent.alive = false;
            continue;
        }
        if ((ent.def->kind == ActorKind::Enemy || ent.def->kind == ActorKind::Boss) && ent.def->behavior.touchDamage > 0 &&
            overlap(player, entBox(ent))) {
            ent.touchedHero = true;
            heroTouching_ = true;
            if (ent.def->behavior.pattern == AiPattern::MazeGhost && frightened_ > 0.f) {
                score_ += 200;
                ent.alive = false;
                ent.waiting = true;
                ent.respawn = 4.f;
                sfx("explode");
                burst(ent.x, ent.y, 8, 80, 160, 255);
            } else {
                hurtPlayer(ent.def->behavior.touchDamage);
            }
        }
    }

    if (phase_ == Phase::Playing && overlapsFlag(playerBox(), TileHazard)) hurtPlayer(1);

    if (phase_ == Phase::Playing) {
        if (hp_ < hpBefore) runHeroScripts(dt, kHatHurt);
        if (heroTouching_) runHeroScripts(dt, kHatTouch);
        hpMark_ = hp_;
        for (auto& ent : ents_) {
            if (!ent.alive || ent.projectile || !ent.def || ent.def->scripts.empty()) continue;
            if (ent.touchedHero) runActorScripts(ent, dt, kHatTouch, false);
        }
    }

    int coinsLeft = 0;
    int bossesLeft = 0;
    for (const auto& ent : ents_) {
        if (!ent.alive || !ent.def) continue;
        if (ent.def->kind == ActorKind::Item &&
            (ent.def->pickup == Pickup::Coin || ent.def->pickup == Pickup::PowerPellet))
            coinsLeft++;
        if (ent.def->kind == ActorKind::Boss) bossesLeft++;
    }
    if (st->complete == CompleteMode::CollectAll && collectTotal_ > 0 && coinsLeft == 0) completeStage();
    if (st->complete == CompleteMode::BossDead && bossTotal_ > 0 && bossesLeft == 0) completeStage();
    if (st->complete == CompleteMode::BossDead && bossTotal_ == 0) {
        endHold_ += dt;
        if (endHold_ > 1.2f) completeStage();
    }
    if (st->complete == CompleteMode::ReachEnd) {
        const float mapW = st->width * tileW_;
        const float mapH = st->height * tileH_;
        const float maxX = std::max(0.f, mapW - canvas_.width());
        const float maxY = std::max(0.f, mapH - canvas_.height());
        bool reached = false;
        switch (st->scroll) {
            case ScrollMode::AutoUp: reached = camY_ <= 0.5f; break;
            case ScrollMode::AutoDown: reached = camY_ >= maxY - 0.5f; break;
            case ScrollMode::AutoRight: reached = camX_ >= maxX - 0.5f; break;
            case ScrollMode::AutoLeft: reached = camX_ <= 0.5f; break;
            default: reached = px_ > mapW - tileW_ * 2; break;
        }
        if (reached) endHold_ += dt;
        else endHold_ = 0;
        if (endHold_ > 0.7f) completeStage();
    }

    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& p) { return p.life <= 0.f; }),
                 parts_.end());
    for (auto& part : parts_) {
        part.life -= dt;
        part.vy += 280.f * dt;
        part.x += part.vx * dt;
        part.y += part.vy * dt;
    }
    ents_.erase(std::remove_if(ents_.begin(), ents_.end(),
                               [](const Ent& e) { return !e.alive && !e.waiting; }),
                ents_.end());

    if (bossTotal_ > 0 && bossesLeft > 0 && musicId_ != "music_boss") {
        for (const auto& ent : ents_) {
            if (ent.alive && ent.def && ent.def->kind == ActorKind::Boss && ent.awake) {
                musicId_ = "music_boss";
                musicDirty_ = true;
            }
        }
    }
}

void Engine::updateCamera(float dt) {
    const Stage* st = stage();
    if (!st) return;
    const float mapW = st->width * static_cast<float>(tileW_);
    const float mapH = st->height * static_cast<float>(tileH_);
    const float maxX = std::max(0.f, mapW - canvas_.width());
    const float maxY = std::max(0.f, mapH - canvas_.height());
    if (!camReady_) {
        camX_ = 0;
        camY_ = 0;
        if (st->scroll == ScrollMode::AutoUp) camY_ = maxY;
        if (st->scroll == ScrollMode::AutoLeft) camX_ = maxX;
        if (st->scroll == ScrollMode::Follow) {
            camX_ = clampf(px_ + psW_ * 0.5f - canvas_.width() * 0.5f, 0.f, maxX);
            camY_ = clampf(py_ + psH_ * 0.5f - canvas_.height() * 0.5f, 0.f, maxY);
        }
        camReady_ = true;
    }
    switch (st->scroll) {
        case ScrollMode::Locked:
            break;
        case ScrollMode::Follow: {
            const float tx = px_ + psW_ * 0.5f - canvas_.width() * 0.5f;
            const float ty = py_ + psH_ * 0.5f - canvas_.height() * 0.5f;
            const float blend = dt <= 0.f ? 1.f : std::min(1.f, dt * 8.f);
            camX_ += (tx - camX_) * blend;
            camY_ += (ty - camY_) * blend;
            break;
        }
        case ScrollMode::AutoUp: camY_ -= st->autoScroll * dt; break;
        case ScrollMode::AutoDown: camY_ += st->autoScroll * dt; break;
        case ScrollMode::AutoLeft: camX_ -= st->autoScroll * dt; break;
        case ScrollMode::AutoRight: camX_ += st->autoScroll * dt; break;
    }
    camX_ = clampf(camX_, 0.f, maxX);
    camY_ = clampf(camY_, 0.f, maxY);
}

void Engine::clampShooter() {
    const float left = camX_ + 4.f;
    const float top = camY_ + 12.f;
    const float right = camX_ + canvas_.width() - psW_ - 4.f;
    const float bottom = camY_ + canvas_.height() - psH_ - 4.f;
    px_ = clampf(px_, left, std::max(left, right));
    py_ = clampf(py_, top, std::max(top, bottom));
}

bool Engine::solidTile(int tx, int ty) const {
    const Stage* st = stage();
    if (!st) return false;
    if (tx < 0 || ty < 0 || tx >= st->width || ty >= st->height) {
        if (st->physics == PhysicsMode::Maze) return true;
        return tx < 0 || tx >= st->width;
    }
    const uint32_t flags = flagsAt(tx, ty);
    return (flags & TileSolid) != 0;
}

uint32_t Engine::flagsAt(int tx, int ty) const {
    const Stage* st = stage();
    const Tileset* set = tileset();
    if (!st || tx < 0 || ty < 0 || tx >= st->width || ty >= st->height) return 0;
    const size_t i = static_cast<size_t>(ty) * st->width + tx;
    if (i >= tiles_.size()) return 0;
    const int id = tiles_[i];
    if (id <= 0 || !set || id - 1 >= static_cast<int>(set->tiles.size())) return 0;
    return set->tiles[id - 1].flags;
}

bool Engine::boxHits(float x, float y, float w, float h, bool oneWay, float prevBottom, bool passLadders) const {
    if (w <= 0 || h <= 0) return false;
    const int x0 = tileIndex(x, tileW_);
    const int y0 = tileIndex(y, tileH_);
    const int x1 = tileIndex(x + w - 0.01f, tileW_);
    const int y1 = tileIndex(y + h - 0.01f, tileH_);
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            if (passLadders && (ladderAt(tx, ty) || (flagsAt(tx, ty) & TileOneWay))) continue;
            if (solidTile(tx, ty)) return true;
            if (oneWay && (flagsAt(tx, ty) & TileOneWay)) {
                const float top = ty * static_cast<float>(tileH_);
                if (prevBottom <= top + 1.5f) return true;
            }
        }
    }
    return false;
}

bool Engine::ladderAt(int tx, int ty) const {
    const Stage* st = stage();
    const Tileset* set = tileset();
    if (!st || !set || tx < 0 || ty < 0 || tx >= st->width || ty >= st->height) return false;
    auto flagged = [&](const std::vector<uint16_t>& layer) {
        const size_t i = static_cast<size_t>(ty) * st->width + tx;
        if (i >= layer.size()) return false;
        const int id = layer[i];
        if (id <= 0 || id - 1 >= static_cast<int>(set->tiles.size())) return false;
        return (set->tiles[id - 1].flags & TileLadder) != 0;
    };
    return flagged(tiles_) || flagged(st->front);
}

bool Engine::overlapsLadder(Box box) const {
    if (box.w <= 0 || box.h <= 0) return false;
    const int x0 = tileIndex(box.x, tileW_);
    const int y0 = tileIndex(box.y, tileH_);
    const int x1 = tileIndex(box.x + box.w - 0.01f, tileW_);
    const int y1 = tileIndex(box.y + box.h - 0.01f, tileH_);
    for (int ty = y0; ty <= y1; ++ty)
        for (int tx = x0; tx <= x1; ++tx)
            if (ladderAt(tx, ty)) return true;
    return false;
}

bool Engine::overlapsFlag(Box box, uint32_t flag) const {
    const int x0 = tileIndex(box.x, tileW_);
    const int y0 = tileIndex(box.y, tileH_);
    const int x1 = tileIndex(box.x + box.w - 0.01f, tileW_);
    const int y1 = tileIndex(box.y + box.h - 0.01f, tileH_);
    for (int ty = y0; ty <= y1; ++ty)
        for (int tx = x0; tx <= x1; ++tx)
            if (flagsAt(tx, ty) & flag) return true;
    return false;
}

Engine::Box Engine::playerBox() const {
    const Stage* st = stage();
    if (st && st->physics == PhysicsMode::Maze)
        return {px_ + 3.f, py_ + 3.f, static_cast<float>(tileW_) - 6.f, static_cast<float>(tileH_) - 6.f};
    const float ox = (psW_ - hitW_) * 0.5f;
    const float oy = static_cast<float>(psH_ - hitH_);
    return {px_ + ox, py_ + oy, static_cast<float>(hitW_), static_cast<float>(hitH_)};
}

Engine::Box Engine::entBox(const Ent& ent) const {
    const int sw = spriteW(ent.def);
    const int sh = spriteH(ent.def);
    const float hw = ent.def ? ent.def->hitW : 12.f;
    const float hh = ent.def ? ent.def->hitH : 12.f;
    if (ent.projectile) return {ent.x + (sw - hw) * 0.5f, ent.y + (sh - hh) * 0.5f, hw, hh};
    return {ent.x + (sw - hw) * 0.5f, ent.y + (sh - hh), hw, hh};
}

bool Engine::overlap(Box a, Box b) const {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

int Engine::spriteW(const ActorDef* def) const {
    if (!proj_ || !def) return 16;
    const Sprite* sprite = proj_->findSprite(def->spriteId);
    return sprite && sprite->frameW > 0 ? sprite->frameW : 16;
}

int Engine::spriteH(const ActorDef* def) const {
    if (!proj_ || !def) return 16;
    const Sprite* sprite = proj_->findSprite(def->spriteId);
    return sprite && sprite->frameH > 0 ? sprite->frameH : 16;
}

void Engine::moveBody(Ent& ent, float dt, bool gravity, bool passLadders) {
    const Stage* st = stage();
    if (!st) return;
    if (gravity) ent.vy = std::min(560.f, ent.vy + st->gravity * dt);
    const float mag = std::max(std::abs(ent.vx), std::abs(ent.vy)) * dt;
    const int steps = std::max(1, static_cast<int>(std::ceil(mag / 6.f)));
    const float sdt = dt / static_cast<float>(steps);
    ent.blockedX = false;
    ent.onGround = false;
    for (int step = 0; step < steps; ++step) {
        Box box = entBox(ent);
        const float prevBottom = box.y + box.h;
        float nx = box.x + ent.vx * sdt;
        if (boxHits(nx, box.y, box.w, box.h, false, prevBottom, passLadders)) {
            ent.blockedX = true;
            if (ent.vx > 0) {
                const int tx = tileIndex(nx + box.w - 0.01f, tileW_);
                nx = tx * static_cast<float>(tileW_) - box.w - 0.02f;
            } else if (ent.vx < 0) {
                const int tx = tileIndex(nx, tileW_);
                nx = (tx + 1) * static_cast<float>(tileW_) + 0.02f;
            }
            ent.vx = 0;
        }
        const float spriteOffX = box.x - ent.x;
        ent.x = nx - spriteOffX;
        box = entBox(ent);
        float ny = box.y + ent.vy * sdt;
        if (boxHits(box.x, ny, box.w, box.h, ent.vy > 0.f && !passLadders, prevBottom, passLadders)) {
            if (ent.vy > 0) {
                const int ty = tileIndex(ny + box.h - 0.01f, tileH_);
                ny = ty * static_cast<float>(tileH_) - box.h - 0.02f;
                ent.onGround = true;
            } else if (ent.vy < 0) {
                const int ty = tileIndex(ny, tileH_);
                ny = (ty + 1) * static_cast<float>(tileH_) + 0.02f;
            }
            ent.vy = 0;
        }
        const float spriteOffY = box.y - ent.y;
        ent.y = ny - spriteOffY;
    }
    Box landed = entBox(ent);
    if (ent.vy >= 0.f && !passLadders &&
        boxHits(landed.x, landed.y + 1.f, landed.w, landed.h, true, landed.y + landed.h))
        ent.onGround = true;
}

void Engine::grabLadder() {
    Box box = playerBox();
    const float cx = box.x + box.w * 0.5f;
    const float cy = box.y + box.h * 0.5f;
    const int x0 = tileIndex(box.x - 8.f, tileW_);
    const int y0 = tileIndex(box.y - static_cast<float>(tileH_), tileH_);
    const int x1 = tileIndex(box.x + box.w + 8.f, tileW_);
    const int y1 = tileIndex(box.y + box.h + 8.f, tileH_);
    float best = 1.0e9f;
    int bestTx = -1;
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            if (!ladderAt(tx, ty)) continue;
            const float mx = (tx + 0.5f) * tileW_;
            const float my = (ty + 0.5f) * tileH_;
            const float d = (mx - cx) * (mx - cx) + (my - cy) * (my - cy);
            if (d < best) {
                best = d;
                bestTx = tx;
            }
        }
    }
    if (bestTx < 0) return;
    const float oldX = px_;
    const float target = (bestTx + 0.5f) * tileW_ - box.w * 0.5f;
    px_ += target - box.x;
    Box moved = playerBox();
    if (boxHits(moved.x, moved.y, moved.w, moved.h, false, moved.y + moved.h, true)) px_ = oldX;
}

bool Engine::stepOffLadder(const Input& input) {
    if (std::abs(input.axisX) < 0.45f) return false;
    const float oldX = px_;
    const float oldY = py_;
    Box box = playerBox();
    const float feet = box.y + box.h;
    const int dir = input.axisX > 0.f ? 1 : -1;
    const int tx = tileIndex(box.x + box.w * 0.5f, tileW_) + dir;
    const int footTy = tileIndex(feet - 0.01f, tileH_);
    for (int ty = footTy - 1; ty <= footTy + 1; ++ty) {
        const uint32_t flags = flagsAt(tx, ty);
        if (flags & TileLadder) continue;
        if (flags & TileHazard) continue;
        if ((flags & (TileSolid | TileOneWay)) == 0) continue;
        if (solidTile(tx, ty - 1)) continue;
        const float top = static_cast<float>(ty * tileH_);
        if (std::abs(top - feet) <= 4.f) continue;
        if (feet < top - static_cast<float>(tileH_) || feet > top + static_cast<float>(tileH_)) continue;
        const float newFeet = top - 0.02f;
        py_ = oldY + (newFeet - feet);
        const float mid = box.x + box.w * 0.5f;
        px_ = oldX + ((tx + 0.5f) * tileW_ - mid);
        Box stood = playerBox();
        if (boxHits(stood.x, stood.y, stood.w, stood.h, false, stood.y + stood.h, true)) {
            px_ = oldX;
            py_ = oldY;
            continue;
        }
        climbing_ = false;
        vx_ = input.axisX * 40.f;
        vy_ = 0.f;
        onGround_ = true;
        return true;
    }
    return false;
}

bool Engine::groundAhead(const Ent& ent) const {
    Box box = entBox(ent);
    const float x = ent.facing > 0 ? box.x + box.w + 1.f : box.x - 1.f;
    const float y = box.y + box.h + 2.f;
    return solidTile(tileIndex(x, tileW_), tileIndex(y, tileH_)) ||
           (flagsAt(tileIndex(x, tileW_), tileIndex(y, tileH_)) & TileOneWay);
}

bool Engine::readProp(const Ent* ent, const std::string& name, float& out) const {
    const ActorDef* def = ent ? ent->def : hero_;
    const std::vector<float>* values = ent ? &ent->props : &heroProps_;
    if (!def || !values) return false;
    if (name == "health") {
        out = static_cast<float>(ent ? ent->hp : hp_);
        return true;
    }
    if (name == "lives") {
        out = static_cast<float>(lives_);
        return true;
    }
    if (name == "score") {
        out = static_cast<float>(score_);
        return true;
    }
    if (!ent && name == "speed" && hasSpeed_) {
        out = speedVar_;
        return true;
    }
    if (!ent && name == "jump" && hasJump_) {
        out = jumpVar_;
        return true;
    }
    for (size_t i = 0; i < def->props.size() && i < values->size(); ++i) {
        if (def->props[i].name == name) {
            out = (*values)[i];
            return true;
        }
    }
    return false;
}

void Engine::writeProp(Ent* ent, const std::string& name, float value) {
    if (name == "health") {
        const int hp = std::max(0, static_cast<int>(std::lround(value)));
        if (ent) ent->hp = hp;
        else hp_ = hp;
        return;
    }
    if (name == "lives") {
        lives_ = std::max(0, static_cast<int>(std::lround(value)));
        return;
    }
    if (name == "score") {
        score_ = std::max(0, static_cast<int>(std::lround(value)));
        return;
    }
    if (!ent && name == "speed") {
        hasSpeed_ = true;
        speedVar_ = value;
    }
    if (!ent && name == "jump") {
        hasJump_ = true;
        jumpVar_ = value;
    }
    if (ent && ent->def) {
        for (size_t i = 0; i < ent->def->props.size() && i < ent->props.size(); ++i) {
            if (ent->def->props[i].name == name) ent->props[i] = value;
        }
        return;
    }
    for (size_t i = 0; i < (hero_ ? hero_->props.size() : 0) && i < heroProps_.size(); ++i) {
        if (hero_->props[i].name == name) heroProps_[i] = value;
    }
}

bool Engine::blockTest(const Ent* ent, const Block& block) const {
    switch (static_cast<BlockTest>(block.pick)) {
        case BlockTest::Button1: return frameInput_.jump;
        case BlockTest::Button2: return frameInput_.shoot;
        case BlockTest::OnGround: return ent ? ent->onGround : onGround_;
        case BlockTest::TouchHero: return ent ? ent->touchedHero : heroTouching_;
        case BlockTest::WallAhead: {
            const float x = ent ? ent->x + (ent->facing > 0 ? spriteW(ent->def) + 1.f : -1.f)
                                : px_ + (facing_ > 0 ? psW_ + 1.f : -1.f);
            const float y = (ent ? ent->y : py_) + 8.f;
            return solidTile(tileIndex(x, tileW_), tileIndex(y, tileH_));
        }
        case BlockTest::Prop:
        default: {
            float value = 0.f;
            readProp(ent, block.name, value);
            const int op = static_cast<int>(block.num2);
            if (op == 1) return value < block.num;
            if (op == 2) return std::abs(value - block.num) < 0.05f;
            return value > block.num;
        }
    }
}

void Engine::applyStatement(Ent* ent, const Block& block) {
    float* vx = ent ? &ent->vx : &vx_;
    float* vy = ent ? &ent->vy : &vy_;
    int* facing = ent ? &ent->facing : &facing_;
    const Stage* st = stage();
    switch (block.kind) {
        case BlockKind::Move:
            *vx = static_cast<float>(*facing) * block.num;
            if (!ent) lockX_ = true;
            break;
        case BlockKind::Turn: *facing *= -1; break;
        case BlockKind::Jump: {
            float jump = st ? st->jumpVel : 280.f;
            if (!ent && hasJump_) jump = jumpVar_;
            *vy = -jump;
            if (!ent) {
                lockY_ = true;
                onGround_ = false;
            } else {
                ent->onGround = false;
            }
            break;
        }
        case BlockKind::SetVelocity:
            *vx = block.num;
            *vy = block.num2;
            if (!ent) {
                lockX_ = true;
                lockY_ = true;
            }
            break;
        case BlockKind::FacePlayer:
            if (ent) {
                ent->facing = px_ < ent->x ? -1 : 1;
            } else {
                float best = 1.0e9f;
                for (const auto& other : ents_) {
                    if (!other.alive || other.projectile || !other.def) continue;
                    if (other.def->kind != ActorKind::Enemy && other.def->kind != ActorKind::Boss) continue;
                    const float dx = other.x - px_;
                    if (std::abs(dx) < best) {
                        best = std::abs(dx);
                        facing_ = dx < 0.f ? -1 : 1;
                    }
                }
            }
            break;
        case BlockKind::Bounce:
            *vx = -*vx;
            *vy = -*vy;
            break;
        case BlockKind::SetProp: writeProp(ent, block.name, block.num); break;
        case BlockKind::ChangeProp: {
            float value = 0.f;
            readProp(ent, block.name, value);
            writeProp(ent, block.name, value + block.num);
            break;
        }
        case BlockKind::Shoot: {
            const ActorDef* owner = ent ? ent->def : hero_;
            if (!proj_ || !owner) break;
            const std::string& gunId = !owner->projectileId.empty() ? owner->projectileId : weaponId_;
            const ActorDef* gun = proj_->findActor(gunId);
            if (!gun) break;
            const float x = ent ? ent->x : px_;
            const float y = ent ? ent->y : py_;
            const float shot = gun->behavior.shotSpeed > 1.f ? gun->behavior.shotSpeed : 180.f;
            spawnShot(*gun, x + 4.f, y + 4.f, static_cast<float>(*facing) * shot, 0.f, ent == nullptr);
            sfx("shoot");
            break;
        }
        case BlockKind::Sound: sfx(block.name); break;
        case BlockKind::Vanish:
            if (ent) ent->alive = false;
            break;
        case BlockKind::AddScore: score_ += static_cast<int>(std::lround(block.num)); break;
        case BlockKind::HurtHero: hurtPlayer(std::max(1, static_cast<int>(std::lround(block.num)))); break;
        case BlockKind::WhenSpawned:
        case BlockKind::EveryFrame:
        case BlockKind::WhenButton1:
        case BlockKind::WhenButton2:
        case BlockKind::WhenTouchHero:
        case BlockKind::WhenHurt:
        case BlockKind::Wait:
        case BlockKind::If:
        case BlockKind::Repeat:
            break;
    }
}

bool Engine::runBody(Ent* ent, ScriptRt* rt, const std::vector<Block>& body, int& pc) {
    while (pc >= 0 && pc < static_cast<int>(body.size())) {
        if (++scriptSteps_ > 96) {
            pc = static_cast<int>(body.size());
            return true;
        }
        const Block& block = body[static_cast<size_t>(pc)];
        if (block.kind == BlockKind::Wait) {
            if (rt) rt->wait = std::max(0.f, block.num);
            ++pc;
            return false;
        }
        if (block.kind == BlockKind::If) {
            if (blockTest(ent, block)) {
                int inner = 0;
                if (!runBody(ent, rt, block.inner, inner)) return false;
            }
            ++pc;
            continue;
        }
        if (block.kind == BlockKind::Repeat) {
            const int times = std::clamp(static_cast<int>(std::lround(block.num)), 0, 12);
            for (int i = 0; i < times; ++i) {
                int inner = 0;
                if (!runBody(ent, rt, block.inner, inner)) return false;
            }
            ++pc;
            continue;
        }
        applyStatement(ent, block);
        ++pc;
    }
    return true;
}

void Engine::runHeroScripts(float dt, uint32_t mask) {
    if (!hero_ || hero_->scripts.empty()) return;
    if (heroRt_.size() != hero_->scripts.size()) heroRt_.assign(hero_->scripts.size(), {});
    scriptSteps_ = 0;
    for (size_t i = 0; i < hero_->scripts.size(); ++i) {
        ScriptRt& rt = heroRt_[i];
        if ((mask & kHatFrame) && rt.wait > 0.f) rt.wait = std::max(0.f, rt.wait - dt);
        if (rt.wait > 0.f) continue;
        const Script& script = hero_->scripts[i];
        const BlockKind hat = script.hat.kind;
        const bool every = hat == BlockKind::EveryFrame && (mask & kHatFrame);
        const bool spawn = hat == BlockKind::WhenSpawned && (mask & kHatSpawn) && !rt.done;
        const bool button1 = hat == BlockKind::WhenButton1 && (mask & kHatBtn1) && (rt.pc > 0 || frameInput_.jumpEdge);
        const bool button2 = hat == BlockKind::WhenButton2 && (mask & kHatBtn2) && (rt.pc > 0 || frameInput_.shootEdge);
        const bool touch = hat == BlockKind::WhenTouchHero && (mask & kHatTouch);
        const bool hurt = hat == BlockKind::WhenHurt && (mask & kHatHurt);
        if (!every && !spawn && !button1 && !button2 && !touch && !hurt) continue;
        int pc = every ? 0 : rt.pc;
        const bool finished = runBody(nullptr, &rt, script.body, pc);
        if (every) continue;
        rt.pc = finished ? 0 : pc;
        if (finished && spawn) rt.done = true;
    }
}

void Engine::runActorScripts(Ent& ent, float dt, uint32_t mask, bool tickWaits) {
    if (!ent.def || ent.def->scripts.empty()) return;
    if (ent.scriptRt.size() != ent.def->scripts.size()) ent.scriptRt.assign(ent.def->scripts.size(), {});
    scriptSteps_ = 0;
    for (size_t i = 0; i < ent.def->scripts.size(); ++i) {
        ScriptRt& rt = ent.scriptRt[i];
        if (tickWaits && rt.wait > 0.f) rt.wait = std::max(0.f, rt.wait - dt);
        if (rt.wait > 0.f) continue;
        const Script& script = ent.def->scripts[i];
        const BlockKind hat = script.hat.kind;
        const bool every = hat == BlockKind::EveryFrame && (mask & kHatFrame);
        const bool spawn = hat == BlockKind::WhenSpawned && (mask & kHatSpawn) && !rt.done;
        const bool button1 = hat == BlockKind::WhenButton1 && (mask & kHatBtn1) && (rt.pc > 0 || frameInput_.jumpEdge);
        const bool button2 = hat == BlockKind::WhenButton2 && (mask & kHatBtn2) && (rt.pc > 0 || frameInput_.shootEdge);
        const bool touch = hat == BlockKind::WhenTouchHero && (mask & kHatTouch);
        const bool hurt = hat == BlockKind::WhenHurt && (mask & kHatHurt);
        if (!every && !spawn && !button1 && !button2 && !touch && !hurt) continue;
        int pc = every ? 0 : rt.pc;
        const bool finished = runBody(&ent, &rt, script.body, pc);
        if (every) continue;
        rt.pc = finished ? 0 : pc;
        if (finished && spawn) rt.done = true;
    }
}

void Engine::updatePlatform(float dt, const Input& input) {
    const Stage* st = stage();
    if (!st || !hero_) return;
    if (std::abs(input.axisX) > 0.2f) facing_ = input.axisX > 0 ? 1 : -1;
    lockX_ = false;
    lockY_ = false;
    runHeroScripts(dt, kHatLive);
    const float jumpVel = hasJump_ ? jumpVar_ : st->jumpVel;
    Box box = playerBox();
    Box reach = box;
    reach.x -= 8.f;
    reach.w += 16.f;
    reach.y -= 4.f;
    reach.h += static_cast<float>(tileH_) + 8.f;
    Box above = box;
    above.y -= static_cast<float>(tileH_);
    above.h += static_cast<float>(tileH_);
    const bool water = overlapsFlag(box, TileWater);
    const bool ice = onGround_ && overlapsFlag({box.x, box.y + box.h, box.w, 2.f}, TileIce);
    const bool climbInput = std::abs(input.axisY) > 0.45f;
    if (!climbInput) ladderCool_ = false;
    if (!climbing_ && !ladderCool_ && climbInput &&
        (overlapsLadder(box) || overlapsLadder(reach) || (onGround_ && overlapsLadder(above)))) {
        climbing_ = true;
        grabLadder();
        box = playerBox();
    }
    if (climbing_ && !overlapsLadder(box) && !overlapsLadder(reach)) climbing_ = false;
    if (climbing_ && input.jumpEdge) {
        climbing_ = false;
        vy_ = -jumpVel;
        sfx("jump");
    }
    float speed = (hasSpeed_ ? speedVar_ : st->playerSpeed) * (water ? 0.62f : 1.f);
    bool climbed = false;
    if (climbing_) {
        if (stepOffLadder(input)) {
            climbed = false;
            ladderCool_ = true;
        } else {
            climbed = true;
            if (!lockX_) vx_ = input.axisX * speed * 0.75f;
            if (!lockY_) vy_ = input.axisY * speed;
            Box next = playerBox();
            next.y -= 3.f;
            if (input.axisY < -0.45f && !overlapsLadder(next)) vy_ = 0.f;
        }
    }
    if (!climbed) {
        if (!lockX_) {
            const float target = input.axisX * speed;
            const float accel = (ice ? 280.f : 1600.f) * dt;
            if (std::abs(target - vx_) <= accel) vx_ = target;
            else vx_ += target > vx_ ? accel : -accel;
        }
        const float gravity = st->gravity * (water ? 0.35f : 1.f);
        if (!lockY_) vy_ = std::min(560.f, vy_ + gravity * dt);
        if (!lockY_ && input.jumpEdge && onGround_) {
            vy_ = -jumpVel;
            onGround_ = false;
            sfx("jump");
        }
        if (!input.jump && wasJump_ && vy_ < 0.f) vy_ *= 0.55f;
    }
    wasJump_ = input.jump;
    Ent body;
    body.def = hero_;
    body.x = px_;
    body.y = py_;
    body.vx = vx_;
    body.vy = vy_;
    moveBody(body, dt, false, climbed);
    px_ = body.x;
    py_ = body.y;
    vx_ = body.vx;
    vy_ = body.vy;
    onGround_ = climbed ? false : body.onGround;
    blockedX_ = body.blockedX;
}

void Engine::updateShooter(float dt, const Input& input) {
    const Stage* st = stage();
    if (!st) return;
    const float speed = st->playerSpeed;
    vx_ = input.axisX * speed;
    vy_ = input.axisY * speed;
    if (std::abs(input.axisX) > 0.2f) facing_ = input.axisX > 0 ? 1 : -1;
    runHeroScripts(dt, kHatLive);
    px_ += vx_ * dt;
    py_ += vy_ * dt;
    clampShooter();
    onGround_ = false;
}

void Engine::updateTopDown(float dt, const Input& input) {
    const Stage* st = stage();
    if (!st || !hero_) return;
    vx_ = input.axisX * st->playerSpeed;
    vy_ = input.axisY * st->playerSpeed;
    if (std::abs(input.axisX) > 0.2f) facing_ = input.axisX > 0 ? 1 : -1;
    runHeroScripts(dt, kHatLive);
    Ent body;
    body.def = hero_;
    body.x = px_;
    body.y = py_;
    body.vx = vx_;
    body.vy = vy_;
    moveBody(body, dt, false);
    px_ = body.x;
    py_ = body.y;
    vx_ = body.vx;
    vy_ = body.vy;
}

void Engine::chooseGhostDir(Ent& ghost, int cx, int cy) {
    const Stage* st = stage();
    if (!st) return;
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int tx = tileIndex(px_ + tileW_ * 0.5f, tileW_);
    int ty = tileIndex(py_ + tileH_ * 0.5f, tileH_);
    if (ghostScatter_ && frightened_ <= 0.f) {
        tx = (ghost.ghostIndex % 2 == 0) ? 1 : st->width - 2;
        ty = (ghost.ghostIndex / 2 % 2 == 0) ? 1 : st->height - 2;
    }
    std::mt19937 rng(static_cast<uint32_t>(time_ * 1000.f) + ghost.ghostIndex * 17 + 3);
    int best = -1;
    int bestScore = 1000000;
    int fallback = -1;
    for (int i = 0; i < 4; ++i) {
        if (solidTile(cx + dirs[i][0], cy + dirs[i][1])) continue;
        const bool reverse = dirs[i][0] == -ghost.dirX && dirs[i][1] == -ghost.dirY && (ghost.dirX || ghost.dirY);
        if (reverse) {
            fallback = i;
            continue;
        }
        int score = std::abs(cx + dirs[i][0] - tx) + std::abs(cy + dirs[i][1] - ty);
        if (frightened_ > 0.f) score = static_cast<int>(rng() % 100);
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best < 0) best = fallback;
    if (best >= 0) {
        ghost.dirX = dirs[best][0];
        ghost.dirY = dirs[best][1];
    }
}

void Engine::advanceGrid(float& x, float& y, int& dirX, int& dirY, int wantX, int wantY, float dt, float speed, Ent* ghost) {
    float left = speed * dt;
    for (int guard = 0; guard < 4 && left > 0.0001f; ++guard) {
        float rx = std::fmod(x, static_cast<float>(tileW_));
        float ry = std::fmod(y, static_cast<float>(tileH_));
        if (rx < 0) rx += tileW_;
        if (ry < 0) ry += tileH_;
        const bool aligned = rx < 0.4f && ry < 0.4f;
        if (aligned) {
            x -= rx;
            y -= ry;
            const int cx = static_cast<int>(std::lround(x / tileW_));
            const int cy = static_cast<int>(std::lround(y / tileH_));
            auto open = [&](int dx, int dy) { return !solidTile(cx + dx, cy + dy); };
            if (!ghost) {
                if ((wantX || wantY) && open(wantX, wantY)) {
                    dirX = wantX;
                    dirY = wantY;
                } else if ((dirX || dirY) && !open(dirX, dirY)) {
                    dirX = 0;
                    dirY = 0;
                }
            } else {
                chooseGhostDir(*ghost, cx, cy);
                dirX = ghost->dirX;
                dirY = ghost->dirY;
            }
            if (dirX == 0 && dirY == 0) return;
        }
        float dist = 0.f;
        if (aligned) dist = static_cast<float>(dirX != 0 ? tileW_ : tileH_);
        else if (dirX > 0) dist = static_cast<float>(tileW_) - rx;
        else if (dirX < 0) dist = rx;
        else if (dirY > 0) dist = static_cast<float>(tileH_) - ry;
        else if (dirY < 0) dist = ry;
        else return;
        if (dist < 0.001f) dist = static_cast<float>(dirX != 0 ? tileW_ : tileH_);
        const float step = std::min(left, dist);
        x += dirX * step;
        y += dirY * step;
        left -= step;
    }
}

void Engine::updateMaze(float dt, const Input& input) {
    ghostClock_ += dt;
    if (ghostClock_ > (ghostScatter_ ? 5.f : 8.f)) {
        ghostClock_ = 0;
        ghostScatter_ = !ghostScatter_;
    }
    if (reverseGhosts_) {
        for (auto& ent : ents_) {
            if (!ent.alive || !ent.def || ent.def->behavior.pattern != AiPattern::MazeGhost) continue;
            ent.dirX = -ent.dirX;
            ent.dirY = -ent.dirY;
        }
        reverseGhosts_ = false;
    }
    runHeroScripts(dt, kHatLive);
    int wantX = 0, wantY = 0;
    if (std::abs(input.axisX) > std::abs(input.axisY)) wantX = input.axisX > 0.3f ? 1 : (input.axisX < -0.3f ? -1 : 0);
    else if (std::abs(input.axisY) > 0.3f) wantY = input.axisY > 0 ? 1 : -1;
    const Stage* st = stage();
    const float speed = st ? st->playerSpeed : 70.f;
    int pdx = static_cast<int>(std::lround(vx_));
    int pdy = static_cast<int>(std::lround(vy_));
    advanceGrid(px_, py_, pdx, pdy, wantX, wantY, dt, speed, nullptr);
    vx_ = static_cast<float>(pdx);
    vy_ = static_cast<float>(pdy);
    if (pdx != 0) facing_ = pdx;
    for (auto& ent : ents_) {
        if (!ent.alive || !ent.def || ent.def->behavior.pattern != AiPattern::MazeGhost) continue;
        float gspeed = ent.def->behavior.speed * (frightened_ > 0.f ? 0.65f : 1.f);
        advanceGrid(ent.x, ent.y, ent.dirX, ent.dirY, 0, 0, dt, gspeed, &ent);
    }
}

void Engine::tryPlayerFire(const Input& input) {
    if (!proj_ || fireCd_ > 0.f || (!input.shoot && !input.shootEdge)) return;
    const ActorDef* gun = proj_->findActor(weaponId_);
    const Stage* st = stage();
    if (!gun || !st) return;
    float ax = input.axisX;
    float ay = input.axisY;
    if (st->physics == PhysicsMode::ShooterVert && std::abs(ax) < 0.25f && std::abs(ay) < 0.25f) ay = -1.f;
    if (st->physics == PhysicsMode::ShooterHorz && std::abs(ax) < 0.25f && std::abs(ay) < 0.25f) ax = 1.f;
    if (std::abs(ax) < 0.2f && std::abs(ay) < 0.2f) ax = static_cast<float>(facing_);
    const float len = std::max(0.01f, std::sqrt(ax * ax + ay * ay));
    const float shot = gun->behavior.shotSpeed > 1.f ? gun->behavior.shotSpeed : 200.f;
    const float svx = ax / len * shot;
    const float svy = ay / len * shot;
    const int burstN = std::max(1, gun->behavior.burst);
    const float spread = gun->behavior.spread;
    for (int i = 0; i < burstN; ++i) {
        float ang = 0.f;
        if (burstN > 1) ang = (static_cast<float>(i) / static_cast<float>(burstN - 1) - 0.5f) * spread;
        const float cs = std::cos(ang);
        const float sn = std::sin(ang);
        spawnShot(*gun, px_ + psW_ * 0.5f, py_ + psH_ * 0.5f, svx * cs - svy * sn, svx * sn + svy * cs, true);
    }
    fireCd_ = hero_ && hero_->behavior.fireInterval > 0.f ? hero_->behavior.fireInterval : 0.28f;
    sfx("shoot");
}

void Engine::spawnShot(const ActorDef& gun, float x, float y, float vx, float vy, bool fromPlayer) {
    if (ents_.size() >= 1000) return;
    Ent shot;
    shot.def = &gun;
    shot.projectile = true;
    shot.fromPlayer = fromPlayer;
    shot.x = x;
    shot.y = y;
    shot.vx = vx;
    shot.vy = vy;
    shot.life = gun.behavior.lifetime > 0 ? gun.behavior.lifetime : 1.5f;
    shot.hp = 1;
    shot.alive = true;
    shot.awake = true;
    ents_.push_back(shot);
}

void Engine::updateEnt(size_t index, float dt) {
    Ent& ent = ents_[index];
    const Stage* st = stage();
    if (!ent.def || !st) return;
    const Behavior& behavior = ent.def->behavior;
    const bool near = ent.x + 32 > camX_ - 64 && ent.x < camX_ + canvas_.width() + 64 && ent.y + 32 > camY_ - 64 &&
                      ent.y < camY_ + canvas_.height() + 80;
    if (!ent.awake) {
        if (near) ent.awake = true;
        else return;
    }
    if (!ent.def->scripts.empty()) {
        runActorScripts(ent, dt, kHatLive, true);
        const ActorKind kind = ent.def->kind;
        if (kind == ActorKind::Enemy || kind == ActorKind::Boss || kind == ActorKind::Decoration)
            moveBody(ent, dt, behavior.gravity);
        if (ent.y > st->height * tileH_ + 40) ent.alive = false;
        return;
    }
    ent.timer += dt;
    ent.fireCd -= dt;
    const float speed = behavior.speed;
    switch (behavior.pattern) {
        case AiPattern::Patrol:
            ent.vx = ent.facing * speed;
            break;
        case AiPattern::Fly:
            ent.vx = ent.facing * speed;
            ent.vy = 0;
            break;
        case AiPattern::Chase:
            if (behavior.gravity) {
                ent.facing = px_ < ent.x ? -1 : 1;
                ent.vx = ent.facing * speed;
            } else {
                const float dx = px_ - ent.x;
                const float dy = py_ - ent.y;
                const float len = std::max(1.f, std::sqrt(dx * dx + dy * dy));
                ent.vx = dx / len * speed;
                ent.vy = dy / len * speed;
            }
            break;
        case AiPattern::Sine:
            ent.x = ent.originX + std::sin((time_ + ent.phase) * behavior.period) * behavior.amplitude;
            ent.vy = speed;
            ent.vx = 0;
            break;
        case AiPattern::Hopper:
            if (ent.onGround && ent.timer > behavior.period) {
                ent.facing = px_ < ent.x ? -1 : 1;
                ent.vx = ent.facing * speed;
                ent.vy = -st->jumpVel * 0.78f;
                ent.timer = 0;
            }
            break;
        case AiPattern::Dive:
            if (ent.phase < 1.f && ent.timer > 0.45f) {
                const float dx = px_ - ent.x;
                const float dy = py_ - ent.y;
                const float len = std::max(1.f, std::sqrt(dx * dx + dy * dy));
                ent.vx = dx / len * speed;
                ent.vy = dy / len * speed;
                ent.phase = 1.f;
            }
            break;
        case AiPattern::Boss: {
            const float cycle = std::fmod(ent.timer, 6.f);
            if (cycle < 2.4f) {
                ent.vx = std::sin(time_ * 2.f) * speed;
                ent.vy = std::cos(time_ * 1.3f) * speed * 0.35f;
            } else if (cycle < 4.2f) {
                ent.vx *= 0.9f;
                ent.vy *= 0.9f;
            } else {
                const float dx = px_ - ent.x;
                const float dy = py_ - ent.y;
                const float len = std::max(1.f, std::sqrt(dx * dx + dy * dy));
                ent.vx = dx / len * speed * 1.7f;
                ent.vy = dy / len * speed * 1.7f;
            }
            ent.x = clampf(ent.x, camX_ + 8.f, camX_ + canvas_.width() - spriteW(ent.def) - 8.f);
            break;
        }
        case AiPattern::Bounce:
            break;
        default:
            ent.vx = 0;
            if (!behavior.gravity) ent.vy = 0;
            break;
    }

    const bool flying = behavior.pattern == AiPattern::Sine || behavior.pattern == AiPattern::Dive ||
                        behavior.pattern == AiPattern::Boss || behavior.pattern == AiPattern::Fly ||
                        behavior.pattern == AiPattern::Bounce ||
                        (behavior.pattern == AiPattern::Chase && !behavior.gravity) ||
                        behavior.pattern == AiPattern::Turret || behavior.pattern == AiPattern::Shooter ||
                        behavior.pattern == AiPattern::Idle || behavior.pattern == AiPattern::None;
    if (behavior.pattern == AiPattern::Sine) {
        ent.y += ent.vy * dt;
    } else if (flying && behavior.pattern != AiPattern::Turret && behavior.pattern != AiPattern::Shooter &&
               behavior.pattern != AiPattern::Idle && behavior.pattern != AiPattern::None) {
        ent.x += ent.vx * dt;
        ent.y += ent.vy * dt;
        if (behavior.pattern == AiPattern::Fly || behavior.pattern == AiPattern::Bounce) {
            if (ent.x < 0 || ent.blockedX) ent.facing *= -1;
            const float mapW = st->width * static_cast<float>(tileW_);
            if (ent.x < 4) {
                ent.x = 4;
                ent.facing = 1;
                ent.vx = std::abs(ent.vx);
            }
            if (ent.x > mapW - spriteW(ent.def) - 4) {
                ent.x = mapW - spriteW(ent.def) - 4;
                ent.facing = -1;
                ent.vx = -std::abs(ent.vx);
            }
            if (behavior.pattern == AiPattern::Bounce) {
                if (ent.y < camY_ + 8) ent.vy = std::abs(ent.vy);
                if (ent.y > camY_ + canvas_.height() - 24) ent.vy = -std::abs(ent.vy);
            }
        }
    } else if (behavior.gravity || behavior.pattern == AiPattern::Patrol || behavior.pattern == AiPattern::Hopper ||
               behavior.pattern == AiPattern::Chase) {
        moveBody(ent, dt, behavior.gravity);
        if (behavior.pattern == AiPattern::Patrol) {
            if (ent.blockedX) ent.facing *= -1;
            else if (behavior.turnAtEdge && ent.onGround && !groundAhead(ent)) ent.facing *= -1;
        }
        if (behavior.pattern == AiPattern::Chase && ent.blockedX && ent.onGround) ent.vy = -st->jumpVel * 0.7f;
    }

    const bool canShoot = behavior.pattern == AiPattern::Turret || behavior.pattern == AiPattern::Shooter ||
                          behavior.pattern == AiPattern::Boss;
    if (canShoot && ent.fireCd <= 0.f && !ent.def->projectileId.empty()) {
        const ActorDef* gun = proj_->findActor(ent.def->projectileId);
        if (gun) {
            float svx = 0, svy = 0;
            const float shot = behavior.shotSpeed;
            if (behavior.aim == AimMode::Up) svy = -shot;
            else if (behavior.aim == AimMode::Down) svy = shot;
            else if (behavior.aim == AimMode::Facing) svx = ent.facing * shot;
            else if (behavior.aim == AimMode::Fixed) {
                const float a = behavior.shotAngle * kPi / 180.f;
                svx = std::cos(a) * shot;
                svy = std::sin(a) * shot;
            } else {
                const float dx = px_ - ent.x;
                const float dy = py_ - ent.y;
                const float len = std::max(1.f, std::sqrt(dx * dx + dy * dy));
                svx = dx / len * shot;
                svy = dy / len * shot;
            }
            const int burstN = std::max(1, behavior.burst);
            for (int i = 0; i < burstN; ++i) {
                float ang = 0;
                if (burstN > 1) ang = (static_cast<float>(i) / (burstN - 1) - 0.5f) * behavior.spread;
                const float cs = std::cos(ang);
                const float sn = std::sin(ang);
                const float rx = svx * cs - svy * sn;
                const float ry = svx * sn + svy * cs;
                spawnShot(*gun, ent.x + spriteW(ent.def) * 0.5f, ent.y + spriteH(ent.def) * 0.5f, rx, ry, false);
            }
            ent.fireCd = behavior.fireInterval;
            sfx("shoot");
        }
    }

    if (ent.y > st->height * tileH_ + 40) ent.alive = false;
}

void Engine::updateProjectile(Ent& ent, float dt) {
    ent.x += ent.vx * dt;
    ent.y += ent.vy * dt;
    ent.life -= dt;
    if (ent.life <= 0) {
        ent.alive = false;
        return;
    }
    if (ent.x < camX_ - 180 || ent.x > camX_ + canvas_.width() + 180 || ent.y < camY_ - 180 ||
        ent.y > camY_ + canvas_.height() + 180) {
        ent.alive = false;
        return;
    }
    Box box = entBox(ent);
    bool hitTile = false;
    const int x0 = tileIndex(box.x, tileW_);
    const int y0 = tileIndex(box.y, tileH_);
    const int x1 = tileIndex(box.x + box.w - 0.01f, tileW_);
    const int y1 = tileIndex(box.y + box.h - 0.01f, tileH_);
    const Stage* st = stage();
    for (int ty = y0; st && ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            const uint32_t flags = flagsAt(tx, ty);
            if (flags & TileDestructible) {
                const size_t i = static_cast<size_t>(ty) * st->width + tx;
                if (i < tiles_.size()) tiles_[i] = 0;
                burst(tx * tileW_ + 4.f, ty * tileH_ + 4.f, 5, 180, 140, 90);
                hitTile = true;
            } else if (flags & TileSolid) {
                hitTile = true;
            }
        }
    }
    if (hitTile) {
        ent.alive = false;
        return;
    }
    const int damage = ent.def ? std::max(1, ent.def->behavior.touchDamage) : 1;
    if (ent.fromPlayer) {
        for (auto& other : ents_) {
            if (!other.alive || other.projectile || &other == &ent || !other.def) continue;
            if (other.def->kind != ActorKind::Enemy && other.def->kind != ActorKind::Boss) continue;
            if (!overlap(box, entBox(other))) continue;
            other.hp -= damage;
            ent.alive = false;
            if (!other.def->scripts.empty()) runActorScripts(other, 0.f, kHatHurt, false);
            if (other.hp <= 0) killEnt(other);
            else burst(other.x, other.y, 4, 255, 255, 255);
            return;
        }
    } else if (overlap(box, playerBox())) {
        ent.alive = false;
        hurtPlayer(damage);
    }
}

void Engine::killEnt(Ent& ent) {
    if (!ent.alive) return;
    ent.alive = false;
    if (ent.def) {
        score_ += std::max(0, ent.def->behavior.score);
        if (!ent.def->dropActorId.empty() && proj_) {
            std::mt19937 rng(static_cast<uint32_t>(ent.x * 13 + ent.y * 7 + score_));
            const float roll = (rng() % 1000) / 1000.f;
            if (roll < ent.def->dropChance) {
                if (const ActorDef* drop = proj_->findActor(ent.def->dropActorId)) {
                    Ent item;
                    item.def = drop;
                    item.x = ent.x;
                    item.y = ent.y;
                    item.alive = true;
                    item.awake = true;
                    item.hp = 1;
                    ents_.push_back(item);
                }
            }
        }
    }
    sfx("explode");
    burst(ent.x, ent.y, 12, 255, 180, 60);
    if (proj_ && proj_->settings.screenShake) shake_ = std::max(shake_, 0.18f);
}

void Engine::hurtPlayer(int amount) {
    if (phase_ != Phase::Playing || invuln_ > 0.f || pending_ > 0.f || amount <= 0) return;
    hp_ -= amount;
    invuln_ = 1.15f;
    sfx("hurt");
    if (proj_ && proj_->settings.screenShake) shake_ = 0.32f;
    burst(px_ + 4, py_ + 4, 8, 255, 70, 70);
    if (hp_ <= 0) loseLife();
}

void Engine::loseLife() {
    if (phase_ != Phase::Playing) return;
    lives_ -= 1;
    burst(px_, py_, 14, 255, 210, 80);
    sfx("explode");
    if (lives_ <= 0) {
        hp_ = 0;
        endGame(false);
        return;
    }
    hp_ = proj_ ? std::max(1, proj_->settings.maxHealth) : 3;
    px_ = spawnX_;
    py_ = spawnY_;
    vx_ = vy_ = 0;
    invuln_ = 1.6f;
    climbing_ = false;
    ladderCool_ = false;
}

void Engine::burst(float x, float y, int count, uint8_t r, uint8_t g, uint8_t b) {
    std::mt19937 rng(static_cast<uint32_t>(x * 3 + y * 11 + time_ * 100));
    for (int i = 0; i < count; ++i) {
        if (parts_.size() > 180) break;
        Particle part;
        part.x = x;
        part.y = y;
        part.vx = static_cast<int>(rng() % 180) - 90.f;
        part.vy = static_cast<int>(rng() % 160) - 120.f;
        part.life = part.maxLife = 0.25f + (rng() % 20) / 100.f;
        part.r = r;
        part.g = g;
        part.b = b;
        parts_.push_back(part);
    }
}

bool Engine::debugBlocked(float x, float y) const {
    return solidTile(tileIndex(x, tileW_), tileIndex(y, tileH_));
}

float Engine::debugProp(const std::string& name) const {
    float value = 0.f;
    if (readProp(nullptr, name, value)) return value;
    for (const auto& ent : ents_) {
        if (!ent.alive || !ent.def) continue;
        if (readProp(&ent, name, value)) return value;
    }
    return -1000000.f;
}

float Engine::debugBulletX() const {
    for (const auto& ent : ents_)
        if (ent.alive && ent.projectile && ent.fromPlayer) return ent.x;
    return -1.f;
}

void Engine::drawTile(uint16_t id, int dx, int dy) {
    if (id == 0) return;
    const Tileset* set = tileset();
    int visual = id;
    if (set && id - 1 < static_cast<int>(set->tiles.size())) {
        const TileDef& def = set->tiles[id - 1];
        if (def.frames > 1) visual = id + static_cast<int>(time_ * def.fps) % def.frames;
    }
    if (!set || set->rgba.empty() || set->imgW <= 0) {
        const uint32_t flags = flagsAt(dx / std::max(1, tileW_) , dy / std::max(1, tileH_));
        uint8_t r = 90, g = 90, b = 110;
        if (flags & TileHazard) { r = 200; g = 60; b = 70; }
        else if (flags & TileLadder) { r = 170; g = 130; b = 70; }
        else if (flags & TileSolid) { r = 130; g = 90; b = 70; }
        canvas_.fillRect(dx, dy, tileW_, tileH_, r, g, b);
        return;
    }
    const int cols = std::max(1, set->columns);
    const int cell = visual - 1;
    const int sx = (cell % cols) * tileW_;
    const int sy = (cell / cols) * tileH_;
    canvas_.blit(set->rgba.data(), set->imgW, set->imgH, sx, sy, tileW_, tileH_, dx, dy, false);
}

void Engine::drawSprite(const Sprite* sprite, int frame, int dx, int dy, bool flip, uint8_t tr, uint8_t tg, uint8_t tb) {
    if (!sprite || sprite->rgba.empty()) return;
    const int frames = std::max(1, sprite->frames);
    frame = (frame % frames + frames) % frames;
    const int sx = frame * sprite->frameW;
    const int rowH = sprite->frameH;
    // Tint by redrawing through a temporary is expensive; approximate by blit then skip if white.
    (void)tr;
    (void)tg;
    (void)tb;
    canvas_.blit(sprite->rgba.data(), sprite->imgW, sprite->imgH, sx, 0, sprite->frameW, rowH, dx, dy, flip);
}

void Engine::drawEnt(const Ent& ent, int ox, int oy) {
    if (!ent.alive || !ent.def || !proj_) return;
    const Sprite* sprite = proj_->findSprite(ent.def->spriteId);
    int dx = static_cast<int>(std::lround(ent.x)) - ox;
    int dy = static_cast<int>(std::lround(ent.y)) - oy;
    if (ent.def->kind == ActorKind::Item) dy += static_cast<int>(std::sin(time_ * 4.f + ent.x * 0.05f) * 1.5f);
    const int frames = sprite ? std::max(1, sprite->frames) : 1;
    const float fps = sprite ? sprite->fps : 8.f;
    const int frame = static_cast<int>(ent.anim * fps) % frames;
    const bool flip = ent.facing < 0 && (!sprite || sprite->directions < 2);
    if (!sprite || sprite->rgba.empty()) {
        uint8_t r = 255, g = 80, b = 80;
        if (ent.def->kind == ActorKind::Item) { r = 255; g = 220; b = 60; }
        if (ent.def->kind == ActorKind::Check) { r = 80; g = 220; b = 120; }
        if (ent.def->behavior.pattern == AiPattern::MazeGhost && frightened_ > 0.f) { r = 80; g = 140; b = 255; }
        canvas_.fillRect(dx, dy, spriteW(ent.def), spriteH(ent.def), r, g, b);
        return;
    }
    if (ent.def->behavior.pattern == AiPattern::MazeGhost && frightened_ > 0.f && (static_cast<int>(time_ * 12) % 2 == 0))
        return;
    drawSprite(sprite, frame, dx, dy, flip);
}

void Engine::drawPlayer(int ox, int oy) {
    if (!hero_ || phase_ == Phase::Title) return;
    if (invuln_ > 0.f && static_cast<int>(time_ * 16) % 2 == 0) return;
    const Sprite* sprite = proj_ ? proj_->findSprite(hero_->spriteId) : nullptr;
    const int dx = static_cast<int>(std::lround(px_)) - ox;
    const int dy = static_cast<int>(std::lround(py_)) - oy;
    int frame = 0;
    if (sprite) {
        const int frames = std::max(1, sprite->frames);
        const bool moving = std::abs(vx_) > 8.f || std::abs(vy_) > 8.f;
        frame = (moving || !onGround_) ? static_cast<int>(playerAnim_ * sprite->fps) % frames : 0;
    }
    const bool flip = facing_ < 0;
    if (!sprite || sprite->rgba.empty()) canvas_.fillRect(dx, dy, psW_, psH_, 80, 180, 255);
    else drawSprite(sprite, frame, dx, dy, flip);
}

void Engine::center(const std::string& text, int y, uint8_t r, uint8_t g, uint8_t b, int scale) {
    const int width = canvas_.textWidth(text, scale);
    const int x = std::max(2, (canvas_.width() - width) / 2);
    canvas_.text(x + scale, y + scale, text, 0, 0, 0, scale);
    canvas_.text(x, y, text, r, g, b, scale);
}

void Engine::drawHud() {
    if (phase_ != Phase::Playing && phase_ != Phase::Paused) return;
    char line[64];
    std::snprintf(line, sizeof(line), "%06d", score_);
    canvas_.text(5, 3, line, 0, 0, 0, 1);
    canvas_.text(4, 2, line, 255, 236, 140, 1);
    std::snprintf(line, sizeof(line), "x%d", std::max(0, lives_));
    const int lx = canvas_.width() - canvas_.textWidth(line, 1) - 6;
    canvas_.text(lx + 1, 3, line, 0, 0, 0, 1);
    canvas_.text(lx, 2, line, 255, 255, 255, 1);
    const int hearts = proj_ ? proj_->settings.maxHealth : hp_;
    for (int i = 0; i < hearts; ++i) {
        const bool on = i < hp_;
        canvas_.fillRect(4 + i * 8, 12, 6, 5, on ? 230 : 50, on ? 60 : 40, on ? 80 : 50);
    }
    if (hasKey_) canvas_.text(4 + hearts * 8 + 4, 11, "KEY", 255, 220, 80, 1);
    const Ent* boss = nullptr;
    for (const auto& ent : ents_)
        if (ent.alive && ent.def && ent.def->kind == ActorKind::Boss) boss = &ent;
    if (boss && boss->def) {
        const int bw = canvas_.width() - 24;
        const int maxHp = std::max(1, boss->def->behavior.hp);
        const int fill = std::max(0, bw * boss->hp / maxHp);
        canvas_.fillRect(12, canvas_.height() - 12, bw, 5, 40, 20, 30);
        canvas_.fillRect(12, canvas_.height() - 12, fill, 5, 240, 70, 80);
    }
}

void Engine::drawOverlay() {
    auto block = [&](const std::string& msg, int y, uint8_t r, uint8_t g, uint8_t b, int scale) {
        std::string line;
        auto flush = [&]() {
            center(line, y, r, g, b, scale);
            y += 10 * scale;
            line.clear();
        };
        for (char ch : msg) {
            if (ch == '\n') flush();
            else line.push_back(ch);
        }
        if (!line.empty()) flush();
    };
    if (phase_ == Phase::Title && proj_) {
        canvas_.shade(40);
        center(proj_->settings.title, 48, 255, 214, 90, 2);
        if (static_cast<int>(time_ * 2) % 2 == 0) center("PRESS START", 100, 255, 255, 255, 1);
        char best[64];
        std::snprintf(best, sizeof(best), "TOP %s %06d", board_[0].name, board_[0].score);
        center(best, 130, 180, 220, 255, 1);
        center("JOYSTICK MOVE", 156, 200, 200, 210, 1);
        center("BUTTON 1 JUMP    BUTTON 2 SHOOT", 172, 200, 200, 210, 1);
        center("CABINETCORE", canvas_.height() - 24, 255, 140, 70, 1);
    } else if (phase_ == Phase::Paused) {
        canvas_.shade(140);
        center("PAUSED", 90, 255, 236, 140, 2);
        center("START RESUME", 124, 255, 255, 255, 1);
        center("BUTTON 1 RESTART", 140, 200, 200, 210, 1);
        center("BUTTON 2 TITLE", 154, 200, 200, 210, 1);
    } else if (phase_ == Phase::Intermission) {
        canvas_.shade(150);
        const Stage* st = stage();
        center(st ? st->name : "READY", 70, 255, 214, 90, 2);
        block(st ? st->message : "", 110, 255, 255, 255, 1);
        if (static_cast<int>(time_ * 2) % 2 == 0) center("PRESS START", canvas_.height() - 40, 255, 255, 255, 1);
    } else if (phase_ == Phase::GameOver) {
        canvas_.shade(160);
        center("GAME OVER", 90, 255, 90, 90, 2);
        char line[32];
        std::snprintf(line, sizeof(line), "SCORE %06d", score_);
        center(line, 130, 255, 236, 140, 1);
        center("PRESS START", 160, 255, 255, 255, 1);
    } else if (phase_ == Phase::Victory) {
        canvas_.shade(140);
        center("YOU WIN", 80, 120, 255, 160, 2);
        char line[32];
        std::snprintf(line, sizeof(line), "SCORE %06d", score_);
        center(line, 120, 255, 236, 140, 1);
        center("PRESS START", 156, 255, 255, 255, 1);
    } else if (phase_ == Phase::NameEntry) {
        canvas_.shade(160);
        center("HIGH SCORE", 64, 255, 214, 90, 2);
        std::string shown;
        for (int i = 0; i < 3; ++i) {
            if (i == namePos_ && static_cast<int>(time_ * 3) % 2 == 0) shown.push_back('_');
            else shown.push_back(name_[i]);
            shown.push_back(' ');
        }
        center(shown, 110, 255, 255, 255, 2);
        center("JOYSTICK PICKS LETTER", 150, 200, 200, 210, 1);
        center("BUTTON 1 CONFIRMS", 166, 200, 200, 210, 1);
    }
}

void Engine::render() {
    const Stage* st = stage();
    uint8_t br = 12, bg = 14, bb = 28;
    if (st) {
        if (st->physics == PhysicsMode::Maze) { br = 8; bg = 8; bb = 16; }
        else if (st->physics == PhysicsMode::ShooterVert || st->physics == PhysicsMode::ShooterHorz) {
            br = 8; bg = 10; bb = 36;
        } else {
            br = 18; bg = 16; bb = 32;
        }
    }
    canvas_.clear(br, bg, bb);
    if (phase_ == Phase::Title || !st) {
        drawOverlay();
        return;
    }
    int shakeX = 0, shakeY = 0;
    if (shake_ > 0.f) {
        shakeX = static_cast<int>(std::sin(time_ * 90.f) * shake_ * 7.f);
        shakeY = static_cast<int>(std::cos(time_ * 70.f) * shake_ * 5.f);
    }
    const int camX = static_cast<int>(std::lround(camX_)) + shakeX;
    const int camY = static_cast<int>(std::lround(camY_)) + shakeY;
    const int parallaxX = static_cast<int>(camX_ * 0.4f);
    const int parallaxY = static_cast<int>(camY_ * 0.4f);
    auto drawLayer = [&](const std::vector<uint16_t>& layer, int cx, int cy) {
        if (!st) return;
        const int x0 = std::max(0, cx / tileW_ - 1);
        const int y0 = std::max(0, cy / tileH_ - 1);
        const int x1 = std::min(st->width - 1, (cx + canvas_.width()) / tileW_ + 1);
        const int y1 = std::min(st->height - 1, (cy + canvas_.height()) / tileH_ + 1);
        for (int ty = y0; ty <= y1; ++ty) {
            for (int tx = x0; tx <= x1; ++tx) {
                const size_t i = static_cast<size_t>(ty) * st->width + tx;
                if (i >= layer.size()) continue;
                drawTile(layer[i], tx * tileW_ - cx, ty * tileH_ - cy);
            }
        }
    };
    if (st->back.size() == tiles_.size()) drawLayer(st->back, parallaxX, parallaxY);
    drawLayer(tiles_, camX, camY);
    for (const auto& ent : ents_)
        if (ent.def && (ent.def->kind == ActorKind::Decoration || ent.def->kind == ActorKind::Item ||
                        ent.def->kind == ActorKind::Check))
            drawEnt(ent, camX, camY);
    for (const auto& ent : ents_)
        if (ent.def && (ent.def->kind == ActorKind::Enemy || ent.def->kind == ActorKind::Boss)) drawEnt(ent, camX, camY);
    if (phase_ != Phase::Intermission) drawPlayer(camX, camY);
    for (const auto& ent : ents_)
        if (ent.projectile) drawEnt(ent, camX, camY);
    if (st->front.size() == tiles_.size()) drawLayer(st->front, camX, camY);
    for (const auto& part : parts_) {
        if (part.life <= 0) continue;
        canvas_.fillRect(static_cast<int>(part.x) - camX, static_cast<int>(part.y) - camY, 2, 2, part.r, part.g, part.b);
    }
    if (flash_ > 0.f) {
        const uint8_t alpha = static_cast<uint8_t>(clampf(flash_ / 0.4f, 0.f, 1.f) * 120.f);
        canvas_.fillRect(0, 0, canvas_.width(), canvas_.height(), 255, 255, 255, alpha);
    }
    drawHud();
    drawOverlay();
}

}  // namespace cc
