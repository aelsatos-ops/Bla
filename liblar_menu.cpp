#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <atomic>
#include <vector>
#include <mutex>

#define LOG_TAG "LAR_MENU"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// ===================== CONFIG / MENU FLAGS =====================
// Core (from your original)
static std::atomic<bool> g_god{false};
static std::atomic<bool> g_fly{false};
static std::atomic<bool> g_speed{false};
static std::atomic<bool> g_noclip{false};
static std::atomic<bool> g_inf_scrap{false};
static std::atomic<bool> g_antikick{true};       // ON by default
static std::atomic<bool> g_menu_visible{true};

// Player
static std::atomic<bool> g_noDamage{false};
static std::atomic<bool> g_infHealth{false};
static std::atomic<bool> g_infStamina{false};
static std::atomic<bool> g_noFall{false};
static std::atomic<float> g_healthOverride{100.f};
static std::atomic<float> g_staminaOverride{100.f};

// Movement
static std::atomic<bool> g_superJump{false};
static std::atomic<bool> g_climbAnywhere{false};
static std::atomic<bool> g_ignoreMonsters{false};
static std::atomic<float> g_speedMul{2.0f};
static std::atomic<float> g_flySpeed{8.0f};
static std::atomic<float> g_jumpMul{2.5f};

// World / Loot
static std::atomic<bool> g_freeShop{false};
static std::atomic<bool> g_instantSell{false};
static std::atomic<bool> g_noWeight{false};
static std::atomic<bool> g_magnetLoot{false};
static std::atomic<bool> g_revealScrap{false};
static std::atomic<bool> g_revealExits{false};
static std::atomic<int>   g_scrapAmount{99999};
static std::atomic<float> g_lootRange{25.f};

// Combat
static std::atomic<bool> g_oneHit{false};
static std::atomic<bool> g_infAmmo{false};
static std::atomic<bool> g_noRecoil{false};
static std::atomic<bool> g_rapidFire{false};
static std::atomic<bool> g_infDurability{false};
static std::atomic<bool> g_killAura{false};
static std::atomic<float> g_damageMul{5.0f};
static std::atomic<float> g_auraRange{6.0f};

// Network / Anti-Kick
static std::atomic<bool> g_antiBan{true};
static std::atomic<bool> g_antiTamper{true};
static std::atomic<bool> g_forceHost{false};
static std::atomic<bool> g_spoofMaster{false};
static std::atomic<bool> g_blockReports{true};
static std::atomic<bool> g_stayInRoom{true};
static std::atomic<bool> g_ignoreDenial{true};

// Visuals / ESP
static std::atomic<bool> g_revealPlayers{false};
static std::atomic<bool> g_revealMonsters{false};
static std::atomic<bool> g_fullbright{false};
static std::atomic<bool> g_noFog{false};
static std::atomic<bool> g_espBoxes{false};
static std::atomic<bool> g_espNames{false};
static std::atomic<bool> g_espDistance{false};
static std::atomic<bool> g_tracers{false};
static std::atomic<float> g_espMaxDist{80.f};

// Configuration
static std::atomic<bool> g_menuPaused{false};
static std::atomic<bool> g_logVerbose{true};
static std::atomic<bool> g_safeMode{false};
static std::atomic<bool> g_autoRehook{true};
static std::atomic<int>   g_menuPage{0};
static std::atomic<int>   g_refreshSec{2};

// Misc
static std::atomic<bool> g_fpsUnlock{false};
static std::atomic<bool> g_noTutorial{true};
static std::atomic<bool> g_skipCutscenes{false};
static std::atomic<bool> g_freeCosmetics{false};
static std::atomic<bool> g_unlockAll{false};
static std::atomic<bool> g_debugDraw{false};

static std::atomic<bool> g_running{true};
static std::atomic<int>  g_hooksInstalled{0};
static std::atomic<int>  g_ticks{0};

// offsets / symbols se rellenan en runtime (il2cpp)
static void* g_il2cpp = nullptr;

// tipos il2cpp mínimos
typedef void* (*il2cpp_domain_get_t)();
typedef void* (*il2cpp_domain_get_assemblies_t)(void* domain, size_t* size);
typedef void* (*il2cpp_assembly_get_image_t)(void* assembly);
typedef void* (*il2cpp_class_from_name_t)(void* image, const char* ns, const char* name);
typedef void* (*il2cpp_class_get_method_from_name_t)(void* klass, const char* name, int args);
typedef void* (*il2cpp_class_get_field_from_name_t)(void* klass, const char* name);
typedef void* (*il2cpp_runtime_invoke_t)(void* method, void* obj, void** params, void** exc);
typedef void* (*il2cpp_object_new_t)(void* klass);
typedef void* (*il2cpp_thread_attach_t)(void* domain);
typedef void* (*il2cpp_string_new_t)(const char* str);
typedef const char* (*il2cpp_method_get_name_t)(void* method);

static il2cpp_domain_get_t                 il2cpp_domain_get = nullptr;
static il2cpp_domain_get_assemblies_t      il2cpp_domain_get_assemblies = nullptr;
static il2cpp_assembly_get_image_t         il2cpp_assembly_get_image = nullptr;
static il2cpp_class_from_name_t            il2cpp_class_from_name = nullptr;
static il2cpp_class_get_method_from_name_t il2cpp_class_get_method_from_name = nullptr;
static il2cpp_class_get_field_from_name_t  il2cpp_class_get_field_from_name = nullptr;
static il2cpp_runtime_invoke_t             il2cpp_runtime_invoke = nullptr;
static il2cpp_object_new_t                 il2cpp_object_new = nullptr;
static il2cpp_thread_attach_t              il2cpp_thread_attach = nullptr;
static il2cpp_string_new_t                 il2cpp_string_new = nullptr;
static il2cpp_method_get_name_t            il2cpp_method_get_name = nullptr;

// ===================== ARM64 HOOK ENGINE =====================
struct HookEntry {
    void* target = nullptr;
    void* detour = nullptr;
    void* trampoline = nullptr;
    uint8_t original[16]{};
    bool active = false;
    char name[80]{};
};
static std::vector<HookEntry> g_hooks;
static std::mutex g_hook_mtx;

static size_t page_size() {
    static size_t p = 0;
    if (!p) p = (size_t)sysconf(_SC_PAGESIZE);
    return p ? p : 4096;
}

static bool mprotect_rwx(void* addr, size_t len) {
    uintptr_t start = (uintptr_t)addr & ~(page_size() - 1);
    uintptr_t end = ((uintptr_t)addr + len + page_size() - 1) & ~(page_size() - 1);
    return mprotect((void*)start, end - start, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

static uint32_t encode_b(void* from, void* to) {
    int64_t imm = ((int64_t)to - (int64_t)from) / 4;
    return 0x14000000u | ((uint32_t)(imm) & 0x03FFFFFFu);
}

static void write_branch(void* from, void* to) {
    mprotect_rwx(from, 4);
    __atomic_store_n((uint32_t*)from, encode_b(from, to), __ATOMIC_RELEASE);
    __builtin___clear_cache((char*)from, (char*)from + 4);
}

static bool install_hook(void* target, void* detour, const char* name) {
    if (!target || !detour) return false;
    std::lock_guard<std::mutex> lock(g_hook_mtx);
    for (auto& h : g_hooks)
        if (h.target == target && h.active) return true;

    HookEntry h{};
    h.target = target;
    h.detour = detour;
    strncpy(h.name, name ? name : "?", sizeof(h.name) - 1);
    memcpy(h.original, target, 16);

    h.trampoline = mmap(nullptr, 64, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (h.trampoline == MAP_FAILED) return false;
    memcpy(h.trampoline, h.original, 16);
    write_branch((char*)h.trampoline + 16, (char*)target + 16);
    write_branch(target, detour);

    h.active = true;
    g_hooks.push_back(h);
    LOGI("HOOK %s %p -> %p", h.name, target, detour);
    return true;
}

// ===================== ANTI-KICK =====================
// Bloquea rutas típicas de kick en LAR:
//  - unauthorized_kick string / handler
//  - KickMenuGUI / KickHammer
//  - Photon leave / host kick RPC

static void* find_method(const char* ns, const char* cls, const char* method, int argc) {
    if (!il2cpp_domain_get) return nullptr;
    void* domain = il2cpp_domain_get();
    if (!domain) return nullptr;
    size_t n = 0;
    void** asms = (void**)il2cpp_domain_get_assemblies(domain, &n);
    if (!asms) return nullptr;
    for (size_t i = 0; i < n; i++) {
        void* img = il2cpp_assembly_get_image(asms[i]);
        if (!img) continue;
        void* k = il2cpp_class_from_name(img, ns, cls);
        if (!k) continue;
        void* m = il2cpp_class_get_method_from_name(k, method, argc);
        if (m) return m;
    }
    return nullptr;
}

static void* find_method_any(const char* ns, const char* cls, const char* method) {
    for (int a = 0; a <= 8; a++) {
        void* m = find_method(ns, cls, method, a);
        if (m) return m;
    }
    return nullptr;
}

static void* method_code_ptr(void* methodInfo) {
    return methodInfo ? *(void**)methodInfo : nullptr;
}

// Ejemplo de stub anti-kick: si el juego llama a un método de kick,
// nuestro trampolín no hace nada y evita el leave room.
static void anti_kick_stub(void* thisPtr, void* a1, void* a2) {
    if (g_antikick.load() || g_stayInRoom.load() || g_ignoreDenial.load()) {
        LOGI("anti-kick blocked a kick call self=%p", thisPtr);
        return;
    }
    // si anti-kick off, podrías llamar al original (guardar ptr)
}

extern "C" void lar_antikick_void(void* self, void* a0, void* a1, void* a2, void* a3) {
    if (g_antikick.load() || g_stayInRoom.load() || g_ignoreDenial.load()) {
        if (g_logVerbose.load()) LOGI("[AntiKick] void blocked");
        return;
    }
}

extern "C" uint8_t lar_antikick_bool(void* self, void* a0, void* a1) {
    if (g_antikick.load() || g_antiBan.load() || g_antiTamper.load()) {
        if (g_logVerbose.load()) LOGI("[AntiKick] bool -> false");
        return 0;
    }
    return 0;
}

extern "C" void lar_antikick_disconnect(void* self, void* a0) {
    if (g_antikick.load() || g_stayInRoom.load()) {
        LOGI("[AntiKick] Disconnect/CloseConnection blocked");
        return;
    }
}

extern "C" void lar_report_block(void* self, void* a0, void* a1) {
    if (g_blockReports.load() || g_antiTamper.load()) {
        LOGI("[AntiKick] report/tamper blocked");
        return;
    }
}

extern "C" void lar_damage_absorb(void* self, void* dmg) {
    if (g_god.load() || g_noDamage.load() || g_infHealth.load()) {
        if (g_logVerbose.load()) LOGD("[God] damage absorbed");
        return;
    }
}

// ===================== HOOK TARGET TABLES =====================
struct HookTarget {
    const char* ns;
    const char* cls;
    const char* method;
    void* detour;
    const char* tag;
};

static const HookTarget kKickTargets[] = {
    {"LethalApeR", "KickMenuGUI", "KickPlayer", (void*)lar_antikick_void, "KickMenuGUI.KickPlayer"},
    {"LethalApeR", "KickMenuGUI", "Kick", (void*)lar_antikick_void, "KickMenuGUI.Kick"},
    {"LethalApeR", "KickHammer", "DoKick", (void*)lar_antikick_void, "KickHammer.DoKick"},
    {"LethalApeR", "KickHammer", "Kick", (void*)lar_antikick_void, "KickHammer.Kick"},
    {"LethalApeR", "KickHammer", "OnTriggerEnter", (void*)lar_antikick_void, "KickHammer.OnTriggerEnter"},
    {"LethalApeR.Networking", "DestroyGuard", "OnKick", (void*)lar_antikick_void, "DestroyGuard.OnKick"},
    {"LethalApeR.Networking", "DestroyGuard", "Kick", (void*)lar_antikick_void, "DestroyGuard.Kick"},
    {"LethalApeR.Networking.LarApi", "DenialInfo", "Apply", (void*)lar_antikick_void, "DenialInfo.Apply"},
    {"LethalApeR.Diagnostics", "SessionValidator", "Validate", (void*)lar_antikick_bool, "SessionValidator.Validate"},
    {"LethalApeR.Backend", "TamperReportBridge", "Report", (void*)lar_report_block, "TamperReportBridge.Report"},
    {"LethalApeR.Gameplay", "RoundStateGuard", "ForceEnd", (void*)lar_antikick_void, "RoundStateGuard.ForceEnd"},
    {"Photon.Pun", "PhotonNetwork", "CloseConnection", (void*)lar_antikick_disconnect, "Photon.CloseConnection"},
    {"Photon.Pun", "PhotonNetwork", "LeaveRoom", (void*)lar_antikick_void, "Photon.LeaveRoom"},
    {"Photon.Realtime", "LoadBalancingClient", "Disconnect", (void*)lar_antikick_disconnect, "LBC.Disconnect"},
    {"Photon.Realtime", "LoadBalancingPeer", "Disconnect", (void*)lar_antikick_disconnect, "LBP.Disconnect"},
};

static const HookTarget kPlayerTargets[] = {
    {"LethalApeR.Damage", "DamagableObject", "TakeDamage", (void*)lar_damage_absorb, "Damagable.TakeDamage"},
    {"LethalApeR.Damage", "DamagableObject", "ApplyDamage", (void*)lar_damage_absorb, "Damagable.ApplyDamage"},
    {"LethalApeR.Gameplay", "LimbBreakController", "Break", (void*)lar_antikick_void, "LimbBreak.Break"},
};

static int install_table(const HookTarget* table, size_t count) {
    int ok = 0;
    for (size_t i = 0; i < count; i++) {
        void* mi = find_method_any(table[i].ns, table[i].cls, table[i].method);
        if (!mi) {
            if (g_logVerbose.load()) LOGW("miss %s", table[i].tag);
            continue;
        }
        void* code = method_code_ptr(mi);
        if (!code) continue;
        if (install_hook(code, table[i].detour, table[i].tag)) ok++;
    }
    return ok;
}

static void install_all_hooks() {
    if (g_safeMode.load()) {
        LOGW("safeMode ON — hooks skipped");
        return;
    }
    int n = 0;
    n += install_table(kKickTargets, sizeof(kKickTargets) / sizeof(kKickTargets[0]));
    n += install_table(kPlayerTargets, sizeof(kPlayerTargets) / sizeof(kPlayerTargets[0]));
    g_hooksInstalled = n;
    LOGI("hooks installed: %d", n);
}

// ===================== MENU (texto / estado) =====================
// En VR real se dibuja con ImGui + OpenXR overlay o con el sistema de UI del juego.
// Aquí dejamos el estado + log para que veas que vive.

static const char* onoff(bool v) { return v ? "ON " : "OFF"; }

static void dump_menu_state() {
    if (!g_menu_visible.load()) return;

    LOGI("=== LethalApeR Menu ===");
    LOGI("hooks:%d ticks:%d page:%d", g_hooksInstalled.load(), g_ticks.load(), g_menuPage.load());
    LOGI("--- PLAYER ---");
    LOGI("God         : %s", onoff(g_god.load()));
    LOGI("NoDamage    : %s", onoff(g_noDamage.load()));
    LOGI("InfHealth   : %s", onoff(g_infHealth.load()));
    LOGI("InfStamina  : %s", onoff(g_infStamina.load()));
    LOGI("NoFall      : %s", onoff(g_noFall.load()));
    LOGI("HealthOv    : %.0f", g_healthOverride.load());
    LOGI("StaminaOv   : %.0f", g_staminaOverride.load());
    LOGI("--- MOVEMENT ---");
    LOGI("Fly         : %s", onoff(g_fly.load()));
    LOGI("NoClip      : %s", onoff(g_noclip.load()));
    LOGI("Speed       : %s  x%.1f", onoff(g_speed.load()), g_speedMul.load());
    LOGI("SuperJump   : %s  x%.1f", onoff(g_superJump.load()), g_jumpMul.load());
    LOGI("IgnoreMons  : %s", onoff(g_ignoreMonsters.load()));
    LOGI("ClimbAny    : %s", onoff(g_climbAnywhere.load()));
    LOGI("FlySpeed    : %.1f", g_flySpeed.load());
    LOGI("--- WORLD / LOOT ---");
    LOGI("InfScrap    : %s  (%d)", onoff(g_inf_scrap.load()), g_scrapAmount.load());
    LOGI("FreeShop    : %s", onoff(g_freeShop.load()));
    LOGI("InstantSell : %s", onoff(g_instantSell.load()));
    LOGI("NoWeight    : %s", onoff(g_noWeight.load()));
    LOGI("LootMagnet  : %s  r=%.0f", onoff(g_magnetLoot.load()), g_lootRange.load());
    LOGI("RevealScrap : %s", onoff(g_revealScrap.load()));
    LOGI("RevealExits : %s", onoff(g_revealExits.load()));
    LOGI("--- COMBAT ---");
    LOGI("OneHit      : %s", onoff(g_oneHit.load()));
    LOGI("InfAmmo     : %s", onoff(g_infAmmo.load()));
    LOGI("NoRecoil    : %s", onoff(g_noRecoil.load()));
    LOGI("RapidFire   : %s", onoff(g_rapidFire.load()));
    LOGI("InfDura     : %s", onoff(g_infDurability.load()));
    LOGI("KillAura    : %s  r=%.1f", onoff(g_killAura.load()), g_auraRange.load());
    LOGI("DmgMul      : x%.1f", g_damageMul.load());
    LOGI("--- NETWORK / ANTI-KICK ---");
    LOGI("AntiKick    : %s", onoff(g_antikick.load()));
    LOGI("AntiBan     : %s", onoff(g_antiBan.load()));
    LOGI("AntiTamper  : %s", onoff(g_antiTamper.load()));
    LOGI("ForceHost   : %s", onoff(g_forceHost.load()));
    LOGI("SpoofMaster : %s", onoff(g_spoofMaster.load()));
    LOGI("BlockReports: %s", onoff(g_blockReports.load()));
    LOGI("StayInRoom  : %s", onoff(g_stayInRoom.load()));
    LOGI("IgnoreDenial: %s", onoff(g_ignoreDenial.load()));
    LOGI("--- VISUALS / ESP ---");
    LOGI("RevealPlay  : %s", onoff(g_revealPlayers.load()));
    LOGI("RevealMons  : %s", onoff(g_revealMonsters.load()));
    LOGI("Fullbright  : %s", onoff(g_fullbright.load()));
    LOGI("NoFog       : %s", onoff(g_noFog.load()));
    LOGI("ESP Boxes   : %s", onoff(g_espBoxes.load()));
    LOGI("ESP Names   : %s", onoff(g_espNames.load()));
    LOGI("ESP Dist    : %s", onoff(g_espDistance.load()));
    LOGI("Tracers     : %s", onoff(g_tracers.load()));
    LOGI("ESP MaxDist : %.0f", g_espMaxDist.load());
    LOGI("--- CONFIGURATION ---");
    LOGI("MenuVisible : %s", onoff(g_menu_visible.load()));
    LOGI("MenuPaused  : %s", onoff(g_menuPaused.load()));
    LOGI("VerboseLog  : %s", onoff(g_logVerbose.load()));
    LOGI("SafeMode    : %s", onoff(g_safeMode.load()));
    LOGI("AutoRehook  : %s", onoff(g_autoRehook.load()));
    LOGI("MenuPage    : %d", g_menuPage.load());
    LOGI("RefreshSec  : %d", g_refreshSec.load());
    LOGI("--- MISC ---");
    LOGI("FPS Unlock  : %s", onoff(g_fpsUnlock.load()));
    LOGI("NoTutorial  : %s", onoff(g_noTutorial.load()));
    LOGI("SkipCuts    : %s", onoff(g_skipCutscenes.load()));
    LOGI("FreeCosmetics: %s", onoff(g_freeCosmetics.load()));
    LOGI("UnlockAll   : %s", onoff(g_unlockAll.load()));
    LOGI("DebugDraw   : %s", onoff(g_debugDraw.load()));
    LOGI("========================");
}

// ===================== FEATURE HOOKS (stubs) =====================
// Estos se conectan a los métodos reales de:
//  - Damage / health (God)
//  - Locomotion (Fly / Speed / NoClip)
//  - ScrapSystem (Inf Scrap)
// Cuando tengas MethodInfo* de LARMenu / Player / Scrap, se rellenan.

static void apply_features() {
    // ejemplo: si g_god -> invocar SetInvincible(true) o similar
    // ejemplo: si g_inf_scrap -> set scrap count alto
    if (g_god.load() || g_infHealth.load() || g_noDamage.load()) {
        // wire health / invincible
    }
    if (g_inf_scrap.load()) {
        // AddScrap(g_scrapAmount)
    }
    if (g_fly.load() || g_noclip.load() || g_speed.load()) {
        // locomotion
    }
    if (g_oneHit.load() || g_killAura.load() || g_infAmmo.load()) {
        // combat
    }
}

// ===================== INIT =====================
static bool resolve_il2cpp() {
    if (g_il2cpp) return true;
    g_il2cpp = dlopen("libil2cpp.so", RTLD_NOW);
    if (!g_il2cpp)
        g_il2cpp = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (!g_il2cpp) {
        LOGE("dlopen libil2cpp.so failed: %s", dlerror());
        return false;
    }
    il2cpp_domain_get = (il2cpp_domain_get_t)dlsym(g_il2cpp, "il2cpp_domain_get");
    il2cpp_domain_get_assemblies = (il2cpp_domain_get_assemblies_t)dlsym(g_il2cpp, "il2cpp_domain_get_assemblies");
    il2cpp_assembly_get_image = (il2cpp_assembly_get_image_t)dlsym(g_il2cpp, "il2cpp_assembly_get_image");
    il2cpp_class_from_name = (il2cpp_class_from_name_t)dlsym(g_il2cpp, "il2cpp_class_from_name");
    il2cpp_class_get_method_from_name = (il2cpp_class_get_method_from_name_t)dlsym(g_il2cpp, "il2cpp_class_get_method_from_name");
    il2cpp_class_get_field_from_name = (il2cpp_class_get_field_from_name_t)dlsym(g_il2cpp, "il2cpp_class_get_field_from_name");
    il2cpp_runtime_invoke = (il2cpp_runtime_invoke_t)dlsym(g_il2cpp, "il2cpp_runtime_invoke");
    il2cpp_object_new = (il2cpp_object_new_t)dlsym(g_il2cpp, "il2cpp_object_new");
    il2cpp_thread_attach = (il2cpp_thread_attach_t)dlsym(g_il2cpp, "il2cpp_thread_attach");
    il2cpp_string_new = (il2cpp_string_new_t)dlsym(g_il2cpp, "il2cpp_string_new");
    il2cpp_method_get_name = (il2cpp_method_get_name_t)dlsym(g_il2cpp, "il2cpp_method_get_name");

    if (!il2cpp_domain_get || !il2cpp_class_from_name) {
        LOGE("il2cpp symbols missing");
        return false;
    }
    LOGI("il2cpp resolved");
    return true;
}

static void* main_thread(void*) {
    sleep(3); // esperar a que il2cpp arranque
    if (!resolve_il2cpp()) {
        LOGE("il2cpp failed — menu still runs");
    } else {
        if (il2cpp_thread_attach && il2cpp_domain_get)
            il2cpp_thread_attach(il2cpp_domain_get());

        // buscar clases conocidas del juego
        void* kickMenu = find_method("LethalApeR", "KickMenuGUI", "KickPlayer", 1);
        void* kickHammer = find_method("LethalApeR", "KickHammer", "DoKick", 1);
        LOGI("KickMenuGUI.KickPlayer = %p", kickMenu);
        LOGI("KickHammer.DoKick     = %p", kickHammer);

        // aquí irían los inline hooks / detours a anti_kick_stub
        sleep(1);
        install_all_hooks();
    }

    while (g_running.load()) {
        if (!g_menuPaused.load()) {
            if (g_menu_visible.load()) dump_menu_state();
            apply_features();
            g_ticks++;
            if (g_autoRehook.load() && g_hooksInstalled.load() == 0 && g_il2cpp)
                install_all_hooks();
        }
        sleep(g_refreshSec.load() > 0 ? g_refreshSec.load() : 2);
    }
    return nullptr;
}

__attribute__((constructor))
static void on_load() {
    LOGI("liblar_menu.so loaded — anti-kick default ON");
    pthread_t t;
    pthread_create(&t, nullptr, main_thread, nullptr);
    pthread_detach(t);
}

__attribute__((destructor))
static void on_unload() {
    g_running = false;
    LOGI("liblar_menu.so unloaded");
}

// JNI opcional por si lo cargas desde Java/smali
extern "C" JNIEXPORT void JNICALL
Java_com_lar_Menu_setFlag(JNIEnv*, jclass, jint id, jboolean v) {
    bool b = (v != JNI_FALSE);
    switch (id) {
        case 0: g_antikick  = b; break;
        case 1: g_god       = b; break;
        case 2: g_fly       = b; break;
        case 3: g_speed     = b; break;
        case 4: g_noclip    = b; break;
        case 5: g_inf_scrap = b; break;
        case 6: g_menu_visible = b; break;
        case 7:  g_noDamage = b; break;
        case 8:  g_infHealth = b; break;
        case 9:  g_infStamina = b; break;
        case 10: g_oneHit = b; break;
        case 11: g_infAmmo = b; break;
        case 12: g_killAura = b; break;
        case 13: g_antiBan = b; break;
        case 14: g_forceHost = b; break;
        case 15: g_revealPlayers = b; break;
        case 16: g_espBoxes = b; break;
        case 17: g_safeMode = b; break;
        case 18: g_superJump = b; break;
        case 19: g_freeShop = b; break;
        case 20: g_fullbright = b; break;
        default: break;
    }
    dump_menu_state();
}

extern "C" JNIEXPORT void JNICALL
Java_com_lar_Menu_set(JNIEnv* e, jclass c, jint id, jboolean v) {
    Java_com_lar_Menu_setFlag(e, c, id, v);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_lar_Menu_get(JNIEnv*, jclass, jint id) {
    switch (id) {
        case 0: return g_antikick.load();
        case 1: return g_god.load();
        case 2: return g_fly.load();
        case 3: return g_speed.load();
        case 4: return g_noclip.load();
        case 5: return g_inf_scrap.load();
        case 6: return g_menu_visible.load();
        default: return JNI_FALSE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_lar_Menu_toggle(JNIEnv* e, jclass c, jint id) {
    jboolean cur = Java_com_lar_Menu_get(e, c, id);
    Java_com_lar_Menu_setFlag(e, c, id, cur ? JNI_FALSE : JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_lar_Menu_setFloat(JNIEnv*, jclass, jint id, jfloat v) {
    switch (id) {
        case 100: g_speedMul = v; break;
        case 101: g_flySpeed = v; break;
        case 102: g_jumpMul = v; break;
        case 103: g_damageMul = v; break;
        case 104: g_auraRange = v; break;
        case 105: g_lootRange = v; break;
        case 106: g_espMaxDist = v; break;
        case 107: g_healthOverride = v; break;
        case 108: g_staminaOverride = v; break;
        default: break;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_lar_Menu_setInt(JNIEnv*, jclass, jint id, jint v) {
    switch (id) {
        case 200: g_scrapAmount = v; break;
        case 201: g_menuPage = v; break;
        case 202: g_refreshSec = v < 1 ? 1 : v; break;
        default: break;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_lar_Menu_draw(JNIEnv*, jclass) {
    dump_menu_state();
}

extern "C" void lar_menu_start(void) { LOGI("lar_menu_start"); }
extern "C" void lar_menu_stop(void)  { g_running = false; }
