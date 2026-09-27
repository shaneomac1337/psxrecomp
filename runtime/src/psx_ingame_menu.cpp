/* psx_ingame_menu.cpp - in-game pause menu over the Vulkan present pass.
 *
 * recomp-ui supplies the menu model and its ImGui presentation (the same theme
 * as the launcher); this file owns the ImGui context, the imgui_impl_vulkan
 * backend bound to the renderer's overlay pass (gpu_vk_ui_overlay.h), font
 * rasterisation for the current window size, and the PSX item set. */

#include "psx_ingame_menu.h"

#include <cstdint>
#include "gpu_vk_renderer.h"
#include "gpu_vk_ui_overlay.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "launcher_theme.h"
#include "recomp_runtime_ui.h"
#include "recomp_runtime_ui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

/* Layout is authored for a 720-line screen and scaled to the window; glyphs
 * are rasterised at the real density so text stays sharp at 1440p/4K. */
constexpr float kLogicalHeight = 720.0f;

PsxIngameMenuHost g_host;
std::string g_title = "Game";
std::string g_font_path;

ImGuiContext *g_ctx;
bool g_backend_ready;
bool g_backend_failed;
float g_font_scale;
bool g_frame_ready;

/* Pointer: hidden until the mouse moves, hidden again by key/pad input. */
bool g_mouse_visible;
float g_mouse_nx, g_mouse_ny;

RecompRuntimeUi *g_ui;

/* Always-on ring of menu events for the debug server (ingame_menu). */
struct MenuEvent {
    uint32_t seq;
    char kind;          /* i input, m mouse move, b button, s selection change */
    int a, b;
    int section, row, in_section;
};
constexpr int kRing = 48;
MenuEvent g_ring[kRing];
uint32_t g_ring_seq;
int g_last_section = -1, g_last_row = -1, g_last_in = -1;

void note(char kind, int a, int b) {
    MenuEvent &e = g_ring[g_ring_seq % kRing];
    e.seq = g_ring_seq++;
    e.kind = kind;
    e.a = a;
    e.b = b;
    e.section = g_ui ? (int)g_ui->section_index : -1;
    e.row = g_ui ? (int)g_ui->row_index : -1;
    e.in_section = g_ui ? g_ui->in_section : -1;
    g_last_section = e.section;
    g_last_row = e.row;
    g_last_in = e.in_section;
}
bool g_quit_armed;
bool g_launcher_armed;
bool g_open_states_after_close;

const char *const kScreenChoices[] = {"Off (sharp pixels)", "CRT", "Composite",
                                      "Trinitron"};

enum ItemId { kResume, kStates, kQuit, kFullscreen, kScreen, kVolume, kLauncher };

const RecompRuntimeUiItem kItems[] = {
    {"game.resume", "Game", "Resume", "Close the menu and return to the match.",
     RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr},
    {"game.states", "Game", "Save / load state",
     "Open the save-state slots (with screenshots).",
     RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr},
    {"game.quit", "Game", "Quit to desktop",
     "Exit the game. Progress that is not on a memory card or save state is lost.",
     RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr},
    {RECOMP_RUNTIME_UI_KEY_FULLSCREEN, "Display", "Fullscreen",
     "Borderless fullscreen or a window.", RECOMP_RUNTIME_UI_BOOL, 0, 1, 1,
     nullptr, 0, nullptr},
    {"display.screen_filter", "Display", "Screen filter",
     "Sharp pixels, or a CRT television look.", RECOMP_RUNTIME_UI_CHOICE, 0, 3,
     1, kScreenChoices, 4, nullptr},
    {RECOMP_RUNTIME_UI_KEY_VOLUME, "Audio", "Volume", "Game volume.",
     RECOMP_RUNTIME_UI_INT, 0, 100, 5, nullptr, 0, nullptr},
    {"system.launcher", "Settings", "Open full settings",
     "Restart into the launcher: resolution, widescreen, mods, controls.",
     RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr},
};
constexpr size_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);

int item_id(const RecompRuntimeUiItem *item) {
    return static_cast<int>(item - kItems);
}

int cb_get(void *, const RecompRuntimeUiItem *item, int *out) {
    switch (item_id(item)) {
    case kFullscreen:
        *out = g_host.get_fullscreen ? g_host.get_fullscreen() : 0;
        return 1;
    case kScreen:
        *out = g_host.get_screen_kind ? g_host.get_screen_kind() : 0;
        return 1;
    case kVolume:
        *out = g_host.get_volume ? g_host.get_volume() : 100;
        return 1;
    default:
        return 0;
    }
}

int cb_set(void *, const RecompRuntimeUiItem *item, int value) {
    switch (item_id(item)) {
    case kFullscreen:
        if (!g_host.set_fullscreen) return 0;
        g_host.set_fullscreen(value);
        return 1;
    case kScreen:
        if (!g_host.set_screen_kind) return 0;
        g_host.set_screen_kind(value);
        return 1;
    case kVolume:
        if (!g_host.set_volume) return 0;
        g_host.set_volume(value);
        return 1;
    default:
        return 0;
    }
}

int cb_action(void *, const RecompRuntimeUiItem *item) {
    const int id = item_id(item);
    if (id != kQuit) g_quit_armed = false;
    if (id != kLauncher) g_launcher_armed = false;
    switch (id) {
    case kResume:
        recomp_runtime_ui_close(g_ui);
        return 1;
    case kStates:
        g_open_states_after_close = true;
        recomp_runtime_ui_close(g_ui);
        return 1;
    case kQuit:
        if (!g_quit_armed) {
            g_quit_armed = true;
            recomp_runtime_ui_set_status(g_ui, "Press again to quit");
            return 0;
        }
        if (g_host.quit) g_host.quit();
        return 1;
    case kLauncher:
        if (!g_launcher_armed) {
            g_launcher_armed = true;
            recomp_runtime_ui_set_status(g_ui, "The game restarts - press again");
            return 0;
        }
        if (g_host.open_launcher) g_host.open_launcher();
        return 1;
    default:
        return 0;
    }
}

void cb_save(void *) {
    if (g_host.save_settings) g_host.save_settings();
}

void cb_visibility(void *, int open) {
    if (open) return;
    g_quit_armed = g_launcher_armed = false;
    if (g_open_states_after_close) {
        g_open_states_after_close = false;
        if (g_host.open_save_states) g_host.open_save_states();
    }
}

RecompRuntimeUiConfig g_config;

bool ensure_model() {
    if (g_ui) return true;
    g_config = RecompRuntimeUiConfig{};
    g_config.title = g_title.c_str();
    g_config.subtitle = "Paused";
    g_config.items = kItems;
    g_config.item_count = kItemCount;
    g_config.callbacks.get_value = cb_get;
    g_config.callbacks.set_value = cb_set;
    g_config.callbacks.run_action = cb_action;
    g_config.callbacks.save = cb_save;
    g_config.callbacks.visibility_changed = cb_visibility;
    g_config.theme = "psx";
    g_config.accept_label = "Enter / Cross";
    g_config.back_label = "Esc / Circle";
    g_ui = recomp_runtime_ui_create(&g_config);
    return g_ui != nullptr;
}

PFN_vkVoidFunction load_vk(const char *name, void *user) {
    const VkUiOverlayEnv *env = static_cast<const VkUiOverlayEnv *>(user);
    return env->get_instance_proc_addr(env->instance, name);
}

void record_overlay(VkCommandBuffer cb) {
    if (!g_frame_ready || !g_ui || !recomp_runtime_ui_is_open(g_ui)) return;
    ImGui::SetCurrentContext(g_ctx);
    ImDrawData *dd = ImGui::GetDrawData();
    if (dd && dd->Valid) ImGui_ImplVulkan_RenderDrawData(dd, cb);
}

bool ensure_backend() {
    if (g_backend_ready) return true;
    if (g_backend_failed) return false;
    VkUiOverlayEnv env;
    if (!vk_renderer_ui_env(&env)) return false;   /* not Vulkan right now */
    if (!g_ctx) {
        IMGUI_CHECKVERSION();
        ImGuiContext *prev = ImGui::GetCurrentContext();
        g_ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(g_ctx);
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::StyleColorsDark();
        (void)prev;
    }
    ImGui::SetCurrentContext(g_ctx);
    if (!ImGui_ImplVulkan_LoadFunctions(env.api_version, load_vk, &env)) {
        g_backend_failed = true;
        return false;
    }
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.ApiVersion = env.api_version;
    ii.Instance = env.instance;
    ii.PhysicalDevice = env.physical_device;
    ii.Device = env.device;
    ii.QueueFamily = env.queue_family;
    ii.Queue = env.queue;
    ii.RenderPass = env.render_pass;
    ii.MinImageCount = 2;
    ii.ImageCount = env.image_count;
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.DescriptorPoolSize = 16;
    if (!ImGui_ImplVulkan_Init(&ii)) {
        g_backend_failed = true;
        return false;
    }
    g_backend_ready = true;
    vk_renderer_set_ui_overlay(record_overlay);
    return true;
}

/* (Re)rasterise the font for the window's scale; rebuilt only on change. */
void ensure_fonts(float scale) {
    if (std::fabs(scale - g_font_scale) < 0.01f) return;
    ImGuiIO &io = ImGui::GetIO();
    if (g_font_scale > 0.0f) ImGui_ImplVulkan_DestroyFontsTexture();
    io.Fonts->Clear();
    const LauncherTheme theme = launcher_theme_by_name("psx");
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.RasterizerDensity = scale;
    static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0};
    ImFont *font = nullptr;
    if (!g_font_path.empty()) {
        if (FILE *f = std::fopen(g_font_path.c_str(), "rb")) {
            std::fclose(f);
            font = io.Fonts->AddFontFromFileTTF(g_font_path.c_str(),
                                                theme.font_body, &cfg, kRanges);
        }
    }
    if (!font) {
        ImFontConfig def = cfg;
        def.SizePixels = theme.font_body;
        io.Fonts->AddFontDefault(&def);
    }
    ImGui_ImplVulkan_CreateFontsTexture();
    g_font_scale = scale;
}

} // namespace

extern "C" void psx_ingame_menu_configure(const PsxIngameMenuHost *host,
                                          const char *title,
                                          const char *font_path) {
    if (host) g_host = *host;
    if (title && title[0]) g_title = title;
    g_font_path = font_path ? font_path : "";
}

extern "C" int psx_ingame_menu_open(void) {
    if (!ensure_model() || !ensure_backend()) return 0;
    g_quit_armed = g_launcher_armed = false;
    g_frame_ready = false;
    recomp_runtime_ui_open(g_ui);
    return 1;
}

extern "C" void psx_ingame_menu_close(void) {
    if (g_ui) recomp_runtime_ui_close(g_ui);
}

extern "C" int psx_ingame_menu_is_open(void) {
    return g_ui && recomp_runtime_ui_is_open(g_ui);
}

extern "C" void psx_ingame_menu_input(int input, int pressed, int repeat) {
    if (!g_ui) return;
    g_mouse_visible = false;
    recomp_runtime_ui_handle_input(g_ui, static_cast<RecompRuntimeUiInput>(input),
                                   pressed, repeat);
    note('i', input, repeat);
}

extern "C" void psx_ingame_menu_mouse_move(float nx, float ny) {
    g_mouse_visible = true;
    g_mouse_nx = nx;
    g_mouse_ny = ny;
    note('m', (int)(nx * 1000.0f), (int)(ny * 1000.0f));
}

extern "C" void psx_ingame_menu_mouse_button(int down) {
    if (!g_ctx || !g_backend_ready) return;
    ImGui::SetCurrentContext(g_ctx);
    g_mouse_visible = true;
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, down != 0);
    note('b', down, 0);
}

extern "C" void psx_ingame_menu_frame(float dt_seconds) {
    g_frame_ready = false;
    if (!g_backend_ready || !psx_ingame_menu_is_open()) return;
    int w = 0, h = 0;
    vk_renderer_drawable_size(&w, &h);
    if (w <= 0 || h <= 0) return;
    ImGui::SetCurrentContext(g_ctx);
    const float scale = std::max(1.0f, static_cast<float>(h) / kLogicalHeight);
    ensure_fonts(scale);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(w) / scale,
                            static_cast<float>(h) / scale);
    io.DisplayFramebufferScale = ImVec2(scale, scale);
    io.DeltaTime = dt_seconds > 0.0f ? dt_seconds : 1.0f / 60.0f;
    if (g_mouse_visible)
        io.AddMousePosEvent(g_mouse_nx * io.DisplaySize.x, g_mouse_ny * io.DisplaySize.y);
    else
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    recomp_runtime_ui_render_imgui(g_ui);
    ImGui::Render();
    g_frame_ready = true;
    if ((int)g_ui->section_index != g_last_section || (int)g_ui->row_index != g_last_row ||
        g_ui->in_section != g_last_in)
        note('s', 0, 0);   /* changed by the presentation (hover / click) */
}

extern "C" int psx_ingame_menu_debug_json(char *out, int cap) {
    if (!out || cap <= 0) return 0;
    int n = std::snprintf(out, (size_t)cap,
        "\"available\":%s,\"open\":%s,\"section\":%d,\"row\":%d,\"in_section\":%d,"
        "\"mouse_visible\":%s,\"events\":[",
        g_backend_ready ? "true" : "false", psx_ingame_menu_is_open() ? "true" : "false",
        g_ui ? (int)g_ui->section_index : -1, g_ui ? (int)g_ui->row_index : -1,
        g_ui ? g_ui->in_section : -1, g_mouse_visible ? "true" : "false");
    const uint32_t first = g_ring_seq > (uint32_t)kRing ? g_ring_seq - kRing : 0;
    for (uint32_t q = first; q < g_ring_seq && n < cap; ++q) {
        const MenuEvent &e = g_ring[q % kRing];
        n += std::snprintf(out + n, (size_t)(cap - n),
            "%s{\"seq\":%u,\"kind\":\"%c\",\"a\":%d,\"b\":%d,\"section\":%d,\"row\":%d,\"in\":%d}",
            q == first ? "" : ",", e.seq, e.kind, e.a, e.b, e.section, e.row, e.in_section);
    }
    if (n < cap) n += std::snprintf(out + n, (size_t)(cap - n), "]");
    return n < cap ? n : cap - 1;
}
