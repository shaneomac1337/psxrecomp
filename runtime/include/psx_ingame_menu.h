#ifndef PSX_INGAME_MENU_H
#define PSX_INGAME_MENU_H

/* psx_ingame_menu.cpp - the in-game pause menu (Esc / PS button).
 *
 * The shared recomp-ui runtime menu model, drawn with Dear ImGui's Vulkan
 * backend over the frozen game frame. The menu owns presentation and item
 * behaviour; the host owns the pause loop, input translation, and everything
 * the items change (window, screen filter, volume, persistence, exit), which
 * it supplies through PsxIngameMenuHost. Only built into launcher + Vulkan
 * builds (PSX_HAVE_INGAME_MENU); psx_ingame_menu_open() returns 0 whenever
 * the Vulkan present path is not the active one. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsxIngameMenuHost {
    int  (*get_fullscreen)(void);
    void (*set_fullscreen)(int on);
    int  (*get_screen_kind)(void);        /* ScreenKind 0..3 */
    void (*set_screen_kind)(int kind);
    int  (*get_volume)(void);             /* 0..100 */
    void (*set_volume)(int percent);
    void (*open_save_states)(void);       /* after the menu has closed */
    void (*open_launcher)(void);          /* restart into the launcher */
    void (*quit)(void);
    void (*save_settings)(void);          /* persist fullscreen / filter */
} PsxIngameMenuHost;

/* Semantic inputs; values match RecompRuntimeUiInput. */
enum {
    PSX_MENU_INPUT_TOGGLE = 0,
    PSX_MENU_INPUT_BACK,
    PSX_MENU_INPUT_UP,
    PSX_MENU_INPUT_DOWN,
    PSX_MENU_INPUT_LEFT,
    PSX_MENU_INPUT_RIGHT,
    PSX_MENU_INPUT_ACCEPT,
};

/* title: game name shown in the menu header; font_path: launcher TTF (may be
 * NULL/missing - ImGui's built-in font is used). Strings are copied. */
void psx_ingame_menu_configure(const PsxIngameMenuHost *host,
                               const char *title, const char *font_path);
/* Opens the menu; returns 0 (and stays closed) if it cannot be shown. */
int  psx_ingame_menu_open(void);
void psx_ingame_menu_close(void);
int  psx_ingame_menu_is_open(void);
void psx_ingame_menu_input(int input, int pressed, int repeat);
/* Mouse, in window-normalised coordinates (0..1). Moving shows the pointer;
 * any keyboard/pad input hides it again so hover never fights the selection. */
void psx_ingame_menu_mouse_move(float nx, float ny);
void psx_ingame_menu_mouse_button(int down);
/* Build this frame's UI; call once per host present while open. */
void psx_ingame_menu_frame(float dt_seconds);
/* Debug server: menu state + a ring of recent inputs / selection changes as
 * a JSON object body (no braces). Returns bytes written. */
int  psx_ingame_menu_debug_json(char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* PSX_INGAME_MENU_H */
