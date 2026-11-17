#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

PSP_MODULE_INFO("AK47_Desert_Ops", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BUF_WIDTH (512)
#define SCR_WIDTH (480)
#define SCR_HEIGHT (272)

static unsigned int __attribute__((aligned(16))) list[262144];

typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

// 3D Kup Verisi (Düsmanlar, Konteynerler ve AK-47 Parcalari icin)
Vertex __attribute__((aligned(16))) cube_verts[36] = {
    // On
    {0xffffffff, -0.5f, -0.5f,  0.5f}, {0xffffffff,  0.5f, -0.5f,  0.5f}, {0xffffffff,  0.5f,  0.5f,  0.5f},
    {0xffffffff, -0.5f, -0.5f,  0.5f}, {0xffffffff,  0.5f,  0.5f,  0.5f}, {0xffffffff, -0.5f,  0.5f,  0.5f},
    // Arka
    {0xffcccccc,  0.5f, -0.5f, -0.5f}, {0xffcccccc, -0.5f, -0.5f, -0.5f}, {0xffcccccc, -0.5f,  0.5f, -0.5f},
    {0xffcccccc,  0.5f, -0.5f, -0.5f}, {0xffcccccc, -0.5f,  0.5f, -0.5f}, {0xffcccccc,  0.5f,  0.5f, -0.5f},
    // Ust
    {0xffeeeeee, -0.5f,  0.5f,  0.5f}, {0xffeeeeee,  0.5f,  0.5f,  0.5f}, {0xffeeeeee,  0.5f,  0.5f, -0.5f},
    {0xffeeeeee, -0.5f,  0.5f,  0.5f}, {0xffeeeeee,  0.5f,  0.5f, -0.5f}, {0xffeeeeee, -0.5f,  0.5f, -0.5f},
    // Alt
    {0xff888888, -0.5f, -0.5f, -0.5f}, {0xff888888,  0.5f, -0.5f, -0.5f}, {0xff888888,  0.5f, -0.5f,  0.5f},
    {0xff888888, -0.5f, -0.5f, -0.5f}, {0xff888888,  0.5f, -0.5f,  0.5f}, {0xff888888, -0.5f, -0.5f,  0.5f},
    // Sag
    {0xffdddddd,  0.5f, -0.5f,  0.5f}, {0xffdddddd,  0.5f, -0.5f, -0.5f}, {0xffdddddd,  0.5f,  0.5f, -0.5f},
    {0xffdddddd,  0.5f, -0.5f,  0.5f}, {0xffdddddd,  0.5f,  0.5f, -0.5f}, {0xffdddddd,  0.5f,  0.5f,  0.5f},
    // Sol
    {0xffbbbbbb, -0.5f, -0.5f, -0.5f}, {0xffbbbbbb, -0.5f, -0.5f,  0.5f}, {0xffbbbbbb, -0.5f,  0.5f,  0.5f},
    {0xffbbbbbb, -0.5f, -0.5f, -0.5f}, {0xffbbbbbb, -0.5f,  0.5f,  0.5f}, {0xffbbbbbb, -0.5f,  0.5f, -0.5f}
};

#define MAX_ENEMIES 6
typedef struct {
    float x, y, z;
    int hp;
    int active;
    float shoot_timer;
} Enemy;

Enemy enemies[MAX_ENEMIES];

float px = 0.0f, py = 1.6f, pz = 10.0f;
float yaw = 0.0f, pitch = 0.0f;
int player_hp = 100;
int ammo = 30;
int reserve_ammo = 120;
int score = 0;
int wave = 1;
float recoil = 0.0f;
int ads_mode = 0;
int fire_cooldown = 0;
int reload_timer = 0;

int exit_callback(int arg1, int arg2, void *common) {
    sceKernelExitGame();
    return 0;
}

int CallbackThread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int SetupCallbacks(void) {
    int thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
    if(thid >= 0) sceKernelStartThread(thid, 0, 0);
    return thid;
}

void spawn_wave(int w) {
    int count = (w + 2 > MAX_ENEMIES) ? MAX_ENEMIES : w + 2;
    for(int i = 0; i < MAX_ENEMIES; i++) {
        if(i < count) {
            enemies[i].active = 1;
            enemies[i].hp = 60 + (w * 15);
            enemies[i].x = ((rand() % 24) - 12) * 1.0f;
            enemies[i].y = 1.0f;
            enemies[i].z = -10.0f - (rand() % 15);
            enemies[i].shoot_timer = 2.0f + (rand() % 3);
        } else {
            enemies[i].active = 0;
        }
    }
}

void draw_box(float x, float y, float z, float sx, float sy, float sz, unsigned int color) {
    sceGumPushMatrix();
    ScePspFVector3 pos = {x, y, z};
    ScePspFVector3 scale = {sx, sy, sz};
    sceGumTranslate(&pos);
    sceGumScale(&scale);
    sceGuColor(color);
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, 0, cube_verts);
    sceGumPopMatrix();
}

void draw_ak47() {
    sceGumPushMatrix();
    sceGumLoadIdentity();

    float gun_x = ads_mode ? 0.0f : 0.28f;
    float gun_y = ads_mode ? -0.16f : -0.24f;
    float gun_z = -0.55f + recoil;

    ScePspFVector3 pos = {gun_x, gun_y, gun_z};
    sceGumTranslate(&pos);
    sceGumRotateX(recoil * 1.5f);

    // Govde (Koyu Metal - ABGR formati)
    draw_box(0.0f, 0.0f, 0.0f, 0.06f, 0.08f, 0.35f, 0xff2a2a2a);
    // Ahsap El Kundagi
    draw_box(0.0f, -0.01f, -0.25f, 0.055f, 0.065f, 0.22f, 0xff1e4582);
    // Namlu
    draw_box(0.0f, 0.02f, -0.45f, 0.02f, 0.02f, 0.25f, 0xff181818);
    // Arpacik
    draw_box(0.0f, 0.05f, -0.52f, 0.01f, 0.03f, 0.01f, 0xff111111);
    // Kavisli Sarjor
    sceGumPushMatrix();
    ScePspFVector3 mag_pos = {0.0f, -0.11f, -0.05f};
    sceGumTranslate(&mag_pos);
    sceGumRotateX(0.35f);
    draw_box(0.0f, 0.0f, 0.0f, 0.045f, 0.18f, 0.07f, 0xff222222);
    sceGumPopMatrix();

    sceGumPopMatrix();
}

void shoot_ray() {
    if(ammo <= 0 || reload_timer > 0) return;
    ammo--;
    recoil = 0.08f;
    fire_cooldown = 6;

    float dx = -sinf(yaw) * cosf(pitch);
    float dy = sinf(pitch);
    float dz = -cosf(yaw) * cosf(pitch);

    for(int i = 0; i < MAX_ENEMIES; i++) {
        if(!enemies[i].active) continue;
        float vx = enemies[i].x - px;
        float vy = enemies[i].y - py;
        float vz = enemies[i].z - pz;
        float dist = sqrtf(vx*vx + vy*vy + vz*vz);
        if(dist > 0.1f) {
            float dot = (vx*dx + vy*dy + vz*dz) / dist;
            if(dot > 0.985f) { // Isabet!
                enemies[i].hp -= 35;
                if(enemies[i].hp <= 0) {
                    enemies[i].active = 0;
                    score += 100;
                }
                break;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    SetupCallbacks();
    pspDebugScreenInit();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void*)0x110000, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH/2), 2048 - (SCR_HEIGHT/2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    spawn_wave(wave);
    SceCtrlData pad;

    while(1) {
        sceCtrlReadBufferPositive(&pad, 1);

        // Analog stick ile bakis acisi
        float ax = (pad.Lx - 128) / 128.0f;
        float ay = (pad.Ly - 128) / 128.0f;
        if(fabsf(ax) > 0.2f) yaw -= ax * 0.045f;
        if(fabsf(ay) > 0.2f) pitch -= ay * 0.035f;
        if(pitch > 1.2f) pitch = 1.2f;
        if(pitch < -1.2f) pitch = -1.2f;

        // Yon tuslari ile hareket
        float speed = 0.18f;
        if(pad.Buttons & PSP_CTRL_UP) {
            px -= sinf(yaw) * speed;
            pz -= cosf(yaw) * speed;
        }
        if(pad.Buttons & PSP_CTRL_DOWN) {
            px += sinf(yaw) * speed;
            pz += cosf(yaw) * speed;
        }
        if(pad.Buttons & PSP_CTRL_LEFT) {
            px -= cosf(yaw) * speed;
            pz += sinf(yaw) * speed;
        }
        if(pad.Buttons & PSP_CTRL_RIGHT) {
            px += cosf(yaw) * speed;
            pz -= sinf(yaw) * speed;
        }

        // L Tusu: Nisan Alma (ADS)
        ads_mode = (pad.Buttons & PSP_CTRL_LTRIGGER) ? 1 : 0;

        // R Tusu: Ates Etme
        if(fire_cooldown > 0) fire_cooldown--;
        if((pad.Buttons & PSP_CTRL_RTRIGGER) && fire_cooldown == 0) {
            shoot_ray();
        }

        // Kare Tusu: Sarjor Degistirme
        if((pad.Buttons & PSP_CTRL_SQUARE) && ammo < 30 && reload_timer == 0 && reserve_ammo > 0) {
            reload_timer = 60;
        }
        if(reload_timer > 0) {
            reload_timer--;
            if(reload_timer == 0) {
                int needed = 30 - ammo;
                int take = (reserve_ammo >= needed) ? needed : reserve_ammo;
                ammo += take;
                reserve_ammo -= take;
            }
        }

        if(recoil > 0.0f) recoil *= 0.8f;

        // Dusman Yapay Zekasi ve Dalga Kontrolu
        int alive_count = 0;
        for(int i = 0; i < MAX_ENEMIES; i++) {
            if(!enemies[i].active) continue;
            alive_count++;
            float dx = px - enemies[i].x;
            float dz = pz - enemies[i].z;
            float d = sqrtf(dx*dx + dz*dz);
            if(d > 4.0f) {
                enemies[i].x += (dx / d) * 0.03f;
                enemies[i].z += (dz / d) * 0.03f;
            }
            enemies[i].shoot_timer -= 0.016f;
            if(enemies[i].shoot_timer <= 0.0f) {
                player_hp -= 8;
                enemies[i].shoot_timer = 2.5f;
                if(player_hp <= 0) {
                    player_hp = 100;
                    score = 0;
                    wave = 1;
                    ammo = 30;
                    reserve_ammo = 120;
                    spawn_wave(wave);
                }
            }
        }
        if(alive_count == 0) {
            wave++;
            reserve_ammo += 60;
            spawn_wave(wave);
        }

        // 3D Sahne Cizimi
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xff5599cc); // Col gokyuzu
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumPerspective(ads_mode ? 45.0f : 70.0f, 16.0f/9.0f, 0.1f, 100.0f);

        sceGumMatrixMode(GU_VIEW);
        sceGumLoadIdentity();
        sceGumRotateX(-pitch);
        sceGumRotateY(-yaw);
        ScePspFVector3 cam = {-px, -py, -pz};
        sceGumTranslate(&cam);

        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();

        // Col Zemini
        draw_box(0.0f, -0.5f, 0.0f, 60.0f, 1.0f, 60.0f, 0xff66aacc);

        // Siper Konteynerleri ve Sandiklar
        draw_box(-5.0f, 1.2f, -4.0f, 3.0f, 2.4f, 6.0f, 0xff223388);
        draw_box(6.0f, 1.2f, -8.0f, 3.0f, 2.4f, 6.0f, 0xff774422);
        draw_box(0.0f, 0.6f, 2.0f, 2.0f, 1.2f, 1.5f, 0xff447799);

        // Teroristler
        for(int i = 0; i < MAX_ENEMIES; i++) {
            if(!enemies[i].active) continue;
            // Govde
            draw_box(enemies[i].x, 1.0f, enemies[i].z, 0.7f, 1.4f, 0.4f, 0xff222299);
            // Kafa
            draw_box(enemies[i].x, 2.0f, enemies[i].z, 0.45f, 0.45f, 0.45f, 0xff88aadd);
        }

        // Birinci Sahis AK-47 Modeli
        draw_ak47();

        sceGuFinish();
        sceGuSync(0, 0);

        // HUD Bilgileri
        pspDebugScreenSetOffset((int)0);
        pspDebugScreenSetXY(1, 1);
        pspDebugScreenPrintf("CAN: %d | MERMI: %d/%d | DALGA: %d | SKOR: %d", player_hp, ammo, reserve_ammo, wave, score);
        if(reload_timer > 0) {
            pspDebugScreenSetXY(25, 16);
            pspDebugScreenPrintf("SARJOR DEGISTIRILIYOR...");
        } else if(!ads_mode) {
            pspDebugScreenSetXY(33, 17);
            pspDebugScreenPrintf("+");
        }

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
