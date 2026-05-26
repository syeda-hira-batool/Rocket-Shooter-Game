#include "raylib.h"
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <ctime>

//   CONSTANTS
const int SW = 600;
const int SH = 800;
const int FPS = 60;
const float PLAYER_SPD = 280.f;
const float BULLET_SPD = 520.f;
const float ENEMY_BULLET_SPD = 140.f;

const float STORY_PAGE_TIME = 4.5f;

//  ENUMS
enum GameScreen
{
    SCR_STORY_INTRO,
    SCR_GAMEPLAY,
    SCR_STORY_OUTRO,
    SCR_BOSS_TAUNT,    // boss laughs after player dies
    SCR_BOSS_COLLAPSE, // boss slow-mo collapse after player wins
    SCR_GAMEOVER
};

enum PowerUpType
{
    PU_DOUBLE_SHOT,
    PU_TRIPLE_SHOT,
    PU_RAPID_FIRE,
    PU_SHIELD,
    PU_EXTRA_LIFE,
    PU_COUNT
};

//  STRUCTS
struct Star
{
    float x, y, speed, size;
};
struct Bullet
{
    float x, y;
    bool active;
};
struct EnemyBullet
{
    float x, y, vx, vy;
    bool active;
};

struct Explosion
{
    float x, y, radius, maxRadius, alpha;
    bool active;
};

struct Enemy
{
    float x, y;
    float baseX;
    float moveTimer;
    int hp, maxHp;
    bool alive;
    float shootTimer, shootRate;
};

struct Boss
{
    float x, y, vx;
    int hp, maxHp;
    bool alive;
    int phase;
    float shootTimer, shootRate;
    bool shieldActive;
};

struct Player
{
    float x, y;
    int lives, score;
    bool alive;
    float shootTimer, shootRate;
    int shotType;
    bool hasShield;
    float invTimer;
};

struct PowerOrb
{
    float x, y;
    PowerUpType type;
    bool active;
    float bobTimer;
};

//  PROCEDURAL SOUND
Sound SoundShoot;
Sound SoundExplosion;
Sound SoundGameOver;
Sound SoundLevelUp;
Sound SoundHit;
Sound SoundStart;
Sound SoundPickup;

Music BgMusic;
bool bgMusicLoaded = false;

static Sound MakeToneSound(float freqHz, float freqEnd,
                           float durationSec, float volume,
                           int waveType)
{
    const int sampleRate = 44100;
    int samples = (int)(sampleRate * durationSec);
    short *data = (short *)MemAlloc(samples * sizeof(short));
    for (int i = 0; i < samples; i++)
    {
        float t = (float)i / sampleRate;
        float prog = (float)i / samples;
        float freq = freqHz + (freqEnd - freqHz) * prog;
        float env = 1.0f - prog;
        float val = 0.f;
        if (waveType == 0)
            val = sinf(2.f * 3.14159f * freq * t);
        else if (waveType == 1)
            val = fmodf(freq * t, 1.f) > 0.5f ? 1.f : -1.f;
        else
            val = ((float)(rand() % 32767) / 16383.f) - 1.f;
        data[i] = (short)(val * env * volume * 32000.f);
    }
    Wave w;
    w.frameCount = samples;
    w.sampleRate = sampleRate;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data;
    Sound s = LoadSoundFromWave(w);
    MemFree(data);
    return s;
}

void InitSounds()
{
    SoundShoot = MakeToneSound(800, 300, 0.12f, 0.4f, 1);
    SoundExplosion = MakeToneSound(200, 40, 0.35f, 0.7f, 2);
    SoundGameOver = MakeToneSound(300, 80, 1.2f, 0.8f, 0);
    SoundLevelUp = MakeToneSound(400, 1200, 0.5f, 0.6f, 0);
    SoundHit = MakeToneSound(150, 80, 0.2f, 0.6f, 2);
    SoundStart = MakeToneSound(45, 200, 0.8f, 0.8f, 1);
    SoundPickup = MakeToneSound(600, 1100, 0.18f, 0.5f, 0);

    BgMusic = LoadMusicStream("calango_fx_official-tela-inicio-402266.mp3");
    if (BgMusic.stream.buffer != nullptr)
    {
        bgMusicLoaded = true;
        BgMusic.looping = true;
        SetMusicVolume(BgMusic, 0.35f);
        TraceLog(LOG_INFO, "BG music loaded successfully.");
    }
    else
    {
        bgMusicLoaded = false;
        TraceLog(LOG_WARNING, "BG music NOT found — running without it.");
    }
}

void UnloadSounds()
{
    UnloadSound(SoundShoot);
    UnloadSound(SoundExplosion);
    UnloadSound(SoundGameOver);
    UnloadSound(SoundLevelUp);
    UnloadSound(SoundHit);
    UnloadSound(SoundStart);
    UnloadSound(SoundPickup);
    if (bgMusicLoaded)
        UnloadMusicStream(BgMusic);
}

//  GLOBAL STATE
GameScreen screen = SCR_STORY_INTRO;
int level = 1;
float storyTimer = 0.f;
int storyPage = 0;
bool gameOverSoundPlayed = false;

// Taunt / collapse screen timers
float tauntTimer = 0.f;    // how long taunt screen has shown
float collapseTimer = 0.f; // how long collapse anim has run
bool tauntSoundPlayed = false;
bool collapseSoundPlayed = false;

// Collapse animation state
float collapseY = 0.f;     // boss Y during fall
float collapseAngle = 0.f; // boss rotation during fall
float collapseAlpha = 1.f; // boss fade out

Player player;
Boss boss;

std::vector<Enemy> enemies;
std::vector<Bullet> bullets;
std::vector<EnemyBullet> eBullets;
std::vector<Explosion> explosions;
std::vector<Star> stars;
std::vector<PowerOrb> orbs;
int orbBudget = 3;

Texture2D texPlayer;
Texture2D texEnemy;
Texture2D texBoss;
Texture2D texBackground;
float bgScrollY = 0.f;

//  STORY TEXT
const char *introPages[] = {
    "The year is 3157.",
    "Without warning, the Ugutna Empire\ndescended from the void\nand captured the Earth.",
    "Billions of humans were captured\nand enslaved on the alien homeworld: Ugutna Prime.",
    "You alone escaped.\nDrifting in the last fighter ship,\narmed with a single cannon.",
    "You are humanity's last hope.\n\n-- Press ENTER to begin --"};
const int introPageCount = 5;

const char *outroPages[] = {
    "The Emperor has fallen.",
    "His empire crumbles.\nAcross the galaxy,\nprison doors swing open.",
    "One by one,\nthe humans walk free.",
    "You did it.\n\nHumanity lives again.",
    "-- Press ENTER to continue --"};
const int outroPageCount = 5;

//  FORWARD DECLARATIONS
void InitGame();
void InitStars();
void SpawnEnemies(int lvl);
void SpawnBoss();
void SpawnExplosion(float x, float y, float maxR);
void TryDropOrb(float x, float y);
void ApplyPowerUp(PowerUpType pu);

void UpdateStars(float dt);
void UpdateStory(float dt);
void UpdateGameplay(float dt);
void UpdateOutro(float dt);
void UpdateGameOver();
void UpdateBossTaunt(float dt);
void UpdateBossCollapse(float dt);

void DrawBackground();
void DrawStars();
void DrawHUD();
void DrawStory(const char **pages, int count);
void DrawGameplay();
void DrawGameOver();
void DrawBossTaunt();
void DrawBossCollapse();

//  ORB HELPERS
Color OrbColor(PowerUpType pu)
{
    switch (pu)
    {
    case PU_SHIELD:
        return {0, 160, 255, 255};
    case PU_TRIPLE_SHOT:
        return {220, 40, 40, 255};
    case PU_DOUBLE_SHOT:
        return {180, 60, 255, 255};
    case PU_EXTRA_LIFE:
        return {255, 220, 0, 255};
    case PU_RAPID_FIRE:
        return {255, 130, 0, 255};
    default:
        return WHITE;
    }
}
const char *OrbLabel(PowerUpType pu)
{
    switch (pu)
    {
    case PU_SHIELD:
        return "SHIELD";
    case PU_TRIPLE_SHOT:
        return "x3";
    case PU_DOUBLE_SHOT:
        return "x2";
    case PU_EXTRA_LIFE:
        return "+LIFE";
    case PU_RAPID_FIRE:
        return "FAST";
    default:
        return "?";
    }
}

//  INIT
void InitGame()
{
    PlaySound(SoundStart);
    player.x = SW / 2.f;
    player.y = SH - 100.f;
    player.lives = 5;
    player.score = 0;
    player.alive = true;
    player.shootTimer = 0.f;
    player.shootRate = 0.25f;
    player.shotType = 1;
    player.hasShield = false;
    player.invTimer = 0.f;

    bullets.clear();
    eBullets.clear();
    explosions.clear();
    orbs.clear();

    level = 1;
    screen = SCR_STORY_INTRO;
    storyPage = 0;
    storyTimer = 0.f;
    gameOverSoundPlayed = false;
    tauntSoundPlayed = false;
    collapseSoundPlayed = false;
    tauntTimer = 0.f;
    collapseTimer = 0.f;

    SpawnEnemies(level);
}

void InitStars()
{
    stars.clear();
    for (int i = 0; i < 120; i++)
    {
        Star s;
        s.x = (float)(rand() % SW);
        s.y = (float)(rand() % SH);
        s.speed = 40.f + (rand() % 80);
        s.size = 1.f + (rand() % 3);
        stars.push_back(s);
    }
}

//  SPAWN
void SpawnEnemies(int lvl)
{
    enemies.clear();
    eBullets.clear();
    orbs.clear();
    orbBudget = 2 + rand() % 2;

    if (lvl == 5)
    {
        SpawnBoss();
        return;
    }

    int cols = 6;
    int rows = 2 + lvl;
    float base = 2.5f - lvl * 0.2f;

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            Enemy e;
            e.x = 60.f + c * 82.f;
            e.y = 80.f + r * 64.f;
            e.baseX = e.x;
            e.moveTimer = (float)(rand() % 628) / 100.f;
            e.hp = 1 + (lvl / 2);
            e.maxHp = e.hp;
            e.alive = true;
            e.shootRate = base + (float)(rand() % 10000) / 500.f;
            e.shootTimer = e.shootRate * ((float)rand() / RAND_MAX);
            enemies.push_back(e);
        }
    }
}

void SpawnBoss()
{
    boss.x = SW / 2.f;
    boss.y = 120.f;
    boss.vx = 110.f;
    boss.maxHp = 500;
    boss.hp = boss.maxHp;
    boss.alive = true;
    boss.phase = 1;
    boss.shootRate = 0.9f;
    boss.shootTimer = boss.shootRate;
    boss.shieldActive = false;

    int cols = 5;
    float spacing = 90.f;
    float startX = SW / 2.f - (cols - 1) * spacing / 2.f;
    for (int r = 0; r < 2; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            Enemy e;
            e.x = startX + c * spacing;
            e.y = 240.f + r * 70.f;
            e.baseX = e.x;
            e.moveTimer = (float)(rand() % 628) / 100.f;
            e.hp = 3;
            e.maxHp = 3;
            e.alive = true;
            e.shootRate = 1.4f + (float)(rand() % 1000) / 50.f;
            e.shootTimer = e.shootRate * ((float)rand() / RAND_MAX);
            enemies.push_back(e);
        }
    }
}

void SpawnExplosion(float x, float y, float maxR)
{
    Explosion ex;
    ex.x = x;
    ex.y = y;
    ex.radius = 4.f;
    ex.maxRadius = maxR;
    ex.alpha = 1.f;
    ex.active = true;
    explosions.push_back(ex);
}

void TryDropOrb(float x, float y)
{
    if (orbBudget <= 0)
        return;
    if (rand() % 100 >= 25)
        return;
    orbBudget--;
    PowerOrb orb;
    orb.x = x;
    orb.y = y;
    orb.type = (PowerUpType)(rand() % (int)PU_COUNT);
    orb.active = true;
    orb.bobTimer = (float)(rand() % 628) / 100.f;
    orbs.push_back(orb);
}

void ApplyPowerUp(PowerUpType pu)
{
    switch (pu)
    {
    case PU_DOUBLE_SHOT:
        player.shotType = 2;
        break;
    case PU_TRIPLE_SHOT:
        player.shotType = 3;
        break;
    case PU_RAPID_FIRE:
        player.shootRate *= 0.5f;
        break;
    case PU_SHIELD:
        player.hasShield = true;
        break;
    case PU_EXTRA_LIFE:
        player.lives++;
        break;
    default:
        break;
    }
}

//  UPDATE — STARS
void UpdateStars(float dt)
{
    for (auto &s : stars)
    {
        s.y += s.speed * dt;
        if (s.y > SH)
        {
            s.y = 0;
            s.x = (float)(rand() % SW);
        }
    }
    bgScrollY += 40.f * dt;
    if (bgScrollY >= SH)
        bgScrollY -= SH;
}

//  UPDATE — STORY
void UpdateStory(float dt)
{
    storyTimer += dt;
    bool isLastPage = (storyPage == introPageCount - 1);
    if (isLastPage)
    {
        if (IsKeyPressed(KEY_ENTER))
        {
            screen = SCR_GAMEPLAY;
            storyPage = 0;
            storyTimer = 0.f;
        }
    }
    else
    {
        if (storyTimer >= STORY_PAGE_TIME || IsKeyPressed(KEY_ENTER))
        {
            storyPage++;
            storyTimer = 0.f;
        }
    }
}

//  UPDATE — BOSS TAUNT (shown when player dies)
void UpdateBossTaunt(float dt)
{
    if (!tauntSoundPlayed)
    {
        // Deep evil laugh: low descending square wave
        PlaySound(MakeToneSound(180, 60, 1.5f, 0.9f, 1));
        tauntSoundPlayed = true;
    }
    tauntTimer += dt;
    // After 3.5 seconds auto-advance to game over
    if (tauntTimer >= 3.5f || IsKeyPressed(KEY_ENTER))
    {
        screen = SCR_GAMEOVER;
    }
}

//  UPDATE — BOSS COLLAPSE (shown when player wins)
void UpdateBossCollapse(float dt)
{
    // Slow-motion: dt scaled down to 30% speed
    float sdt = dt * 0.30f;

    if (!collapseSoundPlayed)
    {
        PlaySound(SoundExplosion);
        // Spawn a chain of big explosions around the boss
        for (int i = 0; i < 6; i++)
            SpawnExplosion(
                boss.x + (float)(rand() % 160 - 80),
                boss.y + (float)(rand() % 120 - 60),
                60.f + rand() % 40);
        collapseY = boss.y;
        collapseAngle = 0.f;
        collapseAlpha = 1.f;
        collapseSoundPlayed = true;
    }

    collapseTimer += dt; // real time (for UI)

    // Boss slowly tilts and falls down
    collapseAngle += 45.f * sdt; // rotate ~13.5 deg/sec real time
    collapseY += 120.f * sdt;    // fall ~36 px/sec real time
    collapseAlpha -= 0.4f * sdt; // fade ~12% per sec real time
    if (collapseAlpha < 0.f)
        collapseAlpha = 0.f;

    // Keep spawning small explosions during collapse
    if ((int)(collapseTimer * 8) % 2 == 0 && collapseAlpha > 0.1f)
    {
        SpawnExplosion(
            boss.x + (float)(rand() % 120 - 60),
            collapseY + (float)(rand() % 80 - 40),
            30.f + rand() % 30);
    }

    // Update existing explosions (also slow-mo)
    for (auto &ex : explosions)
    {
        if (!ex.active)
            continue;
        ex.radius += 80.f * sdt;
        ex.alpha -= 1.5f * sdt;
        if (ex.alpha <= 0)
        {
            ex.alpha = 0;
            ex.active = false;
        }
    }

    // After 5 real seconds, go to game over (win)
    if (collapseTimer >= 5.0f || IsKeyPressed(KEY_ENTER))
    {
        screen = SCR_GAMEOVER;
    }
}

//  UPDATE — GAMEPLAY
void UpdateGameplay(float dt)
{
    if (!player.alive)
        return;

    float dt_safe = (dt > 0.05f) ? 0.05f : dt;

    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
        player.x -= PLAYER_SPD * dt_safe;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
        player.x += PLAYER_SPD * dt_safe;
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
        player.y -= PLAYER_SPD * dt_safe;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
        player.y += PLAYER_SPD * dt_safe;
    if (player.x < 20)
        player.x = 20;
    if (player.x > SW - 20)
        player.x = SW - 20;
    if (player.y < 20)
        player.y = 20;
    if (player.y > SH - 20)
        player.y = SH - 20;

    if (player.invTimer > 0)
        player.invTimer -= dt_safe;

    player.shootTimer -= dt_safe;
    if ((IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_Z)) && player.shootTimer <= 0)
    {
        player.shootTimer = player.shootRate;
        PlaySound(SoundShoot);
        if (player.shotType == 1)
        {
            bullets.push_back({player.x, player.y - 20, true});
        }
        else if (player.shotType == 2)
        {
            bullets.push_back({player.x - 10, player.y - 20, true});
            bullets.push_back({player.x + 10, player.y - 20, true});
        }
        else
        {
            bullets.push_back({player.x, player.y - 20, true});
            bullets.push_back({player.x - 18, player.y - 10, true});
            bullets.push_back({player.x + 18, player.y - 10, true});
        }
    }

    for (auto &b : bullets)
    {
        if (!b.active)
            continue;
        b.y -= BULLET_SPD * dt_safe;
        if (b.y < -10)
            b.active = false;
    }

    int aliveCount = 0;
    float swayAmp = (level == 5) ? 30.f : 18.f + level * 4.f;
    float swaySpeed = (level == 5) ? 2.0f : 1.2f + level * 0.3f;

    for (auto &e : enemies)
    {
        if (!e.alive)
            continue;
        aliveCount++;
        e.moveTimer += dt_safe * swaySpeed;
        e.x = e.baseX + sinf(e.moveTimer) * swayAmp;
        e.shootTimer -= dt_safe;
        if (e.shootTimer <= 0)
        {
            e.shootTimer = e.shootRate;
            if (level == 5)
            {
                eBullets.push_back({e.x, e.y + 20, 0, ENEMY_BULLET_SPD * 1.2f, true});
                eBullets.push_back({e.x, e.y + 20, -60.f, ENEMY_BULLET_SPD * 1.2f, true});
                eBullets.push_back({e.x, e.y + 20, 60.f, ENEMY_BULLET_SPD * 1.2f, true});
            }
            else if (level <= 2)
            {
                eBullets.push_back({e.x, e.y + 20, 0, ENEMY_BULLET_SPD, true});
            }
            else if (level == 3)
            {
                eBullets.push_back({e.x, e.y + 20, 0, ENEMY_BULLET_SPD, true});
                if (rand() % 2 == 0)
                {
                    eBullets.push_back({e.x, e.y + 20, -50.f, ENEMY_BULLET_SPD, true});
                    eBullets.push_back({e.x, e.y + 20, 50.f, ENEMY_BULLET_SPD, true});
                }
            }
            else
            {
                eBullets.push_back({e.x, e.y + 20, 0, ENEMY_BULLET_SPD, true});
                eBullets.push_back({e.x, e.y + 20, -70.f, ENEMY_BULLET_SPD, true});
                eBullets.push_back({e.x, e.y + 20, 70.f, ENEMY_BULLET_SPD, true});
            }
        }
    }

    for (auto &eb : eBullets)
    {
        if (!eb.active)
            continue;
        eb.x += eb.vx * dt_safe;
        eb.y += eb.vy * dt_safe;
        if (eb.y > SH + 10 || eb.x < -20 || eb.x > SW + 20)
            eb.active = false;
    }

    if (level == 5 && boss.alive)
    {
        float bossSpd = (boss.phase == 1)   ? 110.f
                        : (boss.phase == 2) ? 160.f
                                            : 220.f;
        float dir = (boss.vx >= 0) ? 1.f : -1.f;
        boss.vx = dir * bossSpd;
        boss.x += boss.vx * dt_safe;
        if (boss.x > SW - 70)
        {
            boss.x = SW - 70;
            boss.vx = -fabsf(boss.vx);
        }
        if (boss.x < 70)
        {
            boss.x = 70;
            boss.vx = fabsf(boss.vx);
        }

        float hpRatio = (float)boss.hp / boss.maxHp;
        if (hpRatio < 0.30f)
            boss.phase = 3;
        else if (hpRatio < 0.65f)
            boss.phase = 2;
        else
            boss.phase = 1;

        {
            int escortAlive = 0;
            for (auto &e : enemies)
                if (e.alive)
                    escortAlive++;
            if (escortAlive == 0 && boss.alive)
            {
                int cols2 = 3 + boss.phase;
                float sp2 = (SW - 80.f) / (cols2 - 1);
                for (int c = 0; c < cols2; c++)
                {
                    Enemy e;
                    e.x = 40.f + c * sp2;
                    e.y = 280.f;
                    e.baseX = e.x;
                    e.moveTimer = (float)(rand() % 628) / 100.f;
                    e.hp = boss.phase + 1;
                    e.maxHp = e.hp;
                    e.alive = true;
                    e.shootRate = 1.0f + (float)(rand() % 100) / 100.f;
                    e.shootTimer = e.shootRate * ((float)rand() / RAND_MAX);
                    enemies.push_back(e);
                }
            }
        }

        boss.shootRate = (boss.phase == 1)   ? 0.9f
                         : (boss.phase == 2) ? 0.55f
                                             : 0.28f;
        boss.shootTimer -= dt_safe;
        if (boss.shootTimer <= 0)
        {
            boss.shootTimer = boss.shootRate;
            float bspd = ENEMY_BULLET_SPD * 1.15f;
            eBullets.push_back({boss.x, boss.y + 60, 0, bspd, true});
            eBullets.push_back({boss.x, boss.y + 60, -90.f, bspd, true});
            eBullets.push_back({boss.x, boss.y + 60, 90.f, bspd, true});
            if (boss.phase >= 2)
            {
                float dx = player.x - boss.x;
                float dy = player.y - (boss.y + 60);
                float len = sqrtf(dx * dx + dy * dy);
                if (len > 0)
                {
                    eBullets.push_back({boss.x, boss.y + 60, dx / len * bspd, dy / len * bspd, true});
                }
                eBullets.push_back({boss.x, boss.y + 60, -160.f, bspd, true});
                eBullets.push_back({boss.x, boss.y + 60, 160.f, bspd, true});
            }
            if (boss.phase == 3)
            {
                for (int ang = 0; ang < 8; ang++)
                {
                    float a = ang * (3.14159f * 2.f / 8.f);
                    float vx = cosf(a) * bspd;
                    float vy = sinf(a) * bspd;
                    if (vy < 20.f)
                        vy = 20.f;
                    eBullets.push_back({boss.x, boss.y + 60, vx, vy, true});
                }
            }
        }
    }

    for (auto &ex : explosions)
    {
        if (!ex.active)
            continue;
        ex.radius += 120.f * dt_safe;
        ex.alpha -= 2.2f * dt_safe;
        if (ex.alpha <= 0)
        {
            ex.alpha = 0;
            ex.active = false;
        }
    }

    for (auto &orb : orbs)
    {
        if (!orb.active)
            continue;
        orb.y += 60.f * dt_safe;
        orb.bobTimer += dt_safe * 3.f;
        if (orb.y > SH + 20)
            orb.active = false;
    }

    for (auto &b : bullets)
    {
        if (!b.active)
            continue;
        Rectangle br = {b.x - 3, b.y - 8, 6, 16};
        for (auto &e : enemies)
        {
            if (!e.alive)
                continue;
            Rectangle er = {e.x - 22, e.y - 18, 44, 36};
            if (CheckCollisionRecs(br, er))
            {
                b.active = false;
                e.hp--;
                if (e.hp <= 0)
                {
                    e.alive = false;
                    player.score += 100;
                    SpawnExplosion(e.x, e.y, 40.f);
                    PlaySound(SoundExplosion);
                    TryDropOrb(e.x, e.y);
                }
                break;
            }
        }
        if (level == 5 && boss.alive)
        {
            Rectangle bossr = {boss.x - 60, boss.y - 50, 120, 100};
            if (CheckCollisionRecs(br, bossr))
            {
                b.active = false;
                boss.hp -= 5;
                if (boss.hp <= 0)
                {
                    boss.hp = 0;
                    boss.alive = false;
                    player.score += 5000;
                    // Don't go to outro yet — go to collapse screen first
                    collapseTimer = 0.f;
                    collapseY = boss.y;
                    collapseAngle = 0.f;
                    collapseAlpha = 1.f;
                    collapseSoundPlayed = false;
                    screen = SCR_BOSS_COLLAPSE;
                }
            }
        }
    }

    if (player.invTimer <= 0)
    {
        Rectangle pr = {player.x - 18, player.y - 20, 36, 40};
        for (auto &eb : eBullets)
        {
            if (!eb.active)
                continue;
            Rectangle ebr = {eb.x - 4, eb.y - 8, 8, 16};
            if (CheckCollisionRecs(pr, ebr))
            {
                eb.active = false;
                if (player.hasShield)
                {
                    player.hasShield = false;
                    PlaySound(SoundHit);
                }
                else
                {
                    player.lives--;
                    player.invTimer = 2.0f;
                    SpawnExplosion(player.x, player.y, 30.f);
                    PlaySound(SoundHit);
                    if (player.lives <= 0)
                    {
                        player.alive = false;
                        // Go to taunt screen instead of game over directly
                        tauntTimer = 0.f;
                        tauntSoundPlayed = false;
                        screen = SCR_BOSS_TAUNT;
                    }
                }
                break;
            }
        }
    }

    {
        Rectangle pr = {player.x - 22, player.y - 24, 44, 48};
        for (auto &orb : orbs)
        {
            if (!orb.active)
                continue;
            float orbX = orb.x;
            float orbY = orb.y + sinf(orb.bobTimer) * 5.f;
            float orbR = 13.f;
            float cx = orbX < pr.x ? pr.x : (orbX > pr.x + pr.width ? pr.x + pr.width : orbX);
            float cy = orbY < pr.y ? pr.y : (orbY > pr.y + pr.height ? pr.y + pr.height : orbY);
            float dx = orbX - cx, dy = orbY - cy;
            if (dx * dx + dy * dy < orbR * orbR)
            {
                orb.active = false;
                ApplyPowerUp(orb.type);
                PlaySound(SoundPickup);
                player.score += 50;
            }
        }
    }

    if (level < 5 && aliveCount == 0 && !enemies.empty())
    {
        PlaySound(SoundLevelUp);
        level++;
        bullets.clear();
        eBullets.clear();
        orbs.clear();
        orbBudget = 2 + rand() % 2;
        SpawnEnemies(level);
    }
}

//  UPDATE — OUTRO
void UpdateOutro(float dt)
{
    storyTimer += dt;
    bool isLastPage = (storyPage == outroPageCount - 1);
    if (isLastPage)
    {
        if (IsKeyPressed(KEY_ENTER))
        {
            screen = SCR_GAMEOVER;
            storyPage = 0;
            storyTimer = 0.f;
        }
    }
    else
    {
        if (storyTimer >= STORY_PAGE_TIME || IsKeyPressed(KEY_ENTER))
        {
            storyPage++;
            storyTimer = 0.f;
        }
    }
}

void UpdateGameOver()
{
    if (!gameOverSoundPlayed)
    {
        if (bgMusicLoaded)
            StopMusicStream(BgMusic);
        PlaySound(SoundGameOver);
        gameOverSoundPlayed = true;
    }
    if (IsKeyPressed(KEY_ENTER))
    {
        gameOverSoundPlayed = false;
        tauntSoundPlayed = false;
        collapseSoundPlayed = false;
        if (bgMusicLoaded)
            PlayMusicStream(BgMusic);
        InitGame();
    }
}

//  DRAW — BACKGROUND
void DrawBackground()
{
    if (texBackground.id > 0)
    {
        float scale = (float)SW / texBackground.width;
        int h = (int)(texBackground.height * scale);
        int off = (int)bgScrollY % h;
        DrawTextureEx(texBackground, {0, (float)(off - h)}, 0, scale, WHITE);
        DrawTextureEx(texBackground, {0, (float)off}, 0, scale, WHITE);
        DrawTextureEx(texBackground, {0, (float)(off + h)}, 0, scale, WHITE);
    }
    else
    {
        ClearBackground({5, 5, 15, 255});
    }
}

void DrawStars()
{
    for (auto &s : stars)
    {
        unsigned char b2 = (unsigned char)(160 + s.size * 20);
        DrawCircleV({s.x, s.y}, s.size * 0.6f, {b2, b2, b2, 200});
    }
}

void DrawHUD()
{
    DrawText("LIVES:", 10, 10, 18, LIGHTGRAY);
    for (int i = 0; i < player.lives; i++)
        DrawText("<3", 80 + i * 28, 10, 18, RED);

    std::string sc = "SCORE: " + std::to_string(player.score);
    DrawText(sc.c_str(), SW / 2 - MeasureText(sc.c_str(), 18) / 2, 10, 18, YELLOW);

    std::string lv = "LVL " + std::to_string(level);
    DrawText(lv.c_str(), SW - 70, 10, 18, SKYBLUE);

    if (player.hasShield)
        DrawText("[SHIELD]", SW / 2 - 36, 34, 16, Color{0, 255, 255, 255});

    if (level == 5 && boss.alive)
    {
        float ratio = (float)boss.hp / boss.maxHp;
        DrawRectangle(50, SH - 36, 500, 18, DARKGRAY);
        DrawRectangle(50, SH - 36, (int)(500 * ratio), 18,
                      ratio > 0.5f ? GREEN : ratio > 0.25f ? ORANGE
                                                           : RED);
        DrawRectangleLines(50, SH - 36, 500, 18, WHITE);
        DrawText("EMPEROR UGUTNA",
                 SW / 2 - MeasureText("EMPEROR UGUTNA", 14) / 2,
                 SH - 56, 14, {255, 80, 80, 255});
    }
}

void DrawStory(const char **pages, int count)
{
    if (storyPage >= count)
        return;
    DrawRectangle(0, 0, SW, SH, {0, 0, 0, 160});

    const char *text = pages[storyPage];
    std::string full(text);
    std::vector<std::string> lines;
    std::string cur;
    for (char ch : full)
    {
        if (ch == '\n')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else
            cur += ch;
    }
    lines.push_back(cur);

    int fontSize = 22, lineH = 34;
    int totalH = (int)lines.size() * lineH;
    int startY = SH / 2 - totalH / 2;
    for (int i = 0; i < (int)lines.size(); i++)
    {
        int tw = MeasureText(lines[i].c_str(), fontSize);
        DrawText(lines[i].c_str(), SW / 2 - tw / 2, startY + i * lineH, fontSize, WHITE);
    }

    bool isLast = (storyPage == count - 1);
    if (isLast)
    {
        if ((int)(GetTime() * 2) % 2 == 0)
            DrawText("Press ENTER", SW / 2 - MeasureText("Press ENTER", 16) / 2, SH - 60, 16, LIGHTGRAY);
    }
    else
    {
        for (int i = 0; i < count - 1; i++)
        {
            Color dc = (i == storyPage) ? WHITE : Color{100, 100, 100, 255};
            DrawCircle(SW / 2 - (count - 2) * 10 + i * 20, SH - 50, 5, dc);
        }
        float progress = storyTimer / STORY_PAGE_TIME;
        if (progress > 1.f)
            progress = 1.f;
        DrawRectangle(50, SH - 30, (int)((SW - 100) * progress), 4, {200, 200, 200, 150});
    }
}

// ─────────────────────────────────────────────
//  DRAW — BOSS TAUNT  (player died)
// ─────────────────────────────────────────────
void DrawBossTaunt()
{
    // Dark red overlay
    DrawRectangle(0, 0, SW, SH, {30, 0, 0, 210});

    // Boss image centered, large
    if (texBoss.id > 0)
    {
        float scale = 220.f / texBoss.width;
        int dw = (int)(texBoss.width * scale);
        int dh = (int)(texBoss.height * scale);
        // Slight red tint to look menacing
        DrawTextureEx(texBoss,
                      {SW / 2.f - dw / 2.f, SH / 2.f - dh / 2.f - 60},
                      0, scale, {255, 120, 120, 230});
    }
    else
    {
        // Fallback: big red hexagon
        DrawPoly({SW / 2.f, SH / 2.f - 60}, 6, 110, 0, {200, 40, 40, 230});
        DrawPoly({SW / 2.f, SH / 2.f - 60}, 6, 70, 30, {255, 100, 0, 230});
    }

    // Pulsing "HAHAHAHA" — size oscillates with time
    float pulse = 1.0f + 0.12f * sinf(GetTime() * 8.f);
    int fs = (int)(42 * pulse);
    const char *laugh = "HAHAHAHA!";
    int tw = MeasureText(laugh, fs);
    // Shadow
    DrawText(laugh, SW / 2 - tw / 2 + 3, SH / 2 + 110 + 3, fs, {80, 0, 0, 200});
    // Main text — alternates between red and orange for a fiery feel
    Color lc = ((int)(GetTime() * 6) % 2 == 0)
                   ? Color{255, 40, 0, 255}
                   : Color{255, 180, 0, 255};
    DrawText(laugh, SW / 2 - tw / 2, SH / 2 + 110, fs, lc);

    // Sub text
    const char *sub = "YOU WERE NO MATCH FOR ME!";
    int sw2 = MeasureText(sub, 18);
    DrawText(sub, SW / 2 - sw2 / 2, SH / 2 + 170, 18, {200, 200, 200, 220});

    // Skip hint
    if ((int)(GetTime() * 2) % 2 == 0)
    {
        const char *hint = "Press ENTER to continue";
        int hw = MeasureText(hint, 14);
        DrawText(hint, SW / 2 - hw / 2, SH - 40, 14, {150, 150, 150, 200});
    }
}

// ─────────────────────────────────────────────
//  DRAW — BOSS COLLAPSE  (player won)
// ─────────────────────────────────────────────
void DrawBossCollapse()
{
    // Dimmed overlay — brightens slightly with explosions
    DrawRectangle(0, 0, SW, SH, {0, 0, 0, 160});

    // Draw ongoing explosions
    for (auto &ex : explosions)
    {
        if (!ex.active)
            continue;
        unsigned char a = (unsigned char)(ex.alpha * 255);
        DrawCircleV({ex.x, ex.y}, ex.radius, {255, 180, 40, a});
        DrawCircleV({ex.x, ex.y}, ex.radius * 0.5f, {255, 240, 180, a});
    }

    // Draw the falling / fading boss
    if (collapseAlpha > 0.01f)
    {
        unsigned char ba = (unsigned char)(collapseAlpha * 255);
        if (texBoss.id > 0)
        {
            float scale = 120.f / texBoss.width;
            int dw = (int)(texBoss.width * scale);
            int dh = (int)(texBoss.height * scale);
            DrawTexturePro(
                texBoss,
                {0, 0, (float)texBoss.width, (float)texBoss.height},
                {boss.x, collapseY, (float)dw, (float)dh},
                {dw / 2.f, dh / 2.f},
                collapseAngle,
                {255, 255, 255, ba});
        }
        else
        {
            // Fallback polygons
            Color bc = {200, 60, 60, ba};
            DrawPoly({boss.x, collapseY}, 6, 60, collapseAngle, bc);
            DrawPoly({boss.x, collapseY}, 6, 38, collapseAngle + 30, {255, 180, 0, ba});
        }
    }

    // "SLOW MOTION" label top-centre
    const char *slo = "S L O W   M O T I O N";
    int slw = MeasureText(slo, 14);
    DrawText(slo, SW / 2 - slw / 2, 18, 14, {200, 200, 255, 180});

    // Victory text fades in after 1.5 real seconds
    if (collapseTimer > 1.5f)
    {
        float alpha = (collapseTimer - 1.5f) / 1.5f;
        if (alpha > 1.f)
            alpha = 1.f;
        unsigned char ta = (unsigned char)(alpha * 255);

        const char *vic = "YOU SAVED HUMANITY!";
        int vw = MeasureText(vic, 36);
        DrawText(vic, SW / 2 - vw / 2 + 2, SH - 160 + 2, 36, {60, 40, 0, ta}); // shadow
        DrawText(vic, SW / 2 - vw / 2, SH - 160, 36, {255, 220, 0, ta});

        if (collapseTimer > 3.5f && (int)(GetTime() * 2) % 2 == 0)
        {
            const char *hint = "Press ENTER to continue";
            int hw = MeasureText(hint, 15);
            DrawText(hint, SW / 2 - hw / 2, SH - 100, 15, {200, 200, 200, (unsigned char)(alpha * 200)});
        }
    }
}

void DrawGameplay()
{
    if (player.alive && ((int)(player.invTimer * 10) % 2 == 0))
    {
        if (texPlayer.id > 0)
        {
            float scale = 60.f / texPlayer.width;
            int dw = (int)(texPlayer.width * scale);
            int dh = (int)(texPlayer.height * scale);
            Color tint = player.hasShield ? Color{0, 255, 255, 220} : WHITE;
            DrawTextureEx(texPlayer, {player.x - dw / 2.f, player.y - dh / 2.f}, 0, scale, tint);
        }
        else
        {
            Color pc = player.hasShield ? Color{0, 255, 255, 255} : WHITE;
            DrawTriangle(
                {player.x, player.y - 22},
                {player.x - 18, player.y + 18},
                {player.x + 18, player.y + 18}, pc);
            DrawCircleV({player.x, player.y + 18}, 6, {255, 120, 0, 200});
        }
    }

    for (auto &b : bullets)
        if (b.active)
            DrawRectangle((int)b.x - 3, (int)b.y - 8, 6, 16, YELLOW);

    for (auto &e : enemies)
    {
        if (!e.alive)
            continue;
        if (texEnemy.id > 0)
        {
            float scale = 44.f / texEnemy.width;
            int dw = (int)(texEnemy.width * scale);
            int dh = (int)(texEnemy.height * scale);
            DrawTextureEx(texEnemy, {e.x - dw / 2.f, e.y - dh / 2.f}, 0, scale, WHITE);
        }
        else
        {
            DrawPoly({e.x, e.y}, 4, 20, 45, {80, 220, 80, 255});
        }
        if (e.hp < e.maxHp && e.maxHp > 1)
        {
            DrawRectangle((int)e.x - 15, (int)e.y - 30, 30, 4, DARKGRAY);
            DrawRectangle((int)e.x - 15, (int)e.y - 30, (int)(30.f * e.hp / e.maxHp), 4, RED);
        }
    }

    for (auto &eb : eBullets)
        if (eb.active)
            DrawCircleV({eb.x, eb.y}, 5, LIME);

    for (auto &orb : orbs)
    {
        if (!orb.active)
            continue;
        float bobY = orb.y + sinf(orb.bobTimer) * 5.f;
        Color col = OrbColor(orb.type);
        Color glow = {col.r, col.g, col.b, 80};
        DrawCircleV({orb.x, bobY}, 18.f, glow);
        DrawCircleV({orb.x, bobY}, 13.f, col);
        DrawCircleV({orb.x - 4.f, bobY - 4.f}, 4.f, {255, 255, 255, 160});
        const char *lbl = OrbLabel(orb.type);
        int tw = MeasureText(lbl, 10);
        DrawText(lbl, (int)(orb.x - tw / 2), (int)(bobY + 16), 10, WHITE);
    }

    if (level == 5 && boss.alive)
    {
        if (texBoss.id > 0)
        {
            float scale = 120.f / texBoss.width;
            int dw = (int)(texBoss.width * scale);
            int dh = (int)(texBoss.height * scale);
            Color tint = (boss.phase == 1)   ? WHITE
                         : (boss.phase == 2) ? Color{255, 180, 80, 255}
                                             : Color{255, 80, 80, 255};
            DrawTextureEx(texBoss, {boss.x - dw / 2.f, boss.y - dh / 2.f}, 0, scale, tint);
        }
        else
        {
            Color bc = (boss.phase == 1)   ? Color{200, 60, 60, 255}
                       : (boss.phase == 2) ? Color{220, 100, 30, 255}
                                           : Color{255, 40, 40, 255};
            DrawPoly({boss.x, boss.y}, 6, 60, 0, bc);
            DrawPoly({boss.x, boss.y}, 6, 38, 30, {255, 180, 0, 255});
        }
        std::string ph = "PHASE " + std::to_string(boss.phase);
        DrawText(ph.c_str(), (int)boss.x - MeasureText(ph.c_str(), 14) / 2, (int)boss.y - 80, 14, WHITE);
    }

    for (auto &ex : explosions)
    {
        if (!ex.active)
            continue;
        unsigned char a = (unsigned char)(ex.alpha * 255);
        DrawCircleV({ex.x, ex.y}, ex.radius, {255, 180, 40, a});
        DrawCircleV({ex.x, ex.y}, ex.radius * 0.5f, {255, 240, 180, a});
    }

    DrawHUD();
}

void DrawGameOver()
{
    DrawRectangle(0, 0, SW, SH, {0, 0, 0, 200});
    // Win is determined by whether boss was killed (collapse screen already showed)
    bool won = (screen == SCR_GAMEOVER && level == 5 && !boss.alive);
    const char *msg = won ? "PLAY AGAIN?" : "GAME OVER";
    Color mc = won ? GOLD : RED;

    DrawText(msg, SW / 2 - MeasureText(msg, 38) / 2, SH / 2 - 60, 38, mc);
    std::string sc = "Final Score: " + std::to_string(player.score);
    DrawText(sc.c_str(), SW / 2 - MeasureText(sc.c_str(), 22) / 2, SH / 2 + 10, 22, WHITE);
    DrawText("Press ENTER to play again",
             SW / 2 - MeasureText("Press ENTER to play again", 18) / 2,
             SH / 2 + 60, 18, LIGHTGRAY);
}

int main()
{
    srand((unsigned)time(nullptr));
    InitWindow(SW, SH, "Space Shooter — Ugutna War");
    SetTargetFPS(FPS);
    InitAudioDevice();
    InitSounds();

    texPlayer = LoadTexture("our rocket.png");
    texEnemy = LoadTexture("enemy rocket.png");
    texBoss = LoadTexture("real boss.png");
    texBackground = LoadTexture("background.png");

    InitStars();
    InitGame();

    if (bgMusicLoaded)
        PlayMusicStream(BgMusic);

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        if (bgMusicLoaded && screen != SCR_GAMEOVER && screen != SCR_BOSS_TAUNT)
            UpdateMusicStream(BgMusic);

        UpdateStars(dt);

        switch (screen)
        {
        case SCR_STORY_INTRO:
            UpdateStory(dt);
            break;
        case SCR_GAMEPLAY:
            UpdateGameplay(dt);
            break;
        case SCR_STORY_OUTRO:
            UpdateOutro(dt);
            break;
        case SCR_BOSS_TAUNT:
            UpdateBossTaunt(dt);
            break;
        case SCR_BOSS_COLLAPSE:
            UpdateBossCollapse(dt);
            break;
        case SCR_GAMEOVER:
            UpdateGameOver();
            break;
        }

        BeginDrawing();
        ClearBackground({5, 5, 15, 255});
        DrawBackground();
        DrawStars();

        switch (screen)
        {
        case SCR_STORY_INTRO:
            DrawStory(introPages, introPageCount);
            break;
        case SCR_GAMEPLAY:
            DrawGameplay();
            break;
        case SCR_STORY_OUTRO:
            DrawStory(outroPages, outroPageCount);
            break;
        case SCR_BOSS_TAUNT:
            DrawBossTaunt();
            break;
        case SCR_BOSS_COLLAPSE:
            DrawBossCollapse();
            break;
        case SCR_GAMEOVER:
            DrawGameOver();
            break;
        }

        EndDrawing();
    }

    UnloadTexture(texPlayer);
    UnloadTexture(texEnemy);
    UnloadTexture(texBoss);
    UnloadTexture(texBackground);
    UnloadSounds();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}