#include "WindowManager.hpp"

#include "AppIdentity.hpp"
#include "PortMenu.hpp"
#include "PortPrefs.hpp"

#ifdef __APPLE__
#include "macos/WindowAspect.h"
#endif

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_properties.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <phosg/Strings.hh>
#include <resource_file/BitmapFontRenderer.hh>
#include <resource_file/ResourceFile.hh>

#include "EventManager.h"
#include "Font.hpp"
#include "MemoryManager.h"
#include "QuickDraw.h"
#include "QuickDraw.hpp"
#include "ResourceManager.h"
#include "ResourceManagerRemaster.hpp"
#include "StringConvert.hpp"
#include "Types.hpp"
#include "presentation/DrawerControlLayout.hpp"
#include "presentation/LegacyGameSnapshotSource.hpp"
#include "presentation/LegacyPresentationContext.h"
#include "presentation/PartyRailControlLayout.hpp"
#include "presentation/PartyRailLayout.hpp"
#include "presentation/PartyRailModel.hpp"
#include "presentation/SemanticInputBoundary.h"

using ResourceDASM::ResourceFile;

// Enable these to save an image named debug*.bmp every time the main window or dialog items are recomposited
static constexpr bool ENABLE_RECOMPOSITE_DEBUG = false;
static constexpr bool ENABLE_DIALOG_RECOMPOSITE_DEBUG = false;
static constexpr uint64_t TEXT_CARET_BLINK_INTERVAL_MS = 500;
static constexpr int REMASTER_MINIMUM_WIDTH = 1024;
static constexpr int REMASTER_MINIMUM_HEIGHT = 768;
bool enable_translucent_window_debug = false;
static size_t debug_number = 1;

static realmz::presentation::RuntimeLegacyCommandContext
capture_runtime_legacy_command_context() noexcept {
  const auto legacy_context = RealmzCaptureLegacyPresentationContext();
  realmz::presentation::RuntimeLegacyCommandContext context{
      .screen = realmz::presentation::screen_context_from_legacy(
          legacy_context),
      .world_presentation =
          realmz::presentation::WorldPresentation::none,
      .adaptive_eligible = legacy_context.adaptive_eligible != 0,
  };
  if (!context.adaptive_eligible) {
    return context;
  }
  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    if (snapshot.screen != context.screen) {
      context.adaptive_eligible = false;
      return context;
    }
    context.world_presentation = snapshot.world.presentation;
  } catch (...) {
    // Any snapshot failure makes semantic input ineligible; the embedded
    // Classic frame remains the complete fallback interaction route.
    context.adaptive_eligible = false;
  }
  return context;
}

static std::optional<realmz::presentation::ShellKeyboardKey>
shell_keyboard_key_for_sdl(SDL_Keycode key) noexcept {
  using realmz::presentation::ShellKeyboardKey;
  switch (key) {
    case SDLK_TAB:
      return ShellKeyboardKey::tab;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_RETURN2:
      return ShellKeyboardKey::enter;
    case SDLK_SPACE:
      return ShellKeyboardKey::space;
    default:
      return std::nullopt;
  }
}

inline size_t unwrap_opaque_handle(Handle h) {
  static_assert(sizeof(size_t) == sizeof(Handle));
  return reinterpret_cast<size_t>(h);
}
inline Handle wrap_opaque_handle(size_t h) {
  static_assert(sizeof(size_t) == sizeof(Handle));
  return reinterpret_cast<Handle>(h);
}

inline Rect copy_rect(const ResourceDASM::Rect& src) {
  return Rect{.top = src.y1.load(), .left = src.x1.load(), .bottom = src.y2.load(), .right = src.x2.load()};
}

static void draw_dbox_frame_line(
    CCGrafPort& port,
    const Rect& content_rect,
    int16_t offset,
    uint32_t top_left_color,
    uint32_t bottom_right_color) {
  int32_t left = content_rect.left - offset;
  int32_t top = content_rect.top - offset;
  int32_t right = content_rect.right + offset - 1;
  int32_t bottom = content_rect.bottom + offset - 1;

  port.data.draw_horizontal_line(left, right, top, 0, top_left_color);
  port.data.draw_vertical_line(left, top, bottom, 0, top_left_color);
  port.data.draw_horizontal_line(left, right, bottom, 0, bottom_right_color);
  port.data.draw_vertical_line(right, top, bottom, 0, bottom_right_color);
}

static void draw_dbox_frame(CCGrafPort& port, const Rect& content_rect) {
  constexpr int16_t frame_offset = 6;
  constexpr int16_t edge_offset = frame_offset - 1;
  constexpr int16_t shadow_trim = 2;

  // Draw Classic Mac dBoxProc chrome outside the dialog content rect.
  draw_dbox_frame_line(port, content_rect, frame_offset, 0x000000FF, 0x000000FF);
  port.data.draw_horizontal_line(
      content_rect.left - edge_offset,
      content_rect.right + edge_offset - 1,
      content_rect.top - edge_offset,
      0,
      0xBBBBBBFF);
  port.data.draw_vertical_line(
      content_rect.left - edge_offset,
      content_rect.top - edge_offset,
      content_rect.bottom + edge_offset - 1,
      0,
      0xBBBBBBFF);
  port.data.draw_horizontal_line(
      content_rect.left - frame_offset + shadow_trim,
      content_rect.right + edge_offset - 1,
      content_rect.bottom + edge_offset - 1,
      0,
      0x555555FF);
  port.data.draw_vertical_line(
      content_rect.right + edge_offset - 1,
      content_rect.top - frame_offset + shadow_trim,
      content_rect.bottom + edge_offset - 1,
      0,
      0x555555FF);
  draw_dbox_frame_line(port, content_rect, 4, 0xFFFFFFFF, 0x999999FF);
  draw_dbox_frame_line(port, content_rect, 3, 0x777777FF, 0x777777FF);
  draw_dbox_frame_line(port, content_rect, 2, 0x777777FF, 0x777777FF);
  draw_dbox_frame_line(port, content_rect, 1, 0x777777FF, 0x777777FF);

  port.data.draw_horizontal_line(
      content_rect.left - frame_offset + shadow_trim,
      content_rect.right + frame_offset,
      content_rect.bottom + frame_offset,
      0,
      0x000000FF);
  port.data.draw_vertical_line(
      content_rect.right + frame_offset,
      content_rect.top - frame_offset + shadow_trim,
      content_rect.bottom + frame_offset,
      0,
      0x000000FF);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// SDL and rendering helpers

static phosg::PrefixedLogger wm_log("[WindowManager] ", DEFAULT_LOG_LEVEL);

static size_t generate_opaque_handle() {
  static size_t next_handle = 1;
  return next_handle++;
}

using DialogItemType = ResourceDASM::ResourceFile::DecodedDialogItem::Type;

static int16_t macos_dialog_item_type_for_resource_dasm_type(DialogItemType type) {
  switch (type) {
    case DialogItemType::BUTTON:
      return 4;
    case DialogItemType::CHECKBOX:
      return 5;
    case DialogItemType::RADIO_BUTTON:
      return 6;
    case DialogItemType::RESOURCE_CONTROL:
      return 7;
    case DialogItemType::TEXT:
      return 8;
    case DialogItemType::EDIT_TEXT:
      return 16;
    case DialogItemType::ICON:
      return 32;
    case DialogItemType::PICTURE:
      return 64;
    case DialogItemType::CUSTOM:
      return 0;
    default:
      throw std::logic_error("Unknown dialog item type");
  }
}

template <>
const char* phosg::name_for_enum<DialogItemType>(DialogItemType type) {
  switch (type) {
    case DialogItemType::BUTTON:
      return "BUTTON";
    case DialogItemType::CHECKBOX:
      return "CHECKBOX";
    case DialogItemType::RADIO_BUTTON:
      return "RADIO_BUTTON";
    case DialogItemType::RESOURCE_CONTROL:
      return "RESOURCE_CONTROL";
    case DialogItemType::TEXT:
      return "TEXT";
    case DialogItemType::EDIT_TEXT:
      return "EDIT_TEXT";
    case DialogItemType::ICON:
      return "ICON";
    case DialogItemType::PICTURE:
      return "PICTURE";
    case DialogItemType::CUSTOM:
      return "CUSTOM";
    default:
      throw std::logic_error("Unknown dialog item type");
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Controls

struct DialogItem;

enum class ControlType {
  // The values here match proc_id in the CNTL resource. There are more of
  // these, but we probably won't need them
  BUTTON = 0,
  CHECKBOX = 1,
  RADIO_BUTTON = 2,
  WINDOW_FONT_BUTTON = 8,
  WINDOW_FONT_CHECKBOX = 9,
  WINDOW_FONT_RADIO_BUTTON = 10,
  SCROLL_BAR = 16,
  POPUP_MENU = 1008,
  UNKNOWN = 0x10000,
};

// This structure is "private" (not accessible in C) like DialogItem; see the
// comment on that structure for reasoning
struct Control {
  std::weak_ptr<DialogItem> dialog_item; // May be null for dynamically-created controls!
  int32_t cntl_resource_id; // 0x00010000 = not from a resource
  size_t opaque_handle;
  ControlType type;
  Rect bounds;
  int16_t value;
  int16_t min;
  int16_t max;
  bool visible = true;
  std::string title;

protected:
  static std::shared_ptr<Control> make_shared(
      int32_t cntl_res_id,
      const Rect& bounds,
      int16_t value,
      int16_t min,
      int16_t max,
      int16_t proc_id,
      bool visible,
      const std::string& title) {
    auto ret = std::make_shared<Control>();
    ret->cntl_resource_id = cntl_res_id;
    ret->opaque_handle = generate_opaque_handle();
    switch (proc_id) {
      case 0:
        ret->type = ControlType::BUTTON;
        break;
      case 1:
        ret->type = ControlType::CHECKBOX;
        break;
      case 2:
        ret->type = ControlType::RADIO_BUTTON;
        break;
      case 8:
        ret->type = ControlType::WINDOW_FONT_BUTTON;
        break;
      case 9:
        ret->type = ControlType::WINDOW_FONT_CHECKBOX;
        break;
      case 10:
        ret->type = ControlType::WINDOW_FONT_RADIO_BUTTON;
        break;
      case 16:
        ret->type = ControlType::SCROLL_BAR;
        break;
      case 1008:
        ret->type = ControlType::POPUP_MENU;
        break;
      default:
        throw std::runtime_error(std::format("Unknown control type {}", proc_id));
    }
    ret->bounds = bounds;
    ret->value = value;
    ret->min = min;
    ret->max = max;
    ret->visible = visible;
    ret->title = title;
    return ret;
  }

public:
  // Create a new control manually. This implements the NewControl syscall.
  static std::shared_ptr<Control> from_params(
      const Rect& bounds,
      int16_t value,
      int16_t min,
      int16_t max,
      int16_t proc_id,
      bool visible,
      const std::string& title) {
    return Control::make_shared(0x00010000, bounds, value, min, max, proc_id, visible, title);
  }
  // Create a new control from a resource. This implements the GetNewControl syscall.
  static std::shared_ptr<Control> from_CNTL(int16_t cntl_resource_id) {
    auto data_handle = GetResource(ResourceDASM::RESOURCE_TYPE_CNTL, cntl_resource_id);
    if (!data_handle) {
      throw std::runtime_error(std::format("CNTL resource {} not found", cntl_resource_id));
    }
    auto def = ResourceDASM::ResourceFile::decode_CNTL(*data_handle, GetHandleSize(data_handle));
    Rect bounds = copy_rect(def.bounds);
    return Control::make_shared(cntl_resource_id, bounds, def.value, def.min, def.max, def.proc_id, def.visible, def.title);
  }
  // Create a new control from a dialog item. This implements controls
  // generated from DITL entries. Annoyingly, this can't be implemented here
  // because it depends on the internals of DialogItem, which is still an
  // incomplete type at this point.
  static std::shared_ptr<Control> from_dialog_item(const DialogItem& item);

  ~Control() = default;

  std::string str() const {
    static const std::unordered_map<ControlType, const char*> type_strs{
        {ControlType::BUTTON, "BUTTON"},
        {ControlType::CHECKBOX, "CHECKBOX"},
        {ControlType::RADIO_BUTTON, "RADIO_BUTTON"},
        {ControlType::WINDOW_FONT_BUTTON, "WINDOW_FONT_BUTTON"},
        {ControlType::WINDOW_FONT_CHECKBOX, "WINDOW_FONT_CHECKBOX"},
        {ControlType::WINDOW_FONT_RADIO_BUTTON, "WINDOW_FONT_RADIO_BUTTON"},
        {ControlType::SCROLL_BAR, "SCROLL_BAR"},
        {ControlType::POPUP_MENU, "POPUP_MENU"},
    };
    return std::format(
        "Control(cntl_resource_id={}, opaque_handle={}, type={}, value={}, min={}, max={}, visible={})",
        this->cntl_resource_id,
        this->opaque_handle,
        type_strs.at(this->type),
        this->value,
        this->min,
        this->max,
        this->visible ? "true" : "false");
  }
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Dialog items

// This structure is "private" (not accessible in C) because it isn't directly
// used there: Realmz only interacts with dialog items through syscalls and
// handles, so we can use C++ types here without breaking anything.
struct DialogItem {
public:
  // Identity
  size_t opaque_handle;
  std::weak_ptr<Window> owner_window;

  // Source information
  int32_t ditl_resource_id; // 0x00010000 = not from a DITL
  size_t item_id;

  // Options
  DialogItemType type;
  int16_t resource_id; // From item definition (generally used for controls)
  Rect rect;
  bool enabled;
  std::shared_ptr<Control> control; // May be null

  // For scrollbars: which part is currently held down (0 = none). Set while
  // TrackControl tracks a press so the held arrow can be drawn pushed in.
  int16_t pressed_part = 0;

  static std::unordered_map<size_t, std::weak_ptr<DialogItem>> all_items;

  static std::shared_ptr<DialogItem> get_item_by_handle(size_t handle) {
    auto item = DialogItem::all_items.at(handle).lock();
    if (!item) {
      throw std::logic_error(std::format(
          "Attempted to get missing or destroyed dialog item (handle was {})", handle));
    }
    return item;
  }

private:
  std::string text;

public:
  // Constructor from a definition
  DialogItem(
      int32_t ditl_res_id,
      size_t item_id,
      const ResourceDASM::ResourceFile::DecodedDialogItem& def)
      : opaque_handle{generate_opaque_handle()},
        ditl_resource_id{ditl_res_id},
        item_id{item_id},
        type{def.type},
        resource_id{def.resource_id},
        enabled{def.enabled},
        text{def.text} {
    this->rect.left = def.bounds.x1;
    this->rect.right = def.bounds.x2;
    this->rect.top = def.bounds.y1;
    this->rect.bottom = def.bounds.y2;
    this->control = Control::from_dialog_item(*this); // May return null
  }
  // Constructor from a control
  DialogItem(std::shared_ptr<Control> control)
      : opaque_handle{control->opaque_handle},
        ditl_resource_id{0x00010000},
        item_id{0},
        type{DialogItemType::UNKNOWN},
        resource_id{0},
        rect{control->bounds},
        enabled{true},
        control{control},
        text{control->title} {
    switch (control->type) {
      case ControlType::BUTTON:
        this->type = DialogItemType::BUTTON;
        break;
      case ControlType::CHECKBOX:
        this->type = DialogItemType::CHECKBOX;
        break;
      case ControlType::RADIO_BUTTON:
        this->type = DialogItemType::RADIO_BUTTON;
        break;

      case ControlType::SCROLL_BAR:
        this->type = DialogItemType::RESOURCE_CONTROL;
        break;

      // We don't support these (yet?)
      // case ControlType::WINDOW_FONT_BUTTON:
      // case ControlType::WINDOW_FONT_CHECKBOX:
      // case ControlType::WINDOW_FONT_RADIO_BUTTON:
      // case ControlType::POPUP_MENU:
      default:
        throw std::runtime_error("unsupported control type");
    }
  }

  DialogItem(DialogItemType type, const Rect& disp_rect, const Rect& view_rect)
      : opaque_handle{generate_opaque_handle()},
        type{type},
        text{""},
        enabled{true},
        rect{disp_rect} {
    if (type != DialogItemType::TEXT) {
      throw std::runtime_error("Cannot dynamically create bounded dialog item unless it is type TEXT");
    }
    if (disp_rect.top != view_rect.top ||
        disp_rect.left != view_rect.left ||
        disp_rect.bottom != view_rect.bottom ||
        disp_rect.right != view_rect.right) {
      throw std::runtime_error("Layout and clip rects of monostyled edit record do not match");
    }
  }

  // Create a list of dialog items from a DITL resource
  static std::vector<std::shared_ptr<DialogItem>> from_DITL(int16_t ditl_resource_id) {
    auto data_handle = GetResource(ResourceDASM::RESOURCE_TYPE_DITL, ditl_resource_id);
    if (!data_handle) {
      throw std::runtime_error(std::format("DITL resource {} not found", ditl_resource_id));
    }
    auto defs = ResourceDASM::ResourceFile::decode_DITL(*data_handle, GetHandleSize(data_handle));

    std::vector<std::shared_ptr<DialogItem>> ret;
    for (const auto& decoded_dialog_item : defs) {
      size_t item_id = ret.size() + 1;
      auto di = ret.emplace_back(new DialogItem(ditl_resource_id, item_id, decoded_dialog_item));
      all_items[di->opaque_handle] = di;
    }
    return ret;
  }

  // Create a single dialog item from a CNTL resource. This is necessary for
  // the NewControl and GetNewControl syscalls. The returned dialog item has
  // an item_id of zero, which will be overwritten by add_dialog_item.
  static std::shared_ptr<DialogItem> from_control(std::shared_ptr<Control> control) {
    auto ret = std::make_shared<DialogItem>(control);
    control->dialog_item = ret;
    all_items[ret->opaque_handle] = ret;
    return ret;
  }

  static std::shared_ptr<DialogItem> from_text_edit(const Rect& dest_rect, const Rect& view_rect) {
    auto ret = std::make_shared<DialogItem>(DialogItemType::TEXT, dest_rect, view_rect);
    all_items[ret->opaque_handle] = ret;
    return ret;
  }

  ~DialogItem() {
    all_items.erase(opaque_handle);
  }

  std::string str() const {
    static const std::unordered_map<DialogItemType, const char*> type_strs{
        {DialogItemType::BUTTON, "BUTTON"},
        {DialogItemType::CHECKBOX, "CHECKBOX"},
        {DialogItemType::RADIO_BUTTON, "RADIO_BUTTON"},
        {DialogItemType::RESOURCE_CONTROL, "RESOURCE_CONTROL"},
        {DialogItemType::HELP_BALLOON, "HELP_BALLOON"},
        {DialogItemType::TEXT, "TEXT"},
        {DialogItemType::EDIT_TEXT, "EDIT_TEXT"},
        {DialogItemType::ICON, "ICON"},
        {DialogItemType::PICTURE, "PICTURE"},
        {DialogItemType::CUSTOM, "CUSTOM"},
        {DialogItemType::UNKNOWN, "UNKNOWN"},
    };
    auto text_str = phosg::format_data_string(this->text);
    auto control_str = this->control ? this->control->str() : "NULL";
    return std::format(
        "DialogItem(ditl_resource_id={}, item_id={}, type={}, resource_id={}, rect=Rect(left={}, top={}, right={}, bottom={}), enabled={}, control={}, handle={}, text={})",
        this->ditl_resource_id,
        this->item_id,
        type_strs.at(this->type),
        this->resource_id,
        this->rect.left,
        this->rect.top,
        this->rect.right,
        this->rect.bottom,
        this->enabled ? "true" : "false",
        control_str.c_str(),
        this->opaque_handle,
        text_str.c_str());
  }

  void render_in_port(CCGrafPort& port, bool erase_background) const {
    if (erase_background) {
      port.erase_rect(this->rect);
    }
    switch (type) {
      case ResourceFile::DecodedDialogItem::Type::PICTURE: {
        auto pict_handle = GetPicture(resource_id);
        if (!pict_handle) {
          wm_log.warning_f("Attempted to draw PICT:{}, but it could not be loaded", resource_id);
        } else {
          port.draw_decoded_pict_from_handle(pict_handle, this->rect);
        }
        break;
      }
      case ResourceFile::DecodedDialogItem::Type::ICON:
        // TODO
        wm_log.warning_f("Attempted to draw ICON dialog item, but it's not implemented");
        break;
      case ResourceFile::DecodedDialogItem::Type::TEXT: {
        if (text.length() > 0 && !port.draw_text(text, this->rect)) {
          wm_log.error_f("Error when rendering text item {}: {}", resource_id, SDL_GetError());
        }
        break;
      }
      case ResourceFile::DecodedDialogItem::Type::BUTTON: {
        if (!port.draw_text(text, this->rect)) {
          wm_log.error_f("Error when rendering button text item {}: {}", resource_id, SDL_GetError());
        }
        break;
      }
      case ResourceFile::DecodedDialogItem::Type::EDIT_TEXT: {
        Rect frame_rect = this->rect;
        InsetRect(&frame_rect, -3, -3);
        port.draw_rect_outline(frame_rect);
        this->render_text_in_port(port);
        break;
      }
      case ResourceFile::DecodedDialogItem::Type::CHECKBOX:
      case ResourceFile::DecodedDialogItem::Type::RADIO_BUTTON: {
        // TODO: For now, we just draw radio buttons the same as checkboxes. (Does Realmz even use radio buttons?)
        // Draw checkbox
        const auto& r = this->rect;
        constexpr size_t size = 12;
        Point top_left = {.h = r.left, .v = r.top};
        Point top_right = {.h = static_cast<int16_t>(r.left + size), .v = r.top};
        Point bottom_left = {.h = r.left, .v = static_cast<int16_t>(r.top + size)};
        Point bottom_right = {.h = static_cast<int16_t>(r.left + size), .v = static_cast<int16_t>(r.top + size)};
        port.draw_line(top_left, bottom_left); // Left side
        port.draw_line(top_right, bottom_right); // Right side
        port.draw_line(top_left, top_right); // Top side
        port.draw_line(bottom_left, bottom_right); // Bottom side
        if (control && control->value) {
          // Draw an X also if the checkbox is checked
          port.draw_line(top_left, bottom_right);
          port.draw_line(top_right, bottom_left);
        } else {
          // Clear the background if the checkbox isn't checked
          Rect bg_rect;
          bg_rect.left = top_left.h + 1;
          bg_rect.top = top_left.v + 1;
          bg_rect.right = bottom_right.h - 1;
          bg_rect.bottom = bottom_right.v - 1;
          port.erase_rect(bg_rect);
        }
        int16_t h = get_height();
        int16_t w = get_width();
        if (!port.draw_text(text, Rect{r.top, static_cast<int16_t>(r.left + 12), r.bottom, r.right})) {
          wm_log.error_f("Error when rendering button text item {}: {}", resource_id, SDL_GetError());
        }
        break;
      }
      case ResourceFile::DecodedDialogItem::Type::RESOURCE_CONTROL: {
        if (this->control->type != ControlType::SCROLL_BAR) {
          wm_log.error_f("Could not render resource control {} that is not a scrollbar", resource_id);
          break;
        }
        RGBColor prev_color;
        GetForeColor(&prev_color);

        const auto r = this->rect;
        const auto w = get_width(); // vertical scrollbar: width drives the thumb and arrow heights
        const auto slider_offset = get_slider_offset();

        // Reproduce the classic Mac Appearance ("platinum") scrollbar pixel for
        // pixel. The pieces are painted with foreground fills (not erase_rect),
        // because the shop window installs a stone background pixel pattern
        // (BackPixPat) and erase_rect honors that pattern over the background
        // color. The bevel layout was taken from the original 1994 build. Color
        // values are stored pre-gamma (Mac OS 9 internal framebuffer level) so
        // they render correctly when 2.59 color correction is active; without
        // correction they match the raw Mac OS 9 output rather than the
        // SheepShaver-presented output.
        const RGBColor black = {.red = 0x0000, .green = 0x0000, .blue = 0x0000};
        const RGBColor track_fill = {.red = 0xA9A9, .green = 0xA9A9, .blue = 0xA9A9};
        const RGBColor track_shadow_dark = {.red = 0x7777, .green = 0x7777, .blue = 0x7777}; // recessed shadow, darkest
        const RGBColor track_shadow = {.red = 0x8888, .green = 0x8888, .blue = 0x8888};
        const RGBColor track_highlight = {.red = 0xBABA, .green = 0xBABA, .blue = 0xBABA};
        const RGBColor track_highlight_light = {.red = 0xCCCC, .green = 0xCCCC, .blue = 0xCCCC}; // recessed highlight, lightest
        const RGBColor thumb_highlight = {.red = 0xEEEE, .green = 0xEEEE, .blue = 0xEEEE}; // thumb top-left highlight
        const RGBColor thumb_light = {.red = 0xCCCC, .green = 0xCCCC, .blue = 0xFFFF}; // thumb light periwinkle
        const RGBColor thumb_mid = {.red = 0x9999, .green = 0x9999, .blue = 0xFFFF}; // thumb mid periwinkle
        const RGBColor thumb_shadow = {.red = 0x6666, .green = 0x6666, .blue = 0xCCCC}; // thumb bottom-right shadow
        const RGBColor thumb_grip = {.red = 0x3434, .green = 0x3434, .blue = 0x9999}; // thumb grip line
        // Pressed thumb: the same ramp shifted one step darker while it is dragged.
        const RGBColor thumb_highlight_pressed = {.red = 0xCCCC, .green = 0xCCCC, .blue = 0xFFFF};
        const RGBColor thumb_light_pressed = {.red = 0x9999, .green = 0x9999, .blue = 0xFFFF};
        const RGBColor thumb_mid_pressed = {.red = 0x6666, .green = 0x6666, .blue = 0xCCCC};
        const RGBColor thumb_shadow_pressed = {.red = 0x3434, .green = 0x3434, .blue = 0x9999};
        const RGBColor thumb_grip_pressed = {.red = 0x0000, .green = 0x0000, .blue = 0x5555};
        const RGBColor arrow_face = {.red = 0xDDDD, .green = 0xDDDD, .blue = 0xDDDD};
        const RGBColor arrow_highlight = {.red = 0xFFFF, .green = 0xFFFF, .blue = 0xFFFF}; // arrow top-left highlight
        const RGBColor arrow_shadow = {.red = 0xBABA, .green = 0xBABA, .blue = 0xBABA}; // arrow bottom-right shadow
        // Pressed (pushed-in) arrow palette: darker face with the bevel inverted.
        const RGBColor arrow_face_pressed = {.red = 0x7777, .green = 0x7777, .blue = 0x7777};
        const RGBColor arrow_highlight_pressed = {.red = 0x5555, .green = 0x5555, .blue = 0x5555}; // top-left, now dark
        const RGBColor arrow_shadow_pressed = {.red = 0x9999, .green = 0x9999, .blue = 0x9999}; // bottom-right, now light

        // Painter primitives for the bar: a single pixel, a horizontal run
        // [x0..x1], a vertical run [y0..y1], and a filled box (right/bottom
        // exclusive). These are real member functions rather than captured
        // lambdas, and they take plain ints so the pixel arithmetic at the call
        // sites stays cast-free; the narrowing to the Rect's int16_t fields
        // happens here, in one place.
        struct Painter {
          CCGrafPort& port;
          void pixel(int x, int y, const RGBColor& c) const {
            RGBForeColor(&c);
            port.fill_rect(Rect{.top = static_cast<int16_t>(y), .left = static_cast<int16_t>(x),
                .bottom = static_cast<int16_t>(y + 1), .right = static_cast<int16_t>(x + 1)});
          }
          void hrun(int x0, int x1, int y, const RGBColor& c) const {
            RGBForeColor(&c);
            port.fill_rect(Rect{.top = static_cast<int16_t>(y), .left = static_cast<int16_t>(x0),
                .bottom = static_cast<int16_t>(y + 1), .right = static_cast<int16_t>(x1 + 1)});
          }
          void vrun(int x, int y0, int y1, const RGBColor& c) const {
            RGBForeColor(&c);
            port.fill_rect(Rect{.top = static_cast<int16_t>(y0), .left = static_cast<int16_t>(x),
                .bottom = static_cast<int16_t>(y1 + 1), .right = static_cast<int16_t>(x + 1)});
          }
          void box(int left, int top, int right, int bottom, const RGBColor& c) const {
            RGBForeColor(&c);
            port.fill_rect(Rect{.top = static_cast<int16_t>(top), .left = static_cast<int16_t>(left),
                .bottom = static_cast<int16_t>(bottom), .right = static_cast<int16_t>(right)});
          }
        };
        Painter paint{port};

        const int bx0 = r.left, bx1 = r.right - 1; // black border columns
        const int by0 = r.top, by1 = r.bottom - 1; // black border rows
        const int ix0 = bx0 + 1, ix1 = bx1 - 1; // interior columns
        const int th_h = w; // thumb border-to-border height
        const int ar_h = w - 1; // each arrow button border-to-border height
        const int up_top = by1 - 2 * ar_h; // up-arrow top border row
        const int dn_top = up_top + ar_h; // down-arrow top border row
        const int cx = (ix0 + ix1 + 1) / 2; // glyph/grip center column
        const int thumb_top = r.top + slider_offset;

        // Recessed track well: a flat C0 channel with a 2px dark bevel on the
        // left, a 2px light bevel on the right, run the full interior height.
        paint.box(ix0, by0 + 1, ix1 + 1, by1, track_fill);
        paint.vrun(ix0, by0 + 1, by1 - 1, track_shadow_dark);
        paint.vrun(ix0 + 1, by0 + 1, by1 - 1, track_shadow);
        paint.vrun(ix1 - 1, by0 + 1, by1 - 1, track_highlight);
        paint.vrun(ix1, by0 + 1, by1 - 1, track_highlight_light);

        // Each track segment (above and below the thumb) is its own recessed
        // well with a 2px dark top bevel. Draw it at the channel top and just
        // below the thumb; whichever is hidden by the thumb is simply overdrawn.
        auto seg_top = [&](int y0) {
          int y1 = y0 + 1;
          if (y0 > by0 && y0 < by1) {
            paint.hrun(ix0, ix1 - 1, y0, track_shadow_dark);
            paint.pixel(ix1, y0, track_highlight_light);
          }
          if (y1 > by0 && y1 < by1) {
            paint.pixel(ix0, y1, track_shadow_dark);
            paint.hrun(ix0 + 1, ix1 - 2, y1, track_shadow);
            paint.pixel(ix1 - 1, y1, track_highlight);
            paint.pixel(ix1, y1, track_highlight_light);
          }
        };
        seg_top(by0 + 1);
        seg_top(thumb_top + th_h + 1);

        // Black frame around the whole bar.
        paint.hrun(bx0, bx1, by0, black);
        paint.hrun(bx0, bx1, by1, black);
        paint.vrun(bx0, by0, by1, black);
        paint.vrun(bx1, by0, by1, black);

        // Up / down arrow buttons grouped at the bottom (the original's
        // scroll-arrow placement), each a raised face with a centered triangle.
        // When pressed the same layout is drawn with the pushed-in palette.
        auto draw_arrow = [&](int b0, bool down, bool pressed) {
          const RGBColor& face = pressed ? arrow_face_pressed : arrow_face;
          const RGBColor& hi = pressed ? arrow_highlight_pressed : arrow_highlight;
          const RGBColor& sh = pressed ? arrow_shadow_pressed : arrow_shadow;
          int b1 = b0 + ar_h;
          paint.hrun(bx0, bx1, b0, black);
          paint.hrun(bx0, bx1, b1, black);
          paint.vrun(bx0, b0, b1, black);
          paint.vrun(bx1, b0, b1, black);
          paint.box(ix0, b0 + 1, ix1 + 1, b1, face);
          paint.vrun(ix0, b0 + 1, b1 - 1, hi);
          paint.vrun(ix1, b0 + 1, b1 - 1, sh);
          paint.hrun(ix0, ix1 - 1, b0 + 1, hi);
          paint.pixel(ix1, b0 + 1, face);
          paint.pixel(ix0, b1 - 1, face);
          paint.hrun(ix0 + 1, ix1, b1 - 1, sh);
          const int widths[4] = {2, 4, 6, 8};
          for (int k = 0; k < 4; k++) {
            int ry = down ? b0 + 6 + (3 - k) : b0 + 6 + k;
            int half = widths[k] / 2;
            paint.hrun(cx - half, cx + half - 1, ry, black);
          }
        };
        draw_arrow(up_top, false, this->pressed_part == kControlUpButtonPart);
        draw_arrow(dn_top, true, this->pressed_part == kControlDownButtonPart);

        // Thumb: a periwinkle box with an embossed grip. Black border, then 15
        // interior rows. The thumb travels from the top of the track. While it
        // is being dragged it uses the darker pressed palette (same structure).
        const bool thumb_pressed = (this->pressed_part == kControlIndicatorPart);
        const RGBColor& th_hi = thumb_pressed ? thumb_highlight_pressed : thumb_highlight;
        const RGBColor& th_lt = thumb_pressed ? thumb_light_pressed : thumb_light;
        const RGBColor& th_md = thumb_pressed ? thumb_mid_pressed : thumb_mid;
        const RGBColor& th_sh = thumb_pressed ? thumb_shadow_pressed : thumb_shadow;
        const RGBColor& th_gr = thumb_pressed ? thumb_grip_pressed : thumb_grip;
        const int tt = thumb_top;
        paint.hrun(bx0, bx1, tt, black);
        paint.hrun(bx0, bx1, tt + th_h, black);
        paint.vrun(bx0, tt, tt + th_h, black);
        paint.vrun(bx1, tt, tt + th_h, black);
        const int n = ix1 - ix0 + 1;
        for (int ri = 0; ri < th_h - 1; ri++) {
          int y = tt + 1 + ri;
          if (ri == 0) {
            paint.pixel(ix0, y, th_hi);
            paint.hrun(ix0 + 1, ix1 - 1, y, th_lt);
            paint.pixel(ix1, y, th_md);
          } else if (ri == th_h - 2) {
            paint.pixel(ix0, y, th_md);
            paint.hrun(ix0 + 1, ix1, y, th_sh);
          } else {
            paint.pixel(ix0, y, th_lt);
            paint.hrun(ix0 + 1, ix1 - 1, y, th_md);
            paint.pixel(ix1, y, th_sh);
          }
          // Embossed grip: alternating bright/light and dark rows in the center.
          if (n >= 14 && ri >= 3 && ri <= 10) {
            if (ri % 2 == 1) { // ri 3,5,7,9
              paint.pixel(ix0 + 3, y, th_hi);
              paint.hrun(ix0 + 4, ix0 + 9, y, th_lt);
            } else { // ri 4,6,8,10
              paint.hrun(ix0 + 4, ix0 + 10, y, th_gr);
            }
          }
        }

        RGBForeColor(&prev_color);
        break;
      }
      case ResourceFile::DecodedDialogItem::Type::HELP_BALLOON:
      case ResourceFile::DecodedDialogItem::Type::CUSTOM:
      case ResourceFile::DecodedDialogItem::Type::UNKNOWN:
        // TODO: Should we draw anything for these types?
        break;
      default:
        break;
    }

    if (ENABLE_DIALOG_RECOMPOSITE_DEBUG) {
      static size_t last_debug_number = 0;
      static size_t dialog_debug_number = 1;
      if (last_debug_number != debug_number) {
        last_debug_number = debug_number;
        dialog_debug_number = 1;
      }
      wm_log.info_f("Writing debug{}-dialog{}.bmp for item {} ({} @ {{x1={}, y1={}, x2={}, y2={}}})",
          debug_number, dialog_debug_number, this->item_id, phosg::name_for_enum(this->type),
          this->rect.left, this->rect.top, this->rect.right, this->rect.bottom);
      phosg::save_file(std::format("debug{}-dialog{}.bmp", debug_number, dialog_debug_number++), port.data.serialize(phosg::ImageFormat::WINDOWS_BITMAP));
    }
  }

  void render_text_in_port(CCGrafPort& port) const {
    port.erase_rect(this->rect);
    if (!port.draw_text(text, this->rect)) {
      wm_log.error_f("Error when rendering editable text item {}: {}", resource_id, SDL_GetError());
    }

    // Draw caret if this item is focused
    auto window = this->owner_window.lock();
    if (window && window->is_text_caret_visible() && window->get_focused_item().get() == this) {
      int16_t caret_x = this->rect.left + port.measure_text(text) + 1;
      Point caret_top = {.h = caret_x, .v = this->rect.top};
      Point caret_bottom = {.h = caret_x, .v = static_cast<int16_t>(this->rect.bottom - 1)};
      port.draw_line(caret_top, caret_bottom);
    }
  }

  inline int16_t get_width() const {
    return rect.right - rect.left;
  }

  inline int16_t get_height() const {
    return rect.bottom - rect.top;
  }

  inline const std::string& get_text() const {
    return text;
  }

  void set_text(const std::string& new_text) {
    text = new_text;
    if (control) {
      control->title = text;
    }
  }
  void set_text(std::string&& new_text) {
    text = std::move(new_text);
    if (control) {
      control->title = text;
    }
  }

  void append_text(const std::string& new_text) {
    text += new_text;
  }

  void delete_char() {
    if (!text.empty()) {
      text.pop_back();
    }
  }

  void set_control_visible(bool visible) {
    if (this->control && (this->control->visible != visible)) {
      this->control->visible = visible;
    }
  }

  void move_control(short horizontal, short vertical) {
    if (this->control) {
      Rect& bounds = this->control->bounds;
      int32_t width = bounds.right - bounds.left;
      int32_t height = bounds.bottom - bounds.top;
      bounds.left = horizontal;
      bounds.top = vertical;
      bounds.right = bounds.left + width;
      bounds.bottom = bounds.top + height;
      this->rect = bounds;
    }
  }

  void resize_control(short w, short h) {
    if (this->control) {
      Rect& bounds = this->control->bounds;
      bounds.right = bounds.left + w;
      bounds.bottom = bounds.top + h;
      this->rect = bounds;
    }
  }

  int16_t get_slider_offset() const {
    auto w = get_width();
    auto h = get_height();
    float value_offset = 0.0;
    int16_t slider_offset = 0;
    if (this->control->max > this->control->min) {
      auto value_range = this->control->max - this->control->min;
      value_offset = static_cast<float>(this->control->value - this->control->min) / value_range;
      // Slider offset is from the top of the track. The range is what is left after the thumb
      // (height w) and the two scroll buttons (each height w - 1, grouped at the bottom): the
      // thumb's lowest top is (rect.bottom - 1) - 2 * (w - 1) - w from the top, which is
      // h - 3 * w + 1.
      auto slider_range = h - 3 * w + 1;
      slider_offset = slider_range * value_offset;
    }
    return slider_offset;
  }

  int16_t track_control_part(Point pt) {
    if (pt.h < this->rect.left || pt.h >= this->rect.right ||
        pt.v < this->rect.top || pt.v >= this->rect.bottom) {
      return 0;
    }
    auto slider_offset = get_slider_offset();
    auto w = get_width();
    auto h = get_height();
    auto local_point = pt;
    local_point.h -= this->rect.left;
    local_point.v -= this->rect.top;

    // Both arrows are grouped at the bottom, each of height w - 1: the down
    // button's top border is at (h - 1) - (w - 1) and the up button's at
    // (h - 1) - 2 * (w - 1). The thumb (height w) travels from the top of the
    // track, so its bounds are [slider_offset, slider_offset + w]. These match
    // the metrics used by the draw code above.
    auto ar_h = w - 1;
    auto down_top = h - 1 - ar_h;
    auto up_top = h - 1 - 2 * ar_h;
    auto thumb_top = slider_offset;
    auto thumb_bottom = slider_offset + w;
    if (local_point.v >= down_top) {
      return kControlDownButtonPart;
    } else if (local_point.v >= up_top) {
      return kControlUpButtonPart;
    } else if (local_point.v >= thumb_top && local_point.v <= thumb_bottom) {
      return kControlIndicatorPart;
    } else if (local_point.v < thumb_top) {
      return kControlPageUpPart;
    } else {
      return kControlPageDownPart;
    }
  }

  void set_control_value(short value) {
    if (this->control && (this->control->value != value)) {
      this->control->value = value;
    }
  }

  void set_control_minimum(short min) {
    if (this->control && (this->control->min != min)) {
      this->control->min = min;
      this->control->max = std::max<int16_t>(this->control->max, this->control->min);
      this->control->value = std::max<int16_t>(this->control->min, this->control->value);
    }
  }

  void set_control_maximum(short max) {
    if (this->control && (this->control->max != max)) {
      this->control->max = max;
      this->control->min = std::min<int16_t>(this->control->max, this->control->min);
      this->control->value = std::min<int16_t>(this->control->max, this->control->value);
    }
  }
};

std::unordered_map<size_t, std::weak_ptr<DialogItem>> DialogItem::all_items;

std::shared_ptr<Control> Control::from_dialog_item(const DialogItem& item) {
  ControlType type;
  switch (item.type) {
    case DialogItemType::BUTTON:
      type = ControlType::BUTTON;
      break;
    case DialogItemType::CHECKBOX:
      type = ControlType::CHECKBOX;
      break;
    case DialogItemType::RADIO_BUTTON:
      type = ControlType::RADIO_BUTTON;
      break;
    case DialogItemType::RESOURCE_CONTROL:
      return Control::from_CNTL(item.resource_id);
    default:
      return nullptr;
  }

  auto ret = std::make_shared<Control>();
  ret->cntl_resource_id = 0x00010000;
  ret->opaque_handle = item.opaque_handle;
  ret->type = type;
  ret->bounds = item.rect;
  ret->value = 0;
  ret->min = 0;
  ret->max = 1;
  ret->visible = true;
  ret->title = item.get_text();
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Windows

Window::Window(
    const std::string& title,
    const Rect& bounds,
    int16_t window_kind,
    bool visible,
    bool is_dialog,
    const RGBColor& background_color,
    std::vector<std::shared_ptr<DialogItem>>&& dialog_items)
    : log(std::format("[Window:{}] ", this->ref())),
      title{title},
      port{bounds, true},
      window_kind{window_kind},
      visible(visible),
      is_dialog_flag{is_dialog},
      dialog_items{std::move(dialog_items)},
      focused_item{nullptr} {
  port.rgbBgColor = background_color;

  // Warn if a resource asks for Toolbox chrome that this port does not model.
  bool is_supported_proc_id = ((this->window_kind == dBoxProc) ||
      (this->window_kind == plainDBox) ||
      (this->window_kind == altDBoxProc));
  if (!is_supported_proc_id) {
    phosg::fwrite_fmt(stderr, "Warning: Creating unsupported window kind {}\n", this->window_kind);
  }

  for (auto di : this->dialog_items) {
    // Set the focused text field to be the first EDIT_TEXT item encountered
    if (!focused_item && di->type == DialogItemType::EDIT_TEXT) {
      focused_item = di;
    }

    if (di->type == DialogItemType::TEXT || di->type == DialogItemType::EDIT_TEXT) {
      text_items.emplace_back(di);
    } else if (di->control) {
      control_items.emplace_back(di);
    } else {
      static_items.emplace_back(di);
    }
  }
}

std::shared_ptr<Window> Window::make_shared(
    const std::string& title,
    const Rect& bounds,
    int16_t window_kind,
    bool visible,
    bool is_dialog,
    const RGBColor& background_color,
    std::vector<std::shared_ptr<DialogItem>>&& dialog_items) {
  std::shared_ptr<Window> ret(new Window(
      title, bounds, window_kind, visible, is_dialog, background_color, std::move(dialog_items)));
  // This can't happen in the actual constructor because weak_from_this()
  // doesn't work until the Window is assigned to a shared_ptr
  for (auto& di : ret->dialog_items) {
    di->owner_window = ret;
  }
  return ret;
}

void Window::add_dialog_item(std::shared_ptr<DialogItem> item) {
  item->item_id = this->dialog_items.size();
  this->dialog_items.emplace_back(item);
  item->owner_window = this->weak_from_this();

  this->log.debug_f("Window::add_dialog_item({})", item->str());
  item->render_in_port(this->port, false);
  WindowManager::instance().recomposite_from_window(this->shared_from_this());
}

std::shared_ptr<DialogItem> Window::get_focused_item() {
  return focused_item;
}

bool Window::is_text_caret_visible() const {
  return text_caret_visible;
}

CCGrafPort& Window::get_port() {
  return this->port;
}

void Window::set_focused_item(std::shared_ptr<DialogItem> item) {
  if (focused_item && (focused_item != item)) {
    text_caret_visible = false;
    focused_item->render_text_in_port(this->port);
  }
  focused_item = item;
  reset_text_caret();
  item->render_text_in_port(this->port);
  WindowManager::instance().recomposite_from_window(this->port);
}

void Window::reset_text_caret() {
  text_caret_visible = true;
  text_caret_next_toggle = SDL_GetTicks() + TEXT_CARET_BLINK_INTERVAL_MS;
}

void Window::idle_text_caret() {
  if (!focused_item) {
    return;
  }

  uint64_t now = SDL_GetTicks();
  if (text_caret_next_toggle && (now < text_caret_next_toggle)) {
    return;
  }

  text_caret_visible = text_caret_next_toggle ? !text_caret_visible : true;
  text_caret_next_toggle = now + TEXT_CARET_BLINK_INTERVAL_MS;
  focused_item->render_text_in_port(this->port);
  WindowManager::instance().recomposite_from_window(this->port);
}

void Window::handle_text_input(const std::string& text, std::shared_ptr<DialogItem> item) {
  this->log.debug_f("Window::handle_text_input(\"{}\", {})", text, item->str());
  item->append_text(text);
  reset_text_caret();
  item->render_text_in_port(this->port);
  WindowManager::instance().recomposite_from_window(this->port);
}

void Window::delete_char(std::shared_ptr<DialogItem> item) {
  this->log.debug_f("Window::delete_char({})", item->str());
  item->delete_char();
  reset_text_caret();
  item->render_text_in_port(this->port);
  WindowManager::instance().recomposite_from_window(this->port);
}

void Window::erase_and_render() {
  // Clear the backbuffer before drawing frame
  this->log.debug_f("Window::erase_and_render({:016X})", reinterpret_cast<intptr_t>(&this->port));
  this->port.erase_rect(this->port.to_local_space(this->port.portRect));

  // The DrawDialog procedure draws the entire contents of the specified dialog box. The
  // DrawDialog procedure draws all dialog items, calls the Control Manager procedure
  // DrawControls to draw all controls, and calls the TextEdit procedure TEUpdate to
  // update all static and editable text items and to draw their display rectangles. The
  // DrawDialog procedure also calls the application-defined items’ draw procedures if
  // the items’ rectangles are within the update region.
  for (auto item : this->static_items) {
    item->render_in_port(this->port, false);
  }
  for (auto item : this->control_items) {
    item->render_in_port(this->port, false);
  }
  for (auto item : this->text_items) {
    if (item->type == DialogItemType::EDIT_TEXT) {
      item->render_text_in_port(this->port);
    } else {
      item->render_in_port(this->port, false);
    }
  }

  WindowManager::instance().recomposite_from_window(this->port);
}

void Window::move(int x, int y) {
  this->log.debug_f("Window::move({}, {})", x, y);
  auto& bounds = this->bounds();
  ssize_t x_delta = x - bounds.left;
  ssize_t y_delta = y - bounds.top;
  if (x_delta || y_delta) {
    bounds.left += x_delta;
    bounds.right += x_delta;
    bounds.top += y_delta;
    bounds.bottom += y_delta;
    WindowManager::instance().recomposite_all();
  }
}

void Window::resize(uint16_t w, uint16_t h) {
  this->log.debug_f("Window::resize({}, {})", w, h);

  bool shrank_either_dimension = (this->get_width() > w) || (this->get_height() > h);
  this->port.resize(w, h);

  // Recomposite everything if this window shrank in either dimension (since
  // windows behind it may be revealed); else, recomposite only this window
  if (shrank_either_dimension) {
    WindowManager::instance().recomposite_all();
  } else {
    WindowManager::instance().recomposite_from_window(this->shared_from_this());
  }
}

void Window::show() {
  this->log.debug_f("Window::show()");
  if (!this->visible) {
    this->visible = true;
    this->erase_and_render();
    WindowManager::instance().recomposite_from_window(this->port);
  }
}

const std::vector<std::shared_ptr<DialogItem>>& Window::get_dialog_items() const {
  return this->dialog_items;
}

std::shared_ptr<DialogItem> Window::dialog_item_for_position(const Point& pt, bool enabled_only) {
  for (const auto& item : this->dialog_items) {
    if ((!enabled_only || item->enabled) && PtInRect(pt, &item->rect)) {
      return item;
    }
  }
  return nullptr;
}

inline bool Window::is_dialog() const {
  return this->is_dialog_flag;
}

TEHandle Window::add_text_edit(const Rect& dest_rect, const Rect& view_rect) {
  auto di = DialogItem::from_text_edit(dest_rect, view_rect);
  add_dialog_item(di);
  text_items.emplace_back(di);
  return reinterpret_cast<TEHandle>(di->opaque_handle);
}

void Window::remove_text_edit(std::shared_ptr<DialogItem> item) {
  auto it = std::find(dialog_items.begin(), dialog_items.end(), item);
  if (it != dialog_items.end()) {
    dialog_items.erase(it);
  }
  it = std::find(text_items.begin(), text_items.end(), item);
  if (it != text_items.end()) {
    text_items.erase(it);
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Window manager

WindowManager::WindowManager() = default;
WindowManager::~WindowManager() = default;

static bool window_pos_on_screen(int x, int y, int w, int h) {
  if (SDL_WINDOWPOS_ISCENTERED(x) || SDL_WINDOWPOS_ISUNDEFINED(x) ||
      SDL_WINDOWPOS_ISCENTERED(y) || SDL_WINDOWPOS_ISUNDEFINED(y)) {
    return false;
  }
  SDL_Rect win{x, y, w, h};
  SDL_DisplayID display = SDL_GetDisplayForRect(&win);
  SDL_Rect bounds;
  if (!display || !SDL_GetDisplayUsableBounds(display, &bounds)) {
    return false;
  }
  SDL_Rect overlap;
  if (!SDL_GetRectIntersection(&win, &bounds, &overlap)) {
    return false;
  }
  static constexpr int kMinVisible = 80;
  return overlap.w >= kMinVisible && overlap.h >= kMinVisible;
}

void WindowManager::create_sdl_window() {
  wm_log.debug_f("WindowManager::create_sdl_window()");

  PortPrefs prefs = load_port_prefs();
  this->scale_mode = prefs.scale_mode;
  this->aspect_locked = prefs.aspect_locked;
  this->gamma_idx = prefs.gamma_idx;
  this->presentation_host.set_mode(prefs.presentation_mode);
  realmz::remaster::assets::setResourcePresentationMode(
      prefs.presentation_mode);
  this->windowed_w = prefs.window_w;
  this->windowed_h = prefs.window_h;
  this->windowed_x = prefs.window_x;
  this->windowed_y = prefs.window_y;

  int initial_width = prefs.window_w;
  int initial_height = prefs.window_h;
  if (prefs.presentation_mode ==
      realmz::presentation::PresentationMode::remastered) {
    initial_width = std::max(initial_width, REMASTER_MINIMUM_WIDTH);
    initial_height = std::max(initial_height, REMASTER_MINIMUM_HEIGHT);
    this->windowed_w = initial_width;
    this->windowed_h = initial_height;
  }

  this->sdl_window = sdl_make_shared(SDL_CreateWindow(
      realmz::app::kProductName.data(), initial_width, initial_height,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
  if (!this->sdl_window) {
    throw std::runtime_error(std::format("Could not create SDL window: {}", SDL_GetError()));
  }
  if (window_pos_on_screen(this->windowed_x, this->windowed_y,
          initial_width, initial_height)) {
    SDL_SetWindowPosition(this->sdl_window.get(), this->windowed_x, this->windowed_y);
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(this->sdl_window.get(), nullptr);
  if (!renderer) {
    throw std::runtime_error(std::format("Could not create window renderer: {}", SDL_GetError()));
  }
  this->runtime_legacy_command_bridge =
      std::make_unique<realmz::presentation::RuntimeLegacyCommandBridge>(
          [] { return capture_runtime_legacy_command_context(); },
          [](realmz::presentation::MovementCommand command,
              uint32_t,
              const realmz::presentation::RuntimeLegacyCommandContext&
                  context) {
            const auto surface = RealmzCurrentSemanticInputSurface();
            const bool matching_surface =
                ((surface == REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::exploration)) ||
                ((surface == REALMZ_SEMANTIC_INPUT_DUNGEON) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::dungeon));
            if (!matching_surface) {
              return false;
            }
            const uint32_t tag =
                realmz::presentation::semantic_movement_tag(
                    command, surface);
            return tag && PushSemanticMovementEvent(tag);
          },
          [](realmz::presentation::PartyMemberId member,
              const realmz::presentation::RuntimeLegacyCommandContext&
                  context) {
            const auto surface = RealmzCurrentSemanticInputSurface();
            const bool matching_surface =
                ((surface == REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::exploration)) ||
                ((surface == REALMZ_SEMANTIC_INPUT_DUNGEON) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::dungeon));
            if (!matching_surface) {
              return false;
            }
            const uint32_t tag =
                realmz::presentation::semantic_party_selection_tag(
                    member, surface);
            return tag && PushSemanticPartySelectionEvent(tag);
          },
          [](realmz::presentation::PartyMemberId member,
              uint32_t,
              const realmz::presentation::RuntimeLegacyCommandContext&
                  context) {
            const auto surface = RealmzCurrentSemanticInputSurface();
            const bool matching_surface =
                ((surface == REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::exploration)) ||
                ((surface == REALMZ_SEMANTIC_INPUT_DUNGEON) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::dungeon));
            if (!matching_surface) {
              return false;
            }
            const uint32_t tag =
                realmz::presentation::semantic_open_inventory_tag(
                    member, surface);
            return tag && PushSemanticOpenInventoryEvent(tag);
          },
          [](realmz::presentation::PartyMemberId member,
              uint32_t,
              const realmz::presentation::RuntimeLegacyCommandContext&
                  context) {
            const auto surface = RealmzCurrentSemanticInputSurface();
            const bool matching_surface =
                ((surface == REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::exploration)) ||
                ((surface == REALMZ_SEMANTIC_INPUT_DUNGEON) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::dungeon));
            if (!matching_surface) {
              return false;
            }
            const uint32_t tag =
                realmz::presentation::semantic_open_spellbook_tag(
                    member, surface);
            return tag && PushSemanticOpenSpellbookEvent(tag);
          },
          [](realmz::presentation::RuntimeLegacyMenuCommand command,
              const realmz::presentation::RuntimeLegacyCommandContext&
                  context) {
            const auto surface = RealmzCurrentSemanticInputSurface();
            const bool matching_surface =
                ((surface == REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::exploration)) ||
                ((surface == REALMZ_SEMANTIC_INPUT_DUNGEON) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::dungeon));
            const auto expected = realmz::presentation::
                legacy_menu_command_for_open_save_game(context);
            if (!matching_surface || !expected || command != *expected) {
              return false;
            }
            const uint32_t tag =
                realmz::presentation::semantic_open_save_game_tag(surface);
            return tag && PushSemanticOpenSaveGameEvent(tag);
          },
          [](realmz::presentation::RuntimeLegacyMenuCommand command,
              const realmz::presentation::RuntimeLegacyCommandContext&
                  context) {
            const auto surface = RealmzCurrentSemanticInputSurface();
            const bool matching_surface =
                ((surface == REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::exploration)) ||
                ((surface == REALMZ_SEMANTIC_INPUT_DUNGEON) &&
                    (context.screen ==
                        realmz::presentation::ScreenContext::dungeon));
            const auto expected = realmz::presentation::
                legacy_menu_command_for_open_load_game(context);
            if (!matching_surface || !expected || command != *expected) {
              return false;
            }
            const uint32_t tag =
                realmz::presentation::semantic_open_load_game_tag(surface);
            return tag && PushSemanticOpenLoadGameEvent(tag);
          },
          realmz::presentation::RuntimeLegacyCombatActionSinks{
              .guard_combatant =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_guard_combatant(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_guard_combatant_tag(combatant, surface);
                    return tag && PushSemanticGuardCombatantEvent(tag);
                  },
              .finish_combatant =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_finish_combatant(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_finish_combatant_tag(combatant, surface);
                    return tag && PushSemanticFinishCombatantEvent(tag);
                  },
              .delay_combatant =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_delay_combatant(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_delay_combatant_tag(combatant, surface);
                    return tag && PushSemanticDelayCombatantEvent(tag);
                  },
              .center_active_combatant =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_center_active_combatant(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_center_active_combatant_tag(
                            combatant, surface);
                    return tag &&
                        PushSemanticCenterActiveCombatantEvent(tag);
                  },
              .switch_weapon =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_switch_weapon(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_switch_weapon_tag(combatant, surface);
                    return tag && PushSemanticSwitchWeaponEvent(tag);
                  },
              .cycle_combat_focus =
                  [](realmz::presentation::CombatantId combatant,
                      realmz::presentation::CombatFocusDirection direction,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_cycle_combat_focus(
                            combatant, direction, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_cycle_combat_focus_tag(
                            combatant, direction, surface);
                    return tag && PushSemanticCycleCombatFocusEvent(tag);
                  },
              .open_combat_items =
                  [](realmz::presentation::CombatantId combatant,
                      realmz::presentation::PartyMemberId member,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_open_combat_items(
                            combatant, member, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_open_combat_items_tag(
                            combatant, member, surface);
                    return tag && PushSemanticOpenCombatItemsEvent(tag);
                  },
              .auto_combatant =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_auto_combatant(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_auto_combatant_tag(combatant, surface);
                    return tag && PushSemanticAutoCombatantEvent(tag);
                  },
              .show_combat_range =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_show_combat_range(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_show_combat_range_tag(combatant, surface);
                    return tag && PushSemanticShowCombatRangeEvent(tag);
                  },
              .bandage_combatant =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_bandage_combatant(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_bandage_combatant_tag(combatant, surface);
                    return tag && PushSemanticBandageCombatantEvent(tag);
                  },
              .undo_combatant =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_undo_combatant(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_undo_combatant_tag(combatant, surface);
                    return tag && PushSemanticUndoCombatantEvent(tag);
                  },
              .open_combat_spellbook =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_open_combat_spellbook(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_open_combat_spellbook_tag(
                            combatant, surface);
                    return tag &&
                        PushSemanticOpenCombatSpellbookEvent(tag);
                  },
              .open_combat_targeting =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_open_combat_targeting(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_open_combat_targeting_tag(
                            combatant, surface);
                    return tag &&
                        PushSemanticOpenCombatTargetingEvent(tag);
                  },
              .escape_combat =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_escape_combat(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_escape_combat_tag(combatant, surface);
                    return tag && PushSemanticEscapeCombatEvent(tag);
                  },
              .open_combat_scroll_case =
                  [](realmz::presentation::CombatantId combatant,
                      uint32_t message,
                      const realmz::presentation::
                          RuntimeLegacyCommandContext& context) {
                    const auto surface = RealmzCurrentSemanticInputSurface();
                    const bool matching_surface =
                        (surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
                        (context.screen ==
                            realmz::presentation::ScreenContext::combat);
                    const auto expected = realmz::presentation::
                        legacy_key_message_for_open_combat_scroll_case(
                            combatant, context);
                    if (!matching_surface || !expected ||
                        (message != *expected)) {
                      return false;
                    }
                    const uint32_t tag = realmz::presentation::
                        semantic_open_combat_scroll_case_tag(
                            combatant, surface);
                    return tag &&
                        PushSemanticOpenCombatScrollCaseEvent(tag);
                  },
          });
  this->configure_window_for_presentation_mode();

  this->screen_port.resize(kLogicalWindowWidth, kLogicalWindowHeight);
  this->recomposite_all();
}

WindowPtr WindowManager::create_window(
    const std::string& title,
    const Rect& bounds,
    bool visible,
    bool go_away,
    int16_t proc_id,
    uint32_t ref_con,
    bool is_dialog,
    const RGBColor& background_color,
    std::vector<std::shared_ptr<DialogItem>>&& dialog_items) {
  wm_log.debug_f("WindowManager::create_window(\"{}\", {{x0={}, y0={}, x1={}, y1={}}}, ...)", title, bounds.left, bounds.top, bounds.right, bounds.bottom);

  auto window = Window::make_shared(title, bounds, proc_id, visible, is_dialog, background_color, std::move(dialog_items));
  port_to_window.emplace(&window->get_port(), window);

  // Maintain a shared lookup across all windows of their dialog items, by handle,
  // to support functions that modify the DITLs directly, like SetDialogItemText
  for (auto di : dialog_items) {
    DialogItem::all_items[di->opaque_handle] = di;
  }

  this->link_window_at_front(window);
  this->on_dialog_item_focus_changed();

  // Render the window's contents even if it isn't visible, and recomposite
  window->erase_and_render();
  recomposite_from_window(window);

  return &window->get_port();
}

void WindowManager::destroy_window(WindowPtr port) {
  auto window_it = port_to_window.find(port);
  if (window_it == port_to_window.end()) {
    throw std::logic_error("Attempted to delete nonexistent window");
  }
  wm_log.debug_f("WindowManager::destroy_window({}) port={}", window_it->second->ref(), window_it->second->port.ref());

  // When the window is dismissed via a mousedown, the enqueued mouseup event is either
  // lost when the window is destroyed, or the button is not released in time for it to be
  // handled by the window. It's not clear why the mouseup event isn't handled by the surviving
  // window. In any case, we can simply reset the mouse state to prevent the StillDown function
  // from thinking that the mouse button is still pressed.
  // TODO: figure out a better way of handling this.
  reset_mouse_state();

  bool should_update_focus = (window_it->second == this->top_window);
  this->unlink_window(window_it->second);
  port_to_window.erase(window_it);
  if (should_update_focus) {
    this->on_dialog_item_focus_changed();
  }

  // If the current port is this window's port, set the current port back to
  // the default port
  if (qd.thePort == port) {
    SetPort(&get_default_port());
  }

  // Recomposite everything, since this window many have been obstructing other windows
  this->recomposite_all();
}

std::shared_ptr<Window> WindowManager::window_for_port(WindowPtr port) {
  return this->port_to_window.at(port);
}

std::shared_ptr<DialogItem> WindowManager::dialog_item_for_handle(DialogItemHandle handle) {
  return dialog_items_by_handle.at(handle);
}

std::shared_ptr<Window> WindowManager::front_window() {
  return this->top_window;
}

void WindowManager::link_window_at_front(std::shared_ptr<Window> w) {
  w->window_below = this->top_window;
  if (this->top_window) {
    this->top_window->window_above = w;
  }
  this->top_window = w;
  if (!this->bottom_window) {
    this->bottom_window = w;
  }
  this->verify_window_stack();
}

void WindowManager::unlink_window(std::shared_ptr<Window> w) {
  if (this->top_window == w) {
    this->top_window = w->window_below;
  }
  if (this->bottom_window == w) {
    this->bottom_window = w->window_above;
  }
  if (w->window_below) {
    w->window_below->window_above = w->window_above;
  }
  if (w->window_above) {
    w->window_above->window_below = w->window_below;
  }
  w->window_below.reset();
  w->window_above.reset();
  this->verify_window_stack();
}

void WindowManager::bring_to_front(std::shared_ptr<Window> window) {
  if (window == this->top_window) {
    wm_log.debug_f("WindowManager::bring_to_front({}) port={} (already at front)", window->ref(), window->port.ref());
  } else {
    wm_log.debug_f("WindowManager::bring_to_front({}) port={} (not already at front)", window->ref(), window->port.ref());
    this->unlink_window(window);
    this->link_window_at_front(window);
    this->on_dialog_item_focus_changed();
    if (window->visible) {
      this->recomposite_from_window(window);
    }
  }
}

std::shared_ptr<Window> WindowManager::window_for_point(ssize_t x, ssize_t y) {
  Point pt{.h = static_cast<int16_t>(x), .v = static_cast<int16_t>(y)};
  for (auto window = this->top_window; window; window = window->window_below) {
    if (PtInRect(pt, &window->port.portRect)) {
      return window;
    }
  }
  return nullptr;
}

void WindowManager::on_dialog_item_focus_changed() {
  // Macintosh Toolbox Essentials 6-32

  if (this->text_editing_active) {
    wm_log.info_f("Ending SDL text input");
    SDL_StopTextInput(this->sdl_window.get());
    this->text_editing_active = false;
  }

  if (this->top_window &&
      this->top_window->focused_item &&
      (this->top_window->focused_item->type == DialogItemType::EDIT_TEXT)) {

    wm_log.info_f("Starting SDL text input");

    this->update_text_input_area();

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);

    if (!SDL_StartTextInputWithProperties(this->sdl_window.get(), props)) {
      wm_log.error_f("Could not start text input: {}", SDL_GetError());
    }

    SDL_DestroyProperties(props);

    this->text_editing_active = true;
  }
}

void WindowManager::update_text_input_area() {
  if (!this->sdl_window || !this->top_window ||
      !this->top_window->focused_item ||
      (this->top_window->focused_item->type != DialogItemType::EDIT_TEXT)) {
    return;
  }

  const auto& window_rect = this->top_window->port.portRect;
  const auto& item_rect = this->top_window->focused_item->rect;
  float left = window_rect.left + item_rect.left;
  float top = window_rect.top + item_rect.top;
  float width = item_rect.right - item_rect.left;
  float height = item_rect.bottom - item_rect.top;
  if (!this->classic_to_render_rect(&left, &top, &width, &height)) {
    return;
  }
  float right = left + width;
  float bottom = top + height;
  if (auto* renderer = SDL_GetRenderer(this->sdl_window.get())) {
    SDL_RenderCoordinatesToWindow(renderer, left, top, &left, &top);
    SDL_RenderCoordinatesToWindow(renderer, right, bottom, &right, &bottom);
  }

  const SDL_Rect rect{
      .x = static_cast<int>(SDL_lroundf(left)),
      .y = static_cast<int>(SDL_lroundf(top)),
      .w = static_cast<int>(SDL_lroundf(right - left)),
      .h = static_cast<int>(SDL_lroundf(bottom - top)),
  };
  if (!SDL_SetTextInputArea(this->sdl_window.get(), &rect, 0)) {
    wm_log.error_f("Could not create text area: {}", SDL_GetError());
  }
}

void WindowManager::recomposite(std::shared_ptr<Window> updated_window) {
  if (!this->recomposite_enabled || (updated_window && !updated_window->visible)) {
    return;
  }

  std::shared_ptr<Window> window;
  if (!updated_window || enable_translucent_window_debug) {
    this->screen_port.data.clear(0x000000FF);
    window = this->bottom_window;
  } else {
    window = updated_window;
  }

  for (; window; window = window->window_above) {
    if (window->is_dialog_flag && window->window_kind == dBoxProc) {
      draw_dbox_frame(this->screen_port, window->port.portRect);
    } else {
      // Draw window border
      this->screen_port.data.draw_horizontal_line(window->port.portRect.left - 1, window->port.portRect.right, window->port.portRect.top - 1, 0, 0x000000FF);
      this->screen_port.data.draw_horizontal_line(window->port.portRect.left - 1, window->port.portRect.right, window->port.portRect.bottom, 0, 0x000000FF);
      this->screen_port.data.draw_vertical_line(window->port.portRect.left - 1, window->port.portRect.top, window->port.portRect.bottom, 0, 0x000000FF);
      this->screen_port.data.draw_vertical_line(window->port.portRect.right, window->port.portRect.top, window->port.portRect.bottom, 0, 0x000000FF);
    }

    if (enable_translucent_window_debug) {
      this->screen_port.data.copy_from_with_custom(
          window->port.data,
          window->port.portRect.left,
          window->port.portRect.top,
          window->get_width(),
          window->get_height(),
          0,
          0,
          [](uint32_t dst_c, uint32_t src_c) -> uint32_t {
            return phosg::alpha_blend(dst_c, phosg::replace_alpha(src_c, 0x80)) | 0x000000FF;
          });
    } else {
      this->screen_port.data.copy_from_with_blend(
          window->port.data,
          window->port.portRect.left,
          window->port.portRect.top,
          window->get_width(),
          window->get_height(),
          0,
          0);
    }
  }

  this->present_screen();
}

void WindowManager::present_screen() {
  static_cast<void>(this->presentation_host.present(*this));
}

namespace {

SDL_FRect sdl_rect(const realmz::presentation::LogicalRect& rect) {
  return {
      static_cast<float>(rect.x),
      static_cast<float>(rect.y),
      static_cast<float>(rect.width),
      static_cast<float>(rect.height),
  };
}

struct ShellPanelColors {
  Uint8 red;
  Uint8 green;
  Uint8 blue;
};

ShellPanelColors shell_panel_color(
    realmz::presentation::ShellPanelKind panel) {
  using realmz::presentation::ShellPanelKind;
  switch (panel) {
    case ShellPanelKind::party_rail:
      return {31, 34, 43};
    case ShellPanelKind::action_bar:
      return {38, 34, 31};
    case ShellPanelKind::details:
      return {27, 32, 38};
    case ShellPanelKind::event_log:
      return {25, 29, 34};
    case ShellPanelKind::drawer_tabs:
      return {34, 31, 38};
  }
  return {31, 34, 43};
}

void draw_shell_text(
    SDL_Renderer* renderer,
    TTF_Font* font,
    std::string_view text,
    const realmz::presentation::LogicalRect& bounds,
    SDL_Color color,
    double backing_scale,
    float logical_point_size,
    TTF_FontStyleFlags style = TTF_STYLE_NORMAL,
    float logical_line_height = 0.0f) {
  if (!renderer || !font || text.empty() ||
      (bounds.width <= 0.0) || (bounds.height <= 0.0) ||
      !std::isfinite(backing_scale) || (backing_scale <= 0.0)) {
    return;
  }

  const float raster_point_size = static_cast<float>(
      logical_point_size * backing_scale);
  if (!TTF_SetFontSize(font, raster_point_size)) {
    return;
  }
  TTF_SetFontStyle(font, style);
  const float effective_line_height = logical_line_height > 0.0f
      ? logical_line_height
      : logical_point_size * 1.25f;
  TTF_SetFontLineSkip(font, std::max(
      1, static_cast<int>(std::lround(
             effective_line_height * static_cast<float>(backing_scale)))));
  const int wrap_width = std::max(
      1, static_cast<int>(std::floor(bounds.width * backing_scale)));
  auto surface = sdl_make_unique(TTF_RenderText_Blended_Wrapped(
      font, text.data(), text.size(), color, wrap_width));
  if (!surface) {
    return;
  }
  auto texture = sdl_make_unique(
      SDL_CreateTextureFromSurface(renderer, surface.get()));
  if (!texture) {
    return;
  }
  SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);

  const double natural_width = surface->w / backing_scale;
  const double natural_height = surface->h / backing_scale;
  const double visible_width = std::min(natural_width, bounds.width);
  const double visible_height = std::min(natural_height, bounds.height);
  const SDL_FRect source{
      0.0f,
      0.0f,
      static_cast<float>(visible_width * backing_scale),
      static_cast<float>(visible_height * backing_scale),
  };
  const SDL_FRect destination{
      static_cast<float>(bounds.x),
      static_cast<float>(bounds.y),
      static_cast<float>(visible_width),
      static_cast<float>(visible_height),
  };
  SDL_RenderTexture(renderer, texture.get(), &source, &destination);
}

void draw_shell_text(
    SDL_Renderer* renderer,
    TTF_Font* font,
    std::string_view text,
    const realmz::presentation::LogicalRect& bounds,
    SDL_Color color,
    double backing_scale,
    const realmz::presentation::TextStyleModel& text_style,
    TTF_FontStyleFlags style = TTF_STYLE_NORMAL) {
  draw_shell_text(
      renderer,
      font,
      text,
      bounds,
      color,
      backing_scale,
      static_cast<float>(text_style.point_size),
      style,
      static_cast<float>(text_style.line_height));
}

SDL_Color shell_state_color(
    realmz::presentation::StateEmphasis emphasis) noexcept {
  using realmz::presentation::StateEmphasis;
  switch (emphasis) {
    case StateEmphasis::neutral:
      return {222, 222, 218, 255};
    case StateEmphasis::information:
      return {154, 194, 207, 255};
    case StateEmphasis::positive:
      return {154, 190, 131, 255};
    case StateEmphasis::caution:
      return {231, 188, 105, 255};
    case StateEmphasis::critical:
      return {221, 125, 112, 255};
    case StateEmphasis::inactive:
      return {164, 164, 158, 255};
  }
  return {222, 222, 218, 255};
}

int shell_state_emphasis_priority(
    realmz::presentation::StateEmphasis emphasis) noexcept {
  using realmz::presentation::StateEmphasis;
  switch (emphasis) {
    case StateEmphasis::neutral:
      return 0;
    case StateEmphasis::positive:
      return 1;
    case StateEmphasis::information:
      return 2;
    case StateEmphasis::inactive:
      return 3;
    case StateEmphasis::caution:
      return 4;
    case StateEmphasis::critical:
      return 5;
  }
  return 0;
}

realmz::presentation::StateEmphasis strongest_shell_state_emphasis(
    const std::vector<
        realmz::presentation::PartyRailRenderableStateToken>& tokens)
    noexcept {
  auto strongest = realmz::presentation::StateEmphasis::neutral;
  for (const auto& token : tokens) {
    if (shell_state_emphasis_priority(token.emphasis) >
        shell_state_emphasis_priority(strongest)) {
      strongest = token.emphasis;
    }
  }
  return strongest;
}

void draw_shell_meter(
    SDL_Renderer* renderer,
    const realmz::presentation::LogicalRect& bounds,
    double fraction,
    bool available) {
  const SDL_FRect track = sdl_rect(bounds);
  SDL_SetRenderDrawColor(renderer, 12, 14, 18, 255);
  SDL_RenderFillRect(renderer, &track);
  SDL_SetRenderDrawColor(renderer, 93, 86, 70, 255);
  SDL_RenderRect(renderer, &track);
  const auto clamped = std::clamp(fraction, 0.0, 1.0);
  if (available && (clamped > 0.0)) {
    SDL_FRect fill = track;
    fill.x += 2.0f;
    fill.y += 2.0f;
    fill.w = std::max(
        0.0f, static_cast<float>((bounds.width - 4.0) * clamped));
    fill.h = std::max(0.0f, fill.h - 4.0f);
    SDL_SetRenderDrawColor(renderer, 112, 132, 103, 255);
    SDL_RenderFillRect(renderer, &fill);
  }
}

void draw_shell_focus_corners(
    SDL_Renderer* renderer,
    const realmz::presentation::LogicalRect& bounds) {
  SDL_FRect ring = sdl_rect(bounds);
  ring.x += 3.0f;
  ring.y += 3.0f;
  ring.w = std::max(0.0f, ring.w - 6.0f);
  ring.h = std::max(0.0f, ring.h - 6.0f);
  if ((ring.w < 8.0f) || (ring.h < 8.0f)) {
    return;
  }
  SDL_SetRenderDrawColor(renderer, 250, 232, 174, 255);
  SDL_RenderRect(renderer, &ring);
  const float left = ring.x;
  const float top = ring.y;
  const float right = ring.x + ring.w;
  const float bottom = ring.y + ring.h;
  const float arm = std::min(8.0f, std::min(ring.w, ring.h) / 3.0f);
  SDL_RenderLine(renderer, left, top, left + arm, top);
  SDL_RenderLine(renderer, left, top, left, top + arm);
  SDL_RenderLine(renderer, right, top, right - arm, top);
  SDL_RenderLine(renderer, right, top, right, top + arm);
  SDL_RenderLine(renderer, left, bottom, left + arm, bottom);
  SDL_RenderLine(renderer, left, bottom, left, bottom - arm);
  SDL_RenderLine(renderer, right, bottom, right - arm, bottom);
  SDL_RenderLine(renderer, right, bottom, right, bottom - arm);
}

void draw_shell_panel_contents(
    SDL_Renderer* renderer,
    TTF_Font* font,
    realmz::presentation::ShellPanelKind kind,
    const realmz::presentation::LogicalRect& panel,
    const realmz::presentation::PresentationShellModel& model,
    const std::vector<realmz::presentation::ShellControlPlacement>& controls,
    std::optional<realmz::presentation::ShellRegionId> pressed_control,
    std::optional<realmz::presentation::ShellRegionId> focused_control,
    double backing_scale) {
  using realmz::presentation::ActionAvailability;
  using realmz::presentation::LogicalRect;
  using realmz::presentation::ShellPanelKind;

  constexpr SDL_Color kHeading{224, 205, 165, 255};
  constexpr SDL_Color kBody{222, 222, 218, 255};
  constexpr SDL_Color kMuted{164, 164, 158, 255};
  constexpr SDL_Color kSelected{231, 188, 105, 255};
  const double left = panel.x + 14.0;
  const double width = std::max(0.0, panel.width - 28.0);
  const float heading_size = static_cast<float>(
      model.typography.heading.point_size);
  const float body_size = static_cast<float>(
      model.typography.body.point_size);
  const float caption_size = static_cast<float>(
      model.typography.caption.point_size);

  if (kind == ShellPanelKind::party_rail) {
    if (model.party_rail.members.empty()) {
      draw_shell_text(renderer, font, "PARTY",
          {left, panel.y + 11.0, width, 24.0},
          kHeading, backing_scale, heading_size, TTF_STYLE_BOLD);
      draw_shell_text(renderer, font,
          "Party information appears when a group is active.",
          {left, panel.y + 39.0, width, 46.0},
          kMuted, backing_scale, body_size);
      return;
    }

    const auto party_layout =
        realmz::presentation::compute_party_rail_layout({
            .party_panel = panel,
            .members = model.party_rail.members,
            .typography = model.typography,
        });
    draw_shell_text(renderer, font, party_layout.heading_text,
        party_layout.heading_bounds, kHeading, backing_scale,
        party_layout.heading_text_style, TTF_STYLE_BOLD);
    for (const auto& placed : party_layout.members) {
      const auto& member = model.party_rail.members[placed.member_index];
      const auto party_control = std::ranges::find_if(
          controls,
          [&placed](const auto& candidate) {
            const auto* selection =
                std::get_if<realmz::presentation::SelectPartyMemberAction>(
                    &candidate.payload);
            return candidate.kind ==
                    realmz::presentation::ShellControlKind::party_member &&
                selection && (selection->member == placed.member_id) &&
                (candidate.bounds == placed.card_bounds);
          });
      const bool pressed = party_control != controls.end() &&
          pressed_control && (*pressed_control == party_control->region);
      const bool focused = party_control != controls.end() &&
          focused_control && (*focused_control == party_control->region);
      auto card_rect = sdl_rect(placed.card_bounds);
      SDL_SetRenderDrawColor(
          renderer,
          pressed ? 67 : (member.selected ? 50 : 37),
          pressed ? 57 : (member.selected ? 48 : 40),
          pressed ? 43 : (member.selected ? 43 : 48),
          255);
      SDL_RenderFillRect(renderer, &card_rect);
      SDL_SetRenderDrawColor(
          renderer,
          member.selected ? 187 : 76,
          member.selected ? 145 : 72,
          member.selected ? 78 : 64,
          255);
      SDL_RenderRect(renderer, &card_rect);
      if (pressed) {
        SDL_FRect inner = card_rect;
        inner.x += 2.0f;
        inner.y += 2.0f;
        inner.w = std::max(0.0f, inner.w - 4.0f);
        inner.h = std::max(0.0f, inner.h - 4.0f);
        SDL_RenderRect(renderer, &inner);
      }
      if (focused) {
        draw_shell_focus_corners(renderer, placed.card_bounds);
      }

      draw_shell_text(renderer, font, placed.name_text,
          placed.name_bounds,
          member.selected ? kSelected : kBody,
          backing_scale, placed.name_text_style,
          member.selected ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL);
      draw_shell_text(renderer, font, placed.level_text,
          placed.level_bounds, kMuted, backing_scale,
          placed.level_text_style);
      draw_shell_meter(renderer, placed.stamina_meter_bounds,
          member.stamina.fill_fraction,
          member.stamina.maximum > 0);
      draw_shell_text(renderer, font, placed.stamina_value_text,
          placed.stamina_value_bounds, kMuted, backing_scale,
          placed.stamina_value_text_style);

      const auto emphasis =
          strongest_shell_state_emphasis(placed.state_tokens);
      draw_shell_text(renderer, font, placed.state_text,
          placed.state_bounds, shell_state_color(emphasis), backing_scale,
          placed.state_text_style,
          member.conscious ? TTF_STYLE_NORMAL : TTF_STYLE_BOLD);
    }
    return;
  }

  if (kind == ShellPanelKind::action_bar) {
    draw_shell_text(renderer, font, "ACTIONS", {left, panel.y + 11.0, width, 22.0},
        kHeading, backing_scale, heading_size, TTF_STYLE_BOLD);
    const bool has_semantic_movement = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind ==
              realmz::presentation::ShellControlKind::movement;
        });
    const bool has_semantic_inventory = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind ==
              realmz::presentation::ShellControlKind::open_inventory;
        });
    const bool has_semantic_spellbook = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind ==
              realmz::presentation::ShellControlKind::open_spellbook;
        });
    const bool has_semantic_save = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind ==
              realmz::presentation::ShellControlKind::open_save_game;
        });
    const bool has_semantic_load = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind ==
              realmz::presentation::ShellControlKind::open_load_game;
        });
    const bool has_semantic_guard = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind ==
              realmz::presentation::ShellControlKind::guard_combatant;
        });
    const bool has_semantic_finish = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind ==
              realmz::presentation::ShellControlKind::finish_combatant;
        });
    const bool has_semantic_delay = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind ==
              realmz::presentation::ShellControlKind::delay_combatant;
        });
    const bool has_semantic_center = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::center_active_combatant;
        });
    const bool has_semantic_combat_page = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::combat_action_page;
        });
    const bool has_semantic_switch_weapon = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::switch_weapon_set;
        });
    const bool has_semantic_combat_focus_cycle = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::cycle_combat_focus;
        });
    const bool has_semantic_combat_items = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::open_combat_items;
        });
    const bool has_semantic_auto_combatant = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::auto_combatant;
        });
    const bool has_semantic_show_combat_range = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::show_combat_range;
        });
    const bool has_semantic_bandage_combatant = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::bandage_combatant;
        });
    const bool has_semantic_undo_combatant = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::undo_combatant;
        });
    const bool has_semantic_open_combat_spellbook = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::open_combat_spellbook;
        });
    const bool has_semantic_open_combat_targeting = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::open_combat_targeting;
        });
    const bool has_semantic_escape_combat = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::escape_combat;
        });
    const bool has_semantic_open_combat_scroll_case = std::ranges::any_of(
        controls,
        [](const auto& control) {
          return control.kind == realmz::presentation::
              ShellControlKind::open_combat_scroll_case;
        });
    std::string action_summary =
        "COMPATIBILITY CONTROLS ACTIVE — use the controls inside the game frame";
    if (has_semantic_movement) {
      action_summary = "SEMANTIC MOVEMENT";
      if (has_semantic_inventory) {
        action_summary += " + ITEMS";
      }
      if (has_semantic_spellbook) {
        action_summary += " + SPELLS";
      }
      if (has_semantic_save) {
        action_summary += " + SAVE";
      }
      if (has_semantic_load) {
        action_summary += " + LOAD";
      }
      action_summary += " — other actions remain in the game frame";
    } else if (has_semantic_guard || has_semantic_finish ||
        has_semantic_delay || has_semantic_center ||
        has_semantic_switch_weapon || has_semantic_combat_focus_cycle ||
        has_semantic_combat_items || has_semantic_auto_combatant ||
        has_semantic_show_combat_range || has_semantic_bandage_combatant ||
        has_semantic_undo_combatant ||
        has_semantic_open_combat_spellbook ||
        has_semantic_open_combat_targeting || has_semantic_escape_combat ||
        has_semantic_open_combat_scroll_case) {
      action_summary = "SEMANTIC COMBAT";
      if (has_semantic_guard) {
        action_summary += " + GUARD";
      }
      if (has_semantic_finish) {
        action_summary += " + FINISH";
      }
      if (has_semantic_delay) {
        action_summary += " + DELAY";
      }
      if (has_semantic_center) {
        action_summary += " + CENTER";
      }
      if (has_semantic_switch_weapon) {
        action_summary += " + WEAPON";
      }
      if (has_semantic_combat_focus_cycle) {
        action_summary += " + VIEW";
      }
      if (has_semantic_combat_items) {
        action_summary += " + ITEMS";
      }
      if (has_semantic_auto_combatant) {
        action_summary += " + AUTO";
      }
      if (has_semantic_show_combat_range) {
        action_summary += " + RANGE";
      }
      if (has_semantic_bandage_combatant) {
        action_summary += " + BANDAGE";
      }
      if (has_semantic_undo_combatant) {
        action_summary += " + UNDO";
      }
      if (has_semantic_open_combat_spellbook) {
        action_summary += " + CAST";
      }
      if (has_semantic_open_combat_targeting) {
        action_summary += " + TARGET";
      }
      if (has_semantic_escape_combat) {
        action_summary += " + ESCAPE";
      }
      if (has_semantic_open_combat_scroll_case) {
        action_summary += " + SCROLL";
      }
    }
    double action_summary_width = width;
    for (const auto& control : controls) {
      if (control.kind == realmz::presentation::ShellControlKind::
              combat_action_page) {
        action_summary_width = std::min(
            action_summary_width,
            std::max(0.0, control.bounds.x - 8.0 - left));
      }
    }
    draw_shell_text(renderer, font, action_summary,
        {left, panel.y + 37.0, action_summary_width, 24.0},
        kSelected, backing_scale, caption_size, TTF_STYLE_BOLD);
    if (has_semantic_movement || has_semantic_guard ||
        has_semantic_finish || has_semantic_delay || has_semantic_center ||
        has_semantic_combat_page || has_semantic_switch_weapon ||
        has_semantic_combat_focus_cycle || has_semantic_combat_items ||
        has_semantic_auto_combatant || has_semantic_show_combat_range ||
        has_semantic_bandage_combatant || has_semantic_undo_combatant ||
        has_semantic_open_combat_spellbook ||
        has_semantic_open_combat_targeting || has_semantic_escape_combat ||
        has_semantic_open_combat_scroll_case) {
      for (const auto& control : controls) {
        if ((control.kind !=
                realmz::presentation::ShellControlKind::movement) &&
            (control.kind !=
                realmz::presentation::ShellControlKind::open_inventory) &&
            (control.kind !=
                realmz::presentation::ShellControlKind::open_spellbook) &&
            (control.kind !=
                realmz::presentation::ShellControlKind::open_save_game) &&
            (control.kind !=
                realmz::presentation::ShellControlKind::open_load_game) &&
            (control.kind !=
                realmz::presentation::ShellControlKind::guard_combatant) &&
            (control.kind !=
                realmz::presentation::ShellControlKind::finish_combatant) &&
            (control.kind !=
                realmz::presentation::ShellControlKind::delay_combatant) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::center_active_combatant) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::combat_action_page) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::switch_weapon_set) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::cycle_combat_focus) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::open_combat_items) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::auto_combatant) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::show_combat_range) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::bandage_combatant) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::undo_combatant) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::open_combat_spellbook) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::open_combat_targeting) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::escape_combat) &&
            (control.kind != realmz::presentation::
                    ShellControlKind::open_combat_scroll_case)) {
          continue;
        }
        const bool pressed = pressed_control &&
            (*pressed_control == control.region);
        const bool focused = focused_control &&
            (*focused_control == control.region);
        auto button = sdl_rect(control.bounds);
        SDL_SetRenderDrawColor(
            renderer,
            pressed ? 91 : (control.enabled ? 52 : 39),
            pressed ? 73 : (control.enabled ? 49 : 39),
            pressed ? 46 : (control.enabled ? 43 : 42),
            255);
        SDL_RenderFillRect(renderer, &button);
        SDL_SetRenderDrawColor(
            renderer,
            pressed ? 238 : (control.enabled ? 174 : 92),
            pressed ? 196 : (control.enabled ? 139 : 87),
            pressed ? 110 : (control.enabled ? 81 : 77),
            255);
        SDL_RenderRect(renderer, &button);
        if (pressed) {
          SDL_FRect inner = button;
          inner.x += 2.0f;
          inner.y += 2.0f;
          inner.w = std::max(0.0f, inner.w - 4.0f);
          inner.h = std::max(0.0f, inner.h - 4.0f);
          SDL_RenderRect(renderer, &inner);
        }
        if (focused) {
          draw_shell_focus_corners(renderer, control.bounds);
        }
        const double horizontal_label_inset =
            control.kind == realmz::presentation::ShellControlKind::
                    combat_action_page
            ? 4.0
            : 8.0;
        draw_shell_text(
            renderer,
            font,
            control.label,
            {
                control.bounds.x + horizontal_label_inset,
                control.bounds.y + 13.0,
                std::max(
                    0.0, control.bounds.width - 2.0 * horizontal_label_inset),
                std::max(0.0, control.bounds.height - 18.0),
            },
            control.enabled ? kBody : kMuted,
            backing_scale,
            caption_size,
            TTF_STYLE_BOLD);
      }
    } else {
      std::string actions;
      for (const auto& action : model.actions) {
        if (!actions.empty()) {
          actions += "  ·  ";
        }
        actions += action.label;
        if (action.availability == ActionAvailability::unavailable) {
          actions += " (unavailable)";
        }
      }
      if (actions.empty()) {
        actions = "Movement  ·  Party  ·  Inventory  ·  Spells  ·  Save / Load";
      }
      draw_shell_text(renderer, font, actions,
          {left, panel.y + 65.0, width, std::max(20.0, panel.height - 75.0)},
          kMuted, backing_scale, caption_size);
    }
    return;
  }

  if (kind == ShellPanelKind::details) {
    draw_shell_text(renderer, font, "DETAILS", {left, panel.y + 11.0, width, 22.0},
        kHeading, backing_scale, heading_size, TTF_STYLE_BOLD);
    if (!model.selected_details.member) {
      draw_shell_text(renderer, font, "Select a party member in the game frame.",
          {left, panel.y + 39.0, width, 44.0},
          kMuted, backing_scale, body_size);
      return;
    }
    draw_shell_text(renderer, font,
        model.selected_details.name.empty()
            ? "Selected party member"
            : model.selected_details.name,
        {left, panel.y + 39.0, width, 22.0},
        kBody, backing_scale, body_size, TTF_STYLE_BOLD);
    draw_shell_text(renderer, font,
        std::format("Level {}   ·   Armor {}\nMovement {} / {}",
            model.selected_details.level,
            model.selected_details.armor_class,
            model.selected_details.movement,
            model.selected_details.movement_maximum),
        {left, panel.y + 69.0, width, std::max(28.0, panel.height - 79.0)},
        kMuted, backing_scale, caption_size);
    return;
  }

  if (kind == ShellPanelKind::event_log) {
    draw_shell_text(renderer, font, "EVENT LOG", {left, panel.y + 11.0, width, 22.0},
        kHeading, backing_scale, heading_size, TTF_STYLE_BOLD);
    draw_shell_text(renderer, font,
        model.event_log.entries.empty()
            ? "Semantic game messages will appear here as screen migration continues."
            : model.event_log.entries.back().text,
        {left, panel.y + 39.0, width, std::max(24.0, panel.height - 49.0)},
        kMuted, backing_scale, caption_size);
    return;
  }

  if (kind == ShellPanelKind::drawer_tabs) {
    for (const auto& tab : model.drawers.tabs) {
      const auto control = std::ranges::find_if(
          controls,
          [&tab](const auto& candidate) {
            return candidate.kind ==
                    realmz::presentation::ShellControlKind::drawer_tab &&
                candidate.focus_identifier == tab.focus_identifier;
          });
      if (control == controls.end()) {
        continue;
      }
      const bool pressed = pressed_control &&
          (*pressed_control == control->region);
      const bool focused = focused_control &&
          (*focused_control == control->region);
      auto button = sdl_rect(control->bounds);
      SDL_SetRenderDrawColor(
          renderer,
          pressed ? 91 : (tab.active ? 57 : 39),
          pressed ? 73 : (tab.active ? 51 : 42),
          pressed ? 46 : (tab.active ? 39 : 45),
          255);
      SDL_RenderFillRect(renderer, &button);
      SDL_SetRenderDrawColor(
          renderer,
          tab.active ? 222 : 116,
          tab.active ? 174 : 101,
          tab.active ? 92 : 82,
          255);
      SDL_RenderRect(renderer, &button);
      if (pressed) {
        SDL_FRect inner = button;
        inner.x += 2.0f;
        inner.y += 2.0f;
        inner.w = std::max(0.0f, inner.w - 4.0f);
        inner.h = std::max(0.0f, inner.h - 4.0f);
        SDL_RenderRect(renderer, &inner);
      }
      if (focused) {
        draw_shell_focus_corners(renderer, control->bounds);
      }
      std::string label = control->label;
      if (tab.badge_count != 0U) {
        label += std::format(" ({})", tab.badge_count);
      }
      if (tab.active) {
        label += "\nOPEN";
      }
      draw_shell_text(
          renderer,
          font,
          label,
          {
              control->bounds.x + 7.0,
              control->bounds.y + 5.0,
              std::max(0.0, control->bounds.width - 14.0),
              std::max(0.0, control->bounds.height - 10.0),
          },
          tab.active ? kSelected : kBody,
          backing_scale,
          caption_size,
          TTF_STYLE_BOLD);
    }

    const LogicalRect content{
        left,
        panel.y + 61.0,
        width,
        std::max(20.0, panel.height - 70.0),
    };
    if (!model.drawers.active_panel) {
      draw_shell_text(renderer, font,
          "Open Details or Event log. Tab selects; Return or Space opens.",
          content, kMuted, backing_scale, caption_size);
    } else if (*model.drawers.active_panel ==
        realmz::presentation::DrawerPanel::details) {
      if (!model.selected_details.member) {
        draw_shell_text(renderer, font,
            "DETAILS — Select a party member in the game frame.",
            content, kMuted, backing_scale, caption_size);
      } else {
        draw_shell_text(renderer, font,
            std::format("DETAILS — {}\nLevel {} · Armor {} · Move {}/{}",
                model.selected_details.name.empty()
                    ? "Selected party member"
                    : model.selected_details.name,
                model.selected_details.level,
                model.selected_details.armor_class,
                model.selected_details.movement,
                model.selected_details.movement_maximum),
            content, kBody, backing_scale, caption_size);
      }
    } else {
      draw_shell_text(renderer, font,
          model.event_log.entries.empty()
              ? "EVENT LOG — Semantic game messages will appear here."
              : "EVENT LOG — " + model.event_log.entries.back().text,
          content, kMuted, backing_scale, caption_size);
    }
  }
}

bool classic_point_for_target(
    const realmz::presentation::RemasteredPointerTarget& target,
    Point* classic_point) {
  const auto* legacy =
      std::get_if<realmz::presentation::LegacyPointerTarget>(&target);
  if (!legacy || !classic_point) {
    return false;
  }
  constexpr double kMinimum =
      static_cast<double>(std::numeric_limits<int16_t>::min());
  constexpr double kMaximum =
      static_cast<double>(std::numeric_limits<int16_t>::max());
  // Match the legacy SDL event path's float-to-int conversion. Rounding here
  // would shift half of the scaled subpixels into a neighboring Classic hit
  // region, and captured negative coordinates must also truncate toward zero.
  classic_point->h = static_cast<int16_t>(
      std::clamp(legacy->classic_point.x, kMinimum, kMaximum));
  classic_point->v = static_cast<int16_t>(
      std::clamp(legacy->classic_point.y, kMinimum, kMaximum));
  return true;
}

} // namespace

sdl_texture_ptr WindowManager::create_classic_frame_texture(
    SDL_Renderer* renderer) {
  const auto w = this->screen_port.data.get_width();
  const auto h = this->screen_port.data.get_height();
  if (ENABLE_RECOMPOSITE_DEBUG) {
    wm_log.info_f("Writing debug{}.bmp", debug_number);
    phosg::save_file(std::format("debug{}.bmp", debug_number++),
        this->screen_port.data.serialize(phosg::ImageFormat::WINDOWS_BITMAP));
  }

  const void* surface_data = this->screen_port.data.get_data();
  const float gdisplay = kPortGammaOptions[this->gamma_idx].display_gamma;
  if (gdisplay > 0.0f) {
    if (this->gamma_lut_idx != this->gamma_idx) {
      const float exp = 1.8f / gdisplay;
      this->gamma_lut[0] = 0;
      for (int i = 1; i < 255; i++) {
        this->gamma_lut[i] = static_cast<uint8_t>(
            std::round(255.0f * std::pow(i / 255.0f, exp)));
      }
      this->gamma_lut[255] = 255;
      this->gamma_lut_idx = this->gamma_idx;
    }
    const uint8_t* lut = this->gamma_lut;
    const size_t n = static_cast<size_t>(w) * h;
    this->gamma_scratch.resize(n);
    const uint32_t* src = static_cast<const uint32_t*>(surface_data);
    for (size_t i = 0; i < n; i++) {
      const uint32_t p = src[i];
      this->gamma_scratch[i] =
          (static_cast<uint32_t>(lut[(p >> 24) & 0xFF]) << 24) |
          (static_cast<uint32_t>(lut[(p >> 16) & 0xFF]) << 16) |
          (static_cast<uint32_t>(lut[(p >> 8) & 0xFF]) << 8) |
          (p & 0xFF);
    }
    surface_data = this->gamma_scratch.data();
  }

  auto surface = sdl_make_unique(SDL_CreateSurfaceFrom(
      w, h, SDL_PIXELFORMAT_RGBA8888,
      const_cast<void*>(surface_data), 4 * w));
  if (!surface) {
    wm_log.error_f("Could not create surface: {}", SDL_GetError());
    return {};
  }
  auto texture = sdl_make_unique(
      SDL_CreateTextureFromSurface(renderer, surface.get()));
  if (!texture) {
    wm_log.error_f("Could not create texture: {}", SDL_GetError());
    return {};
  }
  SDL_SetTextureScaleMode(texture.get(), this->scale_mode);
  return texture;
}

void WindowManager::present_classic_frame() {
  this->adaptive_shell_plan.reset();
  this->remastered_input_mapper.reset();
  this->remastered_shell_controls.clear();
  this->remastered_pressed_shell_control.reset();
  this->remastered_shell_keyboard.cancel_route();
  this->remastered_combat_action_page =
      realmz::presentation::CombatActionPage::primary;
  if (!this->sdl_window) {
    return;
  }
  auto* renderer = SDL_GetRenderer(this->sdl_window.get());
  if (!renderer) {
    wm_log.error_f("Could not get window renderer: {}", SDL_GetError());
    return;
  }
  SDL_SetRenderLogicalPresentation(renderer, kLogicalWindowWidth,
      kLogicalWindowHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  auto texture = this->create_classic_frame_texture(renderer);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  if (texture) {
    SDL_RenderTexture(renderer, texture.get(), nullptr, nullptr);
  }
  SDL_RenderPresent(renderer);
  SDL_SyncWindow(this->sdl_window.get());
  this->update_text_input_area();
}

void WindowManager::present_remastered_frame() {
  if (!this->sdl_window) {
    return;
  }
  auto* renderer = SDL_GetRenderer(this->sdl_window.get());
  if (!renderer) {
    wm_log.error_f("Could not get window renderer: {}", SDL_GetError());
    return;
  }
  this->remastered_shell_controls.clear();

  int window_width = 0;
  int window_height = 0;
  SDL_GetWindowSize(this->sdl_window.get(), &window_width, &window_height);
  window_width = std::max(window_width, REMASTER_MINIMUM_WIDTH);
  window_height = std::max(window_height, REMASTER_MINIMUM_HEIGHT);
  SDL_SetRenderLogicalPresentation(renderer, window_width, window_height,
      SDL_LOGICAL_PRESENTATION_LETTERBOX);

  const float reported_density = SDL_GetWindowPixelDensity(
      this->sdl_window.get());
  const double backing_scale =
      std::isfinite(reported_density) && (reported_density > 0.0f)
      ? static_cast<double>(reported_density)
      : 1.0;
  const auto legacy_context = RealmzCaptureLegacyPresentationContext();
  const auto screen =
      realmz::presentation::screen_context_from_legacy(legacy_context);
  if (screen != realmz::presentation::ScreenContext::combat) {
    this->remastered_combat_action_page =
        realmz::presentation::CombatActionPage::primary;
  }
  this->adaptive_shell_plan =
      realmz::presentation::compute_adaptive_shell_plan({
          .mode = realmz::presentation::PresentationMode::remastered,
          .screen = screen,
          .window_size = {
              static_cast<double>(window_width),
              static_cast<double>(window_height),
          },
          .backing_scale = backing_scale,
          .legacy_full_frame_required =
              legacy_context.requires_full_frame != 0,
          // Keep the full Classic frame embedded until all commands needed by
          // the active screen have typed handlers. The compatibility shell's
          // proven movement and party-selection controls remain interactive.
          .semantic_controls_ready = false,
      });

  std::optional<realmz::presentation::PresentationShellModel> shell_model;
  TTF_Font* shell_font = nullptr;
  bool shell_keyboard_route_enabled = false;
  if (this->adaptive_shell_plan->adaptive_layout && TTF_WasInit()) {
    try {
      auto snapshot = realmz::presentation::LegacyGameSnapshotSource().capture();
      const bool snapshot_context_matches = snapshot.screen == screen;
      snapshot.screen = screen;
      const bool panels_collapsed =
          this->adaptive_shell_plan->adaptive_layout->layout_class ==
          realmz::presentation::LayoutClass::compact;
      shell_model = realmz::presentation::build_presentation_shell_model(
          snapshot,
          {},
          {
              .panels_collapsed = panels_collapsed,
              .active_drawer = this->remastered_active_drawer,
              .combat_action_page = this->remastered_combat_action_page,
          });
      const bool world_action_surface =
          (screen == realmz::presentation::ScreenContext::exploration) ||
          (screen == realmz::presentation::ScreenContext::dungeon);
      const bool navigation_available =
          legacy_context.adaptive_eligible != 0 &&
          std::ranges::any_of(
              shell_model->actions,
              [](const auto& action) {
                return action.intent ==
                        realmz::presentation::ActionIntent::navigate &&
                    action.can_invoke();
              });
      const auto inventory_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::open_inventory;
          });
      const std::optional<realmz::presentation::PartyMemberId>
          inventory_member =
              (world_action_surface &&
                  (inventory_action != shell_model->actions.end()))
              ? inventory_action->party_member
              : std::nullopt;
      const bool inventory_available = inventory_member &&
          (inventory_action != shell_model->actions.end()) &&
          inventory_action->can_invoke() && snapshot_context_matches &&
          legacy_context.adaptive_eligible != 0;
      const auto spellbook_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::cast_spell;
          });
      const std::optional<realmz::presentation::PartyMemberId>
          spellbook_member =
              (world_action_surface &&
                  (spellbook_action != shell_model->actions.end()))
              ? spellbook_action->party_member
              : std::nullopt;
      const bool spellbook_available = spellbook_member &&
          (spellbook_action != shell_model->actions.end()) &&
          spellbook_action->can_invoke() && snapshot_context_matches &&
          legacy_context.adaptive_eligible != 0;
      const auto save_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::save_game;
          });
      const bool save_control_visible =
          world_action_surface && save_action != shell_model->actions.end();
      const bool save_available = save_control_visible &&
          save_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_menu_command_for_open_save_game({
              .screen = screen,
              .world_presentation = snapshot.world.presentation,
              .adaptive_eligible =
                  legacy_context.adaptive_eligible != 0,
          }).has_value();
      const auto load_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::load_game;
          });
      const bool load_control_visible =
          world_action_surface && load_action != shell_model->actions.end();
      const bool load_available = load_control_visible &&
          load_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_menu_command_for_open_load_game({
              .screen = screen,
              .world_presentation = snapshot.world.presentation,
              .adaptive_eligible =
                  legacy_context.adaptive_eligible != 0,
          }).has_value();
      const auto guard_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::guard;
          });
      const std::optional<realmz::presentation::CombatantId> guard_combatant =
          (guard_action != shell_model->actions.end())
          ? guard_action->combatant
          : std::nullopt;
      const bool guard_available = guard_combatant &&
          guard_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_guard_combatant(
              *guard_combatant,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const auto finish_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::finish;
          });
      const std::optional<realmz::presentation::CombatantId> finish_combatant =
          (finish_action != shell_model->actions.end())
          ? finish_action->combatant
          : std::nullopt;
      const bool finish_available = finish_combatant &&
          finish_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_finish_combatant(
              *finish_combatant,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const auto delay_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::delay;
          });
      const std::optional<realmz::presentation::CombatantId> delay_combatant =
          (delay_action != shell_model->actions.end())
          ? delay_action->combatant
          : std::nullopt;
      const auto* delay_member = delay_combatant &&
              (*delay_combatant >= 0) && (*delay_combatant <= 0xFF)
          ? snapshot.party.member(
                static_cast<realmz::presentation::PartyMemberId>(
                    *delay_combatant))
          : nullptr;
      const bool delay_available = delay_combatant && delay_member &&
          (delay_member->movement == delay_member->movement_maximum) &&
          delay_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_delay_combatant(
              *delay_combatant,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const auto center_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::center_active;
          });
      const std::optional<realmz::presentation::CombatantId> center_combatant =
          (center_action != shell_model->actions.end())
          ? center_action->combatant
          : std::nullopt;
      const bool center_available = center_combatant &&
          center_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_center_active_combatant(
              *center_combatant,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const auto switch_weapon_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::switch_weapon;
          });
      const auto center_previous_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::center_previous;
          });
      const auto center_next_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::center_next;
          });
      const auto combat_items_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::combat_items;
          });
      const auto auto_combatant_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::auto_combatant;
          });
      const auto show_combat_range_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::show_combat_range;
          });
      const auto bandage_combatant_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::bandage_combatant;
          });
      const auto undo_combatant_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::undo_combatant;
          });
      const auto open_combat_spellbook_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent == realmz::presentation::
                ActionIntent::open_combat_spellbook;
          });
      const auto open_combat_targeting_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent == realmz::presentation::
                ActionIntent::open_combat_targeting;
          });
      const auto escape_combat_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent ==
                realmz::presentation::ActionIntent::escape_combat;
          });
      const auto open_combat_scroll_case_action = std::ranges::find_if(
          shell_model->actions,
          [](const auto& action) {
            return action.intent == realmz::presentation::
                ActionIntent::open_combat_scroll_case;
          });
      const auto modeled_switch_weapon_combatant =
          (switch_weapon_action != shell_model->actions.end())
          ? switch_weapon_action->combatant
          : std::nullopt;
      const auto modeled_center_previous_combatant =
          (center_previous_action != shell_model->actions.end())
          ? center_previous_action->combatant
          : std::nullopt;
      const auto modeled_center_next_combatant =
          (center_next_action != shell_model->actions.end())
          ? center_next_action->combatant
          : std::nullopt;
      const auto modeled_combat_items_combatant =
          (combat_items_action != shell_model->actions.end())
          ? combat_items_action->combatant
          : std::nullopt;
      const auto modeled_combat_items_member =
          (combat_items_action != shell_model->actions.end())
          ? combat_items_action->party_member
          : std::nullopt;
      const auto modeled_auto_combatant =
          (auto_combatant_action != shell_model->actions.end())
          ? auto_combatant_action->combatant
          : std::nullopt;
      const auto modeled_show_combat_range_combatant =
          (show_combat_range_action != shell_model->actions.end())
          ? show_combat_range_action->combatant
          : std::nullopt;
      const auto modeled_bandage_combatant =
          (bandage_combatant_action != shell_model->actions.end())
          ? bandage_combatant_action->combatant
          : std::nullopt;
      const auto modeled_undo_combatant =
          (undo_combatant_action != shell_model->actions.end())
          ? undo_combatant_action->combatant
          : std::nullopt;
      const auto modeled_open_combat_spellbook =
          (open_combat_spellbook_action != shell_model->actions.end())
          ? open_combat_spellbook_action->combatant
          : std::nullopt;
      const auto modeled_open_combat_targeting =
          (open_combat_targeting_action != shell_model->actions.end())
          ? open_combat_targeting_action->combatant
          : std::nullopt;
      const auto modeled_escape_combat =
          (escape_combat_action != shell_model->actions.end())
          ? escape_combat_action->combatant
          : std::nullopt;
      const auto modeled_open_combat_scroll_case =
          (open_combat_scroll_case_action != shell_model->actions.end())
          ? open_combat_scroll_case_action->combatant
          : std::nullopt;
      const auto live_combat_party_actor =
          [&snapshot, snapshot_context_matches, legacy_context, screen](
              std::optional<realmz::presentation::CombatantId> modeled)
          -> std::optional<realmz::presentation::CombatantId> {
        if (!modeled || (*modeled < 0) || (*modeled > 0xFF) ||
            !snapshot_context_matches ||
            legacy_context.adaptive_eligible == 0 ||
            screen != realmz::presentation::ScreenContext::combat ||
            !snapshot.combat || !snapshot.combat->active ||
            snapshot.combat->acting_combatant != *modeled ||
            !snapshot.party.member(
                static_cast<realmz::presentation::PartyMemberId>(*modeled))) {
          return std::nullopt;
        }
        const auto combatant = std::ranges::find(
            snapshot.combat->combatants,
            *modeled,
            &realmz::presentation::CombatantView::id);
        if (combatant == snapshot.combat->combatants.end() ||
            combatant->kind !=
                realmz::presentation::CombatantKind::party_member ||
            !combatant->active || !combatant->targetable ||
            combatant->stamina.current <= 0) {
          return std::nullopt;
        }
        return modeled;
      };
      const auto switch_weapon_combatant =
          live_combat_party_actor(modeled_switch_weapon_combatant);
      const auto center_previous_combatant =
          live_combat_party_actor(modeled_center_previous_combatant);
      const auto center_next_combatant =
          live_combat_party_actor(modeled_center_next_combatant);
      const auto combat_items_combatant =
          live_combat_party_actor(modeled_combat_items_combatant);
      const auto auto_combatant =
          live_combat_party_actor(modeled_auto_combatant);
      const auto show_combat_range_combatant =
          live_combat_party_actor(modeled_show_combat_range_combatant);
      const auto bandage_combatant =
          live_combat_party_actor(modeled_bandage_combatant);
      const auto undo_combatant =
          live_combat_party_actor(modeled_undo_combatant);
      const auto open_combat_spellbook =
          live_combat_party_actor(modeled_open_combat_spellbook);
      const auto open_combat_targeting =
          live_combat_party_actor(modeled_open_combat_targeting);
      const auto escape_combat =
          live_combat_party_actor(modeled_escape_combat);
      const auto open_combat_scroll_case =
          live_combat_party_actor(modeled_open_combat_scroll_case);
      const auto* combat_items_member =
          modeled_combat_items_member
          ? snapshot.party.member(*modeled_combat_items_member)
          : nullptr;
      const std::optional<realmz::presentation::OpenCombatItemsAction>
          open_combat_items =
              combat_items_combatant && modeled_combat_items_member &&
                  combat_items_member && combat_items_member->selected &&
                  snapshot.party.selected_member ==
                      *modeled_combat_items_member
              ? std::optional<realmz::presentation::OpenCombatItemsAction>{
                    realmz::presentation::OpenCombatItemsAction{
                        *combat_items_combatant,
                        *modeled_combat_items_member}}
              : std::nullopt;
      std::optional<realmz::presentation::CombatantId> secondary_combatant;
      bool secondary_combatants_match = true;
      for (const auto combatant : {
               switch_weapon_combatant,
               center_previous_combatant,
               center_next_combatant,
               open_combat_items
                   ? std::optional<realmz::presentation::CombatantId>{
                         open_combat_items->combatant}
                   : std::nullopt,
               auto_combatant,
               show_combat_range_combatant,
               bandage_combatant,
               undo_combatant,
               open_combat_spellbook,
               open_combat_targeting,
               escape_combat,
               open_combat_scroll_case,
           }) {
        if (!combatant) {
          continue;
        }
        if (secondary_combatant && (*secondary_combatant != *combatant)) {
          secondary_combatants_match = false;
          break;
        }
        secondary_combatant = combatant;
      }
      const bool invalid_paged_combat_actions =
          !secondary_combatant || !secondary_combatants_match;
      const bool unavailable_utility_page =
          this->remastered_combat_action_page ==
              realmz::presentation::CombatActionPage::utility &&
          !auto_combatant && !show_combat_range_combatant &&
          !bandage_combatant && !undo_combatant &&
          !open_combat_spellbook && !open_combat_targeting &&
          !escape_combat && !open_combat_scroll_case;
      const bool unavailable_special_page =
          this->remastered_combat_action_page ==
              realmz::presentation::CombatActionPage::special &&
          !open_combat_spellbook && !open_combat_targeting && !escape_combat &&
          !open_combat_scroll_case;
      if ((invalid_paged_combat_actions &&
              this->remastered_combat_action_page !=
                  realmz::presentation::CombatActionPage::primary) ||
          unavailable_utility_page || unavailable_special_page) {
        this->remastered_combat_action_page =
            realmz::presentation::CombatActionPage::primary;
        shell_model->combat_action_page =
            realmz::presentation::CombatActionPage::primary;
      }
      const bool switch_weapon_available = switch_weapon_combatant &&
          switch_weapon_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_switch_weapon(
              *switch_weapon_combatant,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const bool center_previous_available = center_previous_combatant &&
          center_previous_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_cycle_combat_focus(
              *center_previous_combatant,
              realmz::presentation::CombatFocusDirection::previous,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const bool center_next_available = center_next_combatant &&
          center_next_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_cycle_combat_focus(
              *center_next_combatant,
              realmz::presentation::CombatFocusDirection::next,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const bool combat_items_available = open_combat_items &&
          combat_items_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_open_combat_items(
              open_combat_items->combatant,
              open_combat_items->member,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const bool auto_combatant_available = auto_combatant &&
          auto_combatant_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_auto_combatant(
              *auto_combatant,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const bool show_combat_range_available =
          show_combat_range_combatant &&
          show_combat_range_action->can_invoke() &&
          snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_show_combat_range(
              *show_combat_range_combatant,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const bool bandage_combatant_available = bandage_combatant &&
          snapshot.combat && snapshot.combat->bandage_available &&
          bandage_combatant_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_bandage_combatant(
              *bandage_combatant,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const bool undo_combatant_available = undo_combatant &&
          snapshot.combat && snapshot.combat->undo_available &&
          undo_combatant_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_undo_combatant(
              *undo_combatant,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const bool open_combat_spellbook_available =
          open_combat_spellbook && snapshot.combat &&
          snapshot.combat->cast_spell_available &&
          open_combat_spellbook_action->can_invoke() &&
          snapshot_context_matches &&
          realmz::presentation::
              legacy_key_message_for_open_combat_spellbook(
                  *open_combat_spellbook,
                  {
                      .screen = screen,
                      .world_presentation = snapshot.world.presentation,
                      .adaptive_eligible =
                          legacy_context.adaptive_eligible != 0,
                  })
                  .has_value();
      const bool open_combat_targeting_available =
          open_combat_targeting && snapshot.combat &&
          snapshot.combat->target_available &&
          open_combat_targeting_action->can_invoke() &&
          snapshot_context_matches &&
          realmz::presentation::
              legacy_key_message_for_open_combat_targeting(
                  *open_combat_targeting,
                  {
                      .screen = screen,
                      .world_presentation = snapshot.world.presentation,
                      .adaptive_eligible =
                          legacy_context.adaptive_eligible != 0,
                  })
                  .has_value();
      const bool escape_combat_available = escape_combat &&
          escape_combat_action->can_invoke() && snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_escape_combat(
              *escape_combat,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      const bool open_combat_scroll_case_available =
          open_combat_scroll_case && snapshot.combat &&
          snapshot.combat->use_scroll_available &&
          open_combat_scroll_case_action->can_invoke() &&
          snapshot_context_matches &&
          realmz::presentation::legacy_key_message_for_open_combat_scroll_case(
              *open_combat_scroll_case,
              {
                  .screen = screen,
                  .world_presentation = snapshot.world.presentation,
                  .adaptive_eligible =
                      legacy_context.adaptive_eligible != 0,
              }).has_value();
      this->remastered_shell_controls =
          realmz::presentation::compute_shell_control_layout({
              .screen = screen,
              .world_presentation = snapshot.world.presentation,
              .action_panel =
                  this->adaptive_shell_plan->adaptive_layout->action_bar,
              .navigation_available = navigation_available,
              .inventory_member = inventory_member,
              .inventory_available = inventory_available,
              .spellbook_member = spellbook_member,
              .spellbook_available = spellbook_available,
              .save_control_visible = save_control_visible,
              .save_available = save_available,
              .load_control_visible = load_control_visible,
              .load_available = load_available,
              .guard_combatant = guard_combatant,
              .guard_available = guard_available,
              .finish_combatant = finish_combatant,
              .finish_available = finish_available,
              .delay_combatant = delay_combatant,
              .delay_available = delay_available,
              .center_active_combatant = center_combatant,
              .center_active_available = center_available,
              .combat_action_page = shell_model->combat_action_page,
              .switch_weapon_combatant = switch_weapon_combatant,
              .switch_weapon_available = switch_weapon_available,
              .center_previous_combatant = center_previous_combatant,
              .center_previous_available = center_previous_available,
              .center_next_combatant = center_next_combatant,
              .center_next_available = center_next_available,
              .combat_items = open_combat_items,
              .combat_items_available = combat_items_available,
              .auto_combatant = auto_combatant,
              .auto_combatant_available = auto_combatant_available,
              .show_combat_range_combatant =
                  show_combat_range_combatant,
              .show_combat_range_available =
                  show_combat_range_available,
              .bandage_combatant = bandage_combatant,
              .bandage_combatant_available =
                  bandage_combatant_available,
              .undo_combatant = undo_combatant,
              .undo_combatant_available = undo_combatant_available,
              .open_combat_spellbook = open_combat_spellbook,
              .open_combat_spellbook_available =
                  open_combat_spellbook_available,
              .open_combat_targeting = open_combat_targeting,
              .open_combat_targeting_available =
                  open_combat_targeting_available,
              .escape_combat = escape_combat,
              .escape_combat_available = escape_combat_available,
              .open_combat_scroll_case = open_combat_scroll_case,
              .open_combat_scroll_case_available =
                  open_combat_scroll_case_available,
          });
      if (!shell_model->party_rail.members.empty()) {
        const auto party_layout =
            realmz::presentation::compute_party_rail_layout({
                .party_panel =
                    this->adaptive_shell_plan->adaptive_layout->party_rail,
                .members = shell_model->party_rail.members,
                .typography = shell_model->typography,
            });
        const bool selection_available =
            snapshot_context_matches &&
            legacy_context.adaptive_eligible != 0 &&
            ((screen == realmz::presentation::ScreenContext::exploration) ||
                (screen == realmz::presentation::ScreenContext::dungeon));
        const auto party_controls =
            realmz::presentation::compute_party_rail_control_layout(
                {
                    .screen = screen,
                    .selection_available = selection_available,
                },
                shell_model->party_rail,
                party_layout);
        for (const auto& party_control : party_controls) {
          this->remastered_shell_controls.emplace_back(
              realmz::presentation::ShellControlPlacement{
                  .region = party_control.region,
                  .kind =
                      realmz::presentation::ShellControlKind::party_member,
                  .bounds = party_control.bounds,
                  .label = party_control.label,
                  .accessibility_label = party_control.accessibility_label,
                  .focus_identifier = party_control.focus_identifier,
                  .tab_order = party_control.tab_order,
                  .enabled = party_control.enabled,
                  .payload = party_control.payload,
              });
        }
      }
      if (this->adaptive_shell_plan->adaptive_layout->drawer_tabs) {
        const auto drawer_controls =
            realmz::presentation::compute_drawer_control_layout(
                shell_model->drawers,
                *this->adaptive_shell_plan->adaptive_layout->drawer_tabs,
                snapshot_context_matches && !this->text_editing_active &&
                    legacy_context.adaptive_eligible != 0);
        this->remastered_shell_controls.insert(
            this->remastered_shell_controls.end(),
            drawer_controls.begin(),
            drawer_controls.end());
      }
      auto font = load_font(GENEVA_FONT_ID);
      if (auto* ttf = std::get_if<TTF_Font*>(&font)) {
        shell_font = *ttf;
      }
      // Hit regions must never outlive their visible controls. If the shell
      // font cannot be loaded, retain the informational panels but leave their
      // otherwise invisible semantic controls noninteractive.
      if (!shell_font) {
        this->remastered_shell_controls.clear();
      } else {
        const realmz::presentation::RuntimeLegacyCommandContext context{
            .screen = screen,
            .world_presentation = snapshot.world.presentation,
            .adaptive_eligible = legacy_context.adaptive_eligible != 0,
        };
        const auto current_combat_action_page =
            shell_model->combat_action_page;
        const auto combat_action_panel =
            this->adaptive_shell_plan->adaptive_layout->action_bar;
        const bool every_enabled_control_is_live = std::ranges::all_of(
            this->remastered_shell_controls,
            [&context, &snapshot, current_combat_action_page,
                combat_action_panel,
                secondary_combatant](const auto& control) {
              if (!control.enabled) {
                return true;
              }
              if (const auto* page = std::get_if<realmz::presentation::
                      SetCombatActionPageAction>(&control.payload)) {
                const bool valid_transition = realmz::presentation::
                    is_valid_combat_action_page_transition(
                        current_combat_action_page, page->page);
                if (control.kind != realmz::presentation::ShellControlKind::
                        combat_action_page ||
                    context.screen !=
                        realmz::presentation::ScreenContext::combat ||
                    !combat_action_panel.contains(control.bounds) ||
                    !valid_transition || !secondary_combatant ||
                    (*secondary_combatant < 0) ||
                    (*secondary_combatant > 0xFF) ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant !=
                        *secondary_combatant) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    *secondary_combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        *secondary_combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* movement =
                      std::get_if<realmz::presentation::MovePartyAction>(
                          &control.payload)) {
                return control.kind ==
                        realmz::presentation::ShellControlKind::movement &&
                    realmz::presentation::legacy_key_message_for_movement(
                        movement->command, context)
                        .has_value();
              }
              if (const auto* selection =
                      std::get_if<
                          realmz::presentation::SelectPartyMemberAction>(
                          &control.payload)) {
                return control.kind ==
                        realmz::presentation::ShellControlKind::party_member &&
                    snapshot.party.member(selection->member) != nullptr;
              }
              if (const auto* inventory =
                      std::get_if<
                          realmz::presentation::OpenInventoryAction>(
                          &control.payload)) {
                const auto* member =
                    snapshot.party.member(inventory->member);
                return control.kind == realmz::presentation::
                        ShellControlKind::open_inventory &&
                    member && member->selected &&
                    snapshot.party.selected_member == inventory->member &&
                    realmz::presentation::
                        legacy_key_message_for_open_inventory(context)
                        .has_value();
              }
              if (const auto* spellbook =
                      std::get_if<
                          realmz::presentation::OpenSpellbookAction>(
                          &control.payload)) {
                const auto* member =
                    snapshot.party.member(spellbook->member);
                return control.kind == realmz::presentation::
                        ShellControlKind::open_spellbook &&
                    member && member->selected && member->conscious &&
                    member->spell_points.current > 0 &&
                    snapshot.party.selected_member == spellbook->member &&
                    realmz::presentation::
                        legacy_key_message_for_open_spellbook(context)
                        .has_value();
              }
              if (std::holds_alternative<
                      realmz::presentation::OpenSaveGameAction>(
                      control.payload)) {
                return control.kind == realmz::presentation::
                        ShellControlKind::open_save_game &&
                    snapshot.screen == context.screen &&
                    realmz::presentation::
                        legacy_menu_command_for_open_save_game(context)
                        .has_value();
              }
              if (std::holds_alternative<
                      realmz::presentation::OpenLoadGameAction>(
                      control.payload)) {
                return control.kind == realmz::presentation::
                        ShellControlKind::open_load_game &&
                    snapshot.screen == context.screen &&
                    realmz::presentation::
                        legacy_menu_command_for_open_load_game(context)
                        .has_value();
              }
              if (const auto* guard =
                      std::get_if<
                          realmz::presentation::GuardCombatantAction>(
                          &control.payload)) {
                if (control.kind != realmz::presentation::
                        ShellControlKind::guard_combatant ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant != guard->combatant ||
                    !realmz::presentation::
                        legacy_key_message_for_guard_combatant(
                            guard->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    guard->combatant,
                    &realmz::presentation::CombatantView::id);
                return combatant != snapshot.combat->combatants.end() &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* finish =
                      std::get_if<
                          realmz::presentation::FinishCombatantAction>(
                          &control.payload)) {
                if (control.kind != realmz::presentation::
                        ShellControlKind::finish_combatant ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant != finish->combatant ||
                    !realmz::presentation::
                        legacy_key_message_for_finish_combatant(
                            finish->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    finish->combatant,
                    &realmz::presentation::CombatantView::id);
                return combatant != snapshot.combat->combatants.end() &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* delay =
                      std::get_if<
                          realmz::presentation::DelayCombatantAction>(
                          &control.payload)) {
                if (control.kind != realmz::presentation::
                        ShellControlKind::delay_combatant ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant != delay->combatant ||
                    !realmz::presentation::
                        legacy_key_message_for_delay_combatant(
                            delay->combatant, context) ||
                    (delay->combatant < 0) || (delay->combatant > 0xFF)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    delay->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        delay->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0 && member &&
                    member->movement == member->movement_maximum;
              }
              if (const auto* center =
                      std::get_if<realmz::presentation::
                          CenterActiveCombatantAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        center_active_combatant ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant != center->combatant ||
                    !realmz::presentation::
                        legacy_key_message_for_center_active_combatant(
                            center->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    center->combatant,
                    &realmz::presentation::CombatantView::id);
                return combatant != snapshot.combat->combatants.end() &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* switch_weapon =
                      std::get_if<realmz::presentation::
                          SwitchWeaponSetAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        switch_weapon_set ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant !=
                        switch_weapon->combatant ||
                    (switch_weapon->combatant < 0) ||
                    (switch_weapon->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_switch_weapon(
                            switch_weapon->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    switch_weapon->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        switch_weapon->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* cycle_focus =
                      std::get_if<realmz::presentation::
                          CycleCombatFocusAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        cycle_combat_focus ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant !=
                        cycle_focus->combatant ||
                    (cycle_focus->combatant < 0) ||
                    (cycle_focus->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_cycle_combat_focus(
                            cycle_focus->combatant,
                            cycle_focus->direction,
                            context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    cycle_focus->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        cycle_focus->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* combat_items =
                      std::get_if<realmz::presentation::
                          OpenCombatItemsAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        open_combat_items ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant !=
                        combat_items->combatant ||
                    (combat_items->combatant < 0) ||
                    (combat_items->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_open_combat_items(
                            combat_items->combatant,
                            combat_items->member,
                            context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    combat_items->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* actor_member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        combat_items->combatant));
                const auto* selected_member =
                    snapshot.party.member(combat_items->member);
                return combatant != snapshot.combat->combatants.end() &&
                    actor_member && selected_member &&
                    selected_member->selected &&
                    snapshot.party.selected_member == combat_items->member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* auto_combatant =
                      std::get_if<realmz::presentation::
                          AutoCombatantAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        auto_combatant ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant !=
                        auto_combatant->combatant ||
                    (auto_combatant->combatant < 0) ||
                    (auto_combatant->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_auto_combatant(
                            auto_combatant->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    auto_combatant->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        auto_combatant->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* show_combat_range =
                      std::get_if<realmz::presentation::
                          ShowCombatRangeAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        show_combat_range ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant !=
                        show_combat_range->combatant ||
                    (show_combat_range->combatant < 0) ||
                    (show_combat_range->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_show_combat_range(
                            show_combat_range->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    show_combat_range->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        show_combat_range->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* bandage_combatant =
                      std::get_if<realmz::presentation::
                          BandageCombatantAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        bandage_combatant ||
                    !snapshot.combat || !snapshot.combat->active ||
                    !snapshot.combat->bandage_available ||
                    snapshot.combat->acting_combatant !=
                        bandage_combatant->combatant ||
                    (bandage_combatant->combatant < 0) ||
                    (bandage_combatant->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_bandage_combatant(
                            bandage_combatant->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    bandage_combatant->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        bandage_combatant->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* undo_combatant =
                      std::get_if<realmz::presentation::
                          UndoCombatantAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        undo_combatant ||
                    !snapshot.combat || !snapshot.combat->active ||
                    !snapshot.combat->undo_available ||
                    snapshot.combat->acting_combatant !=
                        undo_combatant->combatant ||
                    (undo_combatant->combatant < 0) ||
                    (undo_combatant->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_undo_combatant(
                            undo_combatant->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    undo_combatant->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        undo_combatant->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* open_combat_spellbook =
                      std::get_if<realmz::presentation::
                          OpenCombatSpellbookAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        open_combat_spellbook ||
                    !snapshot.combat || !snapshot.combat->active ||
                    !snapshot.combat->cast_spell_available ||
                    snapshot.combat->acting_combatant !=
                        open_combat_spellbook->combatant ||
                    (open_combat_spellbook->combatant < 0) ||
                    (open_combat_spellbook->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_open_combat_spellbook(
                            open_combat_spellbook->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    open_combat_spellbook->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        open_combat_spellbook->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* open_combat_targeting =
                      std::get_if<realmz::presentation::
                          OpenCombatTargetingAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        open_combat_targeting ||
                    !snapshot.combat || !snapshot.combat->active ||
                    !snapshot.combat->target_available ||
                    snapshot.combat->acting_combatant !=
                        open_combat_targeting->combatant ||
                    (open_combat_targeting->combatant < 0) ||
                    (open_combat_targeting->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_open_combat_targeting(
                            open_combat_targeting->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    open_combat_targeting->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        open_combat_targeting->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* escape_combat =
                      std::get_if<realmz::presentation::EscapeCombatAction>(
                          &control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        escape_combat ||
                    !snapshot.combat || !snapshot.combat->active ||
                    snapshot.combat->acting_combatant !=
                        escape_combat->combatant ||
                    (escape_combat->combatant < 0) ||
                    (escape_combat->combatant > 0xFF) ||
                    !realmz::presentation::legacy_key_message_for_escape_combat(
                        escape_combat->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    escape_combat->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        escape_combat->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (const auto* open_combat_scroll_case =
                      std::get_if<realmz::presentation::
                          OpenCombatScrollCaseAction>(&control.payload)) {
                if (control.kind != realmz::presentation::ShellControlKind::
                        open_combat_scroll_case ||
                    !snapshot.combat || !snapshot.combat->active ||
                    !snapshot.combat->use_scroll_available ||
                    snapshot.combat->acting_combatant !=
                        open_combat_scroll_case->combatant ||
                    (open_combat_scroll_case->combatant < 0) ||
                    (open_combat_scroll_case->combatant > 0xFF) ||
                    !realmz::presentation::
                        legacy_key_message_for_open_combat_scroll_case(
                            open_combat_scroll_case->combatant, context)) {
                  return false;
                }
                const auto combatant = std::ranges::find(
                    snapshot.combat->combatants,
                    open_combat_scroll_case->combatant,
                    &realmz::presentation::CombatantView::id);
                const auto* member = snapshot.party.member(
                    static_cast<realmz::presentation::PartyMemberId>(
                        open_combat_scroll_case->combatant));
                return combatant != snapshot.combat->combatants.end() &&
                    member &&
                    combatant->kind ==
                        realmz::presentation::CombatantKind::party_member &&
                    combatant->active && combatant->targetable &&
                    combatant->stamina.current > 0;
              }
              if (std::holds_alternative<
                      realmz::presentation::SetDrawerPanelAction>(
                      control.payload)) {
                return control.kind ==
                    realmz::presentation::ShellControlKind::drawer_tab;
              }
              return false;
            });
        shell_keyboard_route_enabled =
            snapshot_context_matches && !this->text_editing_active &&
            context.adaptive_eligible && every_enabled_control_is_live &&
            std::ranges::any_of(
                this->remastered_shell_controls,
                [](const auto& control) { return control.enabled; });
      }
    } catch (const std::exception& e) {
      this->remastered_shell_controls.clear();
      static bool warned = false;
      if (!warned) {
        wm_log.warning_f(
            "Could not populate Remastered shell information: {}", e.what());
        warned = true;
      }
    }
  }
  (void)this->remastered_shell_keyboard.reconcile(
      this->remastered_shell_controls, shell_keyboard_route_enabled);

  std::optional<realmz::presentation::ShellRegionId> focused_control;
  if (const auto& focused =
          this->remastered_shell_keyboard.focused_identifier()) {
    const auto control = std::ranges::find_if(
        this->remastered_shell_controls,
        [&focused](const auto& candidate) {
          return candidate.enabled &&
              (candidate.focus_identifier == *focused);
        });
    if (control != this->remastered_shell_controls.end()) {
      focused_control = control->region;
    }
  }
  std::optional<realmz::presentation::ShellRegionId> pressed_control =
      this->remastered_pressed_shell_control
      ? std::optional<realmz::presentation::ShellRegionId>{
            this->remastered_pressed_shell_control->region}
      : std::nullopt;
  if (const auto pressed_identifier =
          this->remastered_shell_keyboard.pressed_identifier()) {
    const auto control = std::ranges::find_if(
        this->remastered_shell_controls,
        [pressed_identifier](const auto& candidate) {
          return candidate.enabled &&
              (candidate.focus_identifier == *pressed_identifier);
        });
    if (control != this->remastered_shell_controls.end()) {
      pressed_control = control->region;
    }
  }

  auto texture = this->create_classic_frame_texture(renderer);
  if (texture) {
    // AdaptiveShell's classic-frame command requires nearest sampling. The
    // Classic renderer still honors the user's filter preference separately.
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);
  }
  for (const auto& command : this->adaptive_shell_plan->commands) {
    if (const auto* clear =
            std::get_if<realmz::presentation::ClearShellCommand>(&command)) {
      (void)clear;
      SDL_SetRenderDrawColor(renderer, 12, 13, 17, 255);
      SDL_RenderClear(renderer);
    } else if (const auto* blit =
                   std::get_if<realmz::presentation::BlitClassicFrameCommand>(
                       &command)) {
      if (texture) {
        const auto source = sdl_rect(blit->source);
        const auto destination = sdl_rect(blit->destination);
        SDL_RenderTexture(renderer, texture.get(), &source, &destination);
        SDL_SetRenderDrawColor(renderer, 144, 112, 65, 255);
        SDL_RenderRect(renderer, &destination);
      }
    } else if (const auto* panel =
                   std::get_if<realmz::presentation::DrawShellPanelCommand>(
                       &command)) {
      const auto destination = sdl_rect(panel->destination);
      const auto color = shell_panel_color(panel->panel);
      SDL_SetRenderDrawColor(
          renderer, color.red, color.green, color.blue, 255);
      SDL_RenderFillRect(renderer, &destination);
      SDL_SetRenderDrawColor(renderer, 91, 76, 55, 255);
      SDL_RenderRect(renderer, &destination);
      if (shell_model && shell_font) {
        draw_shell_panel_contents(
            renderer,
            shell_font,
            panel->panel,
            panel->destination,
            *shell_model,
            this->remastered_shell_controls,
            pressed_control,
            focused_control,
            backing_scale);
      }
    }
  }

  std::vector<realmz::presentation::ShellHitRegion> shell_regions;
  shell_regions.reserve(
      this->remastered_shell_controls.size() +
      this->adaptive_shell_plan->commands.size());
  for (const auto& control : this->remastered_shell_controls) {
    if (control.enabled) {
      shell_regions.emplace_back(realmz::presentation::ShellHitRegion{
          .id = control.region,
          .bounds = control.bounds,
      });
    }
  }
  for (const auto& command : this->adaptive_shell_plan->commands) {
    if (const auto* panel =
            std::get_if<realmz::presentation::DrawShellPanelCommand>(
                &command)) {
      shell_regions.emplace_back(realmz::presentation::ShellHitRegion{
          .id = {static_cast<uint32_t>(panel->panel) + 1U},
          .bounds = panel->destination,
      });
    }
  }
  const auto& interaction =
      this->adaptive_shell_plan->classic_interaction_regions.front();
  this->remastered_input_mapper.emplace(
      this->adaptive_shell_plan->window,
      realmz::presentation::ClassicFramePlacement{
          .destination = interaction.window_rect,
          .source_crop = interaction.classic_rect,
      },
      shell_regions,
      realmz::presentation::ShellRegionId{100U},
      this->adaptive_shell_plan->backing_scale);

  SDL_RenderPresent(renderer);
  SDL_SyncWindow(this->sdl_window.get());
  this->update_text_input_area();
}

bool WindowManager::map_remastered_pointer_motion(
    float x, float y, Point* classic_point) {
  if (!this->remastered_input_mapper) {
    return false;
  }
  const realmz::presentation::LogicalPoint point{x, y};
  const auto target = this->remastered_pointer_capture
      ? this->remastered_input_mapper->map_captured_window_point(
            point, *this->remastered_pointer_capture)
      : this->remastered_input_mapper->map_window_point(point);
  return classic_point_for_target(target, classic_point);
}

bool WindowManager::begin_remastered_pointer(
    float x, float y, Point* classic_point) {
  if (!this->remastered_input_mapper) {
    (void)this->remastered_shell_keyboard.clear_focus();
    return false;
  }
  this->remastered_pressed_shell_control.reset();
  bool keyboard_visual_changed = false;
  const realmz::presentation::LogicalPoint point{x, y};
  this->remastered_pointer_capture =
      this->remastered_input_mapper->begin_pointer_capture(point);
  if (!this->remastered_pointer_capture) {
    return false;
  }
  const auto target =
      this->remastered_input_mapper->map_captured_window_point(
          point, *this->remastered_pointer_capture);
  if (std::holds_alternative<realmz::presentation::LegacyPointerCapture>(
          *this->remastered_pointer_capture)) {
    keyboard_visual_changed =
        this->remastered_shell_keyboard.clear_focus();
    SDL_CaptureMouse(true);
  } else if (const auto* shell_target =
                 std::get_if<realmz::presentation::ShellPointerTarget>(
                     &target)) {
    const auto control = std::ranges::find_if(
        this->remastered_shell_controls,
        [shell_target, point](const auto& candidate) {
          return candidate.enabled &&
              (candidate.region == shell_target->region) &&
              candidate.bounds.contains(point);
        });
    if (control != this->remastered_shell_controls.end()) {
      keyboard_visual_changed =
          this->remastered_shell_keyboard.clear_focus();
      keyboard_visual_changed =
          this->remastered_shell_keyboard.focus_control(
              control->focus_identifier,
              this->remastered_shell_controls,
              this->remastered_shell_keyboard_route_is_eligible()) ||
          keyboard_visual_changed;
      // Copy the complete down-time descriptor. Recomposites may replace the
      // live model before release, but a captured press must never resolve its
      // numeric ID against a new control list.
      this->remastered_pressed_shell_control = *control;
      SDL_CaptureMouse(true);
      this->recomposite_all();
    } else {
      keyboard_visual_changed =
          this->remastered_shell_keyboard.clear_focus();
    }
  } else {
    keyboard_visual_changed =
        this->remastered_shell_keyboard.clear_focus();
  }
  if (keyboard_visual_changed && !this->remastered_pressed_shell_control) {
    this->recomposite_all();
  }
  return classic_point_for_target(target, classic_point);
}

bool WindowManager::end_remastered_pointer(
    float x, float y, Point* classic_point) {
  if (!this->remastered_input_mapper) {
    this->cancel_remastered_pointer_capture();
    return false;
  }
  const realmz::presentation::LogicalPoint point{x, y};
  const auto target = this->remastered_pointer_capture
      ? this->remastered_input_mapper->map_captured_window_point(
            point, *this->remastered_pointer_capture)
      : this->remastered_input_mapper->map_window_point(point);
  const bool is_legacy = classic_point_for_target(target, classic_point);
  const auto pressed_control = this->remastered_pressed_shell_control;
  const auto* shell_target =
      std::get_if<realmz::presentation::ShellPointerTarget>(&target);
  const bool invoke_shell_control = pressed_control && shell_target &&
      (pressed_control->region == shell_target->region) &&
      pressed_control->bounds.contains(point);
  this->cancel_remastered_pointer_capture();
  if (invoke_shell_control) {
    this->dispatch_remastered_shell_control(*pressed_control);
    this->recomposite_all();
  }
  return is_legacy;
}

void WindowManager::cancel_remastered_pointer_capture() {
  if (this->remastered_pointer_capture && this->sdl_window) {
    SDL_CaptureMouse(false);
  }
  this->remastered_pointer_capture.reset();
  this->remastered_pressed_shell_control.reset();
}

bool WindowManager::remastered_shell_keyboard_route_is_eligible() const {
  const auto semantic_surface = RealmzCurrentSemanticInputSurface();
  if ((this->presentation_host.mode() !=
          realmz::presentation::PresentationMode::remastered) ||
      this->text_editing_active || !this->adaptive_shell_plan ||
      !this->adaptive_shell_plan->adaptive_layout ||
      !this->remastered_input_mapper ||
      this->remastered_shell_controls.empty()) {
    return false;
  }
  const auto route = this->adaptive_shell_plan->route;
  if ((route != realmz::presentation::AdaptiveShellRoute::
                    remastered_compatibility_shell) &&
      (route != realmz::presentation::AdaptiveShellRoute::
                    remastered_adaptive)) {
    return false;
  }

  const auto context = capture_runtime_legacy_command_context();
  const bool surface_matches_context =
      ((semantic_surface == REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
          (context.screen ==
              realmz::presentation::ScreenContext::exploration)) ||
      ((semantic_surface == REALMZ_SEMANTIC_INPUT_DUNGEON) &&
          (context.screen == realmz::presentation::ScreenContext::dungeon)) ||
      ((semantic_surface == REALMZ_SEMANTIC_INPUT_COMBAT) &&
          (context.screen == realmz::presentation::ScreenContext::combat));
  if (!context.adaptive_eligible ||
      (context.screen != this->adaptive_shell_plan->screen)) {
    return false;
  }
  bool found_enabled = false;
  std::optional<realmz::presentation::GameSnapshot> snapshot;
  for (const auto& control : this->remastered_shell_controls) {
    if (!control.enabled) {
      continue;
    }
    found_enabled = true;
    if (const auto* page =
            std::get_if<realmz::presentation::SetCombatActionPageAction>(
                &control.payload)) {
      const bool valid_transition = realmz::presentation::
          is_valid_combat_action_page_transition(
              this->remastered_combat_action_page, page->page);
      const auto& layout = *this->adaptive_shell_plan->adaptive_layout;
      if (!surface_matches_context ||
          context.screen != realmz::presentation::ScreenContext::combat ||
          control.kind != realmz::presentation::ShellControlKind::
              combat_action_page ||
          !layout.action_bar.contains(control.bounds) ||
          !valid_transition) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active) {
        return false;
      }
      const auto acting_combatant = snapshot->combat->acting_combatant;
      if (!acting_combatant || (*acting_combatant < 0) ||
          (*acting_combatant > 0xFF) ||
          !snapshot->party.member(
              static_cast<realmz::presentation::PartyMemberId>(
                  *acting_combatant))) {
        return false;
      }
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          *acting_combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* drawer =
            std::get_if<realmz::presentation::SetDrawerPanelAction>(
                &control.payload)) {
      const bool valid_requested_panel = !drawer->panel ||
          *drawer->panel == realmz::presentation::DrawerPanel::details ||
          *drawer->panel == realmz::presentation::DrawerPanel::event_log;
      const auto& layout = *this->adaptive_shell_plan->adaptive_layout;
      if (control.kind !=
              realmz::presentation::ShellControlKind::drawer_tab ||
          layout.layout_class != realmz::presentation::LayoutClass::compact ||
          !layout.drawer_tabs ||
          !layout.drawer_tabs->contains(control.bounds) ||
          !valid_requested_panel) {
        return false;
      }
      continue;
    }
    if (const auto* movement =
            std::get_if<realmz::presentation::MovePartyAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::movement ||
          !realmz::presentation::legacy_key_message_for_movement(
              movement->command, context)) {
        return false;
      }
      continue;
    }
    if (const auto* selection =
            std::get_if<realmz::presentation::SelectPartyMemberAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind !=
              realmz::presentation::ShellControlKind::party_member) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) ||
          !snapshot->party.member(selection->member)) {
        return false;
      }
      continue;
    }
    if (const auto* inventory =
            std::get_if<realmz::presentation::OpenInventoryAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind !=
              realmz::presentation::ShellControlKind::open_inventory ||
          !realmz::presentation::legacy_key_message_for_open_inventory(
              context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      const auto* member = snapshot->party.member(inventory->member);
      if ((snapshot->screen != context.screen) || !member ||
          !member->selected ||
          snapshot->party.selected_member != inventory->member) {
        return false;
      }
      continue;
    }
    if (const auto* spellbook =
            std::get_if<realmz::presentation::OpenSpellbookAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind !=
              realmz::presentation::ShellControlKind::open_spellbook ||
          !realmz::presentation::legacy_key_message_for_open_spellbook(
              context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      const auto* member = snapshot->party.member(spellbook->member);
      if ((snapshot->screen != context.screen) || !member ||
          !member->selected || !member->conscious ||
          (member->spell_points.current <= 0) ||
          snapshot->party.selected_member != spellbook->member) {
        return false;
      }
      continue;
    }
    if (std::holds_alternative<
            realmz::presentation::OpenSaveGameAction>(control.payload)) {
      if (!surface_matches_context ||
          control.kind !=
              realmz::presentation::ShellControlKind::open_save_game ||
          !realmz::presentation::legacy_menu_command_for_open_save_game(
              context)) {
        return false;
      }
      continue;
    }
    if (std::holds_alternative<
            realmz::presentation::OpenLoadGameAction>(control.payload)) {
      if (!surface_matches_context ||
          control.kind !=
              realmz::presentation::ShellControlKind::open_load_game ||
          !realmz::presentation::legacy_menu_command_for_open_load_game(
              context)) {
        return false;
      }
      continue;
    }
    if (const auto* guard =
            std::get_if<realmz::presentation::GuardCombatantAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind !=
              realmz::presentation::ShellControlKind::guard_combatant ||
          !realmz::presentation::legacy_key_message_for_guard_combatant(
              guard->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant != guard->combatant) {
        return false;
      }
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          guard->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* finish =
            std::get_if<realmz::presentation::FinishCombatantAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind !=
              realmz::presentation::ShellControlKind::finish_combatant ||
          !realmz::presentation::legacy_key_message_for_finish_combatant(
              finish->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant != finish->combatant) {
        return false;
      }
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          finish->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* delay =
            std::get_if<realmz::presentation::DelayCombatantAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind !=
              realmz::presentation::ShellControlKind::delay_combatant ||
          !realmz::presentation::legacy_key_message_for_delay_combatant(
              delay->combatant, context) ||
          (delay->combatant < 0) || (delay->combatant > 0xFF)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant != delay->combatant) {
        return false;
      }
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          delay->combatant,
          &realmz::presentation::CombatantView::id);
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              delay->combatant));
      if ((combatant == snapshot->combat->combatants.end()) ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0) || !member ||
          (member->movement != member->movement_maximum)) {
        return false;
      }
      continue;
    }
    if (const auto* center =
            std::get_if<realmz::presentation::CenterActiveCombatantAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::
              ShellControlKind::center_active_combatant ||
          !realmz::presentation::legacy_key_message_for_center_active_combatant(
              center->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant != center->combatant) {
        return false;
      }
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          center->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* switch_weapon =
            std::get_if<realmz::presentation::SwitchWeaponSetAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              switch_weapon_set ||
          (switch_weapon->combatant < 0) ||
          (switch_weapon->combatant > 0xFF) ||
          !realmz::presentation::legacy_key_message_for_switch_weapon(
              switch_weapon->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant !=
              switch_weapon->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              switch_weapon->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          switch_weapon->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) ||
          !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* cycle_focus =
            std::get_if<realmz::presentation::CycleCombatFocusAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              cycle_combat_focus ||
          (cycle_focus->combatant < 0) ||
          (cycle_focus->combatant > 0xFF) ||
          !realmz::presentation::legacy_key_message_for_cycle_combat_focus(
              cycle_focus->combatant,
              cycle_focus->direction,
              context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant !=
              cycle_focus->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              cycle_focus->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          cycle_focus->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) || !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* combat_items =
            std::get_if<realmz::presentation::OpenCombatItemsAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              open_combat_items ||
          (combat_items->combatant < 0) ||
          (combat_items->combatant > 0xFF) ||
          !realmz::presentation::legacy_key_message_for_open_combat_items(
              combat_items->combatant,
              combat_items->member,
              context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant !=
              combat_items->combatant ||
          snapshot->party.selected_member != combat_items->member) {
        return false;
      }
      const auto* actor_member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              combat_items->combatant));
      const auto* selected_member =
          snapshot->party.member(combat_items->member);
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          combat_items->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) ||
          !actor_member || !selected_member || !selected_member->selected ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* auto_combatant =
            std::get_if<realmz::presentation::AutoCombatantAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              auto_combatant ||
          (auto_combatant->combatant < 0) ||
          (auto_combatant->combatant > 0xFF) ||
          !realmz::presentation::legacy_key_message_for_auto_combatant(
              auto_combatant->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant !=
              auto_combatant->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              auto_combatant->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          auto_combatant->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) || !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* show_combat_range =
            std::get_if<realmz::presentation::ShowCombatRangeAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              show_combat_range ||
          (show_combat_range->combatant < 0) ||
          (show_combat_range->combatant > 0xFF) ||
          !realmz::presentation::legacy_key_message_for_show_combat_range(
              show_combat_range->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant !=
              show_combat_range->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              show_combat_range->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          show_combat_range->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) || !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* bandage_combatant =
            std::get_if<realmz::presentation::BandageCombatantAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              bandage_combatant ||
          (bandage_combatant->combatant < 0) ||
          (bandage_combatant->combatant > 0xFF) ||
          !realmz::presentation::legacy_key_message_for_bandage_combatant(
              bandage_combatant->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          !snapshot->combat->bandage_available ||
          snapshot->combat->acting_combatant !=
              bandage_combatant->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              bandage_combatant->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          bandage_combatant->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) || !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* undo_combatant =
            std::get_if<realmz::presentation::UndoCombatantAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              undo_combatant ||
          (undo_combatant->combatant < 0) ||
          (undo_combatant->combatant > 0xFF) ||
          !realmz::presentation::legacy_key_message_for_undo_combatant(
              undo_combatant->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active || !snapshot->combat->undo_available ||
          snapshot->combat->acting_combatant != undo_combatant->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              undo_combatant->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          undo_combatant->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) || !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* open_combat_spellbook =
            std::get_if<realmz::presentation::OpenCombatSpellbookAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              open_combat_spellbook ||
          (open_combat_spellbook->combatant < 0) ||
          (open_combat_spellbook->combatant > 0xFF) ||
          !realmz::presentation::
              legacy_key_message_for_open_combat_spellbook(
                  open_combat_spellbook->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          !snapshot->combat->cast_spell_available ||
          snapshot->combat->acting_combatant !=
              open_combat_spellbook->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              open_combat_spellbook->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          open_combat_spellbook->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) || !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* open_combat_targeting =
            std::get_if<realmz::presentation::OpenCombatTargetingAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              open_combat_targeting ||
          (open_combat_targeting->combatant < 0) ||
          (open_combat_targeting->combatant > 0xFF) ||
          !realmz::presentation::
              legacy_key_message_for_open_combat_targeting(
                  open_combat_targeting->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          !snapshot->combat->target_available ||
          snapshot->combat->acting_combatant !=
              open_combat_targeting->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              open_combat_targeting->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          open_combat_targeting->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) || !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* escape_combat =
            std::get_if<realmz::presentation::EscapeCombatAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              escape_combat ||
          (escape_combat->combatant < 0) ||
          (escape_combat->combatant > 0xFF) ||
          !realmz::presentation::legacy_key_message_for_escape_combat(
              escape_combat->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          snapshot->combat->acting_combatant != escape_combat->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              escape_combat->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          escape_combat->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) || !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    if (const auto* open_combat_scroll_case =
            std::get_if<realmz::presentation::OpenCombatScrollCaseAction>(
                &control.payload)) {
      if (!surface_matches_context ||
          control.kind != realmz::presentation::ShellControlKind::
              open_combat_scroll_case ||
          (open_combat_scroll_case->combatant < 0) ||
          (open_combat_scroll_case->combatant > 0xFF) ||
          !realmz::presentation::
              legacy_key_message_for_open_combat_scroll_case(
                  open_combat_scroll_case->combatant, context)) {
        return false;
      }
      try {
        if (!snapshot) {
          snapshot =
              realmz::presentation::LegacyGameSnapshotSource().capture();
        }
      } catch (...) {
        return false;
      }
      if ((snapshot->screen != context.screen) || !snapshot->combat ||
          !snapshot->combat->active ||
          !snapshot->combat->use_scroll_available ||
          snapshot->combat->acting_combatant !=
              open_combat_scroll_case->combatant) {
        return false;
      }
      const auto* member = snapshot->party.member(
          static_cast<realmz::presentation::PartyMemberId>(
              open_combat_scroll_case->combatant));
      const auto combatant = std::ranges::find(
          snapshot->combat->combatants,
          open_combat_scroll_case->combatant,
          &realmz::presentation::CombatantView::id);
      if ((combatant == snapshot->combat->combatants.end()) || !member ||
          (combatant->kind !=
              realmz::presentation::CombatantKind::party_member) ||
          !combatant->active || !combatant->targetable ||
          (combatant->stamina.current <= 0)) {
        return false;
      }
      continue;
    }
    return false;
  }
  return found_enabled;
}

bool WindowManager::handle_remastered_shell_key(
    const SDL_KeyboardEvent& event) {
  using realmz::presentation::ShellKeyboardEvent;
  using realmz::presentation::ShellKeyboardKey;
  using realmz::presentation::ShellKeyboardPhase;
  using realmz::presentation::ShellPhysicalKeyToken;

  const ShellPhysicalKeyToken token{
      .keyboard = static_cast<uint32_t>(event.which),
      .scancode = static_cast<uint32_t>(event.scancode),
  };
  auto key = shell_keyboard_key_for_sdl(event.key);
  const bool already_owned =
      this->remastered_shell_keyboard.owns_token(token);
  if (!key && !already_owned) {
    return false;
  }
  if (!key) {
    // The physical token is authoritative for release ownership. Keycode can
    // change with keyboard layout/modifier state between down and up.
    key = ShellKeyboardKey::tab;
  }

  constexpr SDL_Keymod kBlockedModifiers = static_cast<SDL_Keymod>(
      SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI);
  if (!already_owned && ((event.mod & kBlockedModifiers) != 0)) {
    return false;
  }

  const auto result = this->remastered_shell_keyboard.handle(
      ShellKeyboardEvent{
          .token = token,
          .key = *key,
          .phase = event.type == SDL_EVENT_KEY_UP
              ? ShellKeyboardPhase::up
              : ShellKeyboardPhase::down,
          .shift = (event.mod & SDL_KMOD_SHIFT) != 0,
          .repeat = event.repeat,
      },
      this->remastered_shell_controls,
      this->remastered_shell_keyboard_route_is_eligible());
  if (result.invoked_control) {
    this->dispatch_remastered_shell_control(*result.invoked_control);
  }
  if (result.visual_state_changed || result.invoked_control) {
    this->recomposite_all();
  }
  return result.consumed;
}

void WindowManager::cancel_remastered_keyboard_route() {
  this->remastered_shell_keyboard.cancel_route();
}

void WindowManager::dispatch_remastered_shell_control(
    const realmz::presentation::ShellControlPlacement& control) {
  if (!control.enabled ||
      (this->presentation_host.mode() !=
          realmz::presentation::PresentationMode::remastered)) {
    return;
  }

  const auto* drawer =
      std::get_if<realmz::presentation::SetDrawerPanelAction>(
          &control.payload);
  const auto* combat_page =
      std::get_if<realmz::presentation::SetCombatActionPageAction>(
          &control.payload);
  const auto* switch_weapon =
      std::get_if<realmz::presentation::SwitchWeaponSetAction>(
          &control.payload);
  const auto* cycle_focus =
      std::get_if<realmz::presentation::CycleCombatFocusAction>(
          &control.payload);
  const auto* combat_items =
      std::get_if<realmz::presentation::OpenCombatItemsAction>(
          &control.payload);
  const auto* auto_combatant =
      std::get_if<realmz::presentation::AutoCombatantAction>(
          &control.payload);
  const auto* show_combat_range =
      std::get_if<realmz::presentation::ShowCombatRangeAction>(
          &control.payload);
  const auto* bandage_combatant =
      std::get_if<realmz::presentation::BandageCombatantAction>(
          &control.payload);
  const auto* undo_combatant =
      std::get_if<realmz::presentation::UndoCombatantAction>(
          &control.payload);
  const auto* open_combat_spellbook =
      std::get_if<realmz::presentation::OpenCombatSpellbookAction>(
          &control.payload);
  const auto* open_combat_targeting =
      std::get_if<realmz::presentation::OpenCombatTargetingAction>(
          &control.payload);
  const auto* escape_combat =
      std::get_if<realmz::presentation::EscapeCombatAction>(
          &control.payload);
  const auto* open_combat_scroll_case =
      std::get_if<realmz::presentation::OpenCombatScrollCaseAction>(
          &control.payload);
  if (drawer) {
    const bool valid_requested_panel = !drawer->panel ||
        *drawer->panel == realmz::presentation::DrawerPanel::details ||
        *drawer->panel == realmz::presentation::DrawerPanel::event_log;
    const auto live_control = std::ranges::find_if(
        this->remastered_shell_controls,
        [&control](const auto& candidate) {
          return candidate.enabled &&
              candidate.focus_identifier == control.focus_identifier;
        });
    if (!this->remastered_shell_keyboard_route_is_eligible() ||
        live_control == this->remastered_shell_controls.end() ||
        *live_control != control ||
        control.kind != realmz::presentation::ShellControlKind::drawer_tab ||
        !this->adaptive_shell_plan ||
        !this->adaptive_shell_plan->adaptive_layout ||
        this->adaptive_shell_plan->adaptive_layout->layout_class !=
            realmz::presentation::LayoutClass::compact ||
        !this->adaptive_shell_plan->adaptive_layout->drawer_tabs ||
        !this->adaptive_shell_plan->adaptive_layout->drawer_tabs->contains(
            control.bounds) ||
        !valid_requested_panel) {
      return;
    }
  } else if (combat_page) {
    const bool valid_transition = realmz::presentation::
        is_valid_combat_action_page_transition(
            this->remastered_combat_action_page, combat_page->page);
    const auto live_control = std::ranges::find_if(
        this->remastered_shell_controls,
        [&control](const auto& candidate) {
          return candidate.enabled && candidate == control;
        });
    if (!this->remastered_shell_keyboard_route_is_eligible() ||
        live_control == this->remastered_shell_controls.end() ||
        control.kind != realmz::presentation::ShellControlKind::
            combat_action_page ||
        !this->adaptive_shell_plan ||
        !this->adaptive_shell_plan->adaptive_layout ||
        this->adaptive_shell_plan->screen !=
            realmz::presentation::ScreenContext::combat ||
        !this->adaptive_shell_plan->adaptive_layout->action_bar.contains(
            control.bounds) ||
        !valid_transition) {
      return;
    }
  } else {
    const auto live_control = std::ranges::find_if(
        this->remastered_shell_controls,
        [&control](const auto& candidate) {
          return candidate.enabled && candidate == control;
        });
    if (!this->remastered_shell_keyboard_route_is_eligible() ||
        live_control == this->remastered_shell_controls.end() ||
        !this->runtime_legacy_command_bridge ||
        RealmzCurrentSemanticInputSurface() ==
            REALMZ_SEMANTIC_INPUT_NONE ||
        ((switch_weapon || cycle_focus || combat_items) &&
            ((switch_weapon &&
                 control.kind != realmz::presentation::ShellControlKind::
                     switch_weapon_set) ||
                (cycle_focus &&
                    control.kind != realmz::presentation::ShellControlKind::
                        cycle_combat_focus) ||
                (combat_items &&
                    control.kind != realmz::presentation::ShellControlKind::
                        open_combat_items) ||
                this->remastered_combat_action_page !=
                    realmz::presentation::CombatActionPage::secondary ||
                !this->adaptive_shell_plan ||
                !this->adaptive_shell_plan->adaptive_layout ||
                this->adaptive_shell_plan->screen !=
                    realmz::presentation::ScreenContext::combat ||
                !this->adaptive_shell_plan->adaptive_layout->action_bar
                     .contains(control.bounds))) ||
        ((auto_combatant || show_combat_range || bandage_combatant ||
             undo_combatant) &&
            ((auto_combatant &&
                 control.kind != realmz::presentation::ShellControlKind::
                     auto_combatant) ||
                (show_combat_range &&
                    control.kind != realmz::presentation::ShellControlKind::
                        show_combat_range) ||
                (bandage_combatant &&
                    control.kind != realmz::presentation::ShellControlKind::
                        bandage_combatant) ||
                (undo_combatant &&
                    control.kind != realmz::presentation::ShellControlKind::
                        undo_combatant) ||
                this->remastered_combat_action_page !=
                    realmz::presentation::CombatActionPage::utility ||
                !this->adaptive_shell_plan ||
                !this->adaptive_shell_plan->adaptive_layout ||
                this->adaptive_shell_plan->screen !=
                    realmz::presentation::ScreenContext::combat ||
                !this->adaptive_shell_plan->adaptive_layout->action_bar
                     .contains(control.bounds))) ||
        ((open_combat_spellbook || open_combat_targeting || escape_combat ||
             open_combat_scroll_case) &&
            ((open_combat_spellbook &&
                 control.kind != realmz::presentation::ShellControlKind::
                     open_combat_spellbook) ||
                (open_combat_targeting &&
                    control.kind != realmz::presentation::ShellControlKind::
                        open_combat_targeting) ||
                (escape_combat &&
                    control.kind != realmz::presentation::ShellControlKind::
                        escape_combat) ||
                (open_combat_scroll_case &&
                    control.kind != realmz::presentation::ShellControlKind::
                        open_combat_scroll_case) ||
                this->remastered_combat_action_page !=
                    realmz::presentation::CombatActionPage::special ||
                !this->adaptive_shell_plan ||
                !this->adaptive_shell_plan->adaptive_layout ||
                this->adaptive_shell_plan->screen !=
                    realmz::presentation::ScreenContext::combat ||
                !this->adaptive_shell_plan->adaptive_layout->action_bar
                     .contains(control.bounds)))) {
      return;
    }
  }

  const realmz::presentation::UIAction action{
      .sequence = this->next_shell_action_sequence,
      .payload = control.payload,
  };
  ++this->next_shell_action_sequence;
  if (this->next_shell_action_sequence == 0) {
    this->next_shell_action_sequence = 1;
  }
  if (drawer) {
    this->remastered_active_drawer = drawer->panel;
    return;
  }
  if (combat_page) {
    this->remastered_combat_action_page = combat_page->page;
    return;
  }
  const auto result = this->runtime_legacy_command_bridge->dispatch(action);
  if (!result.was_handled()) {
    wm_log.warning_f(
        "Remastered shell action {} was not dispatched: {}",
        realmz::presentation::action_name(action.payload),
        result.detail);
  }
}

bool WindowManager::classic_to_render_point(float* x, float* y) const {
  if (!x || !y) {
    return false;
  }
  if (this->presentation_host.mode() !=
      realmz::presentation::PresentationMode::remastered) {
    return true;
  }
  if (!this->remastered_input_mapper) {
    return false;
  }
  const auto mapped =
      this->remastered_input_mapper->classic_to_window_point({*x, *y});
  if (!mapped) {
    return false;
  }
  *x = static_cast<float>(mapped->x);
  *y = static_cast<float>(mapped->y);
  return true;
}

bool WindowManager::classic_to_render_rect(
    float* x, float* y, float* w, float* h) const {
  if (!x || !y || !w || !h || (*w < 0.0f) || (*h < 0.0f)) {
    return false;
  }
  if (this->presentation_host.mode() !=
      realmz::presentation::PresentationMode::remastered) {
    return true;
  }
  if (!this->remastered_input_mapper) {
    return false;
  }
  const auto mapped = this->remastered_input_mapper->classic_to_window_rect({
      *x,
      *y,
      *w,
      *h,
  });
  if (!mapped) {
    return false;
  }
  *x = static_cast<float>(mapped->x);
  *y = static_cast<float>(mapped->y);
  *w = static_cast<float>(mapped->width);
  *h = static_cast<float>(mapped->height);
  return true;
}

bool WindowManager::set_enable_recomposite(bool enable) {
  if (enable == this->recomposite_enabled) {
    return enable;
  }
  this->recomposite_enabled = enable;
  if (this->recomposite_enabled) {
    this->recomposite_all();
  }
  return !enable;
}

WindowManager& WindowManager::instance() {
  static std::unique_ptr<WindowManager> wm;
  if (!wm) {
    wm.reset(new WindowManager());
  }
  return *wm;
}

void WindowManager::recomposite_from_window(CCGrafPort& updated_port) {
  if (updated_port.is_window) {
    this->recomposite(this->window_for_port(&updated_port));
  }
}
void WindowManager::recomposite_from_window(std::shared_ptr<Window> updated_window) {
  this->recomposite(updated_window);
}
void WindowManager::recomposite_all() {
  this->recomposite(nullptr);
}

void WindowManager::set_scale_mode(SDL_ScaleMode mode) {
  if (mode == this->scale_mode) {
    return;
  }
  this->scale_mode = mode;
  this->recomposite_all();
  this->save_prefs();
}

void WindowManager::configure_window_for_presentation_mode() {
  if (!this->sdl_window) {
    return;
  }
  const bool remastered = this->presentation_host.mode() ==
      realmz::presentation::PresentationMode::remastered;
  SDL_SetWindowMinimumSize(
      this->sdl_window.get(),
      remastered ? REMASTER_MINIMUM_WIDTH : kLogicalWindowWidth,
      remastered ? REMASTER_MINIMUM_HEIGHT : kLogicalWindowHeight);

  if (remastered) {
#ifdef __APPLE__
    MacResetWindowAspect(this->sdl_window.get());
#else
    SDL_SetWindowAspectRatio(this->sdl_window.get(), 0.0f, 0.0f);
#endif
    if (!this->is_fullscreen()) {
      int width = 0;
      int height = 0;
      SDL_GetWindowSize(this->sdl_window.get(), &width, &height);
      const int clamped_width = std::max(width, REMASTER_MINIMUM_WIDTH);
      const int clamped_height = std::max(height, REMASTER_MINIMUM_HEIGHT);
      if ((clamped_width != width) || (clamped_height != height)) {
        SDL_SetWindowSize(
            this->sdl_window.get(), clamped_width, clamped_height);
      }
    }
  } else if (!this->is_fullscreen()) {
    if (this->aspect_locked) {
      int width = 0;
      int height = 0;
      SDL_GetWindowSize(this->sdl_window.get(), &width, &height);
      const int snapped_height =
          (width * kLogicalWindowHeight + kLogicalWindowWidth / 2) /
          kLogicalWindowWidth;
      if (snapped_height != height) {
        SDL_SetWindowSize(this->sdl_window.get(), width, snapped_height);
      }
      SDL_SetWindowAspectRatio(
          this->sdl_window.get(), kLogicalAspect, kLogicalAspect);
    } else {
#ifdef __APPLE__
      MacResetWindowAspect(this->sdl_window.get());
#else
      SDL_SetWindowAspectRatio(this->sdl_window.get(), 0.0f, 0.0f);
#endif
    }
  }

  if (auto* renderer = SDL_GetRenderer(this->sdl_window.get())) {
    if (remastered) {
      int width = 0;
      int height = 0;
      SDL_GetWindowSize(this->sdl_window.get(), &width, &height);
      SDL_SetRenderLogicalPresentation(renderer, width, height,
          SDL_LOGICAL_PRESENTATION_LETTERBOX);
    } else {
      SDL_SetRenderLogicalPresentation(renderer, kLogicalWindowWidth,
          kLogicalWindowHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
  }
}

void WindowManager::set_aspect_locked(bool locked) {
  this->aspect_locked = locked;
  if (this->sdl_window &&
      this->presentation_host.mode() ==
          realmz::presentation::PresentationMode::classic) {
    if (locked) {
      int w = 0, h = 0;
      SDL_GetWindowSize(this->sdl_window.get(), &w, &h);
      int snapped_h = (w * kLogicalWindowHeight + kLogicalWindowWidth / 2) / kLogicalWindowWidth;
      if (snapped_h != h && !this->is_fullscreen()) {
        SDL_SetWindowSize(this->sdl_window.get(), w, snapped_h);
      }
      if (!this->is_fullscreen()) {
        SDL_SetWindowAspectRatio(this->sdl_window.get(), kLogicalAspect, kLogicalAspect);
      }
    } else {
#ifdef __APPLE__
      MacResetWindowAspect(this->sdl_window.get());
#else
      SDL_SetWindowAspectRatio(this->sdl_window.get(), 0.0f, 0.0f);
#endif
    }
  }
  this->save_prefs();
}

void WindowManager::set_window_size(int w, int h) {
  if (!this->sdl_window) {
    return;
  }
  if (this->presentation_host.mode() ==
      realmz::presentation::PresentationMode::remastered) {
    w = std::max(w, REMASTER_MINIMUM_WIDTH);
    h = std::max(h, REMASTER_MINIMUM_HEIGHT);
  }
  SDL_SetWindowSize(this->sdl_window.get(), w, h);
  this->recomposite_all();
  this->save_prefs();
}

bool WindowManager::size_fits(int w, int h) const {
  if (!this->sdl_window) {
    return false;
  }
  if ((this->presentation_host.mode() ==
          realmz::presentation::PresentationMode::remastered) &&
      ((w < REMASTER_MINIMUM_WIDTH) || (h < REMASTER_MINIMUM_HEIGHT))) {
    return false;
  }
  SDL_Rect usable;
  SDL_DisplayID display = SDL_GetDisplayForWindow(this->sdl_window.get());
  if (!SDL_GetDisplayUsableBounds(display, &usable)) {
    return true;
  }
  return (w <= usable.w) && (h <= usable.h);
}

void WindowManager::get_window_size(int* w, int* h) const {
  if (this->sdl_window) {
    SDL_GetWindowSize(this->sdl_window.get(), w, h);
  } else {
    *w = *h = 0;
  }
}

bool WindowManager::is_fullscreen() const {
  if (!this->sdl_window) {
    return false;
  }
  return (SDL_GetWindowFlags(this->sdl_window.get()) & SDL_WINDOW_FULLSCREEN) != 0;
}

void WindowManager::note_window_moved() {
  if (this->sdl_window && !this->is_fullscreen()) {
    SDL_GetWindowPosition(this->sdl_window.get(), &this->windowed_x, &this->windowed_y);
  }
}

void WindowManager::save_prefs() {
  PortPrefs prefs;
  prefs.scale_mode = this->scale_mode;
  prefs.aspect_locked = this->aspect_locked;
  prefs.gamma_idx = this->gamma_idx;
  prefs.presentation_mode = this->presentation_host.mode();
  if (this->sdl_window && !this->is_fullscreen()) {
    SDL_GetWindowSize(this->sdl_window.get(), &this->windowed_w, &this->windowed_h);
  }
  prefs.window_w = this->windowed_w;
  prefs.window_h = this->windowed_h;
  prefs.window_x = this->windowed_x;
  prefs.window_y = this->windowed_y;
  save_port_prefs(prefs);
}

void WindowManager::set_gamma_idx(int idx) {
  if (idx < 0 || idx >= kPortGammaCount || idx == this->gamma_idx) {
    return;
  }
  this->gamma_idx = idx;
  this->recomposite_all();
  this->save_prefs();
}

void WindowManager::set_presentation_mode(
    realmz::presentation::PresentationMode mode) {
  if (mode == this->presentation_host.mode()) {
    return;
  }
  // A mode switch can arrive while a legacy control is held. Clear both the
  // shell capture token and EventManager's Classic button-down modifier so
  // Button()/StillDown() cannot remain stuck after the coordinate route changes.
  CancelSemanticGameplayInput();
  reset_mouse_state();
  this->remastered_shell_keyboard.cancel_route();
  this->remastered_combat_action_page =
      realmz::presentation::CombatActionPage::primary;
  this->presentation_host.set_mode(mode);
  realmz::remaster::assets::setResourcePresentationMode(mode);
  RealmzRefreshPresentationAssets();
  this->configure_window_for_presentation_mode();
  this->recomposite_all();
  this->save_prefs();
}

extern "C" void WM_SavePrefs(void) {
  WindowManager::instance().save_prefs();
}

void WindowManager::on_debug_signal() {
  this->print_window_stack();
  enable_translucent_window_debug = !enable_translucent_window_debug;
  this->recomposite_all();
}

void WindowManager::print_window_stack() const {
  Point mouse_loc;
  GetMouseGlobal(&mouse_loc);

  wm_log.debug_f("Window list: top={} bottom={}",
      this->top_window ? this->top_window->ref() : "(null)",
      this->bottom_window ? this->bottom_window->ref() : "(null)");
  wm_log.debug_f("Window stack (top window first; items under cursor shown first):");
  for (auto w = this->top_window; w; w = w->window_below) {
    wm_log.debug_f("  {} port={} {{x1={}, y1={}, x2={}, y2={}}} \"{}\" visible={} dialog={} ({} dialog items) above={} below={}",
        w->ref(), w->port.ref(),
        w->port.portRect.left, w->port.portRect.top, w->port.portRect.right, w->port.portRect.bottom,
        w->title, w->visible ? "true" : "false", w->is_dialog_flag ? "true" : "false", w->dialog_items.size(),
        w->window_above ? w->window_above->ref() : "(null)", w->window_below ? w->window_below->ref() : "(null)");
    if (PtInRect(mouse_loc, &w->port.portRect)) {
      auto local_mouse_loc = w->get_port().to_local_space(mouse_loc);
      for (auto item : w->dialog_items) {
        if (PtInRect(local_mouse_loc, &item->rect)) {
          wm_log.debug_f("    {}", item->str());
        }
      }
    }
  }
}

void WindowManager::verify_window_stack() const {
  if (!this->top_window && !this->bottom_window) {
    return; // Window stack is empty; nothing to check
  }
  if (this->top_window && !this->bottom_window) {
    throw std::logic_error("There is a top window but no bottom window");
  }
  if (!this->top_window && this->bottom_window) {
    throw std::logic_error("There is a bottom window but no top window");
  }
  if (this->top_window->window_above) {
    throw std::logic_error("Top window has a window above it");
  }
  if (this->bottom_window->window_below) {
    throw std::logic_error("Bottom window has a window below it");
  }
  for (auto w = this->top_window; w; w = w->window_below) {
    if (w->window_below) {
      if (w->window_below->window_above != w) {
        throw std::logic_error("Incorrect backlink in window stack (top->bottom)");
      }
    } else if (this->bottom_window != w) {
      throw std::logic_error("Bottom window reference is incorrect");
    }
  }
  for (auto w = this->bottom_window; w; w = w->window_above) {
    if (w->window_above) {
      if (w->window_above->window_below != w) {
        throw std::logic_error("Incorrect backlink in window stack (bottom->top)");
      }
    } else if (this->top_window != w) {
      throw std::logic_error("Top window reference is incorrect");
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Classic Mac OS API

static void SDL_snprintfcat(SDL_OUT_Z_CAP(maxlen) char* text, size_t maxlen, SDL_PRINTF_FORMAT_STRING const char* fmt, ...) {
  size_t length = SDL_strlen(text);
  va_list ap;

  va_start(ap, fmt);
  text += length;
  maxlen -= length;
  (void)SDL_vsnprintf(text, maxlen, fmt, ap);
  va_end(ap);
}

static void PrintDebugInfo(void) {
  int i, n;
  char text[1024];

  SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
  n = SDL_GetNumVideoDrivers();
  if (n == 0) {
    SDL_Log("No built-in video drivers\n");
  } else {
    (void)SDL_snprintf(text, sizeof(text), "Built-in video drivers:");
    for (i = 0; i < n; ++i) {
      if (i > 0) {
        SDL_snprintfcat(text, sizeof(text), ",");
      }
      SDL_snprintfcat(text, sizeof(text), " %s", SDL_GetVideoDriver(i));
    }
    SDL_Log("%s\n", text);
  }

  SDL_Log("Video driver: %s\n", SDL_GetCurrentVideoDriver());

  n = SDL_GetNumRenderDrivers();
  if (n == 0) {
    SDL_Log("No built-in render drivers\n");
  } else {
    SDL_snprintf(text, sizeof(text), "Built-in render drivers:\n");
    for (i = 0; i < n; ++i) {
      SDL_snprintfcat(text, sizeof(text), "  %s\n", SDL_GetRenderDriver(i));
    }
    SDL_Log("%s\n", text);
  }

  SDL_DisplayID dispID = SDL_GetPrimaryDisplay();
  if (dispID == 0) {
    SDL_Log("No primary display found\n");
  } else {
    SDL_snprintf(text, sizeof(text), "Primary display info:\n");
    SDL_snprintfcat(text, sizeof(text), "  Name:\t\t\t%s\n", SDL_GetDisplayName(dispID));
    const SDL_DisplayMode* dispMode = SDL_GetCurrentDisplayMode(dispID);
    SDL_snprintfcat(text, sizeof(text), "  Pixel Format:\t\t%x\n", dispMode->format);
    SDL_snprintfcat(text, sizeof(text), "  Width:\t\t%d\n", dispMode->w);
    SDL_snprintfcat(text, sizeof(text), "  Height:\t\t%d\n", dispMode->h);
    SDL_snprintfcat(text, sizeof(text), "  Pixel Density:\t%f\n", dispMode->pixel_density);
    SDL_snprintfcat(text, sizeof(text), "  Refresh Rate:\t\t%f\n", dispMode->refresh_rate);
    SDL_Log("%s\n", text);
  }
}

void WindowManager_Init(void) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    wm_log.error_f("Couldn't initialize video driver: {}", SDL_GetError());
    return;
  }

  WindowManager::instance().create_sdl_window();

  PrintDebugInfo();

  TTF_Init();

  init_fonts();
}

WindowPtr WindowManager_CreateNewWindow(int16_t res_id, bool is_dialog, WindowPtr behind) {
  Rect bounds;
  int16_t proc_id;
  std::string title;
  bool visible;
  bool go_away;
  uint32_t ref_con;
  size_t num_dialog_items;
  std::vector<std::shared_ptr<DialogItem>> dialog_items;

  if (is_dialog) {
    auto data_handle = GetResource(ResourceDASM::RESOURCE_TYPE_DLOG, res_id);
    if (!data_handle) {
      throw std::runtime_error(std::format("DLOG resource {} not found", res_id));
    }
    auto dlog = ResourceDASM::ResourceFile::decode_DLOG(*data_handle, GetHandleSize(data_handle));
    bounds.left = dlog.bounds.x1;
    bounds.right = dlog.bounds.x2;
    bounds.top = dlog.bounds.y1;
    bounds.bottom = dlog.bounds.y2;
    proc_id = dlog.proc_id;
    title = dlog.title;
    visible = dlog.visible;
    go_away = dlog.go_away;
    ref_con = dlog.ref_con;
    dialog_items = DialogItem::from_DITL(dlog.items_id);

  } else {
    auto data_handle = GetResource(ResourceDASM::RESOURCE_TYPE_WIND, res_id);
    if (!data_handle) {
      throw std::runtime_error(std::format("WIND resource {} not found", res_id));
    }
    auto wind = ResourceDASM::ResourceFile::decode_WIND(*data_handle, GetHandleSize(data_handle));
    bounds.left = wind.bounds.x1;
    bounds.right = wind.bounds.x2;
    bounds.top = wind.bounds.y1;
    bounds.bottom = wind.bounds.y2;
    proc_id = wind.proc_id;
    title = wind.title;
    visible = wind.visible;
    go_away = wind.go_away;
    ref_con = wind.ref_con;
  }

  // If there's a corresponding dctb or wctb, use it to initialize the port's background color.
  // The other color-table fields describe Toolbox frame chrome; WindowManager draws that separately.
  RGBColor background_color = {0xFFFF, 0xFFFF, 0xFFFF};
  try {
    auto clut_data = GetResource(is_dialog ? ResourceDASM::RESOURCE_TYPE_dctb : ResourceDASM::RESOURCE_TYPE_wctb, res_id);
    if (clut_data) {
      auto clut = ResourceDASM::ResourceFile::decode_dctb(*clut_data, GetHandleSize(clut_data));
      background_color.red = clut.at(0).c.r;
      background_color.green = clut.at(0).c.g;
      background_color.blue = clut.at(0).c.b;
    }
  } catch (const std::out_of_range&) {
  }

  return WindowManager::instance().create_window(
      title,
      bounds,
      visible,
      go_away,
      proc_id,
      ref_con,
      is_dialog,
      background_color,
      std::move(dialog_items));
}

void WindowManager_DrawDialog(WindowPtr theWindow) {
  WindowManager::instance().window_for_port(theWindow)->erase_and_render();
}

void WindowManager_DisposeWindow(WindowPtr theWindow) {
  if (theWindow == nullptr) {
    return;
  }
  WindowManager::instance().destroy_window(theWindow);
}

void GetDialogItem(DialogPtr dialog, short item_id, short* item_type, Handle* item_handle, Rect* box) {
  auto window = WindowManager::instance().window_for_port(reinterpret_cast<WindowPtr>(dialog));
  auto& items = window->get_dialog_items();

  try {
    auto item = items.at(item_id - 1);
    // Realmz doesn't use the handle directly; it only passes the handle to other
    // Classic Mac OS API functions. So, we can just return the DialogItem
    // opaque handle instead
    *item_type = macos_dialog_item_type_for_resource_dasm_type(item->type);
    *item_handle = wrap_opaque_handle(item->opaque_handle);
    *box = item->rect;
  } catch (const std::out_of_range&) {
    wm_log.warning_f("GetDialogItem called with invalid item_id {} (there are only {} items)", item_id, items.size());
  }
}

void GetDialogItemText(DialogItemHandle item_handle, Str255 text) {
  size_t handle = unwrap_opaque_handle(item_handle);
  auto item = DialogItem::get_item_by_handle(handle);
  pstr_for_string<256>(text, item->get_text());
}

void SetDialogItemText(DialogItemHandle item_handle, ConstStr255Param text) {
  size_t handle = unwrap_opaque_handle(item_handle);
  auto item = DialogItem::get_item_by_handle(handle);
  auto str = string_for_pstr<256>(text);
  wm_log.debug_f("SetDialogItemText({}, \"{}\")", item->str(), str);

  bool was_empty = item->get_text().empty();
  item->set_text(str);

  auto window = item->owner_window.lock();
  if (window) {
    if (!was_empty) {
      window->get_port().erase_rect(item->rect);
    }
    item->render_in_port(window->get_port(), true);
    WindowManager::instance().recomposite_from_window(window);
  } else {
    wm_log.warning_f("SetDialogItemText({}, \"{}\") cannot re-render window because owner_window is not set", item->str(), str);
  }
}

int16_t StringWidth(ConstStr255Param s) {
  return s[0];
}

Boolean IsDialogEvent(const EventRecord* ev) {
  try {
    auto* port = CCGrafPort::as_port(ev->window_port);
    return WindowManager::instance().window_for_port(port)->is_dialog();
  } catch (const std::out_of_range&) {
    return false;
  }
}

Boolean DialogSelect(const EventRecord* ev, DialogPtr* dialog, short* item_hit) {
  // Inside Macintosh: Toolbox Essentials describes the behavior as such:
  // (from https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-428.html):
  // 1. In response to an activate or update event for the dialog box,
  //    DialogSelect activates or updates its window and returns FALSE.
  // 2. If a key-down event or an auto-key event occurs and there's an editable
  //    text item in the dialog box, DialogSelect uses TextEdit to handle text
  //    entry and editing, and DialogSelect returns TRUE for a function result.
  //    In its itemHit parameter, DialogSelect returns the item number.
  // 3. If a key-down event or an auto-key event occurs and there's no editable
  //    text item in the dialog box, DialogSelect returns FALSE.
  // 4. If the user presses the mouse button while the cursor is in an editable
  //    text item, DialogSelect responds to the mouse activity as appropriate;
  //    that is, either by displaying an insertion point or by selecting text.
  //    If the editable text item is disabled, DialogSelect returns FALSE. If
  //    the editable text item is enabled, DialogSelect returns TRUE and in its
  //    itemHit parameter returns the item number. Normally, editable text
  //    items are disabled, and you use the GetDialogItemText function to read
  //    the information in the items only after the OK button is clicked.
  // 5. If the user presses the mouse button while the cursor is in a control,
  //    DialogSelect calls the Control Manager function TrackControl. If the
  //    user releases the mouse button while the cursor is in an enabled
  //    control, DialogSelect returns TRUE for a function result and in its
  //    itemHit parameter returns the control's item number. Your application
  //    should respond appropriately--for example, by performing a command
  //    after the user clicks the OK button.
  // 6. If the user presses the mouse button while the cursor is in any other
  //    enabled item in the dialog box, DialogSelect returns TRUE for a
  //    function result and in its itemHit parameter returns the item's number.
  //    Generally, only controls should be enabled. If your application creates
  //    a complex control, such as one that measures how far a dial is moved,
  //    your application must handle mouse events in that item before passing
  //    the event to DialogSelect.
  // 7. If the user presses the mouse button while the cursor is in a disabled
  //    item, or if it is in no item, or if any other event occurs,
  //    DialogSelect does nothing.
  // 8. If the event isn't one that DialogSelect specifically checks for (if
  //    it's a null event, for example), and if there's an editable text item
  //    in the dialog box, DialogSelect calls the TextEdit procedure TEIdle to
  //    make the insertion point blink.

  // The above is a lot of logic! Fortunately, we don't have to implement some
  // of it. (1) is not necessary because SDL handles activeness and updates,
  // and we hide all that from Realmz. (2) is not implemented yet but will
  // likely also be handled by SDL, so for key-down events we can just always
  // return false, which takes care of (3). We may have to implement (4) to
  // activate SDL edit controls when the user clicks them (TODO). We may also
  // have to implement (5) later on. (6) is implemented; Realmz uses it for a
  // lot of interactions. (7) requires no action, and (8) is handled when the
  // event queue is idle.

  auto window = WindowManager::instance().window_for_port(CCGrafPort::as_port(ev->window_port));

  // Before any of the expected logic, we implement a debugging feature: the
  // backslash key prints information about the dialog item that the user is
  // hovering over to stderr.
  if ((ev->what == keyDown) && ((ev->message & 0xFF) == static_cast<uint8_t>('\\'))) {
    try {
      const auto& items = window->get_dialog_items();
      fprintf(stderr, "Dialog items at (%hu, %hu):\n", ev->where.h, ev->where.v);
      for (size_t z = 0; z < items.size(); z++) {
        const auto item = items.at(z);
        if (PtInRect(ev->where, &item->rect)) {
          auto s = item->str();
          std::string processed_text_str = phosg::format_data_string(replace_param_text(item->get_text()));
          fprintf(stderr, "%s (processed_text=%s)\n", s.c_str(), processed_text_str.c_str());
        }
      }
    } catch (const std::out_of_range&) {
    }
  }

  // Backspace
  if (ev->what == keyDown && (mac_vk_from_message(ev->message) == MAC_VK_BACKSPACE)) {
    auto item = window->get_focused_item();
    if (!item) {
      return false;
    }
    window->delete_char(item);
  }

  // Handle cases (2) and (3) above. These would normally be emitted as keyDown events, but SDL distinguishes
  // key downs that are part of text input as distinct event types. See EventManager::enqueue_sdl_event().
  if (ev->what == app4Evt) {
    try {
      // Text input always happens in the currently focused item
      auto item = window->get_focused_item();

      // Case (3)
      if (!item) {
        return false;
      } else {
        // Here is where the Classic OS would intercept key down events that took place in an editable
        // text field and delegate processing to TextEdit. Since SDL provides dedicated event types for
        // text editing, we can do the same.
        window->handle_text_input(ev->text, item);
      }
    } catch (const std::out_of_range&) {
    }
  }

  // Handle case (6) described above
  if (ev->what == mouseDown) {
    try {
      auto window_space_pt = window->get_port().to_local_space(ev->where);
      auto item = window->dialog_item_for_position(window_space_pt, true);
      if (item) {
        *item_hit = item->item_id;

        // Currently, only editable text fields can be focused on, for text input
        if (item->type == DialogItemType::EDIT_TEXT) {
          window->set_focused_item(item);
          WindowManager::instance().on_dialog_item_focus_changed();
        }

        return true;
      }
    } catch (const std::out_of_range&) {
    }
  }
  return false;
}

void SystemClick(const EventRecord* theEvent, WindowPtr theWindow) {
  // This is used for handling events in windows belonging to the system, other
  // applications, or desk accessories. On modern systems we never see these
  // events, so we can just do nothing here.
}

void MoveWindow(WindowPtr theWindow, uint16_t hGlobal, uint16_t vGlobal, Boolean front) {
  if (theWindow == nullptr) {
    return;
  }

  auto window = WindowManager::instance().window_for_port(theWindow);
  window->move(hGlobal, vGlobal);
}

void ShowWindow(WindowPtr theWindow) {
  if (theWindow == nullptr) {
    return;
  }

  auto window = WindowManager::instance().window_for_port(theWindow);
  window->show();
}

void SizeWindow(CWindowPtr theWindow, uint16_t w, uint16_t h, Boolean fUpdate) {
  auto window = WindowManager::instance().window_for_port(theWindow);
  window->resize(w, h);
}

DialogPtr GetNewDialog(uint16_t res_id, void* dStorage, WindowPtr behind) {
  return WindowManager_CreateNewWindow(res_id, true, behind);
}

CWindowPtr GetNewCWindow(int16_t res_id, void* wStorage, WindowPtr behind) {
  return WindowManager_CreateNewWindow(res_id, false, behind);
}

void DisposeDialog(DialogPtr theDialog) {
  WindowManager_DisposeWindow(theDialog);
}

void DrawDialog(DialogPtr theDialog) {
  WindowManager_DrawDialog(theDialog);
}

void NumToString(int32_t num, Str255 str) {
  str[0] = snprintf(reinterpret_cast<char*>(&str[1]), 0xFF, "%" PRId32, num);
}

void StringToNum(ConstStr255Param str, int32_t* num) {
  // Inside Macintosh I-490:
  //   StringToNum doesn't actually check whether the characters in the string
  //   are between '0' and '9'; instead, since the ASCII codes for '0' through
  //   '9' are $30 through $39, it just masks off the last four bits and uses
  //   them as a digit. For example, '2:' is converted to the number 30 because
  //   the ASCII code for':' is $3A. Spaces are treated as zeroes, since the
  //   ASCII code for a space is $20.
  // We implement the same behavior here.
  *num = 0;
  if (str[0] > 0) {
    bool negative = (str[1] == '-');
    size_t offset = negative ? 1 : 0;
    for (size_t z = 0; z < static_cast<uint8_t>(str[0]); z++) {
      *num = ((*num) * 10) + (str[z + 1] & 0x0F);
    }
    if (negative) {
      *num = -(*num);
    }
  }
}

void ModalDialog(ModalFilterProcPtr filterProc, short* itemHit) {
  // Retrieve the current window to only process events within that window
  CGrafPtr port = qd.thePort;

  // Skip all events until we get one that's within the current window
  // (condition 1) and not handled as part of the dialog abstraction
  // (conditions 2 & 3). DialogSelect will fill in `item`, which we then return
  EventRecord e;
  DialogPtr dialog;
  short item;
  do {
    WaitNextEvent(everyEvent, &e, 1, NULL);
  } while (e.window_port != port || !IsDialogEvent(&e) || !DialogSelect(&e, &dialog, &item));

  *itemHit = item;
}

ControlHandle GetNewControl(int16_t cntl_id, WindowPtr window) {
  auto w = WindowManager::instance().window_for_port(window);
  auto control = Control::from_CNTL(cntl_id);
  auto item = DialogItem::from_control(control);
  w->add_dialog_item(item);
  return wrap_opaque_handle(item->opaque_handle);
}

ControlHandle NewControl(
    WindowPtr window,
    const Rect* bounds,
    ConstStr255Param title,
    Boolean visible,
    short value,
    short min,
    short max,
    short proc_id,
    int32_t ref_con) {
  auto w = WindowManager::instance().window_for_port(window);
  auto title_str = string_for_pstr<256>(title);
  auto control = Control::from_params(*bounds, value, min, max, proc_id, visible, title_str);
  auto item = DialogItem::from_control(control);
  w->add_dialog_item(item);
  return wrap_opaque_handle(item->opaque_handle);
}

static void render_window_for_item(std::shared_ptr<DialogItem> item) {
  auto window = item->owner_window.lock();
  if (window) {
    item->render_in_port(window->get_port(), false);
    WindowManager::instance().recomposite_from_window(window);
  }
}

static void set_control_visible(ControlHandle handle, bool visible) {
  wm_log.debug_f("set_control_visible({}, {})", reinterpret_cast<void*>(handle), visible);
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  item->set_control_visible(visible);
  render_window_for_item(item);
}

void ShowControl(ControlHandle handle) {
  set_control_visible(handle, true);
}
void HideControl(ControlHandle handle) {
  set_control_visible(handle, false);
}

void GetControlBounds(ControlHandle handle, Rect* rect) {
  if (!handle) {
    return;
  }
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  if (item->control) {
    *rect = item->control->bounds;
  } else {
    // This probably actually resulted in undefined behavior on original MacOS
    rect->left = 0;
    rect->top = 0;
    rect->right = 0;
    rect->bottom = 0;
  }
}

void MoveControl(ControlHandle handle, short h, short v) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  wm_log.debug_f("MoveControl({}, {}, {})", item->str(), h, v);
  item->move_control(h, v);
  render_window_for_item(item);
}

void SizeControl(ControlHandle handle, short w, short h) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  wm_log.debug_f("SizeControl({}, {}, {})", item->str(), w, h);
  item->resize_control(w, h);
  render_window_for_item(item);
}

void DrawControls(WindowPtr port) {
  auto window = WindowManager::instance().window_for_port(port);
  wm_log.debug_f("DrawControls({})", window->ref());
  // TODO: Should we draw all items, or only controls?
  for (const auto& item : window->get_dialog_items()) {
    if (item->control) {
      item->render_in_port(window->get_port(), false);
    }
  }
  WindowManager::instance().recomposite_from_window(window);
}

short FindControl(Point pt, WindowPtr window, ControlHandle* handle) {
  auto w = WindowManager::instance().window_for_port(window);
  for (const auto& item : w->get_dialog_items()) {
    if (item->control && PtInRect(pt, &item->control->bounds) && item->enabled && item->control->visible) {
      *handle = wrap_opaque_handle(item->opaque_handle);
      switch (item->control->type) {
        case ControlType::BUTTON:
        case ControlType::WINDOW_FONT_BUTTON:
          return kControlButtonPart;
        case ControlType::CHECKBOX:
        case ControlType::WINDOW_FONT_CHECKBOX:
          return kControlCheckBoxPart;
        case ControlType::RADIO_BUTTON:
        case ControlType::WINDOW_FONT_RADIO_BUTTON:
          return kControlRadioButtonPart;
        case ControlType::SCROLL_BAR:
          return item->track_control_part(pt);
        case ControlType::POPUP_MENU:
          return kControlMenuPart;
        case ControlType::UNKNOWN:
        default:
          throw std::logic_error("Unknown control type");
      }
    }
  }
  *handle = wrap_opaque_handle(0);
  return 0;
}

short TrackControl(ControlHandle handle, Point pt, ProcPtr action_proc) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));

  if (item->control->type != ControlType::SCROLL_BAR) {
    wm_log.error_f("Tried to call TrackControl of a non-scrollbar control");
    return 0;
  }

  int16_t initial_part = item->track_control_part(pt);

  // The caller passes pt in window-local coordinates (it calls GlobalToLocal
  // before FindControl/TrackControl), and track_control_part / the control rect
  // are local too. GetMouseGlobal, however, returns global coordinates, so
  // capture the global-to-local offset from this initial click and convert
  // every later mouse sample back to local. (Mixing the two added the window
  // origin and made the thumb jump on grab.)
  Point g0;
  GetMouseGlobal(&g0);
  int16_t off_h = static_cast<int16_t>(g0.h - pt.h);
  int16_t off_v = static_cast<int16_t>(g0.v - pt.v);
  auto local_mouse = [&]() -> Point {
    Point g;
    GetMouseGlobal(&g);
    return Point{.v = static_cast<int16_t>(g.v - off_v), .h = static_cast<int16_t>(g.h - off_h)};
  };

  // For the arrow buttons, track the press the way the Mac Control Manager
  // does: keep the button drawn pushed in while the mouse is held, release the
  // highlight if the cursor leaves the button, and return the part only if the
  // mouse comes up over it. (The caller passes a null action proc and scrolls
  // one step on the returned part, so this preserves the one-step-per-click
  // behavior and only adds the visual feedback.) The page regions keep the
  // existing one-shot behavior.
  if (initial_part == kControlUpButtonPart || initial_part == kControlDownButtonPart) {
    int16_t shown = -1;
    while (Button()) {
      int16_t want = (item->track_control_part(local_mouse()) == initial_part) ? initial_part : 0;
      if (want != shown) {
        item->pressed_part = want;
        render_window_for_item(item);
        shown = want;
      }
      SDL_Delay(15);
    }
    if (item->pressed_part != 0) {
      item->pressed_part = 0;
      render_window_for_item(item);
    }
    return (item->track_control_part(local_mouse()) == initial_part) ? initial_part : 0;
  }

  // Thumb (indicator): drag it live. The control value follows the cursor while
  // the mouse is held, with the thumb drawn pushed in. If the caller supplies an
  // action proc it is called each time the value changes so the caller can
  // redraw its contents (for example the shop item list) live during the drag;
  // otherwise the caller redraws once when the drag returns. (Without this the
  // value never changed, so dragging the thumb did nothing.)
  if (initial_part == kControlIndicatorPart) {
    int slider_range = item->get_height() - 3 * item->get_width() + 1;
    int value_range = item->control->max - item->control->min;
    if (slider_range > 0 && value_range > 0) {
      // A real action proc takes the control and the part being tracked. The
      // -1 value is the Toolbox sentinel for "use the control's default proc",
      // which we have nothing to do for, so treat it like none.
      auto live_proc = reinterpret_cast<void (*)(ControlHandle, short)>(action_proc);
      bool have_live_proc = (action_proc != nullptr) && (action_proc != reinterpret_cast<ProcPtr>(-1));

      // Start from the current thumb offset and move it by the cursor delta, so
      // the grab point stays under the cursor with no jump.
      int start_offset = item->get_slider_offset();
      item->pressed_part = kControlIndicatorPart;
      render_window_for_item(item);
      while (Button()) {
        Point cur = local_mouse();
        int off = std::clamp(start_offset + (cur.v - pt.v), 0, slider_range);
        int new_value = item->control->min + static_cast<int>(static_cast<double>(off) / slider_range * value_range + 0.5);
        new_value = std::clamp<int>(new_value, item->control->min, item->control->max);
        if (new_value != item->control->value) {
          item->control->value = static_cast<int16_t>(new_value);
          render_window_for_item(item);
          if (have_live_proc) {
            live_proc(handle, kControlIndicatorPart);
          }
        }
        SDL_Delay(15);
      }
      item->pressed_part = 0;
      render_window_for_item(item);
    }
    return kControlIndicatorPart;
  }

  return initial_part;
}

short GetControlValue(ControlHandle handle) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  return item->control ? item->control->value : 0;
}

short GetControlMinimum(ControlHandle handle) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  return item->control ? item->control->min : 0;
}

short GetControlMaximum(ControlHandle handle) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  return item->control ? item->control->max : 0;
}

void SetControlValue(ControlHandle handle, short value) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  wm_log.debug_f("SetControlValue({}, {})", item->str(), value);
  item->set_control_value(value);
  render_window_for_item(item);
}

void SetControlMinimum(ControlHandle handle, short min) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  wm_log.debug_f("SetControlMinimum({}, {})", item->str(), min);
  item->set_control_minimum(min);
  render_window_for_item(item);
}

void SetControlMaximum(ControlHandle handle, short max) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  wm_log.debug_f("SetControlMaximum({}, {})", item->str(), max);
  item->set_control_maximum(max);
  render_window_for_item(item);
}

void GetControlTitle(ControlHandle handle, Str255 title) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(handle));
  if (item->control) {
    pstr_for_string<256>(title, item->control->title);
  }
}

WindowPtr FrontWindow() {
  auto window = WindowManager::instance().front_window();
  return window ? &window->get_port() : nullptr;
}

int16_t FindWindow(Point p, WindowPtr* wp) {
  auto w = WindowManager::instance().window_for_point(p.h, p.v);
  *wp = w ? &w->get_port() : nullptr;

  if (p.v < 0 && p.h < 0) {
    return inMenuBar;
  } else if (p.v >= 0 && p.h >= 0) {
    return inContent;
  }
  return 0;
}

void BringToFront(WindowPtr w) {
  auto& wm = WindowManager::instance();
  auto window = wm.window_for_port(w);
  wm.bring_to_front(window);
}

void SelectWindow(WindowPtr w) {
  // Apparently SelectWindow moves the focus to the given window and brings it
  // to the front, but does not show it (ShowWindow is still required for
  // that). We don't implement focus in our window manager, so all we have to
  // do here is bring the window to the front.
  BringToFront(w);
}

void DisposeWindow(WindowPtr w) {
  WindowManager_DisposeWindow(w);
}

int WindowManager_SetEnableRecomposite(int enable) {
  return WindowManager::instance().set_enable_recomposite(enable);
}

void WindowManager_RecompositeAlways() {
  auto& wm = WindowManager::instance();
  wm.set_enable_recomposite(wm.set_enable_recomposite(true));
}

void WindowManager_PresentScreen() {
  WindowManager::instance().present_screen();
}

TEHandle TENew(const Rect* destRect, const Rect* viewRect) {
  // We implement *unstyled* TextEdit instances as dialog items. In Classic Mac
  // OS, the reverse was the case: edit control dialog items were implemented
  // using TextEdit. In Realmz's limited use cases, this distinction is not
  // important, since unstyled TextEdit instances are only used for text entry
  // (in the character creation flow and the Speak encounter action).
  auto window = WindowManager::instance().window_for_port(qd.thePort);
  return window->add_text_edit(*destRect, *viewRect);
}

using HorizAlign = ResourceDASM::BitmapFontRenderer::HorizontalAlignment;

struct StyledTextEdit {
  phosg::PrefixedLogger log;
  std::string text;
  Rect layout_rect;
  Rect view_rect;
  HorizAlign align;
  std::unique_ptr<phosg::ImageRGBA8888N> prerendered;

  static std::unordered_set<const StyledTextEdit*> all_instances;

  StyledTextEdit(const Rect& layout, const Rect& view)
      : log(std::format("[STE-{:016X}] ", reinterpret_cast<intptr_t>(this)), wm_log.min_level),
        layout_rect(layout),
        view_rect(view),
        align(HorizAlign::LEFT) {
    StyledTextEdit::all_instances.emplace(this);
    this->log.debug_f("Created with layout={{x0={}, y0={}, x1={}, y1={}}}, view={{x0={}, y0={}, x1={}, y1={}}}",
        this->layout_rect.left, this->layout_rect.top, this->layout_rect.right, this->layout_rect.bottom,
        this->view_rect.left, this->view_rect.top, this->view_rect.right, this->view_rect.bottom);
  }
  ~StyledTextEdit() {
    this->log.debug_f("Destroyed");
    StyledTextEdit::all_instances.erase(this);
  }
  static StyledTextEdit* from_void(void* ptr) {
    auto* ste = reinterpret_cast<StyledTextEdit*>(ptr);
    if (StyledTextEdit::all_instances.count(ste)) {
      return ste;
    } else {
      throw std::runtime_error("Invalid styled TextEdit instance");
    }
  }
  static StyledTextEdit* from_void_if(void* ptr) {
    auto* ste = reinterpret_cast<StyledTextEdit*>(ptr);
    return StyledTextEdit::all_instances.count(ste) ? ste : nullptr;
  }
};

std::unordered_set<const StyledTextEdit*> StyledTextEdit::all_instances;

TEHandle TEStyleNew(const Rect* dest, const Rect* view) {
  // In contrast to unstyled TextEdit instances, styled instances are NOT
  // dialog items. They instead are entirely disconnected from the window
  // subsystem, which is similar to how they were implemented in Classic Mac
  // OS. Despite this, we don't implement styling (yet), but we use the type of
  // the TE instance to determine which implementation to use.
  return reinterpret_cast<TEHandle>(new StyledTextEdit(*dest, *view));
}

void TEDispose(TEHandle te) {
  auto* styled = StyledTextEdit::from_void_if(te);
  if (styled) {
    delete styled;
  } else {
    // It must be an unstyled TextEdit instance; Realmz uses this in
    // textbox-time.c to render wrapped text, it seems
    auto di = DialogItem::get_item_by_handle(reinterpret_cast<size_t>(te));
    auto window = di->owner_window.lock();
    if (window) {
      window->remove_text_edit(di);
    }
  }
}

void TESetText(const void* text, int32_t length, TEHandle te) {
  // In Realmz, this is only called for unstyled TextEdit instances
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(reinterpret_cast<Handle>(te)));
  item->set_text(std::string(reinterpret_cast<const char*>(text), length));
}

void TEUpdateUnstyled(const Rect& r, TEHandle te) {
  auto item = DialogItem::get_item_by_handle(unwrap_opaque_handle(reinterpret_cast<Handle>(te)));
  wm_log.debug_f("TEUpdateUnstyled({{x0={}, y0={}, x1={}, y1={}}}, {})", r.left, r.top, r.right, r.bottom, reinterpret_cast<void*>(te));
  auto window = item->owner_window.lock();
  item->render_text_in_port(window->get_port());
  WindowManager::instance().recomposite_from_window(window);
}

void TEUpdateStyled(const Rect& r, StyledTextEdit* ste) {
  ste->log.debug_f("TEUpdateStyled({{x0={}, y0={}, x1={}, y1={}}})", r.left, r.top, r.right, r.bottom);
  if (!ste->prerendered) {
    ste->log.debug_f("Prerendered text is missing; generating it");
    auto font = load_font(1601); // Theldrow
    if (!std::holds_alternative<ResourceDASM::BitmapFontRenderer>(font)) {
      throw std::logic_error("Theldrow is not a bitmap font");
    }
    const auto& bm_font = std::get<ResourceDASM::BitmapFontRenderer>(font);
    ste->prerendered = std::make_unique<phosg::ImageRGBA8888N>(
        bm_font.wrap_and_render_text<phosg::PixelFormat::RGBA8888_NATIVE>(
            ste->text, ste->layout_rect.right - ste->layout_rect.left, 0, 0x000000FF, ste->align));
  }
  auto* port = CCGrafPort::as_port(qd.thePort);
  if (!port) {
    wm_log.warning_f("TEUpdateStyled: current port is missing; cannot draw text");
  } else {
    ste->log.debug_f("Rendering into {} with layout={{x0={}, y0={}, x1={}, y1={}}}, view={{x0={}, y0={}, x1={}, y1={}}}",
        port->ref(), ste->layout_rect.left, ste->layout_rect.top, ste->layout_rect.right, ste->layout_rect.bottom,
        ste->view_rect.left, ste->view_rect.top, ste->view_rect.right, ste->view_rect.bottom);
    port->erase_rect(ste->view_rect);
    port->data.copy_from_with_blend(
        *ste->prerendered,
        ste->view_rect.left,
        ste->view_rect.top,
        ste->view_rect.right - ste->view_rect.left,
        ste->view_rect.bottom - ste->view_rect.top,
        ste->view_rect.left - ste->layout_rect.left,
        ste->view_rect.top - ste->layout_rect.top,
        ste->layout_rect.right - ste->layout_rect.left,
        ste->layout_rect.bottom - ste->layout_rect.top,
        phosg::ResizeMode::NONE); // Clip if out of bounds
  }
}

void TEUpdate(const Rect* r, TEHandle te) {
  auto* ste = StyledTextEdit::from_void_if(te);
  if (ste) {
    TEUpdateStyled(*r, ste);
  } else {
    TEUpdateUnstyled(*r, te);
  }
}

void TEStyleInsert(const void* text_v, int32_t length, StScrpHandle hSt, TEHandle te) {
  // This function should only be used with styled TextEdit instances
  auto* ste = StyledTextEdit::from_void(te);
  const char* text = reinterpret_cast<const char*>(text_v);
  ste->text.clear();
  ste->text.reserve(length);
  for (size_t z = 0; z < length; z++) {
    char ch = text[z];
    ste->text.push_back((ch == '\r') ? '\n' : ch);
  }
  ste->log.debug_f("TEStyleInsert(\"{}\")", ste->text);
  ste->prerendered.reset();
  // TODO: We currently don't implement styled text. It'd be nice to support
  // this in the future, but TTF fonts make this difficult - there aren't
  // functions that make it easy to render heterogenously-styled text in
  // SDL_ttf.
}

void TESetAlignment(int16_t just, TEHandle te) {
  // In Realmz, this is only called for styled TextEdit instances
  auto* ste = StyledTextEdit::from_void(te);
  ste->log.debug_f("TESetAlignment({})", just);
  switch (just) {
    case 0: // System default (left for Roman charsets)
    case -2: // Always left
      ste->align = HorizAlign::LEFT;
      break;
    case 1: // Always center
      ste->align = HorizAlign::CENTER;
      break;
    case -1: // Always right
      ste->align = HorizAlign::RIGHT;
      break;
    default:
      throw std::logic_error("Invalid text alignment mode");
  }
  ste->prerendered.reset();
}

void TEScroll(int16_t dh, int16_t dv, TEHandle te) {
  // In Realmz, this is only called for styled TextEdit instances
  auto* ste = StyledTextEdit::from_void(te);
  ste->layout_rect.left += dh;
  ste->layout_rect.right += dh;
  ste->layout_rect.top += dv;
  ste->layout_rect.bottom += dv;
  ste->log.debug_f("TEScroll(dh={}, dv={}) => layout={{x0={}, y0={}, x1={}, y1={}}}, view={{x0={}, y0={}, x1={}, y1={}}}",
      dh, dv,
      ste->layout_rect.left, ste->layout_rect.top, ste->layout_rect.right, ste->layout_rect.bottom,
      ste->view_rect.left, ste->view_rect.top, ste->view_rect.right, ste->view_rect.bottom);
  // NOTE: TEScroll is supposed to update the text displayed on screen (that
  // is, render the text to the current port), but Realmz always calls TEUpdate
  // shortly after TEScroll, so as an optimization, we don't render here.
}
