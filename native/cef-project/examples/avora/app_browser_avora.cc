// Copyright (c) 2017 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "examples/avora/client_avora.h"
#include "include/cef_image.h"
#include "include/cef_request_context.h"
#if defined(OS_MAC)
#include "examples/avora/sidebar_resize_handle_mac.h"
#endif
#include "examples/shared/app_factory.h"

#include "include/base/cef_callback.h"
#include "include/cef_callback.h"
#include "include/cef_color_ids.h"
#include "include/cef_parser.h"
#include "include/internal/cef_types_wrappers.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace avora {

namespace {

constexpr char kStartupURL[] = "https://www.google.com";
constexpr int kCommandNewTab = 1;
constexpr int kCommandFocusLocation = 2;
constexpr int kCommandCloseOverlay = 3;
constexpr int kCommandCloseTab = 4;
constexpr int kCommandDevTools = 5;
constexpr int kCommandPinTab = 6;
constexpr int kKeyReturn = 13;
constexpr int kKeyEscape = 27;
constexpr int kToolbarNavButtonSize = 36;
constexpr int kToolbarNavRowHeight = 48;
constexpr int kTrafficLightWidth = 86;
constexpr int kSidebarAddressRowHeight = 44;
constexpr int kSpaceNameLabelHeight = 28;
constexpr int kSidebarHorizontalPad = 10;
constexpr int kExtensionIconSize = 24;
constexpr int kExtensionFieldRightPad = 6;
constexpr int kExtPopupWidth = 320;
constexpr int kExtPopupHeight = 260;
constexpr int kOmniboxTextLeftPad = 12;
constexpr int kAddressChipHeight = kSidebarAddressRowHeight - 8;
constexpr int kNavButtonGap = 4;
constexpr int kNavClusterWidth = kToolbarNavButtonSize * 3 + kNavButtonGap * 2;
// Traffic lights + Back/Forward/Reload share this top sidenav band.
constexpr int kSidebarTopInset = kToolbarNavRowHeight;
constexpr int kDefaultSidebarWidth = 236;
constexpr int kMinSidebarWidth = 220;
constexpr int kMaxSidebarWidth = 360;
constexpr int kContentInset = 14;
constexpr int kDevToolsGap = 10;
constexpr int kMinDevToolsSplitWidth = 50;
constexpr int kTabHeight = 42;
constexpr int kPinnedLabelHeight = 42;
constexpr int kPinnedTabHeight = 36;
constexpr int kPinnedTabGap = 4;
constexpr int kPinnedFolderHeaderHeight = 30;
constexpr int kPinnedEmptyHeight = 44;
constexpr int kTabsLabelHeight = 36;
constexpr int kTabGap = 6;
constexpr int kTabTextInset = 10;
constexpr int kChromeElevatedRed = 24;
constexpr int kChromeElevatedGreen = 28;
constexpr int kChromeElevatedBlue = 32;
constexpr int kOmniboxRed = 24;
constexpr int kOmniboxGreen = 28;
constexpr int kOmniboxBlue = 32;
constexpr int kOmniboxTextRed = 237;
constexpr int kOmniboxTextGreen = 242;
constexpr int kOmniboxTextBlue = 245;
constexpr cef_color_t kTabTextColor = CefColorSetARGB(255, 255, 255, 255);
constexpr cef_color_t kTransparent = CefColorSetARGB(0, 0, 0, 0);
constexpr int kWebSurfaceRed = 255;
constexpr int kWebSurfaceGreen = 255;
constexpr int kWebSurfaceBlue = 255;
constexpr int kNavIconDip = 20;
constexpr int kNavIconRed = 232;
constexpr int kNavIconGreen = 236;
constexpr int kNavIconBlue = 240;
constexpr float kPi = 3.14159265358979323846f;

enum class NavIcon { Back, Forward, Reload, Extensions, NewTab };

float Clamp01(float value) {
  return std::max(0.0f, std::min(1.0f, value));
}

void BlendPixel(std::vector<uint8_t>& pixels,
                int width,
                int height,
                int x,
                int y,
                float coverage,
                uint8_t r,
                uint8_t g,
                uint8_t b,
                uint8_t a) {
  if (x < 0 || y < 0 || x >= width || y >= height || coverage <= 0.0f) {
    return;
  }

  const size_t i = static_cast<size_t>((y * width + x) * 4);
  const uint8_t src_a =
      static_cast<uint8_t>(Clamp01(coverage) * static_cast<float>(a) + 0.5f);
  if (src_a <= pixels[i + 3]) {
    return;
  }

  pixels[i] = r;
  pixels[i + 1] = g;
  pixels[i + 2] = b;
  pixels[i + 3] = src_a;
}

void StrokeSegment(std::vector<uint8_t>& pixels,
                   int width,
                   int height,
                   float x0,
                   float y0,
                   float x1,
                   float y1,
                   float stroke_width,
                   uint8_t r,
                   uint8_t g,
                   uint8_t b,
                   uint8_t a) {
  const float half = stroke_width * 0.5f;
  const float pad = half + 1.25f;
  const int min_x = static_cast<int>(std::floor(std::min(x0, x1) - pad));
  const int max_x = static_cast<int>(std::ceil(std::max(x0, x1) + pad));
  const int min_y = static_cast<int>(std::floor(std::min(y0, y1) - pad));
  const int max_y = static_cast<int>(std::ceil(std::max(y0, y1) + pad));
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float length_sq = dx * dx + dy * dy;

  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      float t = 0.0f;
      if (length_sq > 0.0001f) {
        t = Clamp01(((px - x0) * dx + (py - y0) * dy) / length_sq);
      }
      const float qx = x0 + dx * t;
      const float qy = y0 + dy * t;
      const float dist = std::hypot(px - qx, py - qy);
      BlendPixel(pixels, width, height, x, y, Clamp01(half + 0.55f - dist), r,
                 g, b, a);
    }
  }
}

void StrokeArc(std::vector<uint8_t>& pixels,
               int width,
               int height,
               float cx,
               float cy,
               float radius,
               float start_rad,
               float end_rad,
               float stroke_width,
               uint8_t r,
               uint8_t g,
               uint8_t b,
               uint8_t a) {
  const float half = stroke_width * 0.5f;
  const float pad = radius + half + 1.25f;
  const int min_x = static_cast<int>(std::floor(cx - pad));
  const int max_x = static_cast<int>(std::ceil(cx + pad));
  const int min_y = static_cast<int>(std::floor(cy - pad));
  const int max_y = static_cast<int>(std::ceil(cy + pad));
  auto wrap = [](float angle) {
    while (angle < 0.0f) {
      angle += 2.0f * kPi;
    }
    while (angle >= 2.0f * kPi) {
      angle -= 2.0f * kPi;
    }
    return angle;
  };
  const float start = wrap(start_rad);
  const float end = wrap(end_rad);

  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      const float dx = px - cx;
      const float dy = py - cy;
      const float dist = std::hypot(dx, dy);
      if (dist < 0.001f) {
        continue;
      }
      float angle = wrap(std::atan2(dy, dx));
      bool inside_sweep = false;
      if (start <= end) {
        inside_sweep = angle >= start && angle <= end;
      } else {
        inside_sweep = angle >= start || angle <= end;
      }
      if (!inside_sweep) {
        continue;
      }
      BlendPixel(pixels, width, height, x, y,
                 Clamp01(half + 0.55f - std::fabs(dist - radius)), r, g, b, a);
    }
  }
}

std::vector<uint8_t> RasterizeNavIcon(NavIcon icon, int size, uint8_t alpha) {
  std::vector<uint8_t> pixels(static_cast<size_t>(size * size * 4), 0);
  const float s = static_cast<float>(size);
  const float stroke = std::max(1.55f, s * 0.11f);
  const uint8_t r = static_cast<uint8_t>(kNavIconRed);
  const uint8_t g = static_cast<uint8_t>(kNavIconGreen);
  const uint8_t b = static_cast<uint8_t>(kNavIconBlue);

  if (icon == NavIcon::Reload) {
    const float cx = s * 0.50f;
    const float cy = s * 0.52f;
    const float radius = s * 0.28f;
    const float start = 0.55f;
    const float end = 5.05f;
    StrokeArc(pixels, size, size, cx, cy, radius, start, end, stroke, r, g, b,
              alpha);

    const float tip_x = cx + radius * std::cos(end);
    const float tip_y = cy + radius * std::sin(end);
    const float tx = std::sin(end);
    const float ty = -std::cos(end);
    const float head = s * 0.20f;
    const float nx = -ty;
    const float ny = tx;
    StrokeSegment(
        pixels, size, size, tip_x, tip_y, tip_x - tx * head + nx * head * 0.40f,
        tip_y - ty * head + ny * head * 0.40f, stroke, r, g, b, alpha);
    StrokeSegment(
        pixels, size, size, tip_x, tip_y, tip_x - tx * head - nx * head * 0.18f,
        tip_y - ty * head - ny * head * 0.18f, stroke, r, g, b, alpha);
    return pixels;
  }

  if (icon == NavIcon::NewTab) {
    StrokeSegment(pixels, size, size, s * 0.28f, s * 0.50f, s * 0.72f,
                  s * 0.50f, stroke, r, g, b, alpha);
    StrokeSegment(pixels, size, size, s * 0.50f, s * 0.28f, s * 0.50f,
                  s * 0.72f, stroke, r, g, b, alpha);
    return pixels;
  }

  if (icon == NavIcon::Extensions) {
    const float left = s * 0.24f;
    const float top = s * 0.24f;
    const float right = s * 0.76f;
    const float bottom = s * 0.76f;
    const float knob = s * 0.13f;
    const float cx = (left + right) * 0.5f;
    const float cy = (top + bottom) * 0.5f;
    const float puzzle_stroke = std::max(1.45f, s * 0.10f);

    StrokeSegment(pixels, size, size, left, top, cx - knob, top, puzzle_stroke,
                  r, g, b, alpha);
    StrokeArc(pixels, size, size, cx, top, knob, 0.0f, kPi, puzzle_stroke, r, g,
              b, alpha);
    StrokeSegment(pixels, size, size, cx + knob, top, right, top, puzzle_stroke,
                  r, g, b, alpha);

    StrokeSegment(pixels, size, size, right, top, right, cy - knob,
                  puzzle_stroke, r, g, b, alpha);
    StrokeArc(pixels, size, size, right, cy, knob, -kPi * 0.5f, kPi * 0.5f,
              puzzle_stroke, r, g, b, alpha);
    StrokeSegment(pixels, size, size, right, cy + knob, right, bottom,
                  puzzle_stroke, r, g, b, alpha);

    StrokeSegment(pixels, size, size, right, bottom, cx + knob, bottom,
                  puzzle_stroke, r, g, b, alpha);
    StrokeArc(pixels, size, size, cx, bottom, knob, 0.0f, kPi, puzzle_stroke, r,
              g, b, alpha);
    StrokeSegment(pixels, size, size, cx - knob, bottom, left, bottom,
                  puzzle_stroke, r, g, b, alpha);

    StrokeSegment(pixels, size, size, left, bottom, left, cy + knob,
                  puzzle_stroke, r, g, b, alpha);
    StrokeArc(pixels, size, size, left, cy, knob, -kPi * 0.5f, kPi * 0.5f,
              puzzle_stroke, r, g, b, alpha);
    StrokeSegment(pixels, size, size, left, cy - knob, left, top, puzzle_stroke,
                  r, g, b, alpha);
    return pixels;
  }

  const float x_near = icon == NavIcon::Back ? s * 0.32f : s * 0.68f;
  const float x_far = icon == NavIcon::Back ? s * 0.66f : s * 0.34f;
  StrokeSegment(pixels, size, size, x_far, s * 0.26f, x_near, s * 0.50f, stroke,
                r, g, b, alpha);
  StrokeSegment(pixels, size, size, x_near, s * 0.50f, x_far, s * 0.74f, stroke,
                r, g, b, alpha);
  return pixels;
}

CefRefPtr<CefImage> ScaleFavicon(CefRefPtr<CefImage> src, float target_dip) {
  if (!src || src->IsEmpty() || target_dip <= 0.0f) {
    return src;
  }
  auto dst = CefImage::CreateImage();
  const float scales[] = {1.0f, 2.0f};
  for (float scale : scales) {
    int pw = 0, ph = 0;
    auto bits = src->GetAsBitmap(scale, CEF_COLOR_TYPE_RGBA_8888,
                                 CEF_ALPHA_TYPE_POSTMULTIPLIED, pw, ph);
    if (bits && pw > 0 && ph > 0 && bits->GetSize() > 0) {
      std::vector<uint8_t> data(bits->GetSize());
      bits->GetData(data.data(), data.size(), 0);
      const float declared_scale = static_cast<float>(pw) / target_dip;
      dst->AddBitmap(declared_scale, pw, ph, CEF_COLOR_TYPE_RGBA_8888,
                     CEF_ALPHA_TYPE_POSTMULTIPLIED, data.data(), data.size());
    }
  }
  return dst;
}

CefRefPtr<CefImage> MakeNavIcon(NavIcon icon, uint8_t alpha) {
  auto image = CefImage::CreateImage();
  const float scales[] = {1.0f, 2.0f};
  for (float scale : scales) {
    const int size = static_cast<int>(std::lround(kNavIconDip * scale));
    const auto pixels = RasterizeNavIcon(icon, size, alpha);
    image->AddBitmap(scale, size, size, CEF_COLOR_TYPE_RGBA_8888,
                     CEF_ALPHA_TYPE_POSTMULTIPLIED, pixels.data(),
                     pixels.size());
  }
  return image;
}

cef_color_t Color(int r, int g, int b) {
  return CefColorSetARGB(255, r, g, b);
}

bool IsKeyDown(const CefKeyEvent& event, int windows_key_code) {
  return (event.type == KEYEVENT_RAWKEYDOWN ||
          event.type == KEYEVENT_KEYDOWN) &&
         event.windows_key_code == windows_key_code;
}

bool ContainsScheme(const std::string& value) {
  return value.find("://") != std::string::npos ||
         value.rfind("about:", 0) == 0 || value.rfind("chrome:", 0) == 0;
}

bool LooksLikeDomain(const std::string& value) {
  return value.find('.') != std::string::npos &&
         value.find(' ') == std::string::npos;
}

std::string DisplayLabelForUrl(const std::string& raw_url) {
  if (raw_url.empty()) {
    return "New tab";
  }

  CefURLParts parts;
  if (CefParseURL(raw_url, parts) && parts.host.length > 0) {
    const std::string host = CefString(&parts.host).ToString();
    if (host.rfind("www.", 0) == 0) {
      return host.substr(4);
    }
    return host;
  }

  return raw_url;
}

std::string Trim(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return std::string();
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string NormalizeUrl(const std::string& raw_value) {
  const auto value = Trim(raw_value);
  if (value.empty()) {
    return kStartupURL;
  }

  if (ContainsScheme(value)) {
    return value;
  }

  if (LooksLikeDomain(value) || value.rfind("localhost", 0) == 0) {
    return "https://" + value;
  }

  return "https://www.google.com/search?q=" +
         CefURIEncode(value, true).ToString();
}

std::string JsonEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::string NavHistoryJson(const std::vector<std::string>& history) {
  std::string json = "[";
  for (size_t i = 0; i < history.size(); ++i) {
    if (i > 0) {
      json += ',';
    }
    json += '"';
    json += JsonEscape(history[i]);
    json += '"';
  }
  json += ']';
  return json;
}

struct SessionTab {
  std::string url;
  std::string title;
};

struct SessionPinnedTab {
  std::string url;
  std::string title;
  int folder_id = -1;
};

struct SessionFolder {
  int id = 0;
  std::string name;
};

struct SessionSpace {
  std::string name;
  std::string icon;
  std::vector<SessionTab> tabs;
  int active_tab_index = 0;
  std::vector<std::string> nav_history;
  std::vector<SessionPinnedTab> pinned_tabs;
  std::vector<SessionFolder> folders;
};

struct SessionData {
  std::vector<SessionSpace> spaces;
  int active_space_index = 0;
};

std::string SessionFilePath() {
  const char* home = getenv("HOME");
  if (!home || home[0] == '\0') {
    return std::string();
  }
  return std::string(home) +
         "/Library/Application Support/HailmaryCEF/session.json";
}

std::string SpaceCachePath(int space_id) {
  const char* home = getenv("HOME");
  if (!home || home[0] == '\0') {
    return std::string();
  }
  return std::string(home) +
         "/Library/Application Support/HailmaryCEF/spaces/space_" +
         std::to_string(space_id);
}

std::string SpaceToJson(const SessionSpace& space) {
  std::string json = "{\"name\": \"";
  json += JsonEscape(space.name);
  json += "\", \"icon\": \"";
  json += JsonEscape(space.icon);
  json += "\", \"active_tab_index\": ";
  json += std::to_string(space.active_tab_index);
  json += ", \"tabs\": [";
  for (size_t i = 0; i < space.tabs.size(); ++i) {
    if (i > 0) json += ',';
    json += "\n      {\"url\": \"";
    json += JsonEscape(space.tabs[i].url);
    json += "\", \"title\": \"";
    json += JsonEscape(space.tabs[i].title);
    json += "\"}";
  }
  json += "], \"nav_history\": ";
  json += NavHistoryJson(space.nav_history);
  json += ", \"folders\": [";
  for (size_t i = 0; i < space.folders.size(); ++i) {
    if (i > 0) json += ',';
    json += "{\"id\": ";
    json += std::to_string(space.folders[i].id);
    json += ", \"name\": \"";
    json += JsonEscape(space.folders[i].name);
    json += "\"}";
  }
  json += "], \"pinned_tabs\": [";
  for (size_t i = 0; i < space.pinned_tabs.size(); ++i) {
    if (i > 0) json += ',';
    json += "{\"url\": \"";
    json += JsonEscape(space.pinned_tabs[i].url);
    json += "\", \"title\": \"";
    json += JsonEscape(space.pinned_tabs[i].title);
    json += "\", \"folder_id\": ";
    json += std::to_string(space.pinned_tabs[i].folder_id);
    json += "}";
  }
  json += "]}";
  return json;
}

std::string SessionToJson(const SessionData& session) {
  std::string json = "{\n  \"active_space_index\": ";
  json += std::to_string(session.active_space_index);
  json += ",\n  \"spaces\": [";
  for (size_t i = 0; i < session.spaces.size(); ++i) {
    if (i > 0) json += ',';
    json += "\n    ";
    json += SpaceToJson(session.spaces[i]);
  }
  json += "\n  ]\n}\n";
  return json;
}

void SaveSessionToFile(const SessionData& session) {
  const auto path = SessionFilePath();
  if (path.empty()) {
    return;
  }
  std::ofstream out(path, std::ios::trunc);
  if (out.is_open()) {
    out << SessionToJson(session);
  }
}

SessionSpace ParseSessionSpace(CefRefPtr<CefDictionaryValue> dict) {
  SessionSpace space;
  if (!dict) return space;

  if (dict->HasKey("name")) {
    space.name = dict->GetString("name").ToString();
  }
  if (dict->HasKey("icon")) {
    space.icon = dict->GetString("icon").ToString();
  }
  if (dict->HasKey("active_tab_index") &&
      dict->GetType("active_tab_index") == VTYPE_INT) {
    space.active_tab_index = dict->GetInt("active_tab_index");
  }
  if (dict->HasKey("tabs") && dict->GetType("tabs") == VTYPE_LIST) {
    auto tabs_list = dict->GetList("tabs");
    for (size_t i = 0; i < tabs_list->GetSize(); ++i) {
      if (tabs_list->GetType(i) != VTYPE_DICTIONARY) continue;
      auto tab_dict = tabs_list->GetDictionary(i);
      SessionTab info;
      if (tab_dict->HasKey("url"))
        info.url = tab_dict->GetString("url").ToString();
      if (tab_dict->HasKey("title"))
        info.title = tab_dict->GetString("title").ToString();
      if (!info.url.empty())
        space.tabs.push_back(std::move(info));
    }
  }
  if (dict->HasKey("nav_history") &&
      dict->GetType("nav_history") == VTYPE_LIST) {
    auto hist_list = dict->GetList("nav_history");
    for (size_t i = 0; i < hist_list->GetSize(); ++i) {
      auto entry = hist_list->GetString(i).ToString();
      if (!entry.empty())
        space.nav_history.push_back(std::move(entry));
    }
  }
  if (dict->HasKey("folders") && dict->GetType("folders") == VTYPE_LIST) {
    auto folders_list = dict->GetList("folders");
    for (size_t i = 0; i < folders_list->GetSize(); ++i) {
      if (folders_list->GetType(i) != VTYPE_DICTIONARY) continue;
      auto folder_dict = folders_list->GetDictionary(i);
      SessionFolder folder;
      if (folder_dict->HasKey("id")) folder.id = folder_dict->GetInt("id");
      if (folder_dict->HasKey("name"))
        folder.name = folder_dict->GetString("name").ToString();
      space.folders.push_back(std::move(folder));
    }
  }
  if (dict->HasKey("pinned_tabs") &&
      dict->GetType("pinned_tabs") == VTYPE_LIST) {
    auto pinned_list = dict->GetList("pinned_tabs");
    for (size_t i = 0; i < pinned_list->GetSize(); ++i) {
      if (pinned_list->GetType(i) != VTYPE_DICTIONARY) continue;
      auto ptab_dict = pinned_list->GetDictionary(i);
      SessionPinnedTab info;
      if (ptab_dict->HasKey("url"))
        info.url = ptab_dict->GetString("url").ToString();
      if (ptab_dict->HasKey("title"))
        info.title = ptab_dict->GetString("title").ToString();
      if (ptab_dict->HasKey("folder_id"))
        info.folder_id = ptab_dict->GetInt("folder_id");
      if (!info.url.empty())
        space.pinned_tabs.push_back(std::move(info));
    }
  }
  return space;
}

SessionData LoadSessionFromFile() {
  SessionData session;
  const auto path = SessionFilePath();
  if (path.empty()) return session;

  std::ifstream in(path);
  if (!in.is_open()) return session;

  const std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  if (content.empty()) return session;

  auto value = CefParseJSON(content, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return session;

  auto dict = value->GetDictionary();
  if (!dict) return session;

  if (dict->HasKey("spaces") && dict->GetType("spaces") == VTYPE_LIST) {
    if (dict->HasKey("active_space_index") &&
        dict->GetType("active_space_index") == VTYPE_INT) {
      session.active_space_index = dict->GetInt("active_space_index");
    }
    auto spaces_list = dict->GetList("spaces");
    for (size_t i = 0; i < spaces_list->GetSize(); ++i) {
      if (spaces_list->GetType(i) != VTYPE_DICTIONARY) continue;
      session.spaces.push_back(
          ParseSessionSpace(spaces_list->GetDictionary(i)));
    }
  } else {
    SessionSpace space = ParseSessionSpace(dict);
    if (space.name.empty()) space.name = "Space 1";
    session.spaces.push_back(std::move(space));
  }

  return session;
}

CefBoxLayoutSettings Box(bool horizontal,
                         int spacing = 0,
                         const CefInsets& insets = CefInsets()) {
  CefBoxLayoutSettings settings;
  settings.horizontal = horizontal ? 1 : 0;
  settings.between_child_spacing = spacing;
  settings.inside_border_insets = insets;
  settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
  settings.default_flex = 0;
  return settings;
}

class FixedPanelDelegate : public CefPanelDelegate {
 public:
  FixedPanelDelegate(int width, int height) : size_(width, height) {}
  FixedPanelDelegate(int width, int height, cef_color_t background)
      : size_(width, height), has_background_(true), background_(background) {}

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override { return size_; }

  void OnThemeChanged(CefRefPtr<CefView> view) override {
    if (has_background_ && view) {
      view->SetBackgroundColor(background_);
    }
  }

  void SetPreferredSize(const CefSize& size) { size_ = size; }

 private:
  CefSize size_;
  bool has_background_ = false;
  cef_color_t background_ = 0;

  IMPLEMENT_REFCOUNTING(FixedPanelDelegate);
};

// CEF discards view background colors on every theme pass and only the view's
// own delegate is notified, so a panel that must keep a color needs a delegate
// even when it sizes itself from its children.
class ThemedPanelDelegate : public CefPanelDelegate {
 public:
  explicit ThemedPanelDelegate(cef_color_t background)
      : background_(background) {}

  void OnThemeChanged(CefRefPtr<CefView> view) override {
    if (view) {
      view->SetBackgroundColor(background_);
    }
  }

 private:
  cef_color_t background_;

  IMPLEMENT_REFCOUNTING(ThemedPanelDelegate);
};

class HailmaryWindowDelegate;

class ExtensionPopupWindowDelegate : public CefWindowDelegate,
                                     public CefButtonDelegate {
 public:
  ExtensionPopupWindowDelegate(CefRefPtr<CefWindow> parent,
                               HailmaryWindowDelegate* owner)
      : parent_(parent), owner_(owner) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    popup_window_ = window;

    window->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));

    auto panel = CefPanel::CreatePanel(nullptr);
    panel->SetBackgroundColor(CefColorSetARGB(235, 24, 28, 32));

    CefBoxLayoutSettings vbox;
    vbox.horizontal = 0;
    panel->SetToBoxLayout(vbox);

    auto header = CefPanel::CreatePanel(nullptr);
    header->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
    CefBoxLayoutSettings hbox;
    hbox.horizontal = 1;
    hbox.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    header->SetToBoxLayout(hbox);

    auto title = CefLabelButton::CreateLabelButton(nullptr, "Extensions");
    title->SetTextColor(CEF_BUTTON_STATE_NORMAL,
                        CefColorSetARGB(230, 220, 228, 235));
    title->SetFontList("system-ui, 13px, Bold");
    title->SetInkDropEnabled(false);
    title->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
    title->SetEnabled(false);
    header->AddChildView(title);
    header->GetLayout()->AsBoxLayout()->SetFlexForView(title, 1);

    auto add_btn = CefLabelButton::CreateLabelButton(this, "+ Get extensions");
    add_btn->SetTextColor(CEF_BUTTON_STATE_NORMAL,
                          CefColorSetARGB(230, 180, 200, 220));
    add_btn->SetFontList("system-ui, 12px");
    add_btn->SetBackgroundColor(CefColorSetARGB(20, 255, 255, 255));
    add_btn->SetInkDropEnabled(true);
    add_btn_ = add_btn;
    header->AddChildView(add_btn);

    panel->AddChildView(header);
    header->SetSize(CefSize(kExtPopupWidth, 42));
    panel->GetLayout()->AsBoxLayout()->SetFlexForView(header, 0);

    auto sep = CefPanel::CreatePanel(nullptr);
    sep->SetBackgroundColor(CefColorSetARGB(15, 255, 255, 255));
    panel->AddChildView(sep);
    sep->SetSize(CefSize(kExtPopupWidth, 1));
    panel->GetLayout()->AsBoxLayout()->SetFlexForView(sep, 0);

    auto body = CefPanel::CreatePanel(nullptr);
    body->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
    CefBoxLayoutSettings bodybox;
    bodybox.horizontal = 0;
    bodybox.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    bodybox.main_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    body->SetToBoxLayout(bodybox);

    auto empty_label = CefLabelButton::CreateLabelButton(nullptr,
        "No extensions installed.");
    empty_label->SetTextColor(CEF_BUTTON_STATE_NORMAL,
                              CefColorSetARGB(150, 160, 170, 180));
    empty_label->SetFontList("system-ui, 13px");
    empty_label->SetInkDropEnabled(false);
    empty_label->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
    empty_label->SetEnabled(false);
    body->AddChildView(empty_label);

    auto hint_label = CefLabelButton::CreateLabelButton(nullptr,
        "Click + Get extensions to browse");
    hint_label->SetTextColor(CEF_BUTTON_STATE_NORMAL,
                             CefColorSetARGB(100, 160, 170, 180));
    hint_label->SetFontList("system-ui, 12px");
    hint_label->SetInkDropEnabled(false);
    hint_label->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
    hint_label->SetEnabled(false);
    body->AddChildView(hint_label);

    auto hint_label2 = CefLabelButton::CreateLabelButton(nullptr,
        "the Chrome Web Store.");
    hint_label2->SetTextColor(CEF_BUTTON_STATE_NORMAL,
                              CefColorSetARGB(100, 160, 170, 180));
    hint_label2->SetFontList("system-ui, 12px");
    hint_label2->SetInkDropEnabled(false);
    hint_label2->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
    hint_label2->SetEnabled(false);
    body->AddChildView(hint_label2);

    panel->AddChildView(body);
    panel->GetLayout()->AsBoxLayout()->SetFlexForView(body, 1);

    CefBoxLayoutSettings winbox;
    winbox.horizontal = 0;
    window->SetToBoxLayout(winbox);
    window->AddChildView(panel);
    window->GetLayout()->AsBoxLayout()->SetFlexForView(panel, 1);
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;

  bool CanClose(CefRefPtr<CefWindow> window) override { return true; }

  CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow> window,
                                       bool* is_menu,
                                       bool* can_activate_menu) override {
    *is_menu = true;
    *can_activate_menu = true;
    return parent_;
  }

  bool IsFrameless(CefRefPtr<CefWindow> window) override { return true; }

  void OnButtonPressed(CefRefPtr<CefButton> button) override;

  CefRefPtr<CefWindow> popup_window() { return popup_window_; }

 private:
  CefRefPtr<CefWindow> parent_;
  HailmaryWindowDelegate* owner_;
  CefRefPtr<CefWindow> popup_window_;
  CefRefPtr<CefButton> add_btn_;

  IMPLEMENT_REFCOUNTING(ExtensionPopupWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(ExtensionPopupWindowDelegate);
};

class AddressBarDelegate : public CefTextfieldDelegate {
 public:
  explicit AddressBarDelegate(HailmaryWindowDelegate* owner)
      : owner_(owner) {}

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    if (view && view->AsTextfield()) {
      return CefSize(40, kAddressChipHeight);
    }
    return CefSize(0, 0);
  }

  bool OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                  const CefKeyEvent& event) override;

  void OnFocus(CefRefPtr<CefView> view) override;
  void OnBlur(CefRefPtr<CefView> view) override;

 private:
  HailmaryWindowDelegate* owner_;

  IMPLEMENT_REFCOUNTING(AddressBarDelegate);
  DISALLOW_COPY_AND_ASSIGN(AddressBarDelegate);
};

class HailmaryWindowDelegate : public CefWindowDelegate,
                               public CefButtonDelegate,
                               public CefBrowserViewDelegate {
 public:
  HailmaryWindowDelegate() = default;
  HailmaryWindowDelegate(const HailmaryWindowDelegate&) = delete;
  HailmaryWindowDelegate& operator=(const HailmaryWindowDelegate&) = delete;

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    CEF_REQUIRE_UI_THREAD();

    window_ = window;
    window_->SetTitle("Avora");
    window_->SetBackgroundColor(SidebarBackground());
    window_->SetToFillLayout();
    window_->SetAccelerator(kCommandNewTab, 'T', false, true, false, true);
    window_->SetAccelerator(kCommandFocusLocation, 'L', false, true, false,
                            true);
    window_->SetAccelerator(kCommandCloseOverlay, kKeyEscape, false, false,
                            false, true);
    window_->SetAccelerator(kCommandCloseTab, 'W', false, true, false, true);
    window_->SetAccelerator(kCommandDevTools, 'I', false, false, true, true);
    window_->SetAccelerator(kCommandPinTab, 'P', true, true, false, true);

    BuildShell();
    ApplyOmniboxTheme();
    window_->ThemeChanged();
    window_->Show();
    InstallNativeSidebarResizeHandle();
    InstallNativeSidebarTabClickMonitor();
    SyncSidebarTabClickMonitorTabCount();
    UpdatePinnedSectionHeight();
    InstallContentWellCornerRadius();
    InstallNativeAddressChipOutline();
    InstallNativeSpaceNavigationMonitor();
#if defined(OS_MAC)
    DisableNativeFocusRings(window_->GetWindowHandle());
#endif
    if (auto browser_view = ActiveBrowserView()) {
      browser_view->RequestFocus();
    }
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    SaveSession();

#if defined(OS_MAC)
    if (window_) {
      RemoveSidebarResizeHandle(window_->GetWindowHandle());
      RemoveSidebarTabClickMonitor(window_->GetWindowHandle());
      RemoveToolbarPlusClickMonitor(window_->GetWindowHandle());
      RemoveContentWellCornerOverlay(window_->GetWindowHandle());
      RemoveDevToolsSplitter(window_->GetWindowHandle());
      RemoveAddressChipOutline(window_->GetWindowHandle());
      RemoveSpaceNavigationMonitor(window_->GetWindowHandle());
    }
#endif
    tabs_.clear();
    pinned_folders_.clear();
    pinned_container_ = nullptr;
    active_tab_id_ = -1;
    spaces_.clear();
    space_buttons_.clear();
    spaces_bar_ = nullptr;
    space_name_label_ = nullptr;
    devtools_view_ = nullptr;
    devtools_surface_ = nullptr;
    web_surface_delegate_ = nullptr;
    body_layout_ = nullptr;
    content_well_layout_ = nullptr;
    content_well_ = nullptr;
    address_ = nullptr;
    address_bar_ = nullptr;
    address_left_pad_ = nullptr;
    address_right_pad_ = nullptr;
    root_ = nullptr;
    body_ = nullptr;
    window_ = nullptr;
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    auto browser_view = ActiveBrowserView();
    if (!browser_view) {
      return true;
    }

    auto browser = browser_view->GetBrowser();
    if (browser) {
      return browser->GetHost()->TryCloseBrowser();
    }

    return true;
  }

  bool IsFrameless(CefRefPtr<CefWindow> window) override { return true; }

  bool WithStandardWindowButtons(CefRefPtr<CefWindow> window) override {
    return true;
  }

  bool GetTitlebarHeight(CefRefPtr<CefWindow> window,
                         float* titlebar_height) override {
    *titlebar_height = kToolbarNavRowHeight;
    return true;
  }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    if (IsTabPanel(view)) {
      if (IsPinnedTabPanel(view)) {
        return CefSize(sidebar_width_ - 2 * kSidebarHorizontalPad,
                       kPinnedTabHeight);
      }
      return CefSize(sidebar_width_, kTabHeight);
    }

    if (IsTabButton(view)) {
      if (IsPinnedTabButton(view)) {
        return CefSize(sidebar_width_ - 2 * kSidebarHorizontalPad - 8,
                       kPinnedTabHeight);
      }
      return CefSize(TabButtonWidth(), kTabHeight);
    }

    if (view && view->AsButton()) {
      const auto label = view->AsButton()->AsLabelButton();
      if (label) {
        const auto text = label->GetText().ToString();
        if (text == "Pinned") {
          return CefSize(sidebar_width_ - 20, 42);
        }

        if (text == "Tabs") {
          return CefSize(80, kTabsLabelHeight);
        }

        if (text == "Google" || text == "New tab") {
          return CefSize(TabButtonWidth(), 42);
        }

        if (text == "1P" || text == "G") {
          return CefSize(46, 42);
        }
      }

      return CefSize(36, 36);
    }

    return CefSize(1240, 820);
  }

  CefSize GetMinimumSize(CefRefPtr<CefView> view) override {
    return CefSize(900, 560);
  }

  bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override {
    if (command_id == kCommandNewTab) {
      ShowNewTabOverlay();
      return true;
    }

    if (command_id == kCommandFocusLocation) {
      FocusAddress();
      return true;
    }

    if (command_id == kCommandCloseTab) {
      if (active_tab_id_ >= 0) {
        CloseTab(active_tab_id_);
      }
      return true;
    }

    if (command_id == kCommandDevTools) {
      ToggleDevTools();
      return true;
    }

    if (command_id == kCommandPinTab) {
      TogglePinActiveTab();
      return true;
    }

    if (command_id == kCommandCloseOverlay) {
      if (IsExtensionPopupVisible()) {
        HideExtensionPopup();
        if (auto browser_view = ActiveBrowserView()) {
          browser_view->RequestFocus();
        }
        return true;
      }
      if (rename_space_overlay_visible_) {
        HideRenameSpaceOverlay();
        if (auto browser_view = ActiveBrowserView()) {
          browser_view->RequestFocus();
        }
        return true;
      }
      if (IsPinOverlayVisible()) {
        HidePinOverlay();
        if (auto browser_view = ActiveBrowserView()) {
          browser_view->RequestFocus();
        }
        return true;
      }
      if (IsNewTabOverlayVisible()) {
        HideNewTabOverlay();
        if (auto browser_view = ActiveBrowserView()) {
          browser_view->RequestFocus();
        }
        return true;
      }
    }

    return false;
  }

  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override {
    ConfigureDraggableRegions();
    UpdateNativeSidebarResizeHandle();
    InstallContentWellCornerRadius();
    if (devtools_surface_ && content_well_ && web_surface_delegate_) {
      auto cw_bounds = content_well_->GetBounds();
      const int max_width = std::max(
          kMinDevToolsSplitWidth,
          cw_bounds.width - kDevToolsGap - kMinDevToolsSplitWidth);
      devtools_web_width_ =
          std::clamp(devtools_web_width_, kMinDevToolsSplitWidth, max_width);
      web_surface_delegate_->SetPreferredSize(
          CefSize(devtools_web_width_, 1));
      content_well_->InvalidateLayout();
      content_well_->Layout();
      InstallNativeDevToolsSplitter();
    }
  }

  void OnLayoutChanged(CefRefPtr<CefView> view,
                       const CefRect& new_bounds) override {
    if (view == back_button_ || view == forward_button_ ||
        view == reload_button_ || view == address_ || view == address_bar_ ||
        view == extension_button_ || view == plus_button_) {
      ConfigureDraggableRegions();
    }
  }

  void OnFocus(CefRefPtr<CefView> view) override {
    if (IsTabButton(view)) {
      const int tab_id = FindTabIdForView(view);
      if (tab_id >= 0) {
        ActivateTab(tab_id);
      }
      return;
    }
  }

  void HandleAddressFocus() {
    ScheduleAddressSelectAll();
#if defined(OS_MAC)
    if (window_) {
      DisableNativeFocusRings(window_->GetWindowHandle());
    }
#endif
  }

  void HandleAddressBlur() {
    address_select_all_pending_ = false;
  }

  void OnThemeColorsChanged(CefRefPtr<CefWindow> window,
                            bool chrome_theme) override {
    (void)window;
    (void)chrome_theme;
    ApplyOmniboxTheme();
  }

  void OnThemeChanged(CefRefPtr<CefView> view) override {
    (void)view;
    // address_bar_ has no view-delegate of its own; theme application would
    // otherwise restore a black/transparent panel behind the textfield pill.
    ApplyOmniboxChipFill();
    ApplyOmniboxTextColors();
    ApplySidebarLabelBackgrounds();

    for (const auto& tab : tabs_) {
      ApplyTabChrome(tab.id);
    }
  }

  void OnButtonPressed(CefRefPtr<CefButton> button) override {
    if (button == space_name_label_) {
      ShowRenameSpaceOverlay();
      return;
    }

    if (IsSpaceButton(button)) {
      HandleSpaceButtonPress(button);
      return;
    }

    const int tab_id = FindTabIdForButton(button);
    if (tab_id >= 0) {
      ActivateTab(tab_id);
      return;
    }

    if (button == plus_button_) {
      OpenNewTab();
      return;
    }

    if (button == extension_button_) {
      ToggleExtensionPopup();
      return;
    }

    auto browser = GetBrowser();
    if (!browser) {
      return;
    }

    if (button == back_button_ && browser->CanGoBack()) {
      browser->GoBack();
      UpdateNavigationButtons();
    } else if (button == forward_button_ && browser->CanGoForward()) {
      browser->GoForward();
      UpdateNavigationButtons();
    } else if (button == reload_button_) {
      browser->Reload();
      UpdateNavigationButtons();
    }
  }

  // Fallback for a Return that reaches the window rather than the textfield
  // delegate. Window key events are delivered only after every control has
  // passed on the event, so navigating here is safe while the address field
  // still holds focus.
  bool OnKeyEvent(CefRefPtr<CefWindow> window,
                  const CefKeyEvent& event) override {
    (void)window;
    if (!IsKeyDown(event, kKeyReturn) || !address_ || !address_->HasFocus()) {
      return false;
    }

    NavigateToAddressBarValue();
    return true;
  }

  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override {
    if (is_devtools) {
      DockDevTools(popup_browser_view);
      return true;
    }

    auto popup_delegate = new HailmaryWindowDelegate();
    popup_delegate->pending_browser_view_ = popup_browser_view;
    popup_delegate->is_popup_ = true;
    CefWindow::CreateTopLevelWindow(popup_delegate);
    return true;
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  struct Tab {
    int id = 0;
    int space_id = 0;
    std::string url;
    std::string title;
    bool has_page_title = false;
    CefRefPtr<CefBrowserView> browser_view;
    CefRefPtr<CefPanel> panel;
    CefRefPtr<CefLabelButton> button;
    CefRefPtr<FixedPanelDelegate> panel_delegate;
    CefRefPtr<CefImage> favicon;
    bool pinned = false;
    int folder_id = -1;
  };

  struct Space {
    int id = 0;
    std::string name;
    std::string icon;
    CefRefPtr<CefRequestContext> request_context;
    int active_tab_id = -1;
    int next_tab_id = 1;
    int next_folder_id = 1;
  };

  struct PinnedFolder {
    int id = 0;
    std::string name;
    CefRefPtr<CefPanel> header_panel;
    CefRefPtr<CefPanel> content_panel;
  };

  void SaveSession() {
    if (is_popup_) {
      return;
    }

    SaveCurrentSpaceState();

    SessionData session;
    session.active_space_index = active_space_index_;

    for (size_t si = 0; si < spaces_.size(); ++si) {
      const auto& space = spaces_[si];
      SessionSpace ss;
      ss.name = space.name;
      ss.icon = space.icon;

      for (const auto& tab : tabs_) {
        if (tab.space_id != space.id) continue;
        if (tab.pinned) {
          SessionPinnedTab pinfo;
          pinfo.url = tab.url;
          pinfo.title = tab.title;
          pinfo.folder_id = tab.folder_id;
          ss.pinned_tabs.push_back(std::move(pinfo));
        } else {
          SessionTab info;
          info.url = tab.url;
          info.title = tab.title;
          ss.tabs.push_back(std::move(info));
          if (tab.id == space.active_tab_id) {
            ss.active_tab_index =
                static_cast<int>(ss.tabs.size()) - 1;
          }
        }
      }

      for (const auto& folder : pinned_folders_) {
        SessionFolder sf;
        sf.id = folder.id;
        sf.name = folder.name;
        ss.folders.push_back(std::move(sf));
      }
      ss.nav_history = nav_history_;
      session.spaces.push_back(std::move(ss));
    }

    SaveSessionToFile(session);
  }

  void ToggleDevTools() {
    auto browser = GetBrowser();
    if (!browser) {
      return;
    }

    if (browser->GetHost()->HasDevTools()) {
      browser->GetHost()->CloseDevTools();
      CloseDevToolsPanel();
    } else {
      CefWindowInfo window_info;
      CefBrowserSettings settings;
      browser->GetHost()->ShowDevTools(window_info, nullptr, settings,
                                       CefPoint());
    }
  }

  void DockDevTools(CefRefPtr<CefBrowserView> devtools_view) {
    if (!content_well_ || !content_well_layout_) {
      return;
    }

    CloseDevToolsPanel();

    auto cw_bounds = content_well_->GetBounds();
    devtools_web_width_ =
        std::max(kMinDevToolsSplitWidth,
                 (cw_bounds.width - kDevToolsGap) / 2);

    if (web_surface_delegate_) {
      web_surface_delegate_->SetPreferredSize(
          CefSize(devtools_web_width_, 1));
    }
    content_well_layout_->SetFlexForView(web_surface_, 0);

    devtools_view_ = devtools_view;
    devtools_surface_ = CefPanel::CreatePanel(nullptr);
    devtools_surface_->SetBackgroundColor(
        Color(kWebSurfaceRed, kWebSurfaceGreen, kWebSurfaceBlue));
    devtools_surface_->SetToFillLayout();
    devtools_surface_->AddChildView(devtools_view);

    content_well_->AddChildView(devtools_surface_);
    content_well_layout_->SetFlexForView(devtools_surface_, 1);
    content_well_->InvalidateLayout();
    content_well_->Layout();

    InstallNativeDevToolsSplitter();
    ConfigureDraggableRegions();
  }

  void CloseDevToolsPanel() {
    if (devtools_surface_ && content_well_) {
      RemoveNativeDevToolsSplitter();
      content_well_->RemoveChildView(devtools_surface_);
      devtools_view_ = nullptr;
      devtools_surface_ = nullptr;
      devtools_web_width_ = 0;
      content_well_layout_->SetFlexForView(web_surface_, 1);
      content_well_->InvalidateLayout();
      content_well_->Layout();
      ConfigureDraggableRegions();
    }
  }

  void ApplyOmniboxFill(CefRefPtr<CefView> view) {
    if (!view) {
      return;
    }
    view->SetBackgroundColor(Color(kOmniboxRed, kOmniboxGreen, kOmniboxBlue));
  }

  void ApplyOmniboxChipFill() {
    ApplyOmniboxFill(address_bar_);
    ApplyOmniboxFill(address_);
    ApplyOmniboxFill(extension_button_);
    ApplyOmniboxFill(address_left_pad_);
    ApplyOmniboxFill(address_right_pad_);
    if (!address_bar_) {
      return;
    }
    const size_t child_count = address_bar_->GetChildViewCount();
    for (size_t i = 0; i < child_count; ++i) {
      ApplyOmniboxFill(address_bar_->GetChildViewAt(static_cast<int>(i)));
    }
  }

  CefRefPtr<CefPanel> MakeOmniboxPad(int width) {
    auto pad = CefPanel::CreatePanel(new FixedPanelDelegate(
        width, kAddressChipHeight,
        Color(kOmniboxRed, kOmniboxGreen, kOmniboxBlue)));
    pad->SetBackgroundColor(Color(kOmniboxRed, kOmniboxGreen, kOmniboxBlue));
    return pad;
  }

  void ApplyOmniboxTheme() {
    if (!window_) {
      return;
    }
    const auto fill = Color(kOmniboxRed, kOmniboxGreen, kOmniboxBlue);
    const auto text =
        Color(kOmniboxTextRed, kOmniboxTextGreen, kOmniboxTextBlue);
    window_->SetThemeColor(CEF_ColorTextfieldBackground, fill);
    window_->SetThemeColor(CEF_ColorTextfieldBackgroundDisabled, fill);
    window_->SetThemeColor(CEF_ColorTextfieldFilledBackground, fill);
    window_->SetThemeColor(CEF_ColorTextfieldForeground, text);
    window_->SetThemeColor(CEF_ColorTextfieldForegroundDisabled, text);
    window_->SetThemeColor(CEF_ColorTextfieldForegroundIcon, text);
    window_->SetThemeColor(CEF_ColorTextfieldSelectionForeground, text);
    window_->SetThemeColor(CEF_ColorTextfieldSelectionBackground,
                           Color(37, 91, 86));
    // The address chip's single border is painted by the native overlay, so
    // every stroke Chromium would draw around the textfield must vanish. A
    // fill-matched value is not enough: the strokes are anti-aliased and the
    // focus ring is contrast-blended against its backdrop, so both survive as
    // faint outlines. Fully transparent is the only value that removes them.
    const auto kNone = static_cast<cef_color_t>(0);
    // InkDrop highlight is LayerRegion::kAbove at opacity 1, so an opaque value
    // here would paint over the URL text. Chromium's material mixer maps
    // kColorTextfieldHover to kColorSysStateHoverOnSubtle, not to transparent.
    window_->SetThemeColor(CEF_ColorTextfieldHover, kNone);
    window_->SetThemeColor(CEF_ColorTextfieldOutline, kNone);
    window_->SetThemeColor(CEF_ColorTextfieldOutlineDisabled, kNone);
    window_->SetThemeColor(CEF_ColorTextfieldOutlineInvalid, kNone);
    window_->SetThemeColor(CEF_ColorSidePanelTextfieldBorder, kNone);
    window_->SetThemeColor(CEF_ColorFocusableBorderUnfocused, kNone);
    window_->SetThemeColor(CEF_ColorTextfieldFilledUnderline, kNone);
    window_->SetThemeColor(CEF_ColorTextfieldFilledUnderlineFocused, kNone);
    window_->SetThemeColor(CEF_ColorSysStateFocusRingInverse, kNone);
    window_->SetThemeColor(CEF_ColorSysStateFocusHighlight, kNone);
    window_->SetThemeColor(CEF_ColorSysStateFocus, kNone);
    window_->SetThemeColor(CEF_ColorFocusHighlightDefault, kNone);

    // The focus ring is the one stroke that cannot be hidden with a color
    // alone: views::FocusRing resolves it through GetCascadingAccentColor,
    // which runs CEF_ColorFocusableBorderFocused through BlendForMinContrast
    // against CEF_ColorWindowBackground. Any value we pick gets brightened
    // until it contrasts with that backdrop, which is why transparent and
    // fill-matched values both came back as a grey ring. Pointing the blend at
    // a light backdrop instead lets the chip fill clear the contrast threshold
    // unchanged, so the ring paints in the fill color and disappears.
    window_->SetThemeColor(CEF_ColorFocusableBorderFocused, fill);
    window_->SetThemeColor(CEF_ColorSysStateFocusRing, fill);
    window_->SetThemeColor(CEF_ColorWindowBackground, Color(255, 255, 255));
    window_->SetThemeColor(CEF_ColorAccent, fill);
    window_->SetThemeColor(
        CEF_ColorAccentWithGuaranteedContrastAtopPrimaryBackground, fill);
    window_->SetThemeColor(CEF_ColorSubtleAccent, fill);
    window_->SetThemeColor(CEF_ColorCssSystemHotlight, fill);
    window_->SetThemeColor(CEF_ColorCssSystemHighlight, fill);
    window_->SetThemeColor(CEF_ColorItemHighlight, fill);
    window_->SetThemeColor(CEF_ColorSysOutline, fill);
    window_->SetThemeColor(CEF_ColorSysTonalOutline, fill);
    window_->SetThemeColor(CEF_ColorSysNeutralOutline, fill);
    window_->SetThemeColor(CEF_ColorSysStateInactiveRing, fill);
    window_->SetThemeColor(CEF_ColorSysOmniboxContainer, fill);
    window_->SetThemeColor(CEF_ColorButtonBorder, fill);
    window_->SetThemeColor(CEF_ColorButtonBorderDisabled, fill);
    ApplyOmniboxChipFill();
    ApplyOmniboxTextColors();
  }

  void ApplyOmniboxTextColors() {
    if (!address_) {
      return;
    }
    address_->SetTextColor(
        Color(kOmniboxTextRed, kOmniboxTextGreen, kOmniboxTextBlue));
    address_->SetSelectionTextColor(
        Color(kOmniboxTextRed, kOmniboxTextGreen, kOmniboxTextBlue));
    address_->SetSelectionBackgroundColor(Color(37, 91, 86));
  }

  CefRefPtr<CefBrowser> GetBrowser() {
    auto browser_view = ActiveBrowserView();
    return browser_view ? browser_view->GetBrowser() : nullptr;
  }

  CefRefPtr<CefLabelButton> MakeButton(const CefString& text) {
    auto button = CefLabelButton::CreateLabelButton(this, text);
    button->SetMinimumSize(CefSize(36, 36));
    button->SetMaximumSize(CefSize(36, 36));
    button->SetFontList("Arial, Bold 15px");
    button->SetEnabledTextColors(Color(237, 242, 245));
    button->SetBackgroundColor(
        Color(kChromeElevatedRed, kChromeElevatedGreen, kChromeElevatedBlue));
    return button;
  }

  CefRefPtr<CefLabelButton> MakeNavButton(NavIcon icon,
                                          const CefString& name,
                                          int size = kToolbarNavButtonSize) {
    auto button = CefLabelButton::CreateLabelButton(this, "");
    button->SetMinimumSize(CefSize(size, size));
    button->SetMaximumSize(CefSize(size, size));
    button->SetBackgroundColor(
        Color(kChromeElevatedRed, kChromeElevatedGreen, kChromeElevatedBlue));
    button->SetInkDropEnabled(true);
    button->SetAccessibleName(name);
    button->SetTooltipText(name);
    button->SetImage(CEF_BUTTON_STATE_NORMAL, MakeNavIcon(icon, 230));
    button->SetImage(CEF_BUTTON_STATE_HOVERED, MakeNavIcon(icon, 255));
    button->SetImage(CEF_BUTTON_STATE_PRESSED, MakeNavIcon(icon, 255));
    button->SetImage(CEF_BUTTON_STATE_DISABLED, MakeNavIcon(icon, 88));
    return button;
  }

  void UpdateNavigationButtons() {
    auto browser = GetBrowser();
    if (!back_button_ || !forward_button_) {
      return;
    }

    const bool can_go_back = browser && browser->CanGoBack();
    const bool can_go_forward = browser && browser->CanGoForward();
    back_button_->SetEnabled(can_go_back);
    forward_button_->SetEnabled(can_go_forward);
  }

  cef_color_t SidebarBackground() const {
    return Color(kChromeElevatedRed, kChromeElevatedGreen, kChromeElevatedBlue);
  }

  CefRefPtr<CefLabelButton> MakeSidebarLabel(const CefString& text,
                                             int height,
                                             int width = 0) {
    const int label_width = width > 0 ? width : sidebar_width_ - 20;
    auto label = CefLabelButton::CreateLabelButton(this, text);
    label->SetMinimumSize(CefSize(label_width, height));
    label->SetMaximumSize(CefSize(label_width, height));
    label->SetFontList("Arial, Bold 12px");
    label->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    label->SetEnabledTextColors(Color(154, 166, 173));
    label->SetTextColor(CEF_BUTTON_STATE_DISABLED, Color(154, 166, 173));
    label->SetBackgroundColor(SidebarBackground());
    label->SetEnabled(false);
    sidebar_labels_.push_back(label);
    return label;
  }

  // Sidebar section labels are delegated to this class, so their backgrounds
  // are restored here rather than by a panel delegate.
  void ApplySidebarLabelBackgrounds() {
    for (const auto& label : sidebar_labels_) {
      label->SetBackgroundColor(SidebarBackground());
    }
  }

  int TabButtonWidth() const {
    return std::max(40, sidebar_width_ - kTabTextInset);
  }

  void BuildShell() {
    auto root = CefPanel::CreatePanel(new ThemedPanelDelegate(SidebarBackground()));
    root_ = root;
    root->SetBackgroundColor(SidebarBackground());
    auto root_layout = root->SetToBoxLayout(Box(false, 0));

    auto nav_row = CefPanel::CreatePanel(new FixedPanelDelegate(
        sidebar_width_ - 20, kToolbarNavRowHeight, SidebarBackground()));
    nav_row->SetBackgroundColor(SidebarBackground());
    const int nav_left_clearance =
        std::max(0, kTrafficLightWidth - kSidebarHorizontalPad);
    auto nav_settings =
        Box(true, kNavButtonGap, CefInsets(6, nav_left_clearance, 6, 2));
    nav_settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_END;
    auto nav_layout = nav_row->SetToBoxLayout(nav_settings);

    back_button_ = MakeNavButton(NavIcon::Back, "Back");
    forward_button_ = MakeNavButton(NavIcon::Forward, "Forward");
    reload_button_ = MakeNavButton(NavIcon::Reload, "Reload");
    extension_button_ =
        MakeNavButton(NavIcon::Extensions, "Extensions", kExtensionIconSize);
    extension_button_->SetBackgroundColor(
        Color(kOmniboxRed, kOmniboxGreen, kOmniboxBlue));
    extension_button_->SetInkDropEnabled(false);
    plus_button_ = MakeNavButton(NavIcon::NewTab, "New tab");

    nav_row->AddChildView(back_button_);
    nav_row->AddChildView(forward_button_);
    nav_row->AddChildView(reload_button_);
    nav_layout->SetFlexForView(back_button_, 0);
    nav_layout->SetFlexForView(forward_button_, 0);
    nav_layout->SetFlexForView(reload_button_, 0);

    auto address_row = CefPanel::CreatePanel(new FixedPanelDelegate(
        sidebar_width_ - 20, kSidebarAddressRowHeight, SidebarBackground()));
    address_row->SetBackgroundColor(SidebarBackground());
    auto address_row_layout =
        address_row->SetToBoxLayout(Box(true, 0, CefInsets(4, 0, 4, 0)));

    // Fill-only omnibox: URL + puzzle share address_bar_. No native stroke
    // overlay (it ghosted a second outline). Textfield outline/focus ring
    // stay theme-transparent.
    address_bar_ = CefPanel::CreatePanel(new FixedPanelDelegate(
        sidebar_width_ - 20, kAddressChipHeight,
        Color(kOmniboxRed, kOmniboxGreen, kOmniboxBlue)));
    address_bar_->SetBackgroundColor(
        Color(kOmniboxRed, kOmniboxGreen, kOmniboxBlue));
    auto field_settings = Box(true, 0, CefInsets());
    field_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
    auto address_bar_layout = address_bar_->SetToBoxLayout(field_settings);

    address_left_pad_ = MakeOmniboxPad(kOmniboxTextLeftPad);
    address_right_pad_ = MakeOmniboxPad(kExtensionFieldRightPad);

    address_bar_delegate_ = new AddressBarDelegate(this);
    address_ = CefTextfield::CreateTextfield(address_bar_delegate_);
    address_->SetText(kStartupURL);
    address_->SetFontList("Arial, 13px");
    address_->SetTextColor(
        Color(kOmniboxTextRed, kOmniboxTextGreen, kOmniboxTextBlue));
    address_->SetSelectionTextColor(
        Color(kOmniboxTextRed, kOmniboxTextGreen, kOmniboxTextBlue));
    address_->SetSelectionBackgroundColor(Color(37, 91, 86));
    address_->SetBackgroundColor(
        Color(kOmniboxRed, kOmniboxGreen, kOmniboxBlue));

    address_bar_->AddChildView(address_left_pad_);
    address_bar_->AddChildView(address_);
    address_bar_->AddChildView(extension_button_);
    address_bar_->AddChildView(address_right_pad_);
    address_bar_layout->SetFlexForView(address_, 1);
    address_bar_layout->SetFlexForView(address_left_pad_, 0);
    address_bar_layout->SetFlexForView(extension_button_, 0);
    address_bar_layout->SetFlexForView(address_right_pad_, 0);

    address_row->AddChildView(address_bar_);
    address_row_layout->SetFlexForView(address_bar_, 1);

    space_name_label_ = CefLabelButton::CreateLabelButton(this, "");
    space_name_label_->SetMinimumSize(
        CefSize(sidebar_width_ - 20, kSpaceNameLabelHeight));
    space_name_label_->SetMaximumSize(
        CefSize(sidebar_width_ - 20, kSpaceNameLabelHeight));
    space_name_label_->SetFontList("Arial, Bold 13px");
    space_name_label_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    space_name_label_->SetEnabledTextColors(Color(200, 210, 216));
    space_name_label_->SetBackgroundColor(SidebarBackground());
    space_name_label_->SetInkDropEnabled(false);

    body_ = CefPanel::CreatePanel(new ThemedPanelDelegate(SidebarBackground()));
    body_->SetBackgroundColor(SidebarBackground());
    body_layout_ = body_->SetToBoxLayout(Box(true, 0));

    sidebar_delegate_ =
        new FixedPanelDelegate(sidebar_width_, 720, SidebarBackground());
    sidebar_ = CefPanel::CreatePanel(sidebar_delegate_);
    sidebar_->SetBackgroundColor(SidebarBackground());
    auto sidebar_layout = sidebar_->SetToBoxLayout(
        Box(false, 0,
            CefInsets(0, kSidebarHorizontalPad, 10, kSidebarHorizontalPad)));

    auto pinned_label = MakeSidebarLabel("Pinned", kPinnedLabelHeight);
    pinned_container_ =
        CefPanel::CreatePanel(new ThemedPanelDelegate(SidebarBackground()));
    pinned_container_->SetBackgroundColor(SidebarBackground());
    pinned_container_->SetToBoxLayout(Box(false, kPinnedTabGap, CefInsets()));

    auto tabs_header = CefPanel::CreatePanel(new FixedPanelDelegate(
        sidebar_width_ - 20, kTabsLabelHeight, SidebarBackground()));
    tabs_header->SetBackgroundColor(SidebarBackground());
    auto tabs_header_layout =
        tabs_header->SetToBoxLayout(Box(true, 0, CefInsets()));
    auto tabs_label = MakeSidebarLabel("New Tabs", kTabsLabelHeight, 80);
    tabs_label->SetMinimumSize(CefSize(40, kTabsLabelHeight));
    tabs_label->SetMaximumSize(CefSize(10000, kTabsLabelHeight));
    tabs_header->AddChildView(plus_button_);
    tabs_header->AddChildView(tabs_label);
    tabs_header_layout->SetFlexForView(plus_button_, 0);
    tabs_header_layout->SetFlexForView(tabs_label, 1);

    tabs_container_ =
        CefPanel::CreatePanel(new ThemedPanelDelegate(SidebarBackground()));
    tabs_container_->SetBackgroundColor(SidebarBackground());
    tabs_container_->SetToBoxLayout(Box(false, kTabGap, CefInsets()));
    sidebar_->AddChildView(nav_row);
    sidebar_->AddChildView(address_row);
    sidebar_->AddChildView(space_name_label_);
    sidebar_->AddChildView(pinned_label);
    sidebar_->AddChildView(pinned_container_);
    sidebar_->AddChildView(tabs_header);
    sidebar_->AddChildView(tabs_container_);
    auto spacer = MakeSpacer();
    sidebar_->AddChildView(spacer);
    sidebar_layout->SetFlexForView(spacer, 1);

    BuildSpacesBar();
    if (spaces_bar_) {
      sidebar_->AddChildView(spaces_bar_);
      sidebar_layout->SetFlexForView(spaces_bar_, 0);
    }

    auto content_area = CefPanel::CreatePanel(new ThemedPanelDelegate(SidebarBackground()));
    content_area->SetBackgroundColor(SidebarBackground());
    auto content_area_layout = content_area->SetToBoxLayout(Box(
        false, 0,
        CefInsets(kContentInset, kContentInset, kContentInset, kContentInset)));

    content_well_ = CefPanel::CreatePanel(new ThemedPanelDelegate(SidebarBackground()));
    content_well_->SetBackgroundColor(SidebarBackground());
    content_well_layout_ =
        content_well_->SetToBoxLayout(Box(true, kDevToolsGap, CefInsets()));

    web_surface_delegate_ = new FixedPanelDelegate(4000, 1);
    web_surface_ = CefPanel::CreatePanel(web_surface_delegate_);
    web_surface_->SetBackgroundColor(
        Color(kWebSurfaceRed, kWebSurfaceGreen, kWebSurfaceBlue));
    web_surface_->SetToFillLayout();

    content_well_->AddChildView(web_surface_);
    content_well_layout_->SetFlexForView(web_surface_, 1);
    content_area->AddChildView(content_well_);
    content_area_layout->SetFlexForView(content_well_, 1);

    body_->AddChildView(sidebar_);
    body_->AddChildView(content_area);
    body_layout_->SetFlexForView(content_area, 1);

    root->AddChildView(body_);
    root_layout->SetFlexForView(body_, 1);

    window_->AddChildView(root);
    if (pending_browser_view_) {
      CreateSpace("Space 1");
      AddExistingTab(kStartupURL, pending_browser_view_, true);
      pending_browser_view_ = nullptr;
    } else {
      auto session = LoadSessionFromFile();
      if (!session.spaces.empty()) {
        for (size_t si = 0; si < session.spaces.size(); ++si) {
          const auto& ss = session.spaces[si];
          int space_index = CreateSpace(
              ss.name.empty() ? "Space " + std::to_string(si + 1) : ss.name);
          spaces_[space_index].icon = ss.icon;
          active_space_index_ = space_index;

          nav_history_ = ss.nav_history;
          for (const auto& sf : ss.folders) {
            PinnedFolder folder;
            folder.id = sf.id;
            folder.name = sf.name;
            pinned_folders_.push_back(std::move(folder));
            if (sf.id >= next_folder_id_) {
              next_folder_id_ = sf.id + 1;
            }
          }
          spaces_[space_index].next_folder_id = next_folder_id_;

          for (const auto& sp : ss.pinned_tabs) {
            AddPinnedTab(sp.url, sp.folder_id, false);
          }
          RebuildPinnedSection();

          const int restore_active = std::clamp(
              ss.active_tab_index, 0,
              std::max(0, static_cast<int>(ss.tabs.size()) - 1));
          for (size_t i = 0; i < ss.tabs.size(); ++i) {
            AddTab(ss.tabs[i].url,
                   static_cast<int>(i) == restore_active);
          }
          if (ss.tabs.empty()) {
            AddTab(kStartupURL, true);
          }
          SaveCurrentSpaceState();
        }

        active_space_index_ = std::clamp(
            session.active_space_index, 0,
            std::max(0, static_cast<int>(spaces_.size()) - 1));
        LoadSpaceState(active_space_index_);

        // Show only the active space's active tab
        for (auto& tab : tabs_) {
          if (tab.browser_view) {
            tab.browser_view->SetVisible(
                tab.id == spaces_[active_space_index_].active_tab_id);
          }
        }
        active_tab_id_ = spaces_[active_space_index_].active_tab_id;
        if (active_tab_id_ >= 0 && address_) {
          auto* tab = FindTab(active_tab_id_);
          if (tab) address_->SetText(tab->url);
        }

        // Rebuild sidebar panels for the active space only
        while (tabs_container_->GetChildViewCount() > 0) {
          tabs_container_->RemoveChildView(
              tabs_container_->GetChildViewAt(0));
        }
        for (auto& tab : tabs_) {
          if (tab.space_id != spaces_[active_space_index_].id) continue;
          if (tab.pinned) continue;
          tab.panel = nullptr;
          tab.button = nullptr;
          tab.panel_delegate = nullptr;
          auto panel = MakeTabPanel(tab.id, tab.title);
          tabs_container_->AddChildView(panel);
        }
        RebuildPinnedSectionForActiveSpace();
      } else {
        CreateSpace("Space 1");
        AddTab(kStartupURL, true);
      }
    }
    UpdateSpaceNameLabel();
    UpdateSpaceIndicatorUI();
    ConfigureDraggableRegions();
  }

  CefRefPtr<CefView> MakeSpacer() {
    auto spacer = CefPanel::CreatePanel(
        new FixedPanelDelegate(1, 1, SidebarBackground()));
    spacer->SetBackgroundColor(SidebarBackground());
    return spacer;
  }

  CefRefPtr<CefPanel> MakeTabPanel(int tab_id, const CefString& text) {
    auto panel_delegate = new FixedPanelDelegate(sidebar_width_, kTabHeight);
    auto panel = CefPanel::CreatePanel(panel_delegate);
    auto layout = panel->SetToBoxLayout(
        Box(true, 0, CefInsets(0, 0, 0, kTabTextInset)));

    auto button = CefLabelButton::CreateLabelButton(this, text);
    button->SetFontList("Arial, 13px");
    button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    button->SetAccessibleName("Tab");
    button->SetMinimumSize(CefSize(TabButtonWidth(), kTabHeight));

    panel->AddChildView(button);
    layout->SetFlexForView(button, 1);

    auto* tab = FindTab(tab_id);
    if (tab) {
      tab->panel = panel;
      tab->button = button;
      tab->panel_delegate = panel_delegate;
    }
    ApplyTabChrome(tab_id);
    return panel;
  }

  void SetTabLabel(int tab_id, const std::string& label) {
    auto* tab = FindTab(tab_id);
    if (!tab || !tab->button) {
      return;
    }

    tab->title = label.empty() ? "New tab" : label;
    tab->button->SetText(tab->title);
    ApplyTabChrome(tab_id);
  }

  // Chromium reports the URL as the title for documents without a <title>, so
  // fall back to the host label rather than showing a raw URL in the sidebar.
  void UpdateTabTitle(int tab_id, const std::string& title) {
    constexpr char kPinPrefix[] = "__hm_pin__:";
    constexpr char kRenameSpacePrefix[] = "__hm_space_rename__:";
    if (!title.empty() && title.rfind(kRenameSpacePrefix, 0) == 0) {
      HandleRenameSpaceCommand(title.substr(strlen(kRenameSpacePrefix)));
      return;
    }
    if (!title.empty() && title.rfind(kPinPrefix, 0) == 0) {
      HandlePinCommand(title.substr(strlen(kPinPrefix)));
      return;
    }

    auto* tab = FindTab(tab_id);
    if (!tab) {
      return;
    }

    const auto trimmed = Trim(title);
    if (trimmed.empty() || trimmed == tab->url) {
      SetTabLabel(tab_id, DisplayLabelForUrl(tab->url));
      return;
    }

    tab->has_page_title = true;
    SetTabLabel(tab_id, trimmed);
  }

  void UpdateTabUrl(int tab_id, const std::string& url) {
    constexpr char kNewTabPrefix[] = "hm-new-tab:";
    if (url.empty() || url.rfind(kNewTabPrefix, 0) == 0) {
      return;
    }

    auto* tab = FindTab(tab_id);
    if (!tab) {
      return;
    }

    tab->url = url;
    if (!tab->has_page_title) {
      SetTabLabel(tab_id, DisplayLabelForUrl(url));
    }
    if (tab_id == active_tab_id_ && address_) {
      address_->SetText(url);
    }
    if (tab_id == active_tab_id_) {
      UpdateNavigationButtons();
    }
  }

  void UpdateTabFavicon(int tab_id, CefRefPtr<CefImage> image) {
    auto* tab = FindTab(tab_id);
    if (!tab || !tab->button) {
      return;
    }

    tab->favicon = image;
    auto scaled = ScaleFavicon(image, 32.0f);
    if (scaled && !scaled->IsEmpty()) {
      tab->button->SetImage(CEF_BUTTON_STATE_NORMAL, scaled);
      tab->button->SetImage(CEF_BUTTON_STATE_HOVERED, scaled);
      tab->button->SetImage(CEF_BUTTON_STATE_PRESSED, scaled);
    } else {
      tab->button->SetImage(CEF_BUTTON_STATE_NORMAL, nullptr);
      tab->button->SetImage(CEF_BUTTON_STATE_HOVERED, nullptr);
      tab->button->SetImage(CEF_BUTTON_STATE_PRESSED, nullptr);
    }
  }

  void AddToNavHistory(const std::string& url) {
    if (url.empty()) {
      return;
    }

    constexpr char kNewTabPrefix[] = "hm-new-tab:";
    if (url.rfind(kNewTabPrefix, 0) == 0) {
      return;
    }

    const auto existing =
        std::find(nav_history_.begin(), nav_history_.end(), url);
    if (existing != nav_history_.end()) {
      nav_history_.erase(existing);
    }
    nav_history_.insert(nav_history_.begin(), url);
    constexpr size_t kMaxNavHistory = 15;
    if (nav_history_.size() > kMaxNavHistory) {
      nav_history_.resize(kMaxNavHistory);
    }
  }

  void ShowNewTabOverlay() {
    auto browser = GetBrowser();
    auto browser_view = ActiveBrowserView();
    if (!browser) {
      return;
    }

    new_tab_overlay_visible_ = true;
    if (browser_view) {
      browser_view->RequestFocus();
    }
    browser->GetMainFrame()->ExecuteJavaScript(
        NewTabOverlayScript(nav_history_), browser->GetMainFrame()->GetURL(),
        0);
  }

  void HideNewTabOverlay() {
    auto browser = GetBrowser();
    if (!browser) {
      return;
    }

    new_tab_overlay_visible_ = false;
    browser->GetMainFrame()->ExecuteJavaScript(
        "(() => { const overlay = "
        "document.getElementById('hm-new-tab-overlay');"
        "if (overlay) overlay.remove(); })();",
        browser->GetMainFrame()->GetURL(), 0);
  }

  bool IsNewTabOverlayVisible() const { return new_tab_overlay_visible_; }

  bool IsPinOverlayVisible() const { return pin_overlay_visible_; }

  void ShowPinOverlay() {
    auto browser = GetBrowser();
    auto browser_view = ActiveBrowserView();
    if (!browser) {
      return;
    }

    pin_overlay_visible_ = true;
    if (browser_view) {
      browser_view->RequestFocus();
    }
    browser->GetMainFrame()->ExecuteJavaScript(
        PinTabOverlayScript(pinned_folders_),
        browser->GetMainFrame()->GetURL(), 0);
  }

  void HidePinOverlay() {
    auto browser = GetBrowser();
    if (!browser) {
      return;
    }

    pin_overlay_visible_ = false;
    browser->GetMainFrame()->ExecuteJavaScript(
        "(() => { const overlay = "
        "document.getElementById('hm-pin-overlay');"
        "if (overlay) overlay.remove(); })();",
        browser->GetMainFrame()->GetURL(), 0);
  }

  bool IsExtensionPopupVisible() const { return extension_popup_visible_; }

  void ToggleExtensionPopup() {
    if (extension_popup_visible_) {
      HideExtensionPopup();
    } else {
      ShowExtensionPopup();
    }
  }

  void ShowExtensionPopup() {
    if (!window_ || !extension_button_) {
      return;
    }

    extension_popup_visible_ = true;

    CefPoint btn_origin(0, 0);
    extension_button_->ConvertPointToScreen(btn_origin);
    const auto btn_size = extension_button_->GetSize();
    const int popup_x = btn_origin.x + btn_size.width / 2 - kExtPopupWidth / 2;
    const int popup_y = btn_origin.y + btn_size.height + 6;

    ext_popup_window_delegate_ =
        new ExtensionPopupWindowDelegate(window_, this);
    CefWindow::CreateTopLevelWindow(ext_popup_window_delegate_);
    if (ext_popup_window_delegate_->popup_window()) {
      ext_popup_window_delegate_->popup_window()->SetBounds(
          CefRect(popup_x, popup_y, kExtPopupWidth, kExtPopupHeight));
      ext_popup_window_delegate_->popup_window()->Show();
    }
  }

  void HideExtensionPopup() {
    extension_popup_visible_ = false;
    if (ext_popup_window_delegate_ &&
        ext_popup_window_delegate_->popup_window()) {
      ext_popup_window_delegate_->popup_window()->Close();
    }
    ext_popup_window_delegate_ = nullptr;
  }

  void OnExtensionPopupClosed() {
    extension_popup_visible_ = false;
    ext_popup_window_delegate_ = nullptr;
  }

  void OnGetExtensionsClicked() {
    HideExtensionPopup();
    AddTab("https://chromewebstore.google.com/", true);
  }

  static std::string FoldersJson(const std::vector<PinnedFolder>& folders) {
    std::string json = "[";
    for (size_t i = 0; i < folders.size(); ++i) {
      if (i > 0) {
        json += ',';
      }
      json += "{\"id\":";
      json += std::to_string(folders[i].id);
      json += ",\"name\":\"";
      json += JsonEscape(folders[i].name);
      json += "\"}";
    }
    json += ']';
    return json;
  }

  static std::string PinTabOverlayScript(
      const std::vector<PinnedFolder>& folders) {
    return std::string(R"JS(
(() => {
  const existing = document.getElementById('hm-pin-overlay');
  if (existing) {
    const existingInput = existing.querySelector('input');
    if (existingInput) existingInput.focus();
    return;
  }

  const FOLDERS = )JS") +
           FoldersJson(folders) +
           R"JS(;

  const pinTo = (folderId) => {
    overlay.remove();
    document.title = '__hm_pin__:folder:' + folderId;
  };

  const pinUnfiled = () => {
    overlay.remove();
    document.title = '__hm_pin__:unfiled';
  };

  const createAndPin = (name) => {
    overlay.remove();
    document.title = '__hm_pin__:new:' + name;
  };

  const overlay = document.createElement('div');
  overlay.id = 'hm-pin-overlay';
  overlay.setAttribute('role', 'dialog');
  overlay.setAttribute('aria-modal', 'true');

  const style = document.createElement('style');
  style.textContent = `
    @keyframes hm-pin-backdrop-in {
      from { opacity: 0; }
      to { opacity: 1; }
    }
    @keyframes hm-pin-panel-in {
      from { opacity: 0; transform: scale(0.97) translateY(-12px); }
      to { opacity: 1; transform: scale(1) translateY(0); }
    }
    #hm-pin-overlay {
      position: fixed; inset: 0; z-index: 2147483647;
      display: flex; align-items: flex-start; justify-content: center;
      padding: min(18vh, 160px) 24px 24px; box-sizing: border-box;
      background: rgba(8, 10, 12, 0.55);
      backdrop-filter: blur(12px); -webkit-backdrop-filter: blur(12px);
      animation: hm-pin-backdrop-in 160ms ease-out;
    }
    #hm-pin-overlay .hm-panel {
      width: min(400px, calc(100vw - 48px)); box-sizing: border-box;
      border-radius: 14px; border: 1px solid rgba(255,255,255,0.08);
      background: rgba(24,28,32,0.82);
      box-shadow: 0 24px 64px rgba(0,0,0,0.45),
        0 0 0 1px rgba(255,255,255,0.04) inset;
      backdrop-filter: blur(24px) saturate(140%);
      -webkit-backdrop-filter: blur(24px) saturate(140%);
      overflow: hidden;
      animation: hm-pin-panel-in 200ms cubic-bezier(0.16,1,0.3,1);
    }
    #hm-pin-overlay .hm-title {
      padding: 14px 16px 10px;
      font: 600 14px/1.3 system-ui, -apple-system, sans-serif;
      color: rgb(236,240,244);
      border-bottom: 1px solid rgba(255,255,255,0.06);
    }
    #hm-pin-overlay .hm-options {
      display: flex; flex-direction: column; gap: 2px;
      padding: 8px; max-height: 300px; overflow-y: auto;
    }
    #hm-pin-overlay .hm-option {
      display: flex; align-items: center; gap: 10px;
      padding: 8px 10px; border-radius: 8px;
      border: none; background: transparent; cursor: pointer;
      color: rgb(220,226,232); text-align: left;
      font: 400 14px/1.3 system-ui, -apple-system, sans-serif;
      transition: background 100ms ease;
    }
    #hm-pin-overlay .hm-option:hover {
      background: rgba(255,255,255,0.06);
    }
    #hm-pin-overlay .hm-option.is-selected {
      background: rgba(255,255,255,0.1);
    }
    #hm-pin-overlay .hm-option-icon {
      flex: 0 0 auto; width: 20px; height: 20px;
      display: flex; align-items: center; justify-content: center;
      font-size: 14px; opacity: 0.6;
    }
    #hm-pin-overlay .hm-new-folder-row {
      display: flex; align-items: center; gap: 8px;
      padding: 6px 8px; border-top: 1px solid rgba(255,255,255,0.06);
    }
    #hm-pin-overlay .hm-new-folder-input {
      flex: 1; min-width: 0; height: 32px;
      border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;
      background: rgba(0,0,0,0.3); color: rgb(236,240,244);
      padding: 0 10px; outline: none;
      font: 400 13px/1.3 system-ui, -apple-system, sans-serif;
    }
    #hm-pin-overlay .hm-new-folder-input:focus {
      border-color: rgba(88,199,180,0.5);
    }
    #hm-pin-overlay .hm-new-folder-btn {
      flex: 0 0 auto; height: 32px; padding: 0 12px;
      border: none; border-radius: 6px;
      background: rgba(88,199,180,0.2); color: rgb(126,219,201);
      font: 500 13px/1 system-ui, -apple-system, sans-serif;
      cursor: pointer;
    }
    #hm-pin-overlay .hm-new-folder-btn:hover {
      background: rgba(88,199,180,0.3);
    }
  `;

  const panel = document.createElement('div');
  panel.className = 'hm-panel';

  const title = document.createElement('div');
  title.className = 'hm-title';
  title.textContent = 'Pin tab to\u2026';

  const options = document.createElement('div');
  options.className = 'hm-options';

  let selectedIndex = 0;
  let items = [];

  const renderOptions = () => {
    options.innerHTML = '';
    items = [];

    const unfiledBtn = document.createElement('button');
    unfiledBtn.type = 'button';
    unfiledBtn.className = 'hm-option' + (selectedIndex === 0 ? ' is-selected' : '');
    unfiledBtn.innerHTML = '<span class="hm-option-icon">\u{1F4CC}</span> No folder';
    unfiledBtn.addEventListener('click', pinUnfiled);
    options.appendChild(unfiledBtn);
    items.push({ el: unfiledBtn, action: pinUnfiled });

    FOLDERS.forEach((f, i) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      const idx = i + 1;
      btn.className = 'hm-option' + (selectedIndex === idx ? ' is-selected' : '');
      btn.innerHTML = '<span class="hm-option-icon">\u{1F4C1}</span> ' +
        f.name.replace(/</g, '&lt;');
      btn.addEventListener('click', () => pinTo(f.id));
      options.appendChild(btn);
      items.push({ el: btn, action: () => pinTo(f.id) });
    });

    updateSelection();
  };

  const updateSelection = () => {
    items.forEach((item, i) => {
      item.el.classList.toggle('is-selected', i === selectedIndex);
    });
    if (selectedIndex >= 0 && items[selectedIndex]) {
      items[selectedIndex].el.scrollIntoView({ block: 'nearest' });
    }
  };

  const newFolderRow = document.createElement('div');
  newFolderRow.className = 'hm-new-folder-row';

  const newFolderInput = document.createElement('input');
  newFolderInput.type = 'text';
  newFolderInput.className = 'hm-new-folder-input';
  newFolderInput.placeholder = 'New folder name';

  const newFolderBtn = document.createElement('button');
  newFolderBtn.type = 'button';
  newFolderBtn.className = 'hm-new-folder-btn';
  newFolderBtn.textContent = 'Create & Pin';

  const doCreateAndPin = () => {
    const name = newFolderInput.value.trim();
    if (name) createAndPin(name);
  };

  newFolderBtn.addEventListener('click', doCreateAndPin);
  newFolderInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); doCreateAndPin(); }
    if (e.key === 'Escape') { e.preventDefault(); overlay.remove(); }
  });

  newFolderRow.appendChild(newFolderInput);
  newFolderRow.appendChild(newFolderBtn);

  const close = () => overlay.remove();

  overlay.addEventListener('mousedown', (e) => {
    if (e.target === overlay) close();
  });

  overlay.addEventListener('keydown', (e) => {
    if (e.target === newFolderInput) return;
    if (e.key === 'Escape') { e.preventDefault(); close(); return; }
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      selectedIndex = Math.min(selectedIndex + 1, items.length - 1);
      updateSelection();
      return;
    }
    if (e.key === 'ArrowUp') {
      e.preventDefault();
      selectedIndex = Math.max(selectedIndex - 1, 0);
      updateSelection();
      return;
    }
    if (e.key === 'Enter') {
      e.preventDefault();
      if (selectedIndex >= 0 && items[selectedIndex]) {
        items[selectedIndex].action();
      }
      return;
    }
    if (e.key === 'Tab') {
      e.preventDefault();
      newFolderInput.focus();
    }
  });

  panel.appendChild(title);
  panel.appendChild(options);
  panel.appendChild(newFolderRow);
  overlay.appendChild(style);
  overlay.appendChild(panel);
  document.documentElement.appendChild(overlay);
  renderOptions();
  overlay.focus();
  overlay.setAttribute('tabindex', '-1');
  overlay.focus();
})();
)JS";
  }

  void HandlePinCommand(const std::string& command) {
    auto* tab = ActiveTab();
    if (!tab) {
      return;
    }

    pin_overlay_visible_ = false;

    if (command == "unfiled") {
      PinTab(tab->id, -1);
    } else if (command.rfind("folder:", 0) == 0) {
      int folder_id = std::atoi(command.substr(7).c_str());
      PinTab(tab->id, folder_id);
    } else if (command.rfind("new:", 0) == 0) {
      std::string folder_name = command.substr(4);
      if (!folder_name.empty()) {
        int folder_id = CreatePinnedFolder(folder_name);
        PinTab(tab->id, folder_id);
      }
    }

    auto browser = GetBrowser();
    if (browser && tab->has_page_title) {
      std::string restore =
          "document.title = '" + JsonEscape(tab->title) + "';";
      browser->GetMainFrame()->ExecuteJavaScript(
          restore, browser->GetMainFrame()->GetURL(), 0);
    }
  }

  void TogglePinActiveTab() {
    auto* tab = ActiveTab();
    if (!tab) {
      return;
    }

    if (tab->pinned) {
      UnpinTab(tab->id);
    } else {
      ShowPinOverlay();
    }
  }

  int CreatePinnedFolder(const std::string& name) {
    PinnedFolder folder;
    folder.id = next_folder_id_++;
    folder.name = name;
    pinned_folders_.push_back(std::move(folder));
    RebuildPinnedSection();
    return pinned_folders_.back().id;
  }

  void PinTab(int tab_id, int folder_id) {
    auto* tab = FindTab(tab_id);
    if (!tab || tab->pinned) {
      return;
    }

    tab->pinned = true;
    tab->folder_id = folder_id;

    if (tab->panel && tabs_container_) {
      tabs_container_->RemoveChildView(tab->panel);
      tab->panel = nullptr;
      tab->button = nullptr;
      tab->panel_delegate = nullptr;
    }

    RebuildPinnedSection();
    SyncSidebarTabClickMonitorTabCount();
    UpdatePinnedSectionHeight();
    RelayoutSidebar();
  }

  void UnpinTab(int tab_id) {
    auto* tab = FindTab(tab_id);
    if (!tab || !tab->pinned) {
      return;
    }

    tab->pinned = false;
    tab->folder_id = -1;

    if (tab->panel) {
      tab->panel = nullptr;
      tab->button = nullptr;
      tab->panel_delegate = nullptr;
    }

    auto panel = MakeTabPanel(tab_id, tab->title);
    tabs_container_->AddChildView(panel);

    RebuildPinnedSection();
    SyncSidebarTabClickMonitorTabCount();
    UpdatePinnedSectionHeight();
    RelayoutSidebar();
  }

  CefRefPtr<CefPanel> MakePinnedTabPanel(int tab_id, const CefString& text) {
    auto panel_delegate = new FixedPanelDelegate(
        sidebar_width_ - 2 * kSidebarHorizontalPad, kPinnedTabHeight);
    auto panel = CefPanel::CreatePanel(panel_delegate);
    auto layout = panel->SetToBoxLayout(
        Box(true, 0, CefInsets(0, 4, 0, 4)));

    auto button = CefLabelButton::CreateLabelButton(this, text);
    button->SetFontList("Arial, 12px");
    button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    button->SetAccessibleName("Pinned tab");
    button->SetMinimumSize(CefSize(
        sidebar_width_ - 2 * kSidebarHorizontalPad - 8, kPinnedTabHeight));
    button->SetBackgroundColor(kTransparent);
    button->SetTextColor(CEF_BUTTON_STATE_NORMAL, kTabTextColor);
    button->SetTextColor(CEF_BUTTON_STATE_HOVERED, kTabTextColor);
    button->SetTextColor(CEF_BUTTON_STATE_PRESSED, kTabTextColor);
    button->SetEnabledTextColors(kTabTextColor);

    panel->AddChildView(button);
    layout->SetFlexForView(button, 1);

    auto* tab = FindTab(tab_id);
    if (tab) {
      tab->panel = panel;
      tab->button = button;
      tab->panel_delegate = panel_delegate;

      if (tab->favicon && !tab->favicon->IsEmpty()) {
        auto scaled = ScaleFavicon(tab->favicon, 24.0f);
        if (scaled && !scaled->IsEmpty()) {
          button->SetImage(CEF_BUTTON_STATE_NORMAL, scaled);
          button->SetImage(CEF_BUTTON_STATE_HOVERED, scaled);
          button->SetImage(CEF_BUTTON_STATE_PRESSED, scaled);
        }
      }
    }

    panel->SetBackgroundColor(kTransparent);
    return panel;
  }

  CefRefPtr<CefPanel> MakeFolderHeader(const PinnedFolder& folder) {
    auto panel_delegate = new FixedPanelDelegate(
        sidebar_width_ - 2 * kSidebarHorizontalPad, kPinnedFolderHeaderHeight,
        SidebarBackground());
    auto panel = CefPanel::CreatePanel(panel_delegate);
    auto layout = panel->SetToBoxLayout(
        Box(true, 0, CefInsets(0, 4, 0, 4)));

    auto label = CefLabelButton::CreateLabelButton(this, folder.name);
    label->SetFontList("Arial, Bold 11px");
    label->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    label->SetMinimumSize(CefSize(80, kPinnedFolderHeaderHeight));
    label->SetEnabledTextColors(Color(154, 166, 173));
    label->SetTextColor(CEF_BUTTON_STATE_DISABLED, Color(154, 166, 173));
    label->SetBackgroundColor(SidebarBackground());
    label->SetEnabled(false);

    panel->AddChildView(label);
    layout->SetFlexForView(label, 1);

    panel->SetBackgroundColor(SidebarBackground());
    return panel;
  }

  void RebuildPinnedSection() {
    if (!pinned_container_) {
      return;
    }

    while (pinned_container_->GetChildViewCount() > 0) {
      pinned_container_->RemoveChildView(
          pinned_container_->GetChildViewAt(0));
    }

    for (auto& folder : pinned_folders_) {
      folder.header_panel = nullptr;
      folder.content_panel = nullptr;
    }

    const int space_id = spaces_.empty() ? 0 : spaces_[active_space_index_].id;
    std::vector<int> unfiled_ids;
    std::map<int, std::vector<int>> folder_tab_ids;

    for (auto& tab : tabs_) {
      if (!tab.pinned || tab.space_id != space_id) {
        continue;
      }
      if (tab.folder_id < 0) {
        unfiled_ids.push_back(tab.id);
      } else {
        folder_tab_ids[tab.folder_id].push_back(tab.id);
      }
    }

    for (int tab_id : unfiled_ids) {
      auto* tab = FindTab(tab_id);
      if (!tab) {
        continue;
      }
      auto panel = MakePinnedTabPanel(tab_id, tab->title);
      pinned_container_->AddChildView(panel);
    }

    for (auto& folder : pinned_folders_) {
      auto header = MakeFolderHeader(folder);
      folder.header_panel = header;
      pinned_container_->AddChildView(header);

      auto content = CefPanel::CreatePanel(
          new ThemedPanelDelegate(SidebarBackground()));
      content->SetBackgroundColor(SidebarBackground());
      content->SetToBoxLayout(Box(false, kPinnedTabGap, CefInsets(0, 8, 0, 0)));
      folder.content_panel = content;

      auto it = folder_tab_ids.find(folder.id);
      if (it != folder_tab_ids.end()) {
        for (int tab_id : it->second) {
          auto* tab = FindTab(tab_id);
          if (!tab) {
            continue;
          }
          auto tab_panel = MakePinnedTabPanel(tab_id, tab->title);
          content->AddChildView(tab_panel);
        }
      }

      pinned_container_->AddChildView(content);
    }

    if (unfiled_ids.empty() && folder_tab_ids.empty()) {
      auto empty = CefPanel::CreatePanel(new FixedPanelDelegate(
          sidebar_width_ - 20, kPinnedEmptyHeight, SidebarBackground()));
      empty->SetBackgroundColor(SidebarBackground());
      pinned_container_->AddChildView(empty);
    }

    UpdatePinnedSectionHeight();
    RelayoutSidebar();
  }

  int PinnedSectionPixelHeight() const {
    int height = kPinnedLabelHeight;
    int pinned_count = 0;
    const int space_id = spaces_.empty() ? 0 : spaces_[active_space_index_].id;

    for (const auto& tab : tabs_) {
      if (tab.pinned && tab.space_id == space_id) {
        pinned_count++;
      }
    }

    if (pinned_count == 0 && pinned_folders_.empty()) {
      height += kPinnedEmptyHeight;
      return height;
    }

    int unfiled = 0;
    for (const auto& tab : tabs_) {
      if (tab.pinned && tab.folder_id < 0 && tab.space_id == space_id) {
        unfiled++;
      }
    }

    height += unfiled * (kPinnedTabHeight + kPinnedTabGap);

    for (const auto& folder : pinned_folders_) {
      height += kPinnedFolderHeaderHeight;
      int count = 0;
      for (const auto& tab : tabs_) {
        if (tab.pinned && tab.folder_id == folder.id &&
            tab.space_id == space_id) {
          count++;
        }
      }
      height += count * (kPinnedTabHeight + kPinnedTabGap);
    }

    return height;
  }

  void UpdatePinnedSectionHeight() {
    pinned_section_height_ = PinnedSectionPixelHeight();
#if defined(OS_MAC)
    if (window_) {
      UpdateSidebarTabClickMonitorPinnedHeight(
          window_->GetWindowHandle(), pinned_section_height_);
    }
#endif
  }

  void RelayoutSidebar() {
    if (pinned_container_) {
      pinned_container_->InvalidateLayout();
    }
    if (tabs_container_) {
      tabs_container_->InvalidateLayout();
    }
    if (sidebar_) {
      sidebar_->InvalidateLayout();
      sidebar_->Layout();
    }
  }

  // -----------------------------------------------------------------------
  // Spaces management
  // -----------------------------------------------------------------------

  Space* ActiveSpace() {
    if (active_space_index_ >= 0 &&
        active_space_index_ < static_cast<int>(spaces_.size())) {
      return &spaces_[active_space_index_];
    }
    return nullptr;
  }

  CefRefPtr<CefRequestContext> ActiveSpaceRequestContext() {
    auto* space = ActiveSpace();
    return space ? space->request_context : nullptr;
  }

  int CreateSpace(const std::string& name) {
    Space space;
    space.id = next_space_id_++;
    space.name =
        name.empty() ? "Space " + std::to_string(space.id) : name;

    if (spaces_.empty()) {
      space.request_context = nullptr;
    } else {
      CefRequestContextSettings ctx_settings;
      auto cache_path = SpaceCachePath(space.id);
      if (!cache_path.empty()) {
        CefString(&ctx_settings.cache_path) = cache_path;
      }
      space.request_context =
          CefRequestContext::CreateContext(ctx_settings, nullptr);
    }

    spaces_.push_back(std::move(space));
    return static_cast<int>(spaces_.size()) - 1;
  }

  void SaveCurrentSpaceState() {
    auto* space = ActiveSpace();
    if (!space) return;

    space->active_tab_id = active_tab_id_;
    space->next_tab_id = next_tab_id_;
    space->next_folder_id = next_folder_id_;
  }

  void LoadSpaceState(int index) {
    if (index < 0 || index >= static_cast<int>(spaces_.size())) return;
    auto& space = spaces_[index];
    next_tab_id_ = space.next_tab_id;
    next_folder_id_ = space.next_folder_id;
  }

  void SwitchToSpace(int target_index) {
    if (target_index == active_space_index_) return;
    if (target_index < 0 ||
        target_index >= static_cast<int>(spaces_.size())) return;

    // Hide current space's browser views
    for (auto& tab : tabs_) {
      if (tab.browser_view) {
        tab.browser_view->SetVisible(false);
      }
    }

    // Save current state
    SaveCurrentSpaceState();

    // Detach tab panels from sidebar
    while (tabs_container_ && tabs_container_->GetChildViewCount() > 0) {
      tabs_container_->RemoveChildView(tabs_container_->GetChildViewAt(0));
    }
    while (pinned_container_ && pinned_container_->GetChildViewCount() > 0) {
      pinned_container_->RemoveChildView(
          pinned_container_->GetChildViewAt(0));
    }

    // Save tabs/folders to current space (move ownership of panels)
    auto* old_space = ActiveSpace();
    if (old_space) {
      // Clear panel references (they were just removed from containers)
      for (auto& tab : tabs_) {
        tab.panel = nullptr;
        tab.button = nullptr;
        tab.panel_delegate = nullptr;
      }
    }

    // Swap tab/folder vectors: move current to storage, load target
    // We use a flat tabs_ that survives across spaces, keyed by space_id.
    // No need to move tabs in/out; just toggle visibility in sidebar.

    active_space_index_ = target_index;
    LoadSpaceState(target_index);

    // Rebuild sidebar panels for the target space's tabs
    for (auto& tab : tabs_) {
      if (tab.space_id != spaces_[active_space_index_].id) continue;
      if (tab.pinned) continue;
      auto panel = MakeTabPanel(tab.id, tab.title);
      tabs_container_->AddChildView(panel);
    }

    // Rebuild pinned section
    RebuildPinnedSectionForActiveSpace();

    // Activate the correct tab
    int tab_to_activate = spaces_[active_space_index_].active_tab_id;
    bool found = false;
    for (const auto& tab : tabs_) {
      if (tab.id == tab_to_activate &&
          tab.space_id == spaces_[active_space_index_].id) {
        found = true;
        break;
      }
    }
    if (!found) {
      tab_to_activate = -1;
      for (const auto& tab : tabs_) {
        if (tab.space_id == spaces_[active_space_index_].id) {
          tab_to_activate = tab.id;
          break;
        }
      }
    }

    active_tab_id_ = -1;
    if (tab_to_activate >= 0) {
      ActivateTab(tab_to_activate);
    } else {
      AddTab(kStartupURL, true);
    }

    // Refresh tab metadata from live browsers
    for (auto& tab : tabs_) {
      if (tab.space_id != spaces_[active_space_index_].id) continue;
      if (!tab.browser_view) continue;
      auto browser = tab.browser_view->GetBrowser();
      if (!browser) continue;
      auto frame = browser->GetMainFrame();
      if (frame) {
        std::string live_url = frame->GetURL().ToString();
        if (!live_url.empty() &&
            live_url.rfind("hm-new-tab:", 0) != 0) {
          tab.url = live_url;
        }
      }
    }

    SyncSidebarTabClickMonitorTabCount();
    UpdatePinnedSectionHeight();
    UpdateSpaceNameLabel();
    UpdateSpaceIndicatorUI();
    RelayoutSidebar();
    ConfigureDraggableRegions();
  }

  void SwitchToSpaceNumber(int space_number) {
    int target_index = space_number - 1;
    if (target_index < 0 ||
        target_index >= static_cast<int>(spaces_.size())) {
      return;
    }
    SwitchToSpace(target_index);
  }

  void NavigateSpaces(int direction) {
    int target = active_space_index_ + direction;
    if (target < 0 || target >= static_cast<int>(spaces_.size())) {
      return;
    }
    SwitchToSpace(target);
  }

  void RebuildPinnedSectionForActiveSpace() {
    if (!pinned_container_) return;

    while (pinned_container_->GetChildViewCount() > 0) {
      pinned_container_->RemoveChildView(
          pinned_container_->GetChildViewAt(0));
    }
    for (auto& folder : pinned_folders_) {
      folder.header_panel = nullptr;
      folder.content_panel = nullptr;
    }

    const int space_id = spaces_[active_space_index_].id;

    std::vector<int> unfiled_ids;
    std::map<int, std::vector<int>> folder_tab_ids;
    for (auto& tab : tabs_) {
      if (!tab.pinned || tab.space_id != space_id) continue;
      if (tab.folder_id < 0) {
        unfiled_ids.push_back(tab.id);
      } else {
        folder_tab_ids[tab.folder_id].push_back(tab.id);
      }
    }

    for (int tab_id : unfiled_ids) {
      auto* tab = FindTab(tab_id);
      if (!tab) continue;
      auto panel = MakePinnedTabPanel(tab_id, tab->title);
      pinned_container_->AddChildView(panel);
    }

    for (auto& folder : pinned_folders_) {
      auto header = MakeFolderHeader(folder);
      folder.header_panel = header;
      pinned_container_->AddChildView(header);

      auto content = CefPanel::CreatePanel(
          new ThemedPanelDelegate(SidebarBackground()));
      content->SetBackgroundColor(SidebarBackground());
      content->SetToBoxLayout(Box(false, kPinnedTabGap, CefInsets(0, 8, 0, 0)));
      folder.content_panel = content;

      auto it = folder_tab_ids.find(folder.id);
      if (it != folder_tab_ids.end()) {
        for (int tab_id : it->second) {
          auto* tab = FindTab(tab_id);
          if (!tab) continue;
          auto tab_panel = MakePinnedTabPanel(tab_id, tab->title);
          content->AddChildView(tab_panel);
        }
      }
      pinned_container_->AddChildView(content);
    }

    if (unfiled_ids.empty() && folder_tab_ids.empty()) {
      auto empty = CefPanel::CreatePanel(new FixedPanelDelegate(
          sidebar_width_ - 20, kPinnedEmptyHeight, SidebarBackground()));
      empty->SetBackgroundColor(SidebarBackground());
      pinned_container_->AddChildView(empty);
    }

    UpdatePinnedSectionHeight();
  }

  void BuildSpacesBar() {
    if (!sidebar_) return;

    spaces_bar_ = CefPanel::CreatePanel(
        new ThemedPanelDelegate(SidebarBackground()));
    spaces_bar_->SetBackgroundColor(SidebarBackground());

    UpdateSpaceIndicatorUI();
  }

  static std::string SpaceButtonLabel(const Space& space) {
    static const std::map<std::string, std::string> icon_glyphs = {
      {"home", "\xE2\x8C\x82"},       // ⌂
      {"star", "\xE2\x98\x85"},        // ★
      {"bookmark", "\xE2\x96\x8A"},    // ▊
      {"heart", "\xE2\x99\xA5"},       // ♥
      {"flag", "\xE2\x9A\x91"},        // ⚑
      {"briefcase", "\xE2\x96\xA3"},   // ▣
      {"code", "\xE2\x9F\xA8\xE2\x9F\xA9"}, // ⟨⟩
      {"terminal", "\xE2\x96\xBA"},    // ►
      {"document", "\xE2\x96\xA4"},    // ▤
      {"chart", "\xE2\x96\x81"},       // ▁
      {"music", "\xE2\x99\xAB"},       // ♫
      {"camera", "\xE2\x97\x8E"},      // ◎
      {"film", "\xE2\x96\xA0"},        // ■
      {"headphones", "\xE2\x99\xAA"},  // ♪
      {"mic", "\xE2\x97\x89"},         // ◉
      {"coffee", "\xE2\x98\x95"},      // ☕
      {"plane", "\xE2\x9C\x88"},       // ✈
      {"globe", "\xE2\x97\x8C"},       // ◌
      {"sun", "\xE2\x98\x80"},         // ☀
      {"moon", "\xE2\x98\xBE"},        // ☾
      {"gear", "\xE2\x9A\x99"},        // ⚙
      {"lightning", "\xE2\x9A\xA1"},   // ⚡
      {"shield", "\xE2\x9A\x94"},      // ⚔
      {"wifi", "\xE2\x97\x87"},        // ◇
      {"cloud", "\xE2\x98\x81"},       // ☁
    };
    if (!space.icon.empty()) {
      auto it = icon_glyphs.find(space.icon);
      if (it != icon_glyphs.end()) {
        return it->second;
      }
    }
    // No icon set — show a compact abbreviation (first letter + space number).
    if (space.name.size() <= 2) return space.name;
    std::string abbrev;
    abbrev += static_cast<char>(toupper(space.name[0]));
    for (size_t i = 1; i < space.name.size(); ++i) {
      if (isdigit(space.name[i])) {
        abbrev += space.name[i];
        break;
      }
    }
    if (abbrev.size() == 1) abbrev += static_cast<char>(toupper(space.name[1]));
    return abbrev;
  }

  void UpdateSpaceNameLabel() {
    if (!space_name_label_) return;
    auto* space = ActiveSpace();
    const std::string name = space ? space->name : "";
    space_name_label_->SetText(name);
    space_name_label_->SetAccessibleName(name);
    space_name_label_->SetTooltipText(name);
  }

  void UpdateSpaceIndicatorUI() {
    if (!spaces_bar_) return;

    while (spaces_bar_->GetChildViewCount() > 0) {
      spaces_bar_->RemoveChildView(spaces_bar_->GetChildViewAt(0));
    }
    space_buttons_.clear();

    spaces_bar_->SetVisible(true);

    auto bar_layout = spaces_bar_->SetToBoxLayout(
        Box(true, 4, CefInsets(4, 4, 4, 4)));

    auto left_spacer = CefPanel::CreatePanel(
        new ThemedPanelDelegate(SidebarBackground()));
    left_spacer->SetBackgroundColor(SidebarBackground());
    spaces_bar_->AddChildView(left_spacer);
    bar_layout->SetFlexForView(left_spacer, 1);

    for (size_t i = 0; i < spaces_.size(); ++i) {
      const auto label = SpaceButtonLabel(spaces_[i]);
      auto btn = CefLabelButton::CreateLabelButton(this, label);
      btn->SetFontList("Arial, Bold 14px");
      btn->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
      btn->SetMinimumSize(CefSize(32, 28));
      btn->SetMaximumSize(CefSize(32, 28));
      btn->SetAccessibleName(spaces_[i].name);
      btn->SetTooltipText(spaces_[i].name);

      if (static_cast<int>(i) == active_space_index_) {
        btn->SetEnabledTextColors(Color(236, 240, 244));
      } else {
        btn->SetEnabledTextColors(Color(154, 166, 173));
      }
      btn->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
      btn->SetInkDropEnabled(false);

      spaces_bar_->AddChildView(btn);
      bar_layout->SetFlexForView(btn, 0);
      space_buttons_.push_back(btn);
    }

    auto right_spacer = CefPanel::CreatePanel(
        new ThemedPanelDelegate(SidebarBackground()));
    right_spacer->SetBackgroundColor(SidebarBackground());
    spaces_bar_->AddChildView(right_spacer);
    bar_layout->SetFlexForView(right_spacer, 1);

    auto plus_space = CefLabelButton::CreateLabelButton(this, "+");
    plus_space->SetFontList("Arial, Bold 18px");
    plus_space->SetMinimumSize(CefSize(28, 28));
    plus_space->SetMaximumSize(CefSize(28, 28));
    plus_space->SetAccessibleName("New Space");
    plus_space->SetTooltipText("New Space");
    plus_space->SetEnabledTextColors(Color(154, 166, 173));
    plus_space->SetBackgroundColor(CefColorSetARGB(0, 0, 0, 0));
    plus_space->SetInkDropEnabled(false);
    spaces_bar_->AddChildView(plus_space);
    bar_layout->SetFlexForView(plus_space, 0);
    space_buttons_.push_back(plus_space);

    spaces_bar_->InvalidateLayout();
    spaces_bar_->Layout();
  }

  bool IsSpaceButton(CefRefPtr<CefButton> button) const {
    for (const auto& btn : space_buttons_) {
      if (btn == button) return true;
    }
    return false;
  }

  void HandleSpaceButtonPress(CefRefPtr<CefButton> button) {
    for (size_t i = 0; i < space_buttons_.size(); ++i) {
      if (space_buttons_[i] != button) continue;

      if (static_cast<int>(i) < static_cast<int>(spaces_.size())) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - space_last_click_time_);

        if (static_cast<int>(i) == space_last_click_index_ &&
            elapsed.count() < 400) {
          space_last_click_index_ = -1;
          SwitchToSpace(static_cast<int>(i));
          ShowRenameSpaceOverlay();
        } else {
          space_last_click_index_ = static_cast<int>(i);
          space_last_click_time_ = now;
          SwitchToSpace(static_cast<int>(i));
        }
      } else {
        space_last_click_index_ = -1;
        CreateSpace("");
        SwitchToSpace(static_cast<int>(spaces_.size()) - 1);
      }
      return;
    }
  }

  void ShowRenameSpaceOverlay() {
    auto browser = GetBrowser();
    auto browser_view = ActiveBrowserView();
    if (!browser) return;

    rename_space_overlay_visible_ = true;
    if (browser_view) {
      browser_view->RequestFocus();
    }
    auto* space = ActiveSpace();
    const std::string current_name = space ? space->name : "";
    const std::string current_icon = space ? space->icon : "";
    browser->GetMainFrame()->ExecuteJavaScript(
        RenameSpaceOverlayScript(current_name, current_icon),
        browser->GetMainFrame()->GetURL(), 0);
  }

  void HideRenameSpaceOverlay() {
    auto browser = GetBrowser();
    if (!browser) return;
    rename_space_overlay_visible_ = false;
    browser->GetMainFrame()->ExecuteJavaScript(
        "(() => { const o = document.getElementById('hm-rename-space-overlay');"
        " if (o) o.remove(); })();",
        browser->GetMainFrame()->GetURL(), 0);
  }

  void HandleRenameSpaceCommand(const std::string& payload) {
    rename_space_overlay_visible_ = false;

    std::string name_part = payload;
    std::string icon_part;
    auto tab_pos = payload.find('\t');
    if (tab_pos != std::string::npos) {
      name_part = payload.substr(0, tab_pos);
      icon_part = payload.substr(tab_pos + 1);
    }

    auto trimmed = Trim(name_part);
    if (trimmed.empty()) return;

    auto* space = ActiveSpace();
    if (!space) return;
    space->name = trimmed;
    space->icon = Trim(icon_part);
    UpdateSpaceNameLabel();
    UpdateSpaceIndicatorUI();

    auto browser = GetBrowser();
    auto* tab = ActiveTab();
    if (browser && tab && tab->has_page_title) {
      std::string restore =
          "document.title = '" + JsonEscape(tab->title) + "';";
      browser->GetMainFrame()->ExecuteJavaScript(
          restore, browser->GetMainFrame()->GetURL(), 0);
    }
  }

  static std::string RenameSpaceOverlayScript(
      const std::string& current_name, const std::string& current_icon) {
    std::string s;
    s += "(() => {\n";
    s += "  const existing = document.getElementById('hm-rename-space-overlay');\n";
    s += "  if (existing) { const inp = existing.querySelector('.hm-input'); if (inp) inp.focus(); return; }\n";
    s += "  const CURRENT_NAME = \"" + JsonEscape(current_name) + "\";\n";
    s += "  const CURRENT_ICON = \"" + JsonEscape(current_icon) + "\";\n";
    s += R"HMJS(

  const ICONS = {
    'General': [
      { id: 'home', svg: '<path d="M3 12l9-9 9 9M5 10v10a1 1 0 001 1h3a1 1 0 001-1v-4a1 1 0 011-1h2a1 1 0 011 1v4a1 1 0 001 1h3a1 1 0 001-1V10"/>' },
      { id: 'star', svg: '<path d="M12 2l3.09 6.26L22 9.27l-5 4.87L18.18 22 12 18.56 5.82 22 7 14.14l-5-4.87 6.91-1.01z"/>' },
      { id: 'bookmark', svg: '<path d="M19 21l-7-5-7 5V5a2 2 0 012-2h10a2 2 0 012 2z"/>' },
      { id: 'heart', svg: '<path d="M20.84 4.61a5.5 5.5 0 00-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 00-7.78 7.78L12 21.23l8.84-8.84a5.5 5.5 0 000-7.78z"/>' },
      { id: 'flag', svg: '<path d="M4 15s1-1 4-1 5 2 8 2 4-1 4-1V3s-1 1-4 1-5-2-8-2-4 1-4 1z"/><line x1="4" y1="22" x2="4" y2="15"/>' }
    ],
    'Work': [
      { id: 'briefcase', svg: '<rect x="2" y="7" width="20" height="14" rx="2"/><path d="M16 7V5a2 2 0 00-2-2h-4a2 2 0 00-2 2v2"/>' },
      { id: 'code', svg: '<polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/>' },
      { id: 'terminal', svg: '<polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/>' },
      { id: 'document', svg: '<path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/>' },
      { id: 'chart', svg: '<line x1="18" y1="20" x2="18" y2="10"/><line x1="12" y1="20" x2="12" y2="4"/><line x1="6" y1="20" x2="6" y2="14"/>' }
    ],
    'Media': [
      { id: 'music', svg: '<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>' },
      { id: 'camera', svg: '<path d="M23 19a2 2 0 01-2 2H3a2 2 0 01-2-2V8a2 2 0 012-2h4l2-3h6l2 3h4a2 2 0 012 2z"/><circle cx="12" cy="13" r="4"/>' },
      { id: 'film', svg: '<rect x="2" y="2" width="20" height="20" rx="2.18"/><line x1="7" y1="2" x2="7" y2="22"/><line x1="17" y1="2" x2="17" y2="22"/><line x1="2" y1="12" x2="22" y2="12"/><line x1="2" y1="7" x2="7" y2="7"/><line x1="2" y1="17" x2="7" y2="17"/><line x1="17" y1="7" x2="22" y2="7"/><line x1="17" y1="17" x2="22" y2="17"/>' },
      { id: 'headphones', svg: '<path d="M3 18v-6a9 9 0 0118 0v6"/><path d="M21 19a2 2 0 01-2 2h-1a2 2 0 01-2-2v-3a2 2 0 012-2h3zM3 19a2 2 0 002 2h1a2 2 0 002-2v-3a2 2 0 00-2-2H3z"/>' },
      { id: 'mic', svg: '<path d="M12 1a3 3 0 00-3 3v8a3 3 0 006 0V4a3 3 0 00-3-3z"/><path d="M19 10v2a7 7 0 01-14 0v-2"/><line x1="12" y1="19" x2="12" y2="23"/><line x1="8" y1="23" x2="16" y2="23"/>' }
    ],
    'Lifestyle': [
      { id: 'coffee', svg: '<path d="M18 8h1a4 4 0 010 8h-1"/><path d="M2 8h16v9a4 4 0 01-4 4H6a4 4 0 01-4-4V8z"/><line x1="6" y1="1" x2="6" y2="4"/><line x1="10" y1="1" x2="10" y2="4"/><line x1="14" y1="1" x2="14" y2="4"/>' },
      { id: 'plane', svg: '<path d="M17.8 19.2L16 11l3.5-3.5C21 6 21.5 4 21 3c-1-.5-3 0-4.5 1.5L13 8 4.8 6.2c-.5-.1-.9.1-1.1.5l-.3.5 7.4 4.4-3.8 3.8-2.2-.7-.6.6 2.9 2 2 2.9.6-.6-.7-2.2 3.8-3.8 4.4 7.4.5-.3c.4-.2.6-.6.5-1.1z"/>' },
      { id: 'globe', svg: '<circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 014 10 15.3 15.3 0 01-4 10 15.3 15.3 0 01-4-10 15.3 15.3 0 014-10z"/>' },
      { id: 'sun', svg: '<circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/>' },
      { id: 'moon', svg: '<path d="M21 12.79A9 9 0 1111.21 3 7 7 0 0021 12.79z"/>' }
    ],
    'Tech': [
      { id: 'gear', svg: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 010 2.83 2 2 0 01-2.83 0l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 01-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 012.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z"/>' },
      { id: 'lightning', svg: '<polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/>' },
      { id: 'shield', svg: '<path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>' },
      { id: 'wifi', svg: '<path d="M5 12.55a11 11 0 0114.08 0"/><path d="M1.42 9a16 16 0 0121.16 0"/><path d="M8.53 16.11a6 6 0 016.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/>' },
      { id: 'cloud', svg: '<path d="M18 10h-1.26A8 8 0 109 20h9a5 5 0 000-10z"/>' }
    ]
  };

  let selectedIcon = CURRENT_ICON;
  let gridVisible = false;

  const makeSvg = (svgInner, size) =>
    `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${svgInner}</svg>`;

  const findIconSvg = (iconId) => {
    for (const cat of Object.values(ICONS)) {
      for (const ic of cat) {
        if (ic.id === iconId) return ic.svg;
      }
    }
    return null;
  };

  const overlay = document.createElement('div');
  overlay.id = 'hm-rename-space-overlay';
  overlay.setAttribute('role', 'dialog');
  overlay.setAttribute('aria-modal', 'true');

  const style = document.createElement('style');
  style.textContent = `
    @keyframes hm-rename-backdrop-in {
      from { opacity: 0; } to { opacity: 1; }
    }
    @keyframes hm-rename-panel-in {
      from { opacity: 0; transform: scale(0.97) translateY(-12px); }
      to { opacity: 1; transform: scale(1) translateY(0); }
    }
    #hm-rename-space-overlay {
      position: fixed; inset: 0; z-index: 2147483647;
      display: flex; align-items: flex-start; justify-content: center;
      padding: min(18vh, 160px) 24px 24px; box-sizing: border-box;
      background: rgba(8, 10, 12, 0.55);
      backdrop-filter: blur(12px); -webkit-backdrop-filter: blur(12px);
      animation: hm-rename-backdrop-in 160ms ease-out;
    }
    #hm-rename-space-overlay .hm-panel {
      width: min(420px, calc(100vw - 48px)); box-sizing: border-box;
      border-radius: 14px; border: 1px solid rgba(255,255,255,0.08);
      background: rgba(24,28,32,0.82);
      box-shadow: 0 24px 64px rgba(0,0,0,0.45),
        0 0 0 1px rgba(255,255,255,0.04) inset;
      backdrop-filter: blur(24px) saturate(140%);
      -webkit-backdrop-filter: blur(24px) saturate(140%);
      overflow: hidden;
      animation: hm-rename-panel-in 200ms cubic-bezier(0.16,1,0.3,1);
    }
    #hm-rename-space-overlay .hm-header {
      padding: 14px 16px 10px;
      font: 600 14px/1.3 system-ui, -apple-system, sans-serif;
      color: rgb(236,240,244);
      border-bottom: 1px solid rgba(255,255,255,0.06);
    }
    #hm-rename-space-overlay .hm-body { padding: 12px 16px 16px; }
    #hm-rename-space-overlay .hm-icon-row {
      display: flex; align-items: center; gap: 10px;
      margin-bottom: 12px;
    }
    #hm-rename-space-overlay .hm-icon-preview {
      width: 40px; height: 40px; border-radius: 10px;
      border: 1px solid rgba(255,255,255,0.12);
      background: rgba(0,0,0,0.3);
      display: flex; align-items: center; justify-content: center;
      cursor: pointer; color: rgba(200,210,216,0.6);
      transition: border-color 0.15s, background 0.15s;
      flex-shrink: 0;
    }
    #hm-rename-space-overlay .hm-icon-preview:hover {
      border-color: rgba(88,199,180,0.5);
      background: rgba(88,199,180,0.08);
    }
    #hm-rename-space-overlay .hm-icon-preview.has-icon {
      color: rgb(126,219,201);
      border-color: rgba(88,199,180,0.3);
    }
    #hm-rename-space-overlay .hm-icon-hint {
      font: 400 12px/1.3 system-ui, -apple-system, sans-serif;
      color: rgba(200,210,216,0.5);
    }
    #hm-rename-space-overlay .hm-icon-grid-wrap {
      margin-bottom: 12px; display: none;
      border: 1px solid rgba(255,255,255,0.06);
      border-radius: 10px; background: rgba(0,0,0,0.2);
      padding: 10px; max-height: 260px; overflow-y: auto;
    }
    #hm-rename-space-overlay .hm-icon-grid-wrap.visible { display: block; }
    #hm-rename-space-overlay .hm-icon-cat {
      font: 500 11px/1 system-ui, -apple-system, sans-serif;
      color: rgba(200,210,216,0.4); text-transform: uppercase;
      letter-spacing: 0.5px; margin: 8px 0 6px; padding: 0 2px;
    }
    #hm-rename-space-overlay .hm-icon-cat:first-child { margin-top: 0; }
    #hm-rename-space-overlay .hm-icon-grid {
      display: grid; grid-template-columns: repeat(5, 1fr); gap: 4px;
    }
    #hm-rename-space-overlay .hm-icon-cell {
      width: 100%; aspect-ratio: 1; border-radius: 8px;
      border: 1px solid transparent; background: transparent;
      color: rgba(200,210,216,0.7); cursor: pointer;
      display: flex; align-items: center; justify-content: center;
      transition: all 0.12s;
    }
    #hm-rename-space-overlay .hm-icon-cell:hover {
      background: rgba(255,255,255,0.06);
      color: rgb(236,240,244);
    }
    #hm-rename-space-overlay .hm-icon-cell.selected {
      background: rgba(88,199,180,0.15);
      border-color: rgba(88,199,180,0.4);
      color: rgb(126,219,201);
    }
    #hm-rename-space-overlay .hm-name-row {
      display: flex; align-items: center; gap: 8px;
    }
    #hm-rename-space-overlay .hm-input {
      flex: 1; min-width: 0; height: 36px;
      border: 1px solid rgba(255,255,255,0.12); border-radius: 8px;
      background: rgba(0,0,0,0.3); color: rgb(236,240,244);
      padding: 0 12px; outline: none;
      font: 400 14px/1.3 system-ui, -apple-system, sans-serif;
    }
    #hm-rename-space-overlay .hm-input:focus {
      border-color: rgba(88,199,180,0.5);
    }
    #hm-rename-space-overlay .hm-btn {
      flex: 0 0 auto; height: 36px; padding: 0 16px;
      border: none; border-radius: 8px;
      background: rgba(88,199,180,0.2); color: rgb(126,219,201);
      font: 500 14px/1 system-ui, -apple-system, sans-serif;
      cursor: pointer;
    }
    #hm-rename-space-overlay .hm-btn:hover {
      background: rgba(88,199,180,0.3);
    }
  `;

  const panel = document.createElement('div');
  panel.className = 'hm-panel';

  const header = document.createElement('div');
  header.className = 'hm-header';
  header.textContent = 'Edit Space';

  const body = document.createElement('div');
  body.className = 'hm-body';

  // Icon preview row
  const iconRow = document.createElement('div');
  iconRow.className = 'hm-icon-row';

  const iconPreview = document.createElement('div');
  iconPreview.className = 'hm-icon-preview' + (CURRENT_ICON ? ' has-icon' : '');
  const currentSvg = findIconSvg(CURRENT_ICON);
  iconPreview.innerHTML = currentSvg
    ? makeSvg(currentSvg, 20)
    : makeSvg('<circle cx="12" cy="12" r="1.5"/><circle cx="6" cy="12" r="1.5"/><circle cx="18" cy="12" r="1.5"/>', 20);

  const iconHint = document.createElement('div');
  iconHint.className = 'hm-icon-hint';
  iconHint.textContent = CURRENT_ICON ? 'Click to change icon' : 'Click to pick an icon';

  iconRow.appendChild(iconPreview);
  iconRow.appendChild(iconHint);

  // Icon grid (hidden by default)
  const gridWrap = document.createElement('div');
  gridWrap.className = 'hm-icon-grid-wrap';

  // "None" button
  const noneLabel = document.createElement('div');
  noneLabel.className = 'hm-icon-cat';
  noneLabel.textContent = '';
  const noneGrid = document.createElement('div');
  noneGrid.className = 'hm-icon-grid';
  const noneCell = document.createElement('button');
  noneCell.type = 'button';
  noneCell.className = 'hm-icon-cell' + (!selectedIcon ? ' selected' : '');
  noneCell.title = 'No icon';
  noneCell.innerHTML = makeSvg('<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>', 18);
  noneGrid.appendChild(noneCell);
  gridWrap.appendChild(noneGrid);

  const allCells = [noneCell];

  for (const [catName, icons] of Object.entries(ICONS)) {
    const catLabel = document.createElement('div');
    catLabel.className = 'hm-icon-cat';
    catLabel.textContent = catName;
    gridWrap.appendChild(catLabel);

    const grid = document.createElement('div');
    grid.className = 'hm-icon-grid';

    for (const ic of icons) {
      const cell = document.createElement('button');
      cell.type = 'button';
      cell.className = 'hm-icon-cell' + (selectedIcon === ic.id ? ' selected' : '');
      cell.title = ic.id;
      cell.innerHTML = makeSvg(ic.svg, 20);
      cell.dataset.iconId = ic.id;
      cell.dataset.iconSvg = ic.svg;
      allCells.push(cell);
      grid.appendChild(cell);
    }
    gridWrap.appendChild(grid);
  }

  const selectIcon = (iconId, svgInner) => {
    selectedIcon = iconId;
    allCells.forEach(c => c.classList.remove('selected'));
    if (!iconId) {
      noneCell.classList.add('selected');
      iconPreview.className = 'hm-icon-preview';
      iconPreview.innerHTML = makeSvg('<circle cx="12" cy="12" r="1.5"/><circle cx="6" cy="12" r="1.5"/><circle cx="18" cy="12" r="1.5"/>', 20);
      iconHint.textContent = 'Click to pick an icon';
    } else {
      const match = allCells.find(c => c.dataset.iconId === iconId);
      if (match) match.classList.add('selected');
      iconPreview.className = 'hm-icon-preview has-icon';
      iconPreview.innerHTML = makeSvg(svgInner, 20);
      iconHint.textContent = 'Click to change icon';
    }
  };

  noneCell.addEventListener('click', () => selectIcon('', ''));
  gridWrap.addEventListener('click', (e) => {
    const cell = e.target.closest('.hm-icon-cell[data-icon-id]');
    if (cell) selectIcon(cell.dataset.iconId, cell.dataset.iconSvg);
  });

  iconPreview.addEventListener('click', () => {
    gridVisible = !gridVisible;
    gridWrap.classList.toggle('visible', gridVisible);
  });

  // Name row
  const nameRow = document.createElement('div');
  nameRow.className = 'hm-name-row';

  const input = document.createElement('input');
  input.type = 'text';
  input.className = 'hm-input';
  input.value = CURRENT_NAME;
  input.placeholder = 'Space name';

  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'hm-btn';
  btn.textContent = 'Save';

  const doSave = () => {
    const name = input.value.trim();
    if (name) {
      overlay.remove();
      document.title = '__hm_space_rename__:' + name + '\t' + selectedIcon;
    }
  };

  btn.addEventListener('click', doSave);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); doSave(); }
    if (e.key === 'Escape') { e.preventDefault(); overlay.remove(); }
  });

  overlay.addEventListener('mousedown', (e) => {
    if (e.target === overlay) overlay.remove();
  });
  overlay.addEventListener('keydown', (e) => {
    if (e.target !== input && e.key === 'Escape') {
      e.preventDefault(); overlay.remove();
    }
  });

  nameRow.appendChild(input);
  nameRow.appendChild(btn);

  body.appendChild(iconRow);
  body.appendChild(gridWrap);
  body.appendChild(nameRow);
  panel.appendChild(header);
  panel.appendChild(body);
  overlay.appendChild(style);
  overlay.appendChild(panel);
  document.documentElement.appendChild(overlay);
  input.focus();
  input.select();
})();
)HMJS";
    return s;
  }

  static void OnNativeSpaceNavigate(void* context, int direction) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (!delegate) return;

    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &HailmaryWindowDelegate::NavigateSpaces,
                              base::Unretained(delegate), direction)));
      return;
    }

    delegate->NavigateSpaces(direction);
  }

  void InstallNativeSpaceNavigationMonitor() {
#if defined(OS_MAC)
    if (!window_) return;
    InstallSpaceNavigationMonitor(window_->GetWindowHandle(), this,
                                  &HailmaryWindowDelegate::OnNativeSpaceNavigate);
#endif
  }

  void RemoveNativeSpaceNavigationMonitor() {
#if defined(OS_MAC)
    if (!window_) return;
    RemoveSpaceNavigationMonitor(window_->GetWindowHandle());
#endif
  }

  static std::string NewTabOverlayScript(
      const std::vector<std::string>& nav_history) {
    return std::string(R"JS(
(() => {
  const existing = document.getElementById('hm-new-tab-overlay');
  if (existing) {
    const existingInput = existing.querySelector('input');
    if (existingInput) existingInput.focus();
    return;
  }

  const NAV_HISTORY = )JS") +
           NavHistoryJson(nav_history) +
           R"JS(;

  const normalizeUrl = (rawValue) => {
    const value = rawValue.trim();
    if (!value) return 'https://www.google.com';
    if (value.includes('://') || value.startsWith('about:') ||
        value.startsWith('chrome:')) {
      return value;
    }
    if ((value.includes('.') && !value.includes(' ')) ||
        value.startsWith('localhost')) {
      return `https://${value}`;
    }
    return `https://www.google.com/search?q=${encodeURIComponent(value)}`;
  };

  const getDisplayLabel = (url) => {
    try {
      const parsed = new URL(url);
      if (parsed.pathname === '/search' &&
          parsed.searchParams.has('q') &&
          parsed.hostname.includes('google')) {
        return parsed.searchParams.get('q');
      }
      let host = parsed.hostname;
      if (host.startsWith('www.')) host = host.slice(4);
      return host || url;
    } catch {
      return url;
    }
  };

  const getSecondaryLabel = (url) => {
    try {
      const parsed = new URL(url);
      if (parsed.pathname === '/search' &&
          parsed.searchParams.has('q') &&
          parsed.hostname.includes('google')) {
        return 'Google Search';
      }
      let host = parsed.hostname;
      if (host.startsWith('www.')) host = host.slice(4);
      const path = parsed.pathname === '/' ? '' : parsed.pathname;
      return `${host}${path}`;
    } catch {
      return '';
    }
  };

  const getDomain = (url) => {
    try {
      let host = new URL(url).hostname;
      if (host.startsWith('www.')) host = host.slice(4);
      return host || url;
    } catch {
      return url;
    }
  };

  const getFaviconUrl = (url) => {
    const domain = getDomain(url);
    return `https://www.google.com/s2/favicons?domain=${encodeURIComponent(domain)}&sz=64`;
  };

  const overlay = document.createElement('div');
  overlay.id = 'hm-new-tab-overlay';
  overlay.setAttribute('role', 'dialog');
  overlay.setAttribute('aria-modal', 'true');

  const style = document.createElement('style');
  style.textContent = `
    @keyframes hm-overlay-backdrop-in {
      from { opacity: 0; }
      to { opacity: 1; }
    }
    @keyframes hm-overlay-panel-in {
      from {
        opacity: 0;
        transform: scale(0.97) translateY(-12px);
      }
      to {
        opacity: 1;
        transform: scale(1) translateY(0);
      }
    }
    #hm-new-tab-overlay {
      position: fixed;
      inset: 0;
      z-index: 2147483647;
      display: flex;
      align-items: flex-start;
      justify-content: center;
      padding: min(18vh, 160px) 24px 24px;
      box-sizing: border-box;
      background: rgba(8, 10, 12, 0.55);
      backdrop-filter: blur(12px);
      -webkit-backdrop-filter: blur(12px);
      animation: hm-overlay-backdrop-in 160ms ease-out;
    }
    #hm-new-tab-overlay .hm-panel {
      width: min(560px, calc(100vw - 48px));
      box-sizing: border-box;
      border-radius: 14px;
      border: 1px solid rgba(255, 255, 255, 0.08);
      background: rgba(24, 28, 32, 0.82);
      box-shadow:
        0 24px 64px rgba(0, 0, 0, 0.45),
        0 0 0 1px rgba(255, 255, 255, 0.04) inset;
      backdrop-filter: blur(24px) saturate(140%);
      -webkit-backdrop-filter: blur(24px) saturate(140%);
      overflow: hidden;
      animation: hm-overlay-panel-in 200ms cubic-bezier(0.16, 1, 0.3, 1);
    }
    #hm-new-tab-overlay .hm-input-wrap {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 14px 16px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    }
    #hm-new-tab-overlay .hm-input-icon {
      flex: 0 0 auto;
      width: 16px;
      height: 16px;
      opacity: 0.45;
    }
    #hm-new-tab-overlay input {
      flex: 1 1 auto;
      min-width: 0;
      height: 28px;
      border: none;
      outline: none;
      padding: 0;
      background: transparent;
      color: rgb(236, 240, 244);
      caret-color: rgb(120, 180, 255);
      font: 500 16px/1.4 system-ui, -apple-system, BlinkMacSystemFont,
        'Segoe UI', sans-serif;
      letter-spacing: -0.01em;
    }
    #hm-new-tab-overlay input::placeholder {
      color: rgba(180, 190, 200, 0.55);
    }
    #hm-new-tab-overlay .hm-history {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(72px, 1fr));
      gap: 4px;
      max-height: 320px;
      overflow-y: auto;
      padding: 10px;
    }
    #hm-new-tab-overlay .hm-history:empty {
      display: none;
    }
    #hm-new-tab-overlay .hm-history-item {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: flex-start;
      gap: 6px;
      box-sizing: border-box;
      border: none;
      border-radius: 10px;
      padding: 8px 4px;
      background: transparent;
      color: rgb(220, 226, 232);
      text-align: center;
      cursor: pointer;
      font: inherit;
      transition: background 100ms ease, transform 100ms ease;
    }
    #hm-new-tab-overlay .hm-history-item:hover {
      background: rgba(255, 255, 255, 0.06);
      transform: scale(1.03);
    }
    #hm-new-tab-overlay .hm-history-item.is-selected {
      background: rgba(255, 255, 255, 0.1);
      box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.08) inset;
    }
    #hm-new-tab-overlay .hm-history-favicon {
      flex: 0 0 auto;
      width: 48px;
      height: 48px;
      border-radius: 8px;
      object-fit: contain;
      background: rgba(255, 255, 255, 0.04);
    }
    #hm-new-tab-overlay .hm-history-domain {
      width: 100%;
      min-width: 0;
      font: 400 11px/1.2 system-ui, -apple-system, BlinkMacSystemFont,
        'Segoe UI', sans-serif;
      color: rgba(180, 190, 200, 0.85);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
  `;

  const panel = document.createElement('div');
  panel.className = 'hm-panel';

  const inputWrap = document.createElement('div');
  inputWrap.className = 'hm-input-wrap';

  const inputIcon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  inputIcon.setAttribute('class', 'hm-input-icon');
  inputIcon.setAttribute('viewBox', '0 0 16 16');
  inputIcon.setAttribute('fill', 'none');
  inputIcon.innerHTML =
    '<circle cx="7" cy="7" r="4.5" stroke="currentColor" stroke-width="1.5"/>' +
    '<path d="M10.5 10.5L14 14" stroke="currentColor" stroke-width="1.5" ' +
    'stroke-linecap="round"/>';

  const input = document.createElement('input');
  input.type = 'text';
  input.autocomplete = 'off';
  input.autocapitalize = 'off';
  input.spellcheck = false;
  input.placeholder = 'Search or enter address';

  const historyList = document.createElement('div');
  historyList.className = 'hm-history';

  let filteredItems = [];
  let selectedIndex = -1;

  const navigateTo = (url) => {
    overlay.remove();
    window.location.href = `hm-new-tab:${encodeURIComponent(url)}`;
  };

  const itemMatchesQuery = (url, query) => {
    if (!query) return true;
    const lowerQuery = query.toLowerCase();
    const label = getDisplayLabel(url).toLowerCase();
    const secondary = getSecondaryLabel(url).toLowerCase();
    return label.includes(lowerQuery) ||
      secondary.includes(lowerQuery) ||
      url.toLowerCase().includes(lowerQuery);
  };

  const getGridColumnCount = () => {
    const items = historyList.querySelectorAll('.hm-history-item');
    if (items.length < 2) return 1;
    const firstTop = items[0].offsetTop;
    let cols = 1;
    for (let i = 1; i < items.length; i++) {
      if (items[i].offsetTop === firstTop) {
        cols++;
      } else {
        break;
      }
    }
    return cols;
  };

  const renderHistory = () => {
    const query = input.value.trim();
    filteredItems = NAV_HISTORY.filter((url) => itemMatchesQuery(url, query));
    historyList.innerHTML = '';
    selectedIndex = filteredItems.length > 0 ? 0 : -1;

    filteredItems.forEach((url, index) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'hm-history-item' +
        (index === selectedIndex ? ' is-selected' : '');
      button.dataset.url = url;
      button.title = getDisplayLabel(url);

      const favicon = document.createElement('img');
      favicon.className = 'hm-history-favicon';
      favicon.src = getFaviconUrl(url);
      favicon.alt = '';
      favicon.loading = 'lazy';
      favicon.draggable = false;

      const domain = document.createElement('span');
      domain.className = 'hm-history-domain';
      domain.textContent = getDomain(url);

      button.appendChild(favicon);
      button.appendChild(domain);

      button.addEventListener('mousedown', (event) => {
        event.preventDefault();
      });
      button.addEventListener('click', () => navigateTo(url));

      historyList.appendChild(button);
    });
  };

  const updateSelection = () => {
    const items = historyList.querySelectorAll('.hm-history-item');
    items.forEach((item, index) => {
      item.classList.toggle('is-selected', index === selectedIndex);
    });
    if (selectedIndex >= 0 && items[selectedIndex]) {
      items[selectedIndex].scrollIntoView({ block: 'nearest', inline: 'nearest' });
    }
  };

  const moveSelection = (deltaRow, deltaCol) => {
    if (filteredItems.length === 0) return;
    const cols = getGridColumnCount();
    const row = Math.floor(selectedIndex / cols);
    const col = selectedIndex % cols;
    const newRow = row + deltaRow;
    const newCol = col + deltaCol;
    if (newCol < 0 || newCol >= cols) return;
    const newIndex = newRow * cols + newCol;
    if (newIndex < 0 || newIndex >= filteredItems.length) return;
    selectedIndex = newIndex;
    updateSelection();
  };

  const close = () => overlay.remove();

  overlay.addEventListener('mousedown', (event) => {
    if (event.target === overlay) close();
  });

  input.addEventListener('input', () => renderHistory());

  input.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      event.preventDefault();
      close();
      return;
    }

    if (event.key === 'ArrowDown') {
      event.preventDefault();
      moveSelection(1, 0);
      return;
    }

    if (event.key === 'ArrowUp') {
      event.preventDefault();
      moveSelection(-1, 0);
      return;
    }

    if (event.key === 'ArrowRight' || event.key === 'ArrowLeft') {
      return;
    }

    if (event.key === 'Enter') {
      event.preventDefault();
      if (selectedIndex >= 0 && filteredItems[selectedIndex]) {
        navigateTo(filteredItems[selectedIndex]);
        return;
      }
      navigateTo(normalizeUrl(input.value));
    }
  });

  inputWrap.appendChild(inputIcon);
  inputWrap.appendChild(input);
  panel.appendChild(inputWrap);
  panel.appendChild(historyList);
  panel.addEventListener('mousedown', (event) => {
    if (event.target === panel || event.target === inputWrap) {
      input.focus();
    }
  });

  overlay.appendChild(style);
  overlay.appendChild(panel);
  document.documentElement.appendChild(overlay);
  renderHistory();
  input.focus();
})();
)JS";
  }

  void FocusAddress() {
    if (!address_) {
      return;
    }

    address_->RequestFocus();
    ScheduleAddressSelectAll();
  }

  void ScheduleAddressSelectAll() {
    if (!address_ || address_select_all_pending_) {
      return;
    }

    address_select_all_pending_ = true;
    CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                            &HailmaryWindowDelegate::ApplyAddressSelectAll,
                            base::Unretained(this))));
  }

  void ApplyAddressSelectAll() {
    address_select_all_pending_ = false;
    if (!address_ || !address_->HasFocus()) {
      return;
    }

    address_->SelectAll(false);
  }

  void NavigateToAddressBarValue() {
    if (!address_) {
      return;
    }

    Load(NormalizeUrl(address_->GetText().ToString()));
    if (auto browser_view = ActiveBrowserView()) {
      browser_view->RequestFocus();
    }
  }

  void Load(const std::string& url) {
    auto browser = GetBrowser();
    if (!browser) {
      return;
    }

    address_->SetText(url);
    if (auto* tab = ActiveTab()) {
      tab->url = url;
      tab->has_page_title = false;
      SetTabLabel(tab->id, DisplayLabelForUrl(url));
    }
    browser->GetMainFrame()->LoadURL(url);
    UpdateNavigationButtons();
  }

  void AddPinnedTab(const std::string& url, int folder_id, bool activate) {
    CefBrowserSettings browser_settings;
    browser_settings.background_color =
        Color(kWebSurfaceRed, kWebSurfaceGreen, kWebSurfaceBlue);
    const int tab_id = next_tab_id_++;
    auto ctx = ActiveSpaceRequestContext();
    auto browser_view = CefBrowserView::CreateBrowserView(
        new Client(
            [this, tab_id](const std::string& title) {
              UpdateTabTitle(tab_id, title);
            },
            [this, tab_id](const std::string& url) {
              UpdateTabUrl(tab_id, url);
            },
            [this]() { ShowNewTabOverlay(); },
            [this]() {
              if (IsNewTabOverlayVisible()) {
                HideNewTabOverlay();
              }
            },
            [this](const std::string& url) {
              AddToNavHistory(url);
              AddTab(url, true);
            },
            [this]() { FocusAddress(); },
            [this, tab_id](CefRefPtr<CefImage> image) {
              UpdateTabFavicon(tab_id, image);
            },
            [this]() { ToggleDevTools(); },
            [this](int space_number) { SwitchToSpaceNumber(space_number); }),
        url, browser_settings, nullptr, ctx, this);

    Tab tab;
    tab.id = tab_id;
    tab.url = url;
    tab.title = DisplayLabelForUrl(url);
    tab.browser_view = browser_view;
    tab.pinned = true;
    tab.folder_id = folder_id;
    if (!spaces_.empty()) {
      tab.space_id = spaces_[active_space_index_].id;
    }
    tabs_.push_back(std::move(tab));

    web_surface_->AddChildView(browser_view);
    browser_view->SetVisible(false);

    if (activate || active_tab_id_ < 0) {
      ActivateTab(tab_id);
    }
  }

  void OpenNewTab() { AddTab(kStartupURL, true); }

  void AddTab(const std::string& url, bool activate) {
    CefBrowserSettings browser_settings;
    browser_settings.background_color =
        Color(kWebSurfaceRed, kWebSurfaceGreen, kWebSurfaceBlue);
    const int tab_id = next_tab_id_++;
    auto ctx = ActiveSpaceRequestContext();
    auto browser_view = CefBrowserView::CreateBrowserView(
        new Client(
            [this, tab_id](const std::string& title) {
              UpdateTabTitle(tab_id, title);
            },
            [this, tab_id](const std::string& url) {
              UpdateTabUrl(tab_id, url);
            },
            [this]() { ShowNewTabOverlay(); },
            [this]() {
              if (IsNewTabOverlayVisible()) {
                HideNewTabOverlay();
              }
            },
            [this](const std::string& url) {
              AddToNavHistory(url);
              AddTab(url, true);
            },
            [this]() { FocusAddress(); },
            [this, tab_id](CefRefPtr<CefImage> image) {
              UpdateTabFavicon(tab_id, image);
            },
            [this]() { ToggleDevTools(); },
            [this](int space_number) { SwitchToSpaceNumber(space_number); }),
        url, browser_settings, nullptr, ctx, this);
    AddExistingTab(url, browser_view, activate, tab_id);
  }

  void AddExistingTab(const std::string& url,
                      CefRefPtr<CefBrowserView> browser_view,
                      bool activate,
                      int tab_id = -1) {
    if (tab_id < 0) {
      tab_id = next_tab_id_++;
    }

    Tab tab;
    tab.id = tab_id;
    tab.url = url;
    tab.title = DisplayLabelForUrl(url);
    tab.browser_view = browser_view;
    if (!spaces_.empty()) {
      tab.space_id = spaces_[active_space_index_].id;
    }
    tabs_.push_back(std::move(tab));
    auto panel = MakeTabPanel(tab_id, tabs_.back().title);
    tabs_container_->AddChildView(panel);
    web_surface_->AddChildView(browser_view);
    browser_view->SetVisible(false);

    if (activate || active_tab_id_ < 0) {
      ActivateTab(tab_id);
    }

    SyncSidebarTabClickMonitorTabCount();
  }

  void ActivateTab(int tab_id) {
    auto* tab = FindTab(tab_id);
    if (!tab || tab_id == active_tab_id_) {
      return;
    }

    active_tab_id_ = tab_id;
    for (auto& candidate : tabs_) {
      const bool active = candidate.id == tab_id;
      if (candidate.browser_view) {
        candidate.browser_view->SetVisible(active);
      }
    }

    address_->SetText(tab->url);
    tab->browser_view->RequestFocus();
    UpdateNavigationButtons();
  }

  void CloseTab(int tab_id) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(),
                           [tab_id](const Tab& t) { return t.id == tab_id; });
    if (it == tabs_.end()) {
      return;
    }

    if (it->pinned) {
      return;
    }

    const int space_id = it->space_id;
    int space_tab_count = 0;
    for (const auto& t : tabs_) {
      if (t.space_id == space_id) space_tab_count++;
    }

    if (space_tab_count <= 1) {
      AddTab(kStartupURL, true);
      it = std::find_if(tabs_.begin(), tabs_.end(),
                        [tab_id](const Tab& t) { return t.id == tab_id; });
      if (it == tabs_.end()) {
        return;
      }
    }

    if (it->browser_view) {
      auto browser = it->browser_view->GetBrowser();
      it->browser_view->SetVisible(false);
      web_surface_->RemoveChildView(it->browser_view);
      if (browser) {
        browser->GetHost()->CloseBrowser(false);
      }
    }

    if (it->panel) {
      tabs_container_->RemoveChildView(it->panel);
    }

    const bool was_active = (tab_id == active_tab_id_);
    const int closed_space_id = it->space_id;
    tabs_.erase(it);
    SyncSidebarTabClickMonitorTabCount();

    bool has_space_tabs = false;
    int last_space_tab_id = -1;
    for (const auto& t : tabs_) {
      if (t.space_id == closed_space_id) {
        has_space_tabs = true;
        last_space_tab_id = t.id;
      }
    }

    if (!has_space_tabs) {
      active_tab_id_ = -1;
      if (address_) {
        address_->SetText("");
      }
      AddTab(kStartupURL, true);
      return;
    }

    if (was_active && last_space_tab_id >= 0) {
      ActivateTab(last_space_tab_id);
    }

    if (tabs_container_) {
      tabs_container_->InvalidateLayout();
    }
    if (sidebar_) {
      sidebar_->InvalidateLayout();
      sidebar_->Layout();
    }
  }

  void ActivateTabAtWindowPoint(double window_x, double window_y) {
    const int x = static_cast<int>(std::round(window_x));
    const int y = static_cast<int>(std::round(window_y));
    if (x < 0 || x > sidebar_width_ || y < kSidebarTopInset) {
      return;
    }

    const int pinned_start = kSidebarTopInset + kSidebarAddressRowHeight +
                             kSpaceNameLabelHeight + kPinnedLabelHeight;
    const int pinned_end = kSidebarTopInset + kSidebarAddressRowHeight +
                           kSpaceNameLabelHeight + pinned_section_height_;
    const int first_tab_top = pinned_end + kTabsLabelHeight;

    if (y >= pinned_start && y < pinned_end) {
      ActivatePinnedTabAtY(y - pinned_start);
      return;
    }

    const int row_span = kTabHeight + kTabGap;
    if (y < first_tab_top) {
      return;
    }

    const int space_id = spaces_.empty() ? 0 : spaces_[active_space_index_].id;
    std::vector<int> regular_ids;
    for (const auto& tab : tabs_) {
      if (!tab.pinned && tab.space_id == space_id) {
        regular_ids.push_back(tab.id);
      }
    }

    const int row = (y - first_tab_top) / row_span;
    const int row_y = (y - first_tab_top) % row_span;
    if (row_y >= kTabHeight || row < 0 ||
        row >= static_cast<int>(regular_ids.size())) {
      return;
    }

    ActivateTab(regular_ids[row]);
  }

  void ActivatePinnedTabAtY(int local_y) {
    int cursor = 0;
    const int space_id = spaces_.empty() ? 0 : spaces_[active_space_index_].id;

    for (const auto& tab : tabs_) {
      if (!tab.pinned || tab.folder_id >= 0 || tab.space_id != space_id) {
        continue;
      }
      if (local_y >= cursor && local_y < cursor + kPinnedTabHeight) {
        ActivateTab(tab.id);
        return;
      }
      cursor += kPinnedTabHeight + kPinnedTabGap;
    }

    for (const auto& folder : pinned_folders_) {
      cursor += kPinnedFolderHeaderHeight;
      for (const auto& tab : tabs_) {
        if (!tab.pinned || tab.folder_id != folder.id ||
            tab.space_id != space_id) {
          continue;
        }
        if (local_y >= cursor && local_y < cursor + kPinnedTabHeight) {
          ActivateTab(tab.id);
          return;
        }
        cursor += kPinnedTabHeight + kPinnedTabGap;
      }
    }
  }

  Tab* ActiveTab() { return FindTab(active_tab_id_); }

  CefRefPtr<CefBrowserView> ActiveBrowserView() {
    auto* tab = ActiveTab();
    return tab ? tab->browser_view : nullptr;
  }

  Tab* FindTab(int tab_id) {
    for (auto& tab : tabs_) {
      if (tab.id == tab_id) {
        return &tab;
      }
    }
    return nullptr;
  }

  int FindTabIdForButton(CefRefPtr<CefButton> button) const {
    for (const auto& tab : tabs_) {
      if (tab.button == button) {
        return tab.id;
      }
    }
    return -1;
  }

  int FindTabIdForView(CefRefPtr<CefView> view) const {
    for (const auto& tab : tabs_) {
      if (tab.button == view || tab.panel == view) {
        return tab.id;
      }
    }
    return -1;
  }

  bool IsTabPanel(CefRefPtr<CefView> view) const {
    for (const auto& tab : tabs_) {
      if (tab.panel == view) {
        return true;
      }
    }
    return false;
  }

  bool IsTabButton(CefRefPtr<CefView> view) const {
    for (const auto& tab : tabs_) {
      if (tab.button == view) {
        return true;
      }
    }
    return false;
  }

  bool IsPinnedTabPanel(CefRefPtr<CefView> view) const {
    for (const auto& tab : tabs_) {
      if (tab.pinned && tab.panel == view) {
        return true;
      }
    }
    return false;
  }

  bool IsPinnedTabButton(CefRefPtr<CefView> view) const {
    for (const auto& tab : tabs_) {
      if (tab.pinned && tab.button == view) {
        return true;
      }
    }
    return false;
  }

  // Tabs have no fill of their own and no active-state accent: both the row and
  // its label paint transparent so the sidebar shows through. Re-applied from
  // OnThemeChanged, which otherwise restores an opaque themed fill.
  void ApplyTabChrome(int tab_id) {
    auto* tab = FindTab(tab_id);
    if (!tab) {
      return;
    }

    if (tab->panel) {
      tab->panel->SetBackgroundColor(kTransparent);
    }
    if (tab->button) {
      tab->button->SetBackgroundColor(kTransparent);
      tab->button->SetTextColor(CEF_BUTTON_STATE_NORMAL, kTabTextColor);
      tab->button->SetTextColor(CEF_BUTTON_STATE_HOVERED, kTabTextColor);
      tab->button->SetTextColor(CEF_BUTTON_STATE_PRESSED, kTabTextColor);
      tab->button->SetTextColor(CEF_BUTTON_STATE_DISABLED, kTabTextColor);
      tab->button->SetEnabledTextColors(kTabTextColor);
      tab->button->SetMinimumSize(CefSize(TabButtonWidth(), kTabHeight));
    }
  }

  void ApplySidebarWidth(int width) {
    width = std::clamp(width, kMinSidebarWidth, kMaxSidebarWidth);
    if (width == sidebar_width_) {
      return;
    }

    sidebar_width_ = width;
    if (sidebar_delegate_) {
      sidebar_delegate_->SetPreferredSize(CefSize(sidebar_width_, 720));
    }

    for (auto& tab : tabs_) {
      const int tab_height = tab.pinned ? kPinnedTabHeight : kTabHeight;
      if (tab.panel_delegate) {
        tab.panel_delegate->SetPreferredSize(
            CefSize(sidebar_width_, tab_height));
      }
      if (tab.button) {
        tab.button->SetMinimumSize(
            CefSize(tab.pinned ? sidebar_width_ - 2 * kSidebarHorizontalPad - 8
                               : TabButtonWidth(),
                    tab_height));
      }
      if (tab.panel) {
        tab.panel->InvalidateLayout();
      }
    }

    if (sidebar_) {
      sidebar_->InvalidateLayout();
    }
    if (body_) {
      body_->InvalidateLayout();
      body_->Layout();
    }
    if (root_) {
      root_->InvalidateLayout();
      root_->Layout();
    }
    if (window_) {
      window_->InvalidateLayout();
      window_->Layout();
    }

    UpdateNativeSidebarResizeHandle();
    if (devtools_surface_) {
      InstallNativeDevToolsSplitter();
    }
  }

  static void OnNativeSidebarResize(void* context, int width) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (delegate) {
      delegate->ApplySidebarWidth(width);
    }
  }

  static void OnNativeDevToolsSplitterResize(void* context, int width) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (delegate) {
      delegate->ApplyDevToolsWebWidth(width);
    }
  }

  void ApplyDevToolsWebWidth(int width) {
    if (!devtools_surface_ || !content_well_ || !web_surface_delegate_) {
      return;
    }

    auto cw_bounds = content_well_->GetBounds();
    const int max_width =
        cw_bounds.width - kDevToolsGap - kMinDevToolsSplitWidth;
    width = std::clamp(width, kMinDevToolsSplitWidth, max_width);

    if (width == devtools_web_width_) {
      return;
    }

    devtools_web_width_ = width;
    web_surface_delegate_->SetPreferredSize(CefSize(width, 1));

    if (content_well_) {
      content_well_->InvalidateLayout();
      content_well_->Layout();
    }

    InstallNativeDevToolsSplitter();
    InstallContentWellCornerRadius();
  }

  void ShowCloseButtonForTabRow(int /*row*/) {
    // Tab close UX is rendered by AvoraSidebarTabClickMonitor on macOS.
  }

  void CloseTabAtRow(int row) {
    const int space_id = spaces_.empty() ? 0 : spaces_[active_space_index_].id;
    std::vector<int> regular_ids;
    for (const auto& tab : tabs_) {
      if (!tab.pinned && tab.space_id == space_id) {
        regular_ids.push_back(tab.id);
      }
    }
    if (row < 0 || row >= static_cast<int>(regular_ids.size())) {
      return;
    }
    CloseTab(regular_ids[row]);
  }

  static void OnNativeSidebarClose(void* context, int row) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (!delegate) {
      return;
    }

    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &HailmaryWindowDelegate::CloseTabAtRow,
                              base::Unretained(delegate), row)));
      return;
    }

    delegate->CloseTabAtRow(row);
  }

  static void OnNativeSidebarHover(void* context, int hovered_row) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (!delegate) {
      return;
    }

    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &HailmaryWindowDelegate::ShowCloseButtonForTabRow,
                              base::Unretained(delegate), hovered_row)));
      return;
    }

    delegate->ShowCloseButtonForTabRow(hovered_row);
  }

  static void OnNativeSidebarTabClick(void* context,
                                      double window_x,
                                      double window_y) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (!delegate) {
      return;
    }

    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &HailmaryWindowDelegate::ActivateTabAtWindowPoint,
                              base::Unretained(delegate), window_x, window_y)));
      return;
    }

    delegate->ActivateTabAtWindowPoint(window_x, window_y);
  }

  static void OnNativePlusClick(void* context) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (!delegate) {
      return;
    }

    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &HailmaryWindowDelegate::OpenNewTab,
                              base::Unretained(delegate))));
      return;
    }

    delegate->OpenNewTab();
  }

  static void OnNativeSidebarNavClick(void* context, int button) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (!delegate) {
      return;
    }

    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &HailmaryWindowDelegate::HandleNativeNavClick,
                              base::Unretained(delegate), button)));
      return;
    }

    delegate->HandleNativeNavClick(button);
  }

  static void OnNativeAddressClick(void* context) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (!delegate) {
      return;
    }

    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &HailmaryWindowDelegate::HandleNativeAddressClick,
                              base::Unretained(delegate))));
      return;
    }

    delegate->HandleNativeAddressClick();
  }

  static void OnNativeExtensionClick(void* context) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (!delegate) {
      return;
    }

    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &HailmaryWindowDelegate::HandleNativeExtensionClick,
                              base::Unretained(delegate))));
      return;
    }

    delegate->HandleNativeExtensionClick();
  }

  void HandleNativeExtensionClick() {
    ToggleExtensionPopup();
  }

  void HandleNativeAddressClick() {
    if (!address_) {
      return;
    }

    if (address_->HasFocus()) {
      return;
    }

    address_->RequestFocus();
    ScheduleAddressSelectAll();
  }

  static void OnNativePinAction(void* context,
                                double window_x,
                                double window_y,
                                bool should_pin) {
    auto* delegate = static_cast<HailmaryWindowDelegate*>(context);
    if (!delegate) {
      return;
    }

    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI,
                  CefCreateClosureTask(base::BindOnce(
                      &HailmaryWindowDelegate::HandleNativePinAction,
                      base::Unretained(delegate), window_x, window_y,
                      should_pin)));
      return;
    }

    delegate->HandleNativePinAction(window_x, window_y, should_pin);
  }

  void HandleNativePinAction(double window_x, double window_y,
                             bool should_pin) {
    const int y = static_cast<int>(std::round(window_y));

    if (should_pin) {
      const int first_tab_top = kSidebarTopInset + kSidebarAddressRowHeight +
                                kSpaceNameLabelHeight + pinned_section_height_ +
                                kTabsLabelHeight;
      const int row_span = kTabHeight + kTabGap;
      if (y < first_tab_top) {
        return;
      }
      const int space_id = spaces_.empty() ? 0 : spaces_[active_space_index_].id;
      std::vector<int> regular_ids;
      for (const auto& tab : tabs_) {
        if (!tab.pinned && tab.space_id == space_id) {
          regular_ids.push_back(tab.id);
        }
      }
      const int row = (y - first_tab_top) / row_span;
      if (row < 0 || row >= static_cast<int>(regular_ids.size())) {
        return;
      }
      ActivateTab(regular_ids[row]);
      ShowPinOverlay();
    } else {
      const int pinned_start = kSidebarTopInset + kSidebarAddressRowHeight +
                               kSpaceNameLabelHeight + kPinnedLabelHeight;
      const int local_y = y - pinned_start;
      int tab_id = FindPinnedTabIdAtLocalY(local_y);
      if (tab_id >= 0) {
        UnpinTab(tab_id);
      }
    }
  }

  int FindPinnedTabIdAtLocalY(int local_y) {
    int cursor = 0;
    const int space_id = spaces_.empty() ? 0 : spaces_[active_space_index_].id;
    for (const auto& tab : tabs_) {
      if (!tab.pinned || tab.folder_id >= 0 || tab.space_id != space_id) {
        continue;
      }
      if (local_y >= cursor && local_y < cursor + kPinnedTabHeight) {
        return tab.id;
      }
      cursor += kPinnedTabHeight + kPinnedTabGap;
    }
    for (const auto& folder : pinned_folders_) {
      cursor += kPinnedFolderHeaderHeight;
      for (const auto& tab : tabs_) {
        if (!tab.pinned || tab.folder_id != folder.id ||
            tab.space_id != space_id) {
          continue;
        }
        if (local_y >= cursor && local_y < cursor + kPinnedTabHeight) {
          return tab.id;
        }
        cursor += kPinnedTabHeight + kPinnedTabGap;
      }
    }
    return -1;
  }

  void HandleNativeNavClick(int button) {
    auto browser = GetBrowser();
    if (!browser) {
      return;
    }

    if (button == 0 && browser->CanGoBack()) {
      browser->GoBack();
      UpdateNavigationButtons();
    } else if (button == 1 && browser->CanGoForward()) {
      browser->GoForward();
      UpdateNavigationButtons();
    } else if (button == 2) {
      browser->Reload();
      UpdateNavigationButtons();
    }
  }

  void InstallNativeSidebarResizeHandle() {
#if defined(OS_MAC)
    if (!window_) {
      return;
    }
    InstallSidebarResizeHandle(window_->GetWindowHandle(), 0, sidebar_width_,
                               kMinSidebarWidth, kMaxSidebarWidth, this,
                               &HailmaryWindowDelegate::OnNativeSidebarResize);
#endif
  }

  void InstallNativeDevToolsSplitter() {
#if defined(OS_MAC)
    if (!window_ || !devtools_surface_ || !content_well_) {
      return;
    }
    auto cw_bounds = content_well_->GetBounds();
    const int max_width =
        cw_bounds.width - kDevToolsGap - kMinDevToolsSplitWidth;
    InstallDevToolsSplitter(
        window_->GetWindowHandle(), sidebar_width_, kContentInset,
        devtools_web_width_, kDevToolsGap, kMinDevToolsSplitWidth, max_width,
        this, &HailmaryWindowDelegate::OnNativeDevToolsSplitterResize);
#endif
  }


  void RemoveNativeDevToolsSplitter() {
#if defined(OS_MAC)
    if (!window_) {
      return;
    }
    RemoveDevToolsSplitter(window_->GetWindowHandle());
#endif
  }

  void InstallNativeSidebarTabClickMonitor() {
#if defined(OS_MAC)
    if (!window_) {
      return;
    }
    InstallSidebarTabClickMonitor(
        window_->GetWindowHandle(), kSidebarTopInset, sidebar_width_, this,
        &HailmaryWindowDelegate::OnNativeSidebarTabClick,
        &HailmaryWindowDelegate::OnNativeSidebarHover,
        &HailmaryWindowDelegate::OnNativeSidebarClose,
        &HailmaryWindowDelegate::OnNativePlusClick,
        &HailmaryWindowDelegate::OnNativeSidebarNavClick,
        &HailmaryWindowDelegate::OnNativeAddressClick,
        &HailmaryWindowDelegate::OnNativePinAction,
        &HailmaryWindowDelegate::OnNativeExtensionClick);
#endif
  }

  void InstallNativeToolbarPlusClickMonitor() {
#if defined(OS_MAC)
    if (!window_) {
      return;
    }
    InstallToolbarPlusClickMonitor(window_->GetWindowHandle(),
                                   kToolbarNavRowHeight, this,
                                   &HailmaryWindowDelegate::OnNativePlusClick);
#endif
  }

  void SyncSidebarTabClickMonitorTabCount() {
#if defined(OS_MAC)
    if (!window_) {
      return;
    }
    int regular_count = 0;
    const int space_id = spaces_.empty() ? 0 : spaces_[active_space_index_].id;
    for (const auto& tab : tabs_) {
      if (!tab.pinned && tab.space_id == space_id) {
        regular_count++;
      }
    }
    UpdateSidebarTabClickMonitorTabCount(window_->GetWindowHandle(),
                                         regular_count);
#endif
  }

  void InstallContentWellCornerRadius() {
#if defined(OS_MAC)
    if (!window_) {
      return;
    }
    ApplyContentWellCornerRadius(window_->GetWindowHandle(), 0, sidebar_width_,
                                 kContentInset, 18.0);
#endif
  }

  void InstallNativeAddressChipOutline() {
#if defined(OS_MAC)
    if (!window_) {
      return;
    }
    InstallAddressChipOutline(window_->GetWindowHandle(), sidebar_width_,
                              kSidebarTopInset, kSidebarAddressRowHeight,
                              kSidebarHorizontalPad);
#endif
  }

  void UpdateNativeSidebarResizeHandle() {
#if defined(OS_MAC)
    if (!window_) {
      return;
    }
    UpdateSidebarResizeHandle(window_->GetWindowHandle(), 0, sidebar_width_);
    UpdateSidebarTabClickMonitor(window_->GetWindowHandle(), kSidebarTopInset,
                                 sidebar_width_);
    UpdateAddressChipOutline(window_->GetWindowHandle(), sidebar_width_,
                             kSidebarTopInset, kSidebarAddressRowHeight,
                             kSidebarHorizontalPad);
#endif
  }

  void ConfigureDraggableRegions() {
    if (!window_) {
      return;
    }

    const auto bounds = window_->GetClientAreaBoundsInScreen();
    const int width = std::max(bounds.width, 1);

    std::vector<CefDraggableRegion> regions;
    regions.emplace_back(CefRect(0, 0, width, kToolbarNavRowHeight), true);
    const int nav_left =
        std::max(kTrafficLightWidth,
                 sidebar_width_ - kSidebarHorizontalPad - 2 - kNavClusterWidth);
    regions.emplace_back(
        CefRect(nav_left, 0, kNavClusterWidth, kToolbarNavRowHeight), false);
    AddControlRegion(back_button_, regions);
    AddControlRegion(forward_button_, regions);
    AddControlRegion(reload_button_, regions);
    AddControlRegion(address_bar_, regions);
    AddControlRegion(address_, regions);
    AddControlRegion(extension_button_, regions);
    AddControlRegion(plus_button_, regions);
    if (devtools_surface_) {
      AddControlRegion(devtools_surface_, regions);
    }

    window_->SetDraggableRegions(regions);
  }

  void AddControlRegion(CefRefPtr<CefView> view,
                        std::vector<CefDraggableRegion>& regions) {
    if (!view) {
      return;
    }

    auto bounds = view->GetBounds();
    CefPoint origin(0, 0);
    if (!view->ConvertPointToWindow(origin)) {
      return;
    }

    const int w = std::max(bounds.width, 36);
    const int h = std::max(bounds.height, 36);
    if (w <= 0 || h <= 0) {
      return;
    }

    regions.emplace_back(CefRect(origin.x, origin.y, w, h), false);
  }

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> pending_browser_view_;
  CefRefPtr<CefTextfield> address_;
  CefRefPtr<CefPanel> address_bar_;
  CefRefPtr<CefPanel> address_left_pad_;
  CefRefPtr<CefPanel> address_right_pad_;
  CefRefPtr<CefLabelButton> back_button_;
  CefRefPtr<CefLabelButton> forward_button_;
  CefRefPtr<CefLabelButton> reload_button_;
  CefRefPtr<CefLabelButton> extension_button_;
  CefRefPtr<CefLabelButton> plus_button_;
  CefRefPtr<CefPanel> root_;
  CefRefPtr<CefPanel> body_;
  CefRefPtr<CefPanel> sidebar_;
  CefRefPtr<CefPanel> tabs_container_;
  CefRefPtr<CefPanel> web_surface_;
  CefRefPtr<FixedPanelDelegate> web_surface_delegate_;
  CefRefPtr<CefPanel> content_well_;
  CefRefPtr<CefBoxLayout> content_well_layout_;
  CefRefPtr<CefBoxLayout> body_layout_;
  CefRefPtr<CefBrowserView> devtools_view_;
  CefRefPtr<CefPanel> devtools_surface_;
  int devtools_web_width_ = 0;
  CefRefPtr<FixedPanelDelegate> sidebar_delegate_;
  std::vector<CefRefPtr<CefLabelButton>> sidebar_labels_;
  std::vector<Tab> tabs_;
  int next_tab_id_ = 1;
  int active_tab_id_ = -1;
  int sidebar_width_ = kDefaultSidebarWidth;
  CefRefPtr<AddressBarDelegate> address_bar_delegate_;
  bool is_popup_ = false;
  bool new_tab_overlay_visible_ = false;
  bool pin_overlay_visible_ = false;
  bool extension_popup_visible_ = false;
  bool rename_space_overlay_visible_ = false;
  bool address_select_all_pending_ = false;
  std::vector<std::string> nav_history_;
  std::vector<PinnedFolder> pinned_folders_;
  int next_folder_id_ = 1;
  CefRefPtr<CefPanel> pinned_container_;
  int pinned_section_height_ = kPinnedLabelHeight + kPinnedEmptyHeight;

  std::vector<Space> spaces_;
  int active_space_index_ = 0;
  int next_space_id_ = 1;
  CefRefPtr<CefPanel> spaces_bar_;
  CefRefPtr<CefLabelButton> space_name_label_;
  std::vector<CefRefPtr<CefLabelButton>> space_buttons_;
  int space_last_click_index_ = -1;
  std::chrono::steady_clock::time_point space_last_click_time_;

  CefRefPtr<ExtensionPopupWindowDelegate> ext_popup_window_delegate_;

  friend class AddressBarDelegate;
  friend class ExtensionPopupWindowDelegate;

  IMPLEMENT_REFCOUNTING(HailmaryWindowDelegate);
};

void ExtensionPopupWindowDelegate::OnWindowDestroyed(
    CefRefPtr<CefWindow> window) {
  popup_window_ = nullptr;
  if (owner_) {
    owner_->OnExtensionPopupClosed();
  }
}

void ExtensionPopupWindowDelegate::OnButtonPressed(
    CefRefPtr<CefButton> button) {
  if (button == add_btn_ && owner_) {
    owner_->OnGetExtensionsClicked();
  }
}

bool AddressBarDelegate::OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                                    const CefKeyEvent& event) {
  if (!owner_) {
    return false;
  }

  const bool is_key_down = event.type == KEYEVENT_RAWKEYDOWN ||
                           event.type == KEYEVENT_KEYDOWN ||
                           event.type == KEYEVENT_CHAR;

  if (is_key_down && event.windows_key_code == kKeyEscape &&
      owner_->IsPinOverlayVisible()) {
    owner_->HidePinOverlay();
    if (auto browser_view = owner_->ActiveBrowserView()) {
      browser_view->RequestFocus();
    }
    return true;
  }

  if (is_key_down && event.windows_key_code == kKeyEscape &&
      owner_->IsNewTabOverlayVisible()) {
    owner_->HideNewTabOverlay();
    if (auto browser_view = owner_->ActiveBrowserView()) {
      browser_view->RequestFocus();
    }
    return true;
  }

  if (is_key_down && event.windows_key_code == kKeyReturn) {
    owner_->NavigateToAddressBarValue();
    return true;
  }

  return false;
}

void AddressBarDelegate::OnFocus(CefRefPtr<CefView> view) {
  if (owner_) {
    owner_->HandleAddressFocus();
  }
}

void AddressBarDelegate::OnBlur(CefRefPtr<CefView> view) {
  if (owner_) {
    owner_->HandleAddressBlur();
  }
}

// Browser process app.
class BrowserApp : public CefApp, public CefBrowserProcessHandler {
 public:
  BrowserApp() = default;
  BrowserApp(const BrowserApp&) = delete;
  BrowserApp& operator=(const BrowserApp&) = delete;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override {
    if (process_type.empty()) {
      command_line->AppendSwitch("use-views");
      command_line->AppendSwitch("use-alloy-style");
#if defined(OS_MACOSX)
      command_line->AppendSwitch("use-mock-keychain");
#endif
    }
  }

  void OnContextInitialized() override {
    CefWindow::CreateTopLevelWindow(new HailmaryWindowDelegate());
  }

 private:
  IMPLEMENT_REFCOUNTING(BrowserApp);
};

}  // namespace

}  // namespace avora

namespace shared {

CefRefPtr<CefApp> CreateBrowserProcessApp() {
  return new avora::BrowserApp();
}

}  // namespace shared
