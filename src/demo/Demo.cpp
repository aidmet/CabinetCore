#include "demo/Demo.hpp"

#include "common/Synth.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

namespace cc {
namespace {

struct Pix {
    int w, h;
    std::vector<uint8_t> rgba;
    explicit Pix(int w, int h) : w(w), h(h), rgba(static_cast<size_t>(w) * h * 4, 0) {}
    void rect(int x, int y, int rw, int rh, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        for (int yy = y; yy < y + rh; ++yy) {
            for (int xx = x; xx < x + rw; ++xx) {
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                const size_t i = (static_cast<size_t>(yy) * w + xx) * 4;
                rgba[i] = r;
                rgba[i + 1] = g;
                rgba[i + 2] = b;
                rgba[i + 3] = a;
            }
        }
    }
};

Sprite makeSprite(const std::string& id, const std::string& name, const Pix& img, int frameW, int frames, float fps) {
    Sprite sprite;
    sprite.id = id;
    sprite.name = name;
    sprite.frameW = frameW;
    sprite.frameH = img.h;
    sprite.frames = frames;
    sprite.directions = 1;
    sprite.fps = fps;
    sprite.imgW = img.w;
    sprite.imgH = img.h;
    sprite.rgba = img.rgba;
    return sprite;
}

void paintHero(Pix& img, int ox, int frame) {
    img.rect(ox + 4, 1, 8, 14, 24, 20, 36);
    img.rect(ox + 5, 2, 6, 5, 70, 170, 255);
    img.rect(ox + 6, 4, 4, 2, 230, 255, 255);
    img.rect(ox + 5, 7, 6, 5, 220, 64, 78);
    img.rect(ox + 7, 8, 2, 2, 255, 220, 120);
    if (frame == 0) {
        img.rect(ox + 5, 12, 2, 3, 36, 48, 90);
        img.rect(ox + 9, 12, 2, 3, 36, 48, 90);
    } else {
        img.rect(ox + 4, 12, 2, 3, 36, 48, 90);
        img.rect(ox + 10, 12, 2, 3, 36, 48, 90);
    }
}

void paintShip(Pix& img, int ox, int frame) {
    img.rect(ox + 6, 1, 4, 6, 230, 240, 255);
    img.rect(ox + 4, 6, 8, 6, 80, 190, 255);
    img.rect(ox + 2, 8, 3, 4, 255, 90, 70);
    img.rect(ox + 11, 8, 3, 4, 255, 90, 70);
    img.rect(ox + 7, 11, 2, 3 + frame, 255, 190, 60);
    img.rect(ox + 6, 7, 4, 2, 200, 255, 255);
}

void paintSlime(Pix& img, int ox, int frame) {
    const int bob = frame ? 1 : 0;
    img.rect(ox + 3, 6 + bob, 10, 7, 40, 160, 70);
    img.rect(ox + 2, 8 + bob, 12, 5, 70, 210, 90);
    img.rect(ox + 5, 9 + bob, 2, 2, 20, 40, 20);
    img.rect(ox + 9, 9 + bob, 2, 2, 20, 40, 20);
}

void paintBat(Pix& img, int ox, int frame) {
    const int wing = frame ? 2 : 5;
    img.rect(ox + 6, 6, 4, 4, 70, 50, 90);
    img.rect(ox + 1, 7 - frame, wing, 2, 120, 90, 150);
    img.rect(ox + 10, 7 - frame, wing, 2, 120, 90, 150);
    img.rect(ox + 7, 7, 1, 1, 255, 80, 80);
    img.rect(ox + 9, 7, 1, 1, 255, 80, 80);
}

void paintCrab(Pix& img, int ox, int frame) {
    img.rect(ox + 3, 7, 10, 6, 210, 70, 50);
    img.rect(ox + 1, 8 + frame, 3, 3, 180, 40, 30);
    img.rect(ox + 12, 8 + frame, 3, 3, 180, 40, 30);
    img.rect(ox + 5, 9, 2, 2, 255, 255, 255);
    img.rect(ox + 9, 9, 2, 2, 255, 255, 255);
}

void paintTurret(Pix& img) {
    img.rect(3, 8, 10, 7, 70, 80, 100);
    img.rect(6, 3, 4, 7, 140, 150, 170);
    img.rect(7, 1, 2, 3, 200, 210, 230);
    img.rect(5, 10, 2, 2, 255, 60, 60);
}

void paintBoss(Pix& img, int ox, int frame) {
    img.rect(ox + 4, 6, 24, 16, 50, 40, 80);
    img.rect(ox + 8, 2, 16, 8, 90, 70, 140);
    img.rect(ox + 10, 5, 5, 4, 255, 40, 60);
    img.rect(ox + 18, 5, 5, 4, 255, 40, 60);
    img.rect(ox + 2, 12, 6, 4, 255, 120, 40);
    img.rect(ox + 24, 12, 6, 4, 255, 120, 40);
    img.rect(ox + 12, 20, 8, 6 + frame * 2, 255, 80, 40);
    img.rect(ox + 14, 12, 4, 3, 180, 255, 255);
}

void paintGhost(Pix& img, int ox, int frame) {
    img.rect(ox + 3, 2, 10, 10, 80, 240, 255);
    img.rect(ox + 4, 11, 3, 3 + frame, 80, 240, 255);
    img.rect(ox + 9, 11, 3, 4 - frame, 80, 240, 255);
    img.rect(ox + 5, 5, 2, 3, 20, 30, 60);
    img.rect(ox + 9, 5, 2, 3, 20, 30, 60);
}

void paintDoor(Pix& img) {
    img.rect(3, 1, 10, 15, 40, 90, 160);
    img.rect(5, 3, 6, 10, 20, 40, 80);
    img.rect(9, 8, 2, 2, 255, 220, 80);
}

void paintCheckpoint(Pix& img) {
    img.rect(7, 2, 2, 13, 200, 200, 210);
    img.rect(8, 3, 6, 4, 80, 200, 120);
}

void addCoreAssets(Project& project) {
    {
        Pix hero(32, 16);
        paintHero(hero, 0, 0);
        paintHero(hero, 16, 1);
        project.sprites.push_back(makeSprite("hero", "Runner", hero, 16, 2, 8));
    }
    {
        Pix ship(32, 16);
        paintShip(ship, 0, 0);
        paintShip(ship, 16, 1);
        project.sprites.push_back(makeSprite("ship", "Fighter", ship, 16, 2, 12));
    }
    {
        Pix slime(32, 16);
        paintSlime(slime, 0, 0);
        paintSlime(slime, 16, 1);
        project.sprites.push_back(makeSprite("slime", "Slime", slime, 16, 2, 6));
    }
    {
        Pix bat(32, 16);
        paintBat(bat, 0, 0);
        paintBat(bat, 16, 1);
        project.sprites.push_back(makeSprite("bat", "Bat", bat, 16, 2, 10));
    }
    {
        Pix crab(32, 16);
        paintCrab(crab, 0, 0);
        paintCrab(crab, 16, 1);
        project.sprites.push_back(makeSprite("crab", "Crab", crab, 16, 2, 6));
    }
    {
        Pix turret(16, 16);
        paintTurret(turret);
        project.sprites.push_back(makeSprite("turret", "Turret", turret, 16, 1, 1));
    }
    {
        Pix boss(64, 32);
        paintBoss(boss, 0, 0);
        paintBoss(boss, 32, 1);
        Sprite sprite = makeSprite("boss", "Core", boss, 32, 2, 4);
        sprite.frameH = 32;
        project.sprites.push_back(sprite);
    }
    {
        Pix ghost(32, 16);
        paintGhost(ghost, 0, 0);
        paintGhost(ghost, 16, 1);
        project.sprites.push_back(makeSprite("ghost", "Ghost", ghost, 16, 2, 8));
    }
    {
        Pix bullet(8, 8);
        bullet.rect(1, 3, 6, 2, 255, 240, 120);
        bullet.rect(5, 2, 2, 4, 255, 255, 220);
        project.sprites.push_back(makeSprite("bullet", "Bullet", bullet, 8, 1, 1));
    }
    {
        Pix bullet(8, 8);
        bullet.rect(2, 2, 4, 4, 255, 80, 70);
        project.sprites.push_back(makeSprite("ebullet", "Enemy shot", bullet, 8, 1, 1));
    }
    {
        Pix coin(16, 8);
        coin.rect(2, 1, 4, 6, 255, 210, 40);
        coin.rect(10, 1, 4, 6, 255, 230, 90);
        project.sprites.push_back(makeSprite("coin", "Coin", coin, 8, 2, 8));
    }
    {
        Pix heart(8, 8);
        heart.rect(1, 2, 2, 2, 230, 50, 70);
        heart.rect(4, 2, 2, 2, 230, 50, 70);
        heart.rect(2, 4, 4, 2, 230, 50, 70);
        project.sprites.push_back(makeSprite("heart", "Heart", heart, 8, 1, 1));
    }
    {
        Pix pellet(8, 8);
        pellet.rect(3, 3, 2, 2, 255, 220, 140);
        project.sprites.push_back(makeSprite("pellet", "Pellet", pellet, 8, 1, 1));
    }
    {
        Pix power(8, 8);
        power.rect(2, 2, 4, 4, 255, 255, 255);
        project.sprites.push_back(makeSprite("power", "Power pellet", power, 8, 1, 1));
    }
    {
        Pix key(8, 8);
        key.rect(1, 2, 3, 3, 255, 210, 60);
        key.rect(4, 3, 3, 1, 255, 210, 60);
        project.sprites.push_back(makeSprite("key", "Key", key, 8, 1, 1));
    }
    {
        Pix gun(8, 8);
        gun.rect(1, 3, 6, 2, 255, 180, 40);
        gun.rect(5, 1, 2, 3, 255, 230, 80);
        project.sprites.push_back(makeSprite("gun", "Spread gun", gun, 8, 1, 1));
    }
    {
        Pix door(16, 16);
        paintDoor(door);
        project.sprites.push_back(makeSprite("door", "Exit", door, 16, 1, 1));
    }
    {
        Pix flag(16, 16);
        paintCheckpoint(flag);
        project.sprites.push_back(makeSprite("flag", "Checkpoint", flag, 16, 1, 1));
    }
    {
        Pix drone(16, 16);
        drone.rect(3, 5, 10, 6, 90, 210, 230);
        drone.rect(6, 3, 4, 3, 180, 255, 255);
        drone.rect(2, 7, 2, 2, 255, 80, 80);
        drone.rect(12, 7, 2, 2, 255, 80, 80);
        project.sprites.push_back(makeSprite("drone", "Drone", drone, 16, 1, 1));
    }
    {
        Pix plank(48, 8);
        plank.rect(0, 1, 48, 6, 186, 122, 64);
        plank.rect(0, 0, 48, 2, 230, 180, 90);
        plank.rect(0, 6, 48, 2, 110, 70, 40);
        project.sprites.push_back(makeSprite("plank", "Lift", plank, 48, 1, 1));
    }

    Pix sheet(128, 32);
    auto cell = [&](int index, auto painter) {
        const int col = index % 8;
        const int row = index / 8;
        painter(sheet, col * 16, row * 16);
    };
    cell(0, [](Pix& img, int x, int y) {
        img.rect(x, y, 16, 16, 150, 86, 62);
        img.rect(x, y + 7, 16, 1, 70, 40, 30);
        img.rect(x + 7, y, 1, 7, 70, 40, 30);
        img.rect(x, y + 15, 16, 1, 50, 30, 24);
    });
    cell(1, [](Pix& img, int x, int y) {
        img.rect(x, y + 4, 16, 12, 120, 78, 48);
        img.rect(x, y, 16, 4, 70, 180, 80);
        img.rect(x, y + 4, 16, 1, 30, 90, 40);
    });
    cell(2, [](Pix& img, int x, int y) {
        img.rect(x + 3, y, 2, 16, 190, 150, 70);
        img.rect(x + 11, y, 2, 16, 190, 150, 70);
        for (int rung = 2; rung < 16; rung += 4) img.rect(x + 3, y + rung, 10, 2, 210, 170, 90);
    });
    cell(3, [](Pix& img, int x, int y) {
        for (int i = 0; i < 3; ++i) {
            img.rect(x + 2 + i * 5, y + 10, 3, 5, 200, 60, 70);
            img.rect(x + 3 + i * 5, y + 6, 1, 4, 230, 80, 90);
        }
    });
    cell(4, [](Pix& img, int x, int y) {
        img.rect(x + 1, y + 1, 14, 14, 160, 110, 60);
        img.rect(x + 3, y + 3, 10, 1, 90, 60, 30);
        img.rect(x + 3, y + 3, 1, 10, 90, 60, 30);
        img.rect(x + 3, y + 12, 10, 1, 90, 60, 30);
        img.rect(x + 12, y + 3, 1, 10, 90, 60, 30);
    });
    cell(5, [](Pix& img, int x, int y) {
        img.rect(x + 7, y + 2, 2, 2, 255, 230, 120);
        img.rect(x + 6, y + 4, 4, 2, 255, 230, 120);
        img.rect(x + 7, y + 6, 2, 2, 255, 230, 120);
    });
    cell(6, [](Pix& img, int x, int y) {
        img.rect(x, y, 16, 16, 110, 120, 140);
        img.rect(x, y + 15, 16, 1, 50, 60, 80);
        img.rect(x + 4, y + 4, 2, 2, 220, 230, 240);
        img.rect(x + 10, y + 9, 2, 2, 220, 230, 240);
    });
    cell(7, [](Pix& img, int x, int y) {
        img.rect(x, y, 16, 16, 40, 70, 170);
        img.rect(x + 1, y + 1, 14, 6, 70, 110, 220);
        img.rect(x + 1, y + 9, 14, 6, 30, 50, 130);
    });
    cell(8, [](Pix& img, int x, int y) {
        img.rect(x, y, 16, 16, 40, 90, 180);
        img.rect(x, y + 4, 16, 2, 140, 200, 255);
        img.rect(x, y + 11, 16, 2, 20, 60, 140);
    });
    cell(9, [](Pix& img, int x, int y) {
        img.rect(x, y, 16, 16, 40, 36, 64);
        img.rect(x, y + 8, 16, 1, 20, 16, 32);
    });
    cell(10, [](Pix& img, int x, int y) {
        img.rect(x, y, 16, 16, 170, 220, 230);
        img.rect(x, y + 8, 16, 1, 220, 250, 255);
    });
    cell(11, [](Pix& img, int x, int y) {
        img.rect(x, y, 16, 16, 150, 86, 62);
        img.rect(x + 2, y + 4, 6, 1, 60, 30, 24);
        img.rect(x + 8, y + 9, 6, 1, 60, 30, 24);
        img.rect(x + 5, y + 6, 1, 5, 60, 30, 24);
    });

    Tileset tileset;
    tileset.id = "main";
    tileset.name = "Cabinet tiles";
    tileset.tileW = 16;
    tileset.tileH = 16;
    tileset.columns = 8;
    tileset.imgW = sheet.w;
    tileset.imgH = sheet.h;
    tileset.rgba = sheet.rgba;
    tileset.tiles.resize(12);
    tileset.tiles[0].flags = TileSolid;
    tileset.tiles[1].flags = TileOneWay;
    tileset.tiles[2].flags = TileLadder;
    tileset.tiles[3].flags = TileHazard;
    tileset.tiles[3].damage = 1;
    tileset.tiles[4].flags = TileSolid | TileDestructible;
    tileset.tiles[6].flags = TileSolid;
    tileset.tiles[7].flags = TileSolid;
    tileset.tiles[8].flags = TileWater;
    tileset.tiles[8].frames = 1;
    tileset.tiles[10].flags = TileSolid | TileIce;
    tileset.tiles[11].flags = TileSolid | TileCrumble;
    project.tilesets.push_back(tileset);

    auto add = [&](ActorDef actor) { project.actors.push_back(std::move(actor)); };
    {
        ActorDef actor;
        actor.id = "runner";
        actor.name = "Runner";
        actor.kind = ActorKind::Hero;
        actor.spriteId = "hero";
        actor.projectileId = "bullet";
        actor.hitW = 10;
        actor.hitH = 14;
        actor.behavior.speed = 96;
        actor.behavior.fireInterval = 0.28f;
        actor.behavior.hp = 3;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "ship";
        actor.name = "Fighter";
        actor.kind = ActorKind::Hero;
        actor.spriteId = "ship";
        actor.projectileId = "bullet";
        actor.hitW = 12;
        actor.hitH = 12;
        actor.behavior.fireInterval = 0.16f;
        actor.behavior.gravity = false;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "bullet";
        actor.name = "Player gun";
        actor.kind = ActorKind::Gun;
        actor.spriteId = "bullet";
        actor.hitW = 4;
        actor.hitH = 4;
        actor.behavior.shotSpeed = 240;
        actor.behavior.lifetime = 1.15f;
        actor.behavior.touchDamage = 1;
        actor.behavior.gravity = false;
        actor.behavior.burst = 1;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "spread";
        actor.name = "Spread gun";
        actor.kind = ActorKind::Gun;
        actor.spriteId = "bullet";
        actor.hitW = 4;
        actor.hitH = 4;
        actor.behavior.shotSpeed = 220;
        actor.behavior.lifetime = 1.0f;
        actor.behavior.touchDamage = 1;
        actor.behavior.gravity = false;
        actor.behavior.burst = 3;
        actor.behavior.spread = 0.45f;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "ebullet";
        actor.name = "Enemy gun";
        actor.kind = ActorKind::Gun;
        actor.spriteId = "ebullet";
        actor.hitW = 4;
        actor.hitH = 4;
        actor.behavior.shotSpeed = 110;
        actor.behavior.lifetime = 2.2f;
        actor.behavior.touchDamage = 1;
        actor.behavior.gravity = false;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "slime";
        actor.name = "Slime";
        actor.kind = ActorKind::Enemy;
        actor.spriteId = "slime";
        actor.hitW = 12;
        actor.hitH = 10;
        actor.behavior.pattern = AiPattern::Patrol;
        actor.behavior.speed = 34;
        actor.behavior.hp = 2;
        actor.behavior.score = 100;
        actor.behavior.gravity = true;
        actor.behavior.stomp = true;
        actor.dropActorId = "coin";
        actor.dropChance = 0.4f;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "bat";
        actor.name = "Bat";
        actor.kind = ActorKind::Enemy;
        actor.spriteId = "bat";
        actor.hitW = 12;
        actor.hitH = 8;
        actor.behavior.pattern = AiPattern::Fly;
        actor.behavior.speed = 52;
        actor.behavior.gravity = false;
        actor.behavior.score = 150;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "crab";
        actor.name = "Crab";
        actor.kind = ActorKind::Enemy;
        actor.spriteId = "crab";
        actor.hitW = 12;
        actor.hitH = 10;
        actor.behavior.pattern = AiPattern::Hopper;
        actor.behavior.speed = 40;
        actor.behavior.period = 1.3f;
        actor.behavior.hp = 2;
        actor.behavior.score = 200;
        actor.behavior.gravity = true;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "turret";
        actor.name = "Turret";
        actor.kind = ActorKind::Enemy;
        actor.spriteId = "turret";
        actor.projectileId = "ebullet";
        actor.hitW = 12;
        actor.hitH = 12;
        actor.behavior.pattern = AiPattern::Turret;
        actor.behavior.gravity = false;
        actor.behavior.hp = 4;
        actor.behavior.fireInterval = 1.35f;
        actor.behavior.shotSpeed = 100;
        actor.behavior.aim = AimMode::AtPlayer;
        actor.behavior.score = 250;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "drone";
        actor.name = "Drone";
        actor.kind = ActorKind::Enemy;
        actor.spriteId = "drone";
        actor.hitW = 12;
        actor.hitH = 8;
        actor.behavior.pattern = AiPattern::Sine;
        actor.behavior.gravity = false;
        actor.behavior.speed = 36;
        actor.behavior.amplitude = 36;
        actor.behavior.period = 2.4f;
        actor.behavior.score = 150;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "diver";
        actor.name = "Diver";
        actor.kind = ActorKind::Enemy;
        actor.spriteId = "bat";
        actor.hitW = 12;
        actor.hitH = 8;
        actor.behavior.pattern = AiPattern::Dive;
        actor.behavior.gravity = false;
        actor.behavior.speed = 110;
        actor.behavior.score = 200;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "core";
        actor.name = "Cabinet Core";
        actor.kind = ActorKind::Boss;
        actor.spriteId = "boss";
        actor.projectileId = "ebullet";
        actor.hitW = 26;
        actor.hitH = 22;
        actor.behavior.pattern = AiPattern::Boss;
        actor.behavior.gravity = false;
        actor.behavior.hp = 22;
        actor.behavior.speed = 36;
        actor.behavior.fireInterval = 0.55f;
        actor.behavior.shotSpeed = 90;
        actor.behavior.burst = 5;
        actor.behavior.spread = 1.15f;
        actor.behavior.aim = AimMode::AtPlayer;
        actor.behavior.score = 2500;
        actor.behavior.touchDamage = 1;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "ghost";
        actor.name = "Ghost";
        actor.kind = ActorKind::Enemy;
        actor.spriteId = "ghost";
        actor.hitW = 12;
        actor.hitH = 12;
        actor.behavior.pattern = AiPattern::MazeGhost;
        actor.behavior.speed = 58;
        actor.behavior.gravity = false;
        actor.behavior.touchDamage = 1;
        actor.behavior.score = 200;
        add(actor);
    }
    auto item = [&](const char* id, const char* name, const char* sprite, Pickup pickup, int amount) {
        ActorDef actor;
        actor.id = id;
        actor.name = name;
        actor.kind = ActorKind::Item;
        actor.spriteId = sprite;
        actor.pickup = pickup;
        actor.pickupAmount = amount;
        actor.hitW = 8;
        actor.hitH = 8;
        actor.behavior.gravity = false;
        actor.behavior.touchDamage = 0;
        add(actor);
    };
    item("coin", "Coin", "coin", Pickup::Coin, 50);
    item("heart", "Heart", "heart", Pickup::Heal, 1);
    item("life", "Extra life", "heart", Pickup::Life, 1);
    item("pellet", "Pellet", "pellet", Pickup::Coin, 10);
    item("power", "Power pellet", "power", Pickup::PowerPellet, 50);
    item("key", "Key", "key", Pickup::Key, 1);
    {
        ActorDef actor;
        actor.id = "spread_pack";
        actor.name = "Spread pickup";
        actor.kind = ActorKind::Item;
        actor.spriteId = "gun";
        actor.pickup = Pickup::Weapon;
        actor.projectileId = "spread";
        actor.hitW = 8;
        actor.hitH = 8;
        actor.behavior.touchDamage = 0;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "bomb_pack";
        actor.name = "Screen bomb";
        actor.kind = ActorKind::Item;
        actor.spriteId = "gun";
        actor.pickup = Pickup::Special;
        actor.pickupAmount = 2;
        actor.hitW = 8;
        actor.hitH = 8;
        actor.behavior.gravity = false;
        actor.behavior.touchDamage = 0;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "lift";
        actor.name = "Lift";
        actor.kind = ActorKind::Decoration;
        actor.spriteId = "plank";
        actor.hitW = 48;
        actor.hitH = 8;
        actor.behavior.pattern = AiPattern::Platform;
        actor.behavior.aim = AimMode::Facing;
        actor.behavior.speed = 36;
        actor.behavior.amplitude = 48;
        actor.behavior.gravity = false;
        actor.behavior.touchDamage = 0;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "door";
        actor.name = "Exit";
        actor.kind = ActorKind::Check;
        actor.spriteId = "door";
        actor.hitW = 12;
        actor.hitH = 16;
        actor.needsKey = false;
        actor.behavior.touchDamage = 0;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "locked";
        actor.name = "Locked exit";
        actor.kind = ActorKind::Check;
        actor.spriteId = "door";
        actor.hitW = 12;
        actor.hitH = 16;
        actor.needsKey = true;
        actor.behavior.touchDamage = 0;
        add(actor);
    }
    {
        ActorDef actor;
        actor.id = "checkpoint";
        actor.name = "Checkpoint";
        actor.kind = ActorKind::Check;
        actor.spriteId = "flag";
        actor.checkpoint = true;
        actor.hitW = 10;
        actor.hitH = 16;
        actor.behavior.touchDamage = 0;
        add(actor);
    }
    addStandardSounds(project);
}

void place(Project& project, Stage& stage, const std::string& actor, float x, float y) {
    Placement placement;
    placement.uid = project.freshUid();
    placement.actorId = actor;
    placement.x = x;
    placement.y = y;
    stage.placements.push_back(placement);
}

void paintTile(Stage& stage, int x, int y, uint16_t id) { stage.set(stage.main, x, y, id); }

Stage blankStage(const std::string& id, const std::string& name, int w, int h) {
    Stage stage;
    stage.id = id;
    stage.name = name;
    stage.tilesetId = "main";
    stage.resize(w, h);
    return stage;
}

void buildMaze(Project& project, Stage& stage) {
    const int w = 21;
    const int h = 21;
    stage.resize(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) paintTile(stage, x, y, 8);
    std::vector<uint8_t> open(static_cast<size_t>(w) * h, 0);
    std::mt19937 rng(7);
    std::function<void(int, int)> carve = [&](int x, int y) {
        open[static_cast<size_t>(y) * w + x] = 1;
        int dirs[4] = {0, 1, 2, 3};
        std::shuffle(dirs, dirs + 4, rng);
        const int ox[4] = {1, -1, 0, 0};
        const int oy[4] = {0, 0, 1, -1};
        for (int dir : dirs) {
            const int nx = x + ox[dir] * 2;
            const int ny = y + oy[dir] * 2;
            if (nx <= 0 || ny <= 0 || nx >= w - 1 || ny >= h - 1) continue;
            if (open[static_cast<size_t>(ny) * w + nx]) continue;
            open[static_cast<size_t>(y + oy[dir]) * w + (x + ox[dir])] = 1;
            carve(nx, ny);
        }
    };
    carve(1, 1);
    for (int y = 9; y <= 11; ++y)
        for (int x = 9; x <= 11; ++x) open[static_cast<size_t>(y) * w + x] = 1;
    open[static_cast<size_t>(12) * w + 10] = 1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (open[static_cast<size_t>(y) * w + x]) paintTile(stage, x, y, 0);
    const int px = 1;
    const int py = h - 2;
    place(project, stage, "runner", px * 16.f, py * 16.f);
    place(project, stage, "ghost", 9 * 16.f, 9 * 16.f);
    place(project, stage, "ghost", 11 * 16.f, 9 * 16.f);
    place(project, stage, "ghost", 10 * 16.f, 11 * 16.f);
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            if (!open[static_cast<size_t>(y) * w + x]) continue;
            if (x >= 9 && x <= 11 && y >= 9 && y <= 11) continue;
            if (x == px && y == py) continue;
            const bool corner = (x == 1 && y == 1) || (x == w - 2 && y == 1) || (x == 1 && y == h - 2) ||
                                (x == w - 2 && y == h - 2);
            place(project, stage, corner ? "power" : "pellet", x * 16.f + 4.f, y * 16.f + 4.f);
        }
    }
}

}  // namespace

Project makeTemplate(PhysicsMode mode, const std::string& title) {
    Project project;
    project.settings.title = title;
    project.settings.author = "CabinetCore";
    project.settings.crt = true;
    addCoreAssets(project);
    Stage stage = blankStage("stage1", "Stage 1", 30, 15);
    stage.physics = mode;
    stage.musicId = "music_run";
    if (mode == PhysicsMode::ShooterVert) {
        stage = blankStage("stage1", "Stage 1", 20, 40);
        stage.physics = mode;
        stage.scroll = ScrollMode::AutoUp;
        stage.complete = CompleteMode::ReachEnd;
        stage.autoScroll = 30;
        stage.playerSpeed = 120;
        stage.musicId = "music_shoot";
        place(project, stage, "ship", 9 * 16.f, 36 * 16.f);
    } else if (mode == PhysicsMode::ShooterHorz) {
        stage = blankStage("stage1", "Stage 1", 48, 15);
        stage.physics = mode;
        stage.scroll = ScrollMode::AutoRight;
        stage.complete = CompleteMode::ReachEnd;
        stage.autoScroll = 30;
        stage.playerSpeed = 120;
        stage.musicId = "music_shoot";
        place(project, stage, "ship", 16.f, 6 * 16.f);
    } else if (mode == PhysicsMode::Maze) {
        stage = blankStage("stage1", "Stage 1", 21, 21);
        stage.physics = PhysicsMode::Maze;
        stage.complete = CompleteMode::CollectAll;
        stage.playerSpeed = 74;
        stage.musicId = "music_maze";
        buildMaze(project, stage);
    } else if (mode == PhysicsMode::TopDown) {
        stage.physics = PhysicsMode::TopDown;
        stage.playerSpeed = 100;
        for (int x = 0; x < stage.width; ++x) {
            paintTile(stage, x, 0, 7);
            paintTile(stage, x, stage.height - 1, 7);
        }
        for (int y = 0; y < stage.height; ++y) {
            paintTile(stage, 0, y, 7);
            paintTile(stage, stage.width - 1, y, 7);
        }
        place(project, stage, "runner", 32.f, 32.f);
        place(project, stage, "door", (stage.width - 3) * 16.f, (stage.height - 3) * 16.f);
    } else {
        stage.physics = mode;
        stage.playerSpeed = 96;
        stage.jumpVel = 300;
        stage.gravity = 980;
        for (int x = 0; x < stage.width; ++x) paintTile(stage, x, 13, 1);
        place(project, stage, "runner", 32.f, 12 * 16.f);
        place(project, stage, "door", (stage.width - 3) * 16.f, 12 * 16.f);
        place(project, stage, "slime", 8 * 16.f, 12 * 16.f);
    }
    project.stages.push_back(stage);
    return project;
}

Project makeDemo() {
    Project project = makeTemplate(PhysicsMode::Platform, "Neon Cabinet");
    project.stages.clear();
    project.nextUid = 1;

    Stage roofs = blankStage("rooftops", "Rooftops", 48, 15);
    roofs.physics = PhysicsMode::RunAndGun;
    roofs.scroll = ScrollMode::Follow;
    roofs.complete = CompleteMode::TouchExit;
    roofs.playerSpeed = 96;
    roofs.jumpVel = 300;
    roofs.gravity = 980;
    roofs.musicId = "music_run";
    roofs.message = "Cross the roofs.";
    for (int y = 0; y < roofs.height; ++y)
        for (int x = 0; x < roofs.width; ++x)
            if ((x + y) % 7 == 0) roofs.set(roofs.back, x, y, 10);
    for (int x = 0; x < roofs.width; ++x) {
        paintTile(roofs, x, 13, 1);
        paintTile(roofs, x, 14, 1);
    }
    for (int x = 26; x <= 29; ++x) {
        paintTile(roofs, x, 13, 0);
        paintTile(roofs, x, 14, 4);
    }
    auto platform = [&](int x, int y, int len) {
        for (int i = 0; i < len; ++i) paintTile(roofs, x + i, y, 2);
    };
    platform(6, 10, 4);
    platform(14, 8, 4);
    platform(32, 10, 5);
    platform(39, 8, 4);
    for (int y = 7; y <= 12; ++y) paintTile(roofs, 18, y, 3);
    paintTile(roofs, 10, 12, 5);
    paintTile(roofs, 11, 12, 5);
    paintTile(roofs, 20, 13, 12);
    paintTile(roofs, 21, 13, 12);
    place(project, roofs, "lift", 25 * 16.f, 13 * 16.f);
    place(project, roofs, "bomb_pack", 30 * 16.f, 12 * 16.f);
    place(project, roofs, "runner", 2 * 16.f, 12 * 16.f);
    place(project, roofs, "coin", 7 * 16.f, 9 * 16.f);
    place(project, roofs, "coin", 9 * 16.f, 9 * 16.f);
    place(project, roofs, "key", 15 * 16.f, 7 * 16.f);
    place(project, roofs, "spread_pack", 40 * 16.f, 7 * 16.f);
    place(project, roofs, "heart", 34 * 16.f, 9 * 16.f);
    place(project, roofs, "slime", 8 * 16.f, 12 * 16.f);
    place(project, roofs, "crab", 34 * 16.f, 12 * 16.f);
    place(project, roofs, "bat", 20 * 16.f, 6 * 16.f);
    place(project, roofs, "checkpoint", 22 * 16.f, 12 * 16.f);
    place(project, roofs, "coin", 12 * 16.f, 12 * 16.f);
    place(project, roofs, "locked", 45 * 16.f, 12 * 16.f);
    project.stages.push_back(roofs);

    Stage ready = blankStage("ready_raid", "Get Ready", 20, 15);
    ready.intermission = true;
    ready.message = "STAR RAID\nCLIMB THE SHAFT\nDESTROY THE CORE";
    project.stages.push_back(ready);

    Stage raid = blankStage("raid", "Star Raid", 20, 56);
    raid.physics = PhysicsMode::ShooterVert;
    raid.scroll = ScrollMode::AutoUp;
    raid.complete = CompleteMode::BossDead;
    raid.autoScroll = 34;
    raid.playerSpeed = 125;
    raid.musicId = "music_shoot";
    std::mt19937 rng(3);
    for (int i = 0; i < 80; ++i) {
        const int x = static_cast<int>(rng() % raid.width);
        const int y = static_cast<int>(rng() % raid.height);
        raid.set(raid.back, x, y, 6);
    }
    place(project, raid, "ship", 9 * 16.f, 52 * 16.f);
    for (int i = 0; i < 10; ++i) {
        const float x = (2 + (i % 3) * 6) * 16.f;
        const float y = (48 - i * 4) * 16.f;
        place(project, raid, i % 4 == 3 ? "diver" : "drone", x, y);
    }
    place(project, raid, "turret", 3 * 16.f, 30 * 16.f);
    place(project, raid, "turret", 14 * 16.f, 18 * 16.f);
    place(project, raid, "heart", 9 * 16.f, 24 * 16.f);
    place(project, raid, "core", 5 * 16.f, 3 * 16.f);
    project.stages.push_back(raid);

    Stage readyMaze = blankStage("ready_maze", "Get Ready", 20, 15);
    readyMaze.intermission = true;
    readyMaze.message = "EAT EVERY PELLET\nPOWER PELLETS SCARE GHOSTS";
    project.stages.push_back(readyMaze);

    Stage maze = blankStage("maze", "Pellet Pit", 21, 21);
    maze.physics = PhysicsMode::Maze;
    maze.scroll = ScrollMode::Follow;
    maze.complete = CompleteMode::CollectAll;
    maze.playerSpeed = 74;
    maze.musicId = "music_maze";
    buildMaze(project, maze);
    project.stages.push_back(maze);
    return project;
}

}  // namespace cc
