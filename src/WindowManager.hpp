#pragma once

#include "WindowManager.h"

#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "PortMenu.hpp"
#include "QuickDraw.hpp"
#include "SDLHelpers.hpp"
#include "presentation/AdaptiveShell.hpp"
#include "presentation/PresentationHost.hpp"
#include "presentation/RemasteredInputMapper.hpp"
#include "presentation/RuntimeLegacyCommandBridge.hpp"
#include "presentation/ShellControlLayout.hpp"
#include "presentation/ShellKeyboardInteraction.hpp"

class WindowManager;
class Window;
class DialogItem;
namespace realmz::remaster::assets {
class PartyPortraitTextureCache;
class ShellMaterialTextureCache;
}

class Window : public std::enable_shared_from_this<Window> {
private:
  phosg::PrefixedLogger log;
  std::string title;
  CCGrafPort port;
  int16_t window_kind;
  bool visible;
  bool is_dialog_flag;
  std::vector<std::shared_ptr<DialogItem>> dialog_items; // All items (the below 3 vectors are disjoint subsets of this)
  std::vector<std::shared_ptr<DialogItem>> static_items;
  std::vector<std::shared_ptr<DialogItem>> control_items;
  std::vector<std::shared_ptr<DialogItem>> text_items;
  std::shared_ptr<DialogItem> focused_item;
  bool text_caret_visible = false;
  uint64_t text_caret_next_toggle = 0;
  std::shared_ptr<Window> window_below;
  std::shared_ptr<Window> window_above;

  void reset_text_caret();

  Window(
      const std::string& title,
      const Rect& bounds,
      int16_t window_kind,
      bool visible,
      bool is_dialog,
      const RGBColor& background_color,
      std::vector<std::shared_ptr<DialogItem>>&& dialog_items);

public:
  static std::shared_ptr<Window> make_shared(
      const std::string& title,
      const Rect& bounds,
      int16_t window_kind,
      bool visible,
      bool is_dialog,
      const RGBColor& background_color,
      std::vector<std::shared_ptr<DialogItem>>&& dialog_items);
  ~Window() = default;

  inline std::string ref() const {
    return std::format("W-{:016X}", reinterpret_cast<intptr_t>(this));
  }

  inline const Rect& bounds() const {
    return this->port.portRect;
  }
  inline Rect& bounds() {
    return this->port.portRect;
  }
  inline size_t get_width() {
    return this->port.portRect.right - this->port.portRect.left;
  }
  inline size_t get_height() {
    return this->port.portRect.bottom - this->port.portRect.top;
  }

  void add_dialog_item(std::shared_ptr<DialogItem> item);
  CCGrafPort& get_port();
  std::shared_ptr<DialogItem> get_focused_item();
  bool is_text_caret_visible() const;
  void set_focused_item(std::shared_ptr<DialogItem> item);
  void idle_text_caret();
  void handle_text_input(const std::string& text, std::shared_ptr<DialogItem> item);
  void delete_char(std::shared_ptr<DialogItem> item);
  void erase_and_render();
  void move(int hGlobal, int vGlobal);
  void resize(uint16_t w, uint16_t h);
  void show();
  const std::vector<std::shared_ptr<DialogItem>>& get_dialog_items() const;
  std::shared_ptr<DialogItem> dialog_item_for_position(const Point& pt, bool enabled_only);
  inline bool is_dialog() const;
  TEHandle add_text_edit(const Rect& dest_rect, const Rect& view_rect);
  void remove_text_edit(std::shared_ptr<DialogItem> item);

  friend class WindowManager;
};

class WindowManager : private realmz::presentation::ClassicFrameTarget {
public:
  CCGrafPort screen_port;

private:
  std::unordered_map<DialogItemHandle, std::shared_ptr<DialogItem>> dialog_items_by_handle;
  // TODO(fuzziqersoftware): It'd be nice to get rid of this map and treat Windows similarly to CCGrafPorts. This is
  // nontrivial because Window inherits from std::enable_shared_from_this, which has a private field and could cause
  // Window to no longer be standard layout, which would break compatibility with C code.
  std::unordered_map<WindowPtr, std::shared_ptr<Window>> port_to_window;
  std::shared_ptr<Window> top_window;
  std::shared_ptr<Window> bottom_window;
  sdl_window_shared sdl_window;
  // Declared after the SDL window so its renderer-owned textures are destroyed
  // before SDL_DestroyWindow tears down the associated renderer.
  std::unique_ptr<realmz::remaster::assets::ShellMaterialTextureCache>
      remastered_shell_materials;
  SDL_Renderer* remastered_shell_material_renderer = nullptr;
  bool remastered_shell_materials_attempted = false;
  std::unique_ptr<realmz::remaster::assets::PartyPortraitTextureCache>
      remastered_party_portraits;
  SDL_Renderer* remastered_party_portrait_renderer = nullptr;
  bool remastered_party_portraits_attempted = false;
  bool text_editing_active = false;
  bool recomposite_enabled = true;
  SDL_ScaleMode scale_mode = SDL_SCALEMODE_PIXELART;
  bool aspect_locked = true;
  int gamma_idx = 0;
  realmz::presentation::PresentationHost presentation_host;
  std::optional<realmz::presentation::AdaptiveShellPlan> adaptive_shell_plan;
  std::optional<realmz::presentation::RemasteredInputMapper>
      remastered_input_mapper;
  std::optional<realmz::presentation::RemasteredPointerCapture>
      remastered_pointer_capture;
  std::vector<realmz::presentation::ShellControlPlacement>
      remastered_shell_controls;
  std::optional<realmz::presentation::ShellControlPlacement>
      remastered_pressed_shell_control;
  realmz::presentation::ShellKeyboardInteraction
      remastered_shell_keyboard;
  std::optional<realmz::presentation::DrawerPanel>
      remastered_active_drawer;
  realmz::presentation::CombatActionPage remastered_combat_action_page =
      realmz::presentation::CombatActionPage::primary;
  struct RemasteredCombatCursorSample {
    realmz::presentation::CombatantId combatant = 0;
    realmz::presentation::CombatFieldCell cell;
    int32_t field_origin_x = 0;
    int32_t field_origin_y = 0;
    size_t visible_columns = 0;
    size_t visible_rows = 0;

    bool operator==(const RemasteredCombatCursorSample&) const = default;
  };
  std::optional<RemasteredCombatCursorSample>
      remastered_combat_cursor_sample;
  std::unique_ptr<realmz::presentation::RuntimeLegacyCommandBridge>
      runtime_legacy_command_bridge;
  realmz::presentation::ActionSequence next_shell_action_sequence = 1;
  int windowed_w = kLogicalWindowWidth;
  int windowed_h = kLogicalWindowHeight;
  int windowed_x = SDL_WINDOWPOS_CENTERED;
  int windowed_y = SDL_WINDOWPOS_CENTERED;
  // Cached gamma correction state, so the present path does not rebuild the LUT
  // or reallocate the pixel buffer every frame. The LUT is rebuilt only when
  // gamma_idx changes; the scratch buffer is reused across presents.
  int gamma_lut_idx = -1;
  uint8_t gamma_lut[256] = {};
  std::vector<uint32_t> gamma_scratch;

  WindowManager();

public:
  static WindowManager& instance();
  ~WindowManager();
  void create_sdl_window();
  WindowPtr create_window(
      const std::string& title,
      const Rect& bounds,
      bool visible,
      bool go_away,
      int16_t proc_id,
      uint32_t ref_con,
      bool is_dialog,
      const RGBColor& background_color,
      std::vector<std::shared_ptr<DialogItem>>&& dialog_items);
  void destroy_window(WindowPtr port);
  std::shared_ptr<Window> window_for_port(WindowPtr port);
  std::shared_ptr<DialogItem> dialog_item_for_handle(DialogItemHandle handle);
  std::shared_ptr<Window> front_window();
  void link_window_at_front(std::shared_ptr<Window> window);
  void unlink_window(std::shared_ptr<Window> window);
  void bring_to_front(std::shared_ptr<Window> window);
  std::shared_ptr<Window> window_for_point(ssize_t x, ssize_t y);

  void on_dialog_item_focus_changed();

  void recomposite(std::shared_ptr<Window> updated_window);
  bool set_enable_recomposite(bool enable);

  // Uploads the current contents of screen_port to the SDL window and presents
  // it, without recompositing the window stack first. This is used by code that
  // draws transient graphics directly onto the screen buffer (for example, an
  // item icon dragged by the mouse in the shop), where recompositing would
  // immediately erase those graphics.
  void present_screen();

  // Recomposites the window stack, starting with the given window. Windows below
  // the given window are not recomposited. Updates the SDL window with the
  // rendered result.
  void recomposite_from_window(CCGrafPort& updated_port);
  void recomposite_from_window(std::shared_ptr<Window> updated_window);
  void recomposite_all();

  // SDL render-device reset events invalidate every renderer-owned texture
  // even when the SDL_Renderer pointer itself remains stable.
  void invalidate_remastered_shell_materials();
  void invalidate_remastered_party_portraits();

  inline sdl_window_shared get_sdl_window() const {
    return this->sdl_window;
  }

  void on_debug_signal();

  SDL_ScaleMode get_scale_mode() const { return this->scale_mode; }
  void set_scale_mode(SDL_ScaleMode mode);

  int get_gamma_idx() const { return this->gamma_idx; }
  void set_gamma_idx(int idx);
  realmz::presentation::PresentationMode get_presentation_mode() const {
    return this->presentation_host.mode();
  }
  void set_presentation_mode(realmz::presentation::PresentationMode mode);

  // Replay bypasses physical shell hit-testing but retains the production
  // context mapper and semantic bridge. These methods are callable only from
  // the guarded gameplay-poll coordinator; ordinary UI dispatch is unchanged.
  [[nodiscard]] std::optional<std::uint32_t> replay_movement_key_message(
      const realmz::presentation::UIAction& action,
      std::uint32_t semantic_surface) const noexcept;
  [[nodiscard]] std::optional<realmz::presentation::PartyMemberId>
  replay_party_selection_member(
      const realmz::presentation::UIAction& action,
      std::uint32_t semantic_surface) const noexcept;
  [[nodiscard]] std::optional<std::uint32_t>
  replay_switch_weapon_key_message(
      const realmz::presentation::UIAction& action,
      std::uint32_t semantic_surface) const noexcept;
  [[nodiscard]] realmz::presentation::DispatchResult
  dispatch_replay_semantic_action(
      const realmz::presentation::UIAction& action);

  // Pointer input arrives in the renderer's current logical coordinates. In
  // Remastered mode these helpers route only the embedded Classic region into
  // the legacy event queue; shell chrome remains a separate semantic surface.
  bool map_remastered_pointer_motion(float x, float y, Point* classic_point);
  bool begin_remastered_pointer(float x, float y, Point* classic_point);
  bool end_remastered_pointer(float x, float y, Point* classic_point);
  void cancel_remastered_pointer_capture();

  // Returns true when a code-native shell control owns this physical SDL key
  // event. Consumed events must not also enter the Classic event queue.
  bool handle_remastered_shell_key(const SDL_KeyboardEvent& event);
  void cancel_remastered_keyboard_route();

  // Converts legacy global Classic coordinates into the active renderer's
  // logical space. Popup anchors, IME rectangles, and cursor warps all use the
  // same transform as the visible embedded frame.
  bool classic_to_render_point(float* x, float* y) const;

  bool get_aspect_locked() const { return this->aspect_locked; }
  void set_aspect_locked(bool locked);

  void set_window_size(int w, int h);
  bool size_fits(int w, int h) const;
  void get_window_size(int* w, int* h) const;
  bool is_fullscreen() const;

  void note_window_moved();

  void save_prefs();

private:
  void configure_window_for_presentation_mode();
  void update_text_input_area();
  bool classic_to_render_rect(float* x, float* y, float* w, float* h) const;
  [[nodiscard]] sdl_texture_ptr create_classic_frame_texture(
      SDL_Renderer* renderer);
  [[nodiscard]] const realmz::remaster::assets::ShellMaterialTextureCache*
  ensure_remastered_shell_materials(SDL_Renderer* renderer);
  [[nodiscard]] const realmz::remaster::assets::PartyPortraitTextureCache*
  ensure_remastered_party_portraits(SDL_Renderer* renderer);
  void present_classic_frame() override;
  void present_remastered_frame() override;
  void dispatch_remastered_shell_control(
      const realmz::presentation::ShellControlPlacement& control);
  [[nodiscard]] bool refresh_remastered_combat_cursor_sample(
      const realmz::presentation::RemasteredPointerTarget& target);
  [[nodiscard]] bool remastered_combat_cursor_sample_matches(
      const realmz::presentation::GameSnapshot& snapshot) const noexcept;
  [[nodiscard]] bool remastered_shell_keyboard_route_is_eligible() const;
  void print_window_stack() const;
  void verify_window_stack() const;
};
