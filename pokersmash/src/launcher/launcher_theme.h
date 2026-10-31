// Poker Smash Launcher — visual design system.
//
// Everything the launcher draws goes through here so the look stays consistent:
// one palette, one type scale, one set of primitives. Think of it as the
// stylesheet the launcher would have if it were a web page.
//
// All functions are inline so this stays a single translation unit.

#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cfloat>

namespace launcher {

// The widget helpers below read better unqualified, and nothing in ImGui's
// namespace collides with this file's own names.
using namespace ImGui;

// ===========================================================================
// Palette
//
// Deep, slightly blue-tinted darks so the red accent reads as warm rather than
// alarming. Surfaces step lighter as they come forward: page < card < field.
// ===========================================================================
namespace palette {

// Surfaces.
inline constexpr ImVec4 kBg{0.043f, 0.047f, 0.055f, 1.0f};              // #0B0C0E
inline constexpr ImVec4 kSidebarTop{0.075f, 0.082f, 0.094f, 1.0f};     // #131518
inline constexpr ImVec4 kSidebarBottom{0.031f, 0.035f, 0.043f, 1.0f};  // #08090B
inline constexpr ImVec4 kCard{0.082f, 0.090f, 0.102f, 1.0f};           // #15171A
inline constexpr ImVec4 kCardRaised{0.106f, 0.114f, 0.129f, 1.0f};     // #1B1D21
inline constexpr ImVec4 kField{0.118f, 0.125f, 0.141f, 1.0f};          // #1E2024
inline constexpr ImVec4 kFieldHover{0.145f, 0.153f, 0.173f, 1.0f};     // #252731
inline constexpr ImVec4 kBorder{0.153f, 0.165f, 0.184f, 1.0f};         // #272A2F
inline constexpr ImVec4 kBorderSoft{0.110f, 0.118f, 0.133f, 1.0f};     // #1C1E22

// Accent — poker felt red.
inline constexpr ImVec4 kAccent{0.824f, 0.169f, 0.169f, 1.0f};         // #D22B2B
inline constexpr ImVec4 kAccentHot{0.945f, 0.278f, 0.259f, 1.0f};      // #F14742
inline constexpr ImVec4 kAccentDeep{0.549f, 0.114f, 0.114f, 1.0f};     // #8C1D1D
inline constexpr ImVec4 kAccentWash{0.824f, 0.169f, 0.169f, 0.12f};    // 12% accent

// Text.
inline constexpr ImVec4 kText{0.937f, 0.945f, 0.953f, 1.0f};           // #EFF1F3
inline constexpr ImVec4 kTextMuted{0.612f, 0.643f, 0.682f, 1.0f};      // #9CA4AE
inline constexpr ImVec4 kTextFaint{0.420f, 0.451f, 0.486f, 1.0f};      // #6B737C

// Status.
inline constexpr ImVec4 kOk{0.247f, 0.725f, 0.314f, 1.0f};    // #3FB950
inline constexpr ImVec4 kOkWash{0.247f, 0.725f, 0.314f, 0.14f};
inline constexpr ImVec4 kWarn{0.867f, 0.651f, 0.208f, 1.0f};  // #DDA635
inline constexpr ImVec4 kWarnWash{0.867f, 0.651f, 0.208f, 0.14f};
inline constexpr ImVec4 kBad{0.973f, 0.318f, 0.286f, 1.0f};   // #F85149
inline constexpr ImVec4 kBadWash{0.973f, 0.318f, 0.286f, 0.14f};

}  // namespace palette

// ===========================================================================
// Metrics
// ===========================================================================
namespace metrics {

inline constexpr float kSidebarWidth = 236.0f;
inline constexpr float kBottomBarHeight = 88.0f;
inline constexpr float kPagePadding = 32.0f;
inline constexpr float kCardPadding = 24.0f;
inline constexpr float kRadius = 8.0f;
inline constexpr float kRadiusSmall = 5.0f;
inline constexpr float kNavRowHeight = 46.0f;
inline constexpr float kButtonHeight = 44.0f;
inline constexpr float kRowGap = 14.0f;

}  // namespace metrics

// ===========================================================================
// Type scale
//
// Roboto Medium is embedded in the launcher, so the UI never depends on a font
// being installed. Index 0 is the body size, which keeps any stray ImGui::Text
// call readable.
//
// Note: the members are non-const because ImGui 1.92's ImFont methods and
// AddText/PushFont all take a mutable pointer.
// ===========================================================================
struct Fonts {
  ImFont* micro = nullptr;    // 12 — overlines, eyebrows
  ImFont* caption = nullptr;  // 13 — hints, captions
  ImFont* body = nullptr;     // 15 — default
  ImFont* emphasis = nullptr; // 17 — labels, values
  ImFont* title = nullptr;    // 26 — page titles
  ImFont* display = nullptr;  // 40 — brand
};

inline Fonts& GetFonts() {
  static Fonts fonts;
  return fonts;
}

// Measures `text` as `font` would render it at the font's own size.
inline ImVec2 Measure(ImFont* font, const char* text, float max_width = FLT_MAX) {
  return font->CalcTextSizeA(font->LegacySize, max_width, 0.0f, text);
}

// ===========================================================================
// Icons
//
// Drawn as line art with the draw list, so the launcher ships no icon font and
// no image assets.
// ===========================================================================
enum class Icon { kHome, kGraphics, kPerformance, kGame, kAdvanced };

inline void DrawIcon(Icon icon, const ImVec2& center, float size, ImU32 color, float thickness) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float h = size * 0.5f;

  switch (icon) {
    case Icon::kHome: {
      // Roof, walls, door.
      const ImVec2 roof_l(center.x - h, center.y - h * 0.15f);
      const ImVec2 apex(center.x, center.y - h);
      const ImVec2 roof_r(center.x + h, center.y - h * 0.15f);
      dl->AddLine(roof_l, apex, color, thickness);
      dl->AddLine(apex, roof_r, color, thickness);
      dl->AddLine(ImVec2(center.x - h * 0.75f, center.y - h * 0.15f),
                  ImVec2(center.x - h * 0.75f, center.y + h * 0.85f), color, thickness);
      dl->AddLine(ImVec2(center.x + h * 0.75f, center.y - h * 0.15f),
                  ImVec2(center.x + h * 0.75f, center.y + h * 0.85f), color, thickness);
      dl->AddLine(ImVec2(center.x - h * 0.75f, center.y + h * 0.85f),
                  ImVec2(center.x + h * 0.75f, center.y + h * 0.85f), color, thickness);
      dl->AddRectFilled(ImVec2(center.x - h * 0.22f, center.y + h * 0.28f),
                        ImVec2(center.x + h * 0.22f, center.y + h * 0.85f), color);
      break;
    }

    case Icon::kGraphics: {
      // Monitor with a stand.
      dl->AddRect(ImVec2(center.x - h, center.y - h * 0.8f),
                  ImVec2(center.x + h, center.y + h * 0.45f), color, thickness, 0, 0);
      dl->AddLine(ImVec2(center.x, center.y + h * 0.45f), ImVec2(center.x, center.y + h * 0.8f),
                  color, thickness);
      dl->AddLine(ImVec2(center.x - h * 0.45f, center.y + h * 0.8f),
                  ImVec2(center.x + h * 0.45f, center.y + h * 0.8f), color, thickness);
      break;
    }

    case Icon::kPerformance: {
      // Gauge: a 240 degree arc with a needle.
      const float radius = h * 0.85f;
      dl->PathArcTo(center, radius, IM_PI * 0.75f, IM_PI * 2.25f, 0);
      dl->PathStroke(color, 0, thickness);
      const float angle = IM_PI * 1.5f;
      dl->AddLine(center, ImVec2(center.x + cosf(angle) * radius * 0.7f,
                                 center.y + sinf(angle) * radius * 0.7f),
                  color, thickness);
      dl->AddCircleFilled(center, thickness * 1.1f, color);
      break;
    }

    case Icon::kGame: {
      // Disc: outer ring plus a centre hole.
      dl->AddCircle(center, h * 0.9f, color, 24, thickness);
      dl->AddCircle(center, h * 0.28f, color, 16, thickness);
      break;
    }

    case Icon::kAdvanced: {
      // Three sliders.
      for (int i = 0; i < 3; ++i) {
        const float y = center.y - h + static_cast<float>(i) * h;
        dl->AddLine(ImVec2(center.x - h, y), ImVec2(center.x + h, y), color, thickness);
        const float kx = center.x - h * 0.45f + static_cast<float>(i) * h * 0.45f;
        dl->AddCircleFilled(ImVec2(kx, y), thickness * 1.6f, color);
      }
      break;
    }
  }
}

// ===========================================================================
// Text helpers
// ===========================================================================

// Text with manual letter tracking. Uppercase plus tracking is what makes a
// label read as a label rather than as a sentence.
inline void TrackedText(const char* text, float tracking, ImFont* font, const ImVec4& color) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImU32 col = ImGui::GetColorU32(color);
  const float size = font->LegacySize;

  float x = origin.x;
  for (const char* s = text; *s; ++s) {
    font->RenderChar(dl, size, ImVec2(x, origin.y), col, static_cast<ImWchar>(*s));
    x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, s, s + 1).x + tracking;
  }

  ImGui::Dummy(ImVec2(x - origin.x, ImGui::GetTextLineHeight()));
}

inline void Overline(const char* text) {
  const Fonts& f = GetFonts();
  TrackedText(text, 1.6f, f.micro, palette::kTextFaint);
}

inline void Hint(const char* text, float width = 0.0f) {
  const Fonts& f = GetFonts();
  ImGui::PushFont(f.caption, f.caption->LegacySize);
  ImGui::PushStyleColor(ImGuiCol_Text, palette::kTextFaint);
  if (width > 0.0f) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
  }
  ImGui::TextWrapped("%s", text);
  if (width > 0.0f) {
    ImGui::PopTextWrapPos();
  }
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

// ===========================================================================
// Surfaces
// ===========================================================================

// A rounded card. Returns false if the child was clipped away.
inline bool BeginCard(const char* id, float height = 0.0f, bool raised = false) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, raised ? palette::kCardRaised : palette::kCard);
  ImGui::PushStyleColor(ImGuiCol_Border, palette::kBorder);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, metrics::kRadius);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(metrics::kCardPadding, metrics::kCardPadding));
  return ImGui::BeginChild(id, ImVec2(0, height), true,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

inline void EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

// A hairline rule that fades out at both ends, which reads softer than a solid
// separator.
inline void Hairline(float width = 0.0f) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 left = ImGui::GetColorU32(ImVec4(palette::kBorder.x, palette::kBorder.y,
                                               palette::kBorder.z, 0.0f));
  const ImU32 right = ImGui::GetColorU32(ImVec4(palette::kBorder.x, palette::kBorder.y,
                                                palette::kBorder.z, 0.0f));
  dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + 1.0f), left, right, right, left);
  ImGui::Dummy(ImVec2(w, 1.0f));
}

// ===========================================================================
// Controls
// ===========================================================================

// A status chip: tinted pill with a dot and a label.
inline void StatusChip(bool ok, const char* text) {
  const ImVec4& wash = ok ? palette::kOkWash : palette::kBadWash;
  const ImVec4& ink = ok ? palette::kOk : palette::kBad;

  const Fonts& f = GetFonts();
  const ImVec2 label_size = Measure(f.caption, text);
  const float dot = 7.0f;
  const float pad_x = 10.0f;
  const float gap = 7.0f;
  const ImVec2 size(pad_x * 2.0f + dot + gap + label_size.x, 24.0f);

  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(wash),
                    size.y * 0.5f);
  dl->AddCircleFilled(ImVec2(p.x + pad_x + dot * 0.5f, p.y + size.y * 0.5f), dot * 0.5f,
                      ImGui::GetColorU32(ink));
  dl->AddText(f.caption, f.caption->LegacySize,
              ImVec2(p.x + pad_x + dot + gap, p.y + (size.y - label_size.y) * 0.5f),
              ImGui::GetColorU32(ink), text);

  ImGui::Dummy(size);
}

// A switch. Returns true when the value changed.
inline bool Toggle(const char* id, bool* value, float width = 46.0f, float height = 26.0f) {
  ImGui::PushID(id);

  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // Invisible hit area covering the whole switch.
  ImGui::InvisibleButton("##hit", ImVec2(width, height));
  if (ImGui::IsItemClicked()) {
    *value = !*value;
  }
  const bool hovered = ImGui::IsItemHovered();

  const float radius = height * 0.5f;
  const ImU32 track = *value ? ImGui::GetColorU32(palette::kAccent)
                             : ImGui::GetColorU32(palette::kField);
  const ImU32 track_hover = *value ? ImGui::GetColorU32(palette::kAccentHot)
                                   : ImGui::GetColorU32(palette::kFieldHover);
  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), hovered ? track_hover : track, radius);

  if (*value) {
    // A soft glow so the on-state reads as the primary action.
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                      ImGui::GetColorU32(ImVec4(palette::kAccentHot.x, palette::kAccentHot.y,
                                                palette::kAccentHot.z, 0.25f)),
                      radius);
  }

  const float knob_r = radius - 3.0f;
  const float knob_x = *value ? (p.x + width - radius) : (p.x + radius);
  dl->AddCircleFilled(ImVec2(knob_x, p.y + radius), knob_r, ImGui::GetColorU32(palette::kText));

  ImGui::PopID();
  return ImGui::IsItemDeactivatedAfterEdit();
}

// Primary action button: accent fill, subtle vertical gradient, hover lift.
inline bool PrimaryButton(const char* label, const ImVec2& size) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float radius = metrics::kRadiusSmall;

  ImGui::InvisibleButton(label, size);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  const bool clicked = ImGui::IsItemClicked();

  const ImVec4 top = hovered ? palette::kAccentHot : palette::kAccent;
  const ImVec4 bottom = hovered ? palette::kAccent : palette::kAccentDeep;
  dl->AddRectFilledMultiColor(p, ImVec2(p.x + size.x, p.y + size.y),
                              ImGui::GetColorU32(top), ImGui::GetColorU32(top),
                              ImGui::GetColorU32(bottom), ImGui::GetColorU32(bottom));

  if (hovered || held) {
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y),
                ImGui::GetColorU32(ImVec4(1, 1, 1, held ? 0.35f : 0.18f)), radius, 0, 1.0f);
  }

  const Fonts& f = GetFonts();
  const ImVec2 label_size = Measure(f.emphasis, label);
  dl->AddText(f.emphasis, f.emphasis->LegacySize,
              ImVec2(p.x + (size.x - label_size.x) * 0.5f,
                     p.y + (size.y - label_size.y) * 0.5f),
              ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), label);

  return clicked;
}

// Secondary button: transparent fill, hairline border.
inline bool GhostButton(const char* label, const ImVec2& size) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float radius = metrics::kRadiusSmall;

  ImGui::InvisibleButton(label, size);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  const bool clicked = ImGui::IsItemClicked();

  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                    ImGui::GetColorU32(hovered ? palette::kFieldHover : palette::kField), radius);
  dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(palette::kBorder),
              radius, 0, 1.0f);

  const Fonts& f = GetFonts();
  const ImVec2 label_size = Measure(f.body, label);
  dl->AddText(f.body, f.body->LegacySize,
              ImVec2(p.x + (size.x - label_size.x) * 0.5f,
                     p.y + (size.y - label_size.y) * 0.5f),
              ImGui::GetColorU32(hovered ? palette::kText : palette::kTextMuted), label);

  return clicked;
}

// Sidebar navigation row: icon, label, and an accent bar when active.
inline bool NavItem(const char* label, Icon icon, bool active, float width) {
  const Fonts& f = GetFonts();
  const float height = metrics::kNavRowHeight;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  ImGui::InvisibleButton(label, ImVec2(width, height));
  const bool hovered = ImGui::IsItemHovered();
  const bool clicked = ImGui::IsItemClicked();

  if (active) {
    // Tinted plate plus a 3px accent bar down the leading edge.
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                      ImGui::GetColorU32(palette::kAccentWash), metrics::kRadiusSmall, 0);
    dl->AddRectFilled(p, ImVec2(p.x + 3.0f, p.y + height), ImGui::GetColorU32(palette::kAccent),
                      0.0f);
  } else if (hovered) {
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                      ImGui::GetColorU32(ImVec4(1, 1, 1, 0.04f)), metrics::kRadiusSmall, 0);
  }

  const ImU32 icon_col = ImGui::GetColorU32(active ? palette::kAccentHot : palette::kTextFaint);
  const ImU32 text_col = ImGui::GetColorU32(active ? palette::kText : palette::kTextMuted);

  const float icon_x = p.x + 26.0f;
  const float cy = p.y + height * 0.5f;
  DrawIcon(icon, ImVec2(icon_x, cy), 17.0f, icon_col, 1.6f);

  const ImVec2 label_size = Measure(f.emphasis, label);
  dl->AddText(f.emphasis, f.emphasis->LegacySize,
              ImVec2(icon_x + 20.0f, cy - label_size.y * 0.5f), text_col, label);

  return clicked;
}

// ===========================================================================
// Setting rows
//
// Label and hint on the left, control pinned to the right, hairline between
// rows. This is the workhorse of every settings page.
//
// The control is drawn by a callback so the row can measure the left column
// and vertically centre whatever the callback emits.
// ===========================================================================
template <typename F>
inline void SettingRow(const char* label, const char* hint, float control_width, F&& draw_control,
                       bool first = false) {
  const Fonts& f = GetFonts();
  const float avail = GetContentRegionAvail().x;
  const float gap = 28.0f;
  const float label_w = avail - control_width - gap;

  if (!first) {
    Dummy(ImVec2(0, metrics::kRowGap));
    Hairline();
    Dummy(ImVec2(0, metrics::kRowGap));
  }

  const ImVec2 origin = GetCursorScreenPos();

  // --- Left column: label + hint, wrapped to the remaining width ---
  PushTextWrapPos(origin.x + label_w);
  PushFont(f.emphasis, f.emphasis->LegacySize);
  PushStyleColor(ImGuiCol_Text, palette::kText);
  TextUnformatted(label);
  PopStyleColor();
  PopFont();
  if (hint && *hint) {
    Hint(hint);
  }
  PopTextWrapPos();

  const float left_h = GetCursorScreenPos().y - origin.y;

  // --- Right column: the control, vertically centred on the left block ---
  SetCursorScreenPos(ImVec2(origin.x + avail - control_width, origin.y));
  draw_control();
  const float control_h = GetCursorScreenPos().y - origin.y;

  // Advance past whichever column is taller. The trailing Dummy matters: it
  // submits an item, which is what lets ImGui grow the window's content bounds.
  // Without it the next SetCursorScreenPos trips the "extend window boundaries"
  // assert and the card's content overflows.
  const float row_h = ImMax(left_h, control_h);
  SetCursorScreenPos(ImVec2(origin.x, origin.y + row_h));
  Dummy(ImVec2(0.0f, 0.0f));
}

// A key/value row for read-only summaries.
inline void KeyValueRow(const char* key, const char* value, const ImVec4& value_color) {
  const Fonts& f = GetFonts();
  const float avail = GetContentRegionAvail().x;
  const ImVec2 value_size = Measure(f.body, value);

  PushFont(f.body, f.body->LegacySize);
  PushStyleColor(ImGuiCol_Text, palette::kTextMuted);
  TextUnformatted(key);
  PopStyleColor();
  SameLine(avail - value_size.x);
  PushStyleColor(ImGuiCol_Text, value_color);
  TextUnformatted(value);
  PopStyleColor();
  PopFont();
}

}  // namespace launcher
