// Poker Smash Launcher implementation.

#include "launcher_app.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include <imgui.h>
#include <imgui_internal.h>

#include "launcher_theme.h"
#include "roboto_medium_embedded.h"

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/presenter.h>

#if REX_PLATFORM_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace pokersmash_launcher {

namespace {

using launcher::Icon;
namespace metrics = launcher::metrics;
namespace palette = launcher::palette;

// The game executable the launcher starts.
constexpr const char* kGameExeName = "pokersmash.exe";

// Combo option lists. The empty string means "auto" / unset.
struct Option {
  const char* label;
  const char* value;
};

constexpr Option kBackendOptions[] = {
    {"Auto (recommended)", "auto"},
    {"Direct3D 12", "d3d12"},
    {"Vulkan", "vulkan"},
};

constexpr Option kDisplayOptions[] = {
    {"Windowed", "windowed"},
    {"Borderless", "borderless"},
    {"Fullscreen", "fullscreen"},
};

constexpr Option kResolutionOptions[] = {
    {"Auto (native)", "auto"},
    {"720p", "720p"},
    {"1080p", "1080p"},
    {"1440p", "1440p"},
    {"2160p", "2160p"},
};

constexpr Option kFrameLimitOptions[] = {
    {"Auto", "auto"},
    {"30", "30"},
    {"60", "60"},
    {"120", "120"},
    {"Unlimited", "unlimited"},
};

constexpr Option kLanguageOptions[] = {
    {"English", "English"},
    {"Japanese", "Japanese"},
    {"French", "French"},
    {"German", "German"},
    {"Spanish", "Spanish"},
    {"Italian", "Italian"},
};

// ---------------------------------------------------------------------------
// Combo
//
// ImGui's own combo is styled through the global style, so this only has to
// place it and keep the label/value mapping.
// ---------------------------------------------------------------------------
bool ComboSetting(const char* id, const char* label, const Option* options, size_t count,
                  std::string& value, float width) {
  const launcher::Fonts& f = launcher::GetFonts();

  const char* current = options[0].label;
  for (size_t i = 0; i < count; ++i) {
    if (value == options[i].value) {
      current = options[i].label;
      break;
    }
  }

  ImGui::PushID(id);
  ImGui::PushFont(f.body, f.body->LegacySize);
  ImGui::SetNextItemWidth(width);
  bool changed = false;
  if (ImGui::BeginCombo("##combo", current)) {
    for (size_t i = 0; i < count; ++i) {
      const bool selected = (value == options[i].value);
      if (ImGui::Selectable(options[i].label, selected)) {
        value = options[i].value;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopFont();
  ImGui::PopID();
  return changed;
}

// ---------------------------------------------------------------------------
// Page header
//
// Icon in a tinted square, then the page title and a one-line description.
// ---------------------------------------------------------------------------
void PageHeader(const char* title, const char* description, Icon icon) {
  const launcher::Fonts& f = launcher::GetFonts();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float badge = 52.0f;

  // Icon badge.
  dl->AddRectFilled(origin, ImVec2(origin.x + badge, origin.y + badge),
                    ImGui::GetColorU32(palette::kAccentWash), metrics::kRadius);
  dl->AddRect(origin, ImVec2(origin.x + badge, origin.y + badge),
              ImGui::GetColorU32(ImVec4(palette::kAccent.x, palette::kAccent.y,
                                        palette::kAccent.z, 0.35f)),
              metrics::kRadius, 0, 1.0f);
  launcher::DrawIcon(icon, ImVec2(origin.x + badge * 0.5f, origin.y + badge * 0.5f), 24.0f,
                     ImGui::GetColorU32(palette::kAccentHot), 1.8f);

  // Title + description, to the right of the badge.
  const float text_x = origin.x + badge + 20.0f;
  const ImVec2 title_size = f.title->CalcTextSizeA(f.title->LegacySize, FLT_MAX, 0.0f, title);
  const float text_y = origin.y + (badge - title_size.y - 20.0f) * 0.5f;

  dl->AddText(f.title, f.title->LegacySize, ImVec2(text_x, text_y),
              ImGui::GetColorU32(palette::kText), title);
  dl->AddText(f.caption, f.caption->LegacySize, ImVec2(text_x, text_y + title_size.y + 6.0f),
              ImGui::GetColorU32(palette::kTextMuted), description);

  ImGui::Dummy(ImVec2(0, badge));
  ImGui::Dummy(ImVec2(0, 26.0f));
}

std::string Trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string Unquote(std::string value) {
  value = Trim(std::move(value));
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

std::string Quote(const std::string& value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

}  // namespace

// ===========================================================================
// Construction / lifecycle
// ===========================================================================

LauncherApp::LauncherApp(rex::ui::WindowedAppContext& app_context)
    : rex::ui::WindowedApp(app_context, "pokersmash_launcher"), app_context_(app_context) {}

LauncherApp::~LauncherApp() = default;

std::unique_ptr<rex::ui::WindowedApp> LauncherApp::Create(
    rex::ui::WindowedAppContext& app_context) {
  return std::unique_ptr<rex::ui::WindowedApp>(new LauncherApp(app_context));
}

bool LauncherApp::OnInitialize() {
  LoadSettings();

  // Discover the game once so the Home page can report what was found.
  detected_roots_ = DiscoverGameRoots();
  if (!detected_roots_.empty() && settings_.game_data_root.empty()) {
    settings_.game_data_root = detected_roots_.front().string();
  }

  const auto dir = LauncherDir();
  const auto game_exe = dir / kGameExeName;
  if (std::filesystem::exists(game_exe)) {
    detected_game_exe_ = game_exe.string();
  }

  const auto gpu_dll = dir / "rexgpu-xenos.dll";
  detected_dll_status_ =
      std::filesystem::exists(gpu_dll) ? "rexgpu-xenos.dll found (D3D12 + Vulkan)"
                                       : "rexgpu-xenos.dll NOT found next to the launcher";

  if (!SetupGraphics()) {
    return false;
  }
  if (!SetupUi()) {
    return false;
  }

  // The first frame is drawn by the ImGui drawer's own loop; calling DrawFrame
  // here would run ImGui widget code outside a NewFrame/EndFrame pair.
  return true;
}

void LauncherApp::OnDestroy() {
  SaveSettings();

  // Tear down in reverse order: the ImGui drawer references the presenter and
  // the immediate drawer, so it must go first.
  imgui_drawer_.reset();
  immediate_drawer_.reset();
  if (window_) {
    window_->SetPresenter(nullptr);
  }
  presenter_.reset();
  window_.reset();
  graphics_provider_.reset();
}

bool LauncherApp::SetupGraphics() {
  // The launcher renders its own UI, so it needs a presenter but no emulated
  // GPU. D3D12 is the only presenter backend exercised here; it is available on
  // every Windows 10+ machine the game itself targets.
  if (!rex::ui::d3d12::D3D12Provider::IsD3D12APIAvailable()) {
    REXLOG_ERROR("Launcher: D3D12 is not available on this system");
    return false;
  }

  graphics_provider_ = rex::ui::d3d12::D3D12Provider::Create();
  if (!graphics_provider_) {
    REXLOG_ERROR("Launcher: failed to create the D3D12 provider");
    return false;
  }
  return true;
}

bool LauncherApp::SetupUi() {
  window_ = rex::ui::Window::Create(app_context(), "Poker Smash Launcher", 1100, 700);
  if (!window_) {
    REXLOG_ERROR("Launcher: failed to create the window");
    return false;
  }
  window_->SetTitle("Poker Smash Launcher");
  window_->AddListener(this);
  window_->AddInputListener(this, 0);

  presenter_ = graphics_provider_->CreatePresenter();
  if (!presenter_) {
    REXLOG_ERROR("Launcher: failed to create the presenter");
    return false;
  }
  immediate_drawer_ = graphics_provider_->CreateImmediateDrawer();
  if (!immediate_drawer_) {
    REXLOG_ERROR("Launcher: failed to create the immediate drawer");
    return false;
  }
  immediate_drawer_->SetPresenter(presenter_.get());
  window_->SetPresenter(presenter_.get());

  imgui_drawer_ = std::make_unique<rex::ui::ImGuiDrawer>(
      window_.get(), 64,
      [](ImFontAtlas* atlas) {
        // The SDK seeds the atlas with ProggyTiny plus a merged Japanese font.
        // Neither is wanted here: ProggyTiny is a 10px bitmap-ish face that
        // makes the whole UI look like a debug tool, and the launcher shows no
        // Japanese text. Start from an empty atlas and load the embedded Roboto
        // Medium at every size the type scale needs.
        atlas->Clear();

        launcher::Fonts& fonts = launcher::GetFonts();

        // The font bytes live in a static array, so the atlas must not try to
        // free them when it is torn down.
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;

        static const ImWchar ranges[] = {
            0x0020, 0x00FF,  // Basic Latin + Latin Supplement
            0x2013, 0x2014,  // en dash, em dash
            0x2022, 0x2022,  // bullet
            0x2192, 0x2192,  // rightwards arrow
            0,
        };

        // Index 0 is the default, so any ImGui::Text that forgets to push a
        // font still renders at body size instead of falling back to something
        // unreadable.
        fonts.micro = atlas->AddFontFromMemoryTTF(
            const_cast<void*>(static_cast<const void*>(::kRobotoMedium)),
            static_cast<int>(::kRobotoMediumSize), 12.0f, &cfg, ranges);
        fonts.caption = atlas->AddFontFromMemoryTTF(
            const_cast<void*>(static_cast<const void*>(::kRobotoMedium)),
            static_cast<int>(::kRobotoMediumSize), 13.0f, &cfg, ranges);
        fonts.body = atlas->AddFontFromMemoryTTF(
            const_cast<void*>(static_cast<const void*>(::kRobotoMedium)),
            static_cast<int>(::kRobotoMediumSize), 15.0f, &cfg, ranges);
        fonts.emphasis = atlas->AddFontFromMemoryTTF(
            const_cast<void*>(static_cast<const void*>(::kRobotoMedium)),
            static_cast<int>(::kRobotoMediumSize), 17.0f, &cfg, ranges);
        fonts.title = atlas->AddFontFromMemoryTTF(
            const_cast<void*>(static_cast<const void*>(::kRobotoMedium)),
            static_cast<int>(::kRobotoMediumSize), 26.0f, &cfg, ranges);
        fonts.display = atlas->AddFontFromMemoryTTF(
            const_cast<void*>(static_cast<const void*>(::kRobotoMedium)),
            static_cast<int>(::kRobotoMediumSize), 40.0f, &cfg, ranges);
      },
      [](ImGuiStyle& s, rex::ui::Style&) {
        // --- Geometry: soft, consistent rounding ---
        s.WindowRounding = 0.0f;
        s.ChildRounding = metrics::kRadius;
        s.FrameRounding = metrics::kRadiusSmall;
        s.GrabRounding = metrics::kRadiusSmall;
        s.PopupRounding = metrics::kRadiusSmall;
        s.ScrollbarRounding = metrics::kRadiusSmall;
        s.TabRounding = metrics::kRadiusSmall;

        s.WindowBorderSize = 0.0f;
        s.ChildBorderSize = 1.0f;
        s.FrameBorderSize = 0.0f;
        s.PopupBorderSize = 1.0f;

        s.WindowPadding = ImVec2(0, 0);
        s.FramePadding = ImVec2(12, 9);
        s.ItemSpacing = ImVec2(8, 8);
        s.ItemInnerSpacing = ImVec2(8, 6);
        s.ScrollbarSize = 10.0f;
        s.GrabMinSize = 12.0f;

        // --- Surfaces ---
        s.Colors[ImGuiCol_WindowBg] = palette::kBg;
        s.Colors[ImGuiCol_ChildBg] = palette::kCard;
        s.Colors[ImGuiCol_PopupBg] = ImVec4(0.106f, 0.114f, 0.129f, 0.98f);
        s.Colors[ImGuiCol_Border] = palette::kBorder;
        s.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_Separator] = palette::kBorder;

        // --- Text ---
        s.Colors[ImGuiCol_Text] = palette::kText;
        s.Colors[ImGuiCol_TextDisabled] = palette::kTextFaint;
        s.Colors[ImGuiCol_TextSelectedBg] = palette::kAccentWash;

        // --- Inputs ---
        s.Colors[ImGuiCol_FrameBg] = palette::kField;
        s.Colors[ImGuiCol_FrameBgHovered] = palette::kFieldHover;
        s.Colors[ImGuiCol_FrameBgActive] = palette::kFieldHover;

        // --- Interactive ---
        s.Colors[ImGuiCol_CheckMark] = palette::kAccentHot;
        s.Colors[ImGuiCol_SliderGrab] = palette::kAccent;
        s.Colors[ImGuiCol_SliderGrabActive] = palette::kAccentHot;
        s.Colors[ImGuiCol_Button] = palette::kField;
        s.Colors[ImGuiCol_ButtonHovered] = palette::kFieldHover;
        s.Colors[ImGuiCol_ButtonActive] = palette::kFieldHover;
        s.Colors[ImGuiCol_Header] = palette::kField;
        s.Colors[ImGuiCol_HeaderHovered] = palette::kFieldHover;
        s.Colors[ImGuiCol_HeaderActive] = palette::kFieldHover;

        // --- Scrollbar ---
        s.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.24f, 0.27f, 1.0f);
        s.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.32f, 0.36f, 1.0f);
        s.Colors[ImGuiCol_ScrollbarGrabActive] = palette::kAccent;
      });
  imgui_drawer_->SetPresenterAndImmediateDrawer(presenter_.get(), immediate_drawer_.get());

  // The launcher draws a full-window UI itself; a dialog is the simplest way to
  // get a per-frame Draw callback from the drawer.
  window_->Open();
  frame_dialog_ = std::make_unique<FrameDialog>(imgui_drawer_.get(), this);
  return true;
}

void LauncherApp::FrameDialog::OnDraw(ImGuiIO&) {
  app_->DrawFrame();
}

// ===========================================================================
// UI
// ===========================================================================

void LauncherApp::DrawFrame() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);

  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##root", nullptr, flags);
  ImGui::PopStyleVar();

  // Explicit layout: the root window is exactly the viewport, so the three
  // regions are placed by position rather than by implicit cursor flow. That
  // keeps the bottom bar on screen regardless of how tall the page content is.
  const float total_w = ImGui::GetWindowWidth();
  const float total_h = ImGui::GetWindowHeight();
  const float bottom_h = metrics::kBottomBarHeight;
  const float content_h = total_h - bottom_h;

  ImDrawList* bg = ImGui::GetWindowDrawList();

  // --- Sidebar backdrop: a vertical gradient, lighter at the top ---
  bg->AddRectFilledMultiColor(
      ImVec2(0, 0), ImVec2(metrics::kSidebarWidth, content_h),
      ImGui::GetColorU32(palette::kSidebarTop), ImGui::GetColorU32(palette::kSidebarTop),
      ImGui::GetColorU32(palette::kSidebarBottom), ImGui::GetColorU32(palette::kSidebarBottom));

  // A hairline where the sidebar meets the content, so the two surfaces read as
  // separate planes rather than one flat colour.
  bg->AddLine(ImVec2(metrics::kSidebarWidth, 0), ImVec2(metrics::kSidebarWidth, content_h),
              ImGui::GetColorU32(palette::kBorder), 1.0f);

  // --- Sidebar content ---
  ImGui::SetCursorPos(ImVec2(0, 0));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::BeginChild("##sidebar", ImVec2(metrics::kSidebarWidth, content_h), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
  DrawSidebarContents();
  ImGui::EndChild();
  ImGui::PopStyleColor();

  // --- Content ---
  const float content_w = total_w - metrics::kSidebarWidth;
  ImGui::SetCursorPos(ImVec2(metrics::kSidebarWidth, 0));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::BeginChild("##content", ImVec2(content_w, content_h), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(metrics::kPagePadding, metrics::kPagePadding));
  ImGui::BeginChild("##page", ImVec2(0, 0), false);

  switch (settings_.page) {
    case Page::kHome:
      DrawHomePage();
      break;
    case Page::kGraphics:
      DrawGraphicsPage();
      break;
    case Page::kPerformance:
      DrawPerformancePage();
      break;
    case Page::kGame:
      DrawGamePage();
      break;
    case Page::kAdvanced:
      DrawAdvancedPage();
      break;
  }

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::PopStyleColor();

  // --- Bottom bar ---
  // A soft shadow falling onto the content above the bar, then the bar itself.
  const float shadow_h = 18.0f;
  bg->AddRectFilledMultiColor(
      ImVec2(metrics::kSidebarWidth, content_h - shadow_h),
      ImVec2(total_w, content_h), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.0f)),
      ImGui::GetColorU32(ImVec4(0, 0, 0, 0.0f)), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.45f)),
      ImGui::GetColorU32(ImVec4(0, 0, 0, 0.45f)));
  bg->AddRectFilled(ImVec2(metrics::kSidebarWidth, content_h), ImVec2(total_w, total_h),
                    ImGui::GetColorU32(palette::kCard), 0.0f);
  bg->AddLine(ImVec2(metrics::kSidebarWidth, content_h), ImVec2(total_w, content_h),
              ImGui::GetColorU32(palette::kBorder), 1.0f);

  ImGui::SetCursorPos(ImVec2(metrics::kSidebarWidth, content_h));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::BeginChild("##bottom", ImVec2(content_w, bottom_h), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
  DrawBottomBarContents();
  ImGui::EndChild();
  ImGui::PopStyleColor();

  ImGui::End();
}

void LauncherApp::DrawSidebarContents() {
  const launcher::Fonts& f = launcher::GetFonts();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // --- Brand block ---
  ImGui::Dummy(ImVec2(0, 26.0f));

  const ImVec2 brand_origin = ImGui::GetCursorScreenPos();
  const float brand_x = brand_origin.x + 26.0f;

  // A small accent mark above the wordmark.
  dl->AddRectFilled(ImVec2(brand_x, brand_origin.y), ImVec2(brand_x + 26.0f, brand_origin.y + 3.0f),
                    ImGui::GetColorU32(palette::kAccent), 2.0f);

  const ImVec2 brand_size = f.display->CalcTextSizeA(f.display->LegacySize, FLT_MAX, 0.0f,
                                                     "POKER SMASH");
  dl->AddText(f.display, f.display->LegacySize, ImVec2(brand_x, brand_origin.y + 14.0f),
              ImGui::GetColorU32(palette::kText), "POKER SMASH");
  dl->AddText(f.caption, f.caption->LegacySize, ImVec2(brand_x, brand_origin.y + 14.0f + brand_size.y + 2.0f),
              ImGui::GetColorU32(palette::kTextFaint), "Xbox 360 recompilation");

  const float brand_block_h = 14.0f + brand_size.y + 2.0f + f.caption->LegacySize;
  ImGui::Dummy(ImVec2(0, brand_block_h));

  ImGui::Dummy(ImVec2(0, 30.0f));
  launcher::Overline("MENU");
  ImGui::Dummy(ImVec2(0, 12.0f));

  // --- Navigation ---
  struct NavEntry {
    const char* label;
    Page page;
    Icon icon;
  };
  const NavEntry entries[] = {
      {"Home", Page::kHome, Icon::kHome},
      {"Graphics", Page::kGraphics, Icon::kGraphics},
      {"Performance", Page::kPerformance, Icon::kPerformance},
      {"Game", Page::kGame, Icon::kGame},
      {"Advanced", Page::kAdvanced, Icon::kAdvanced},
  };

  const float row_w = metrics::kSidebarWidth - 20.0f;
  ImGui::SetCursorPosX(10.0f);
  for (const auto& entry : entries) {
    const bool active = settings_.page == entry.page;
    if (launcher::NavItem(entry.label, entry.icon, active, row_w)) {
      settings_.page = entry.page;
    }
    ImGui::Dummy(ImVec2(0, 2.0f));
  }

  // --- Footer: build identity, pinned to the bottom of the sidebar ---
  // The sidebar child is exactly the content height, so measure against it.
  const float footer_h = 46.0f;
  ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footer_h - 12.0f);
  dl->AddLine(ImVec2(brand_x, ImGui::GetCursorScreenPos().y),
              ImVec2(metrics::kSidebarWidth - 20.0f, ImGui::GetCursorScreenPos().y),
              ImGui::GetColorU32(palette::kBorder), 1.0f);
  ImGui::Dummy(ImVec2(0, 12.0f));
  ImGui::SetCursorPosX(26.0f);
  dl->AddText(f.caption, f.caption->LegacySize, ImGui::GetCursorScreenPos(),
              ImGui::GetColorU32(palette::kTextFaint), "ReXGlue 0.10.0");
  ImGui::Dummy(ImVec2(0, f.caption->LegacySize));
}

void LauncherApp::DrawHomePage() {
  const launcher::Fonts& f = launcher::GetFonts();

  PageHeader("Home", "Check that everything is in place, then start the game.",
             Icon::kHome);

  const bool have_game = !detected_game_exe_.empty();
  const bool have_data =
      !settings_.game_data_root.empty() && LooksLikeGameRoot(settings_.game_data_root);
  const bool have_gpu = detected_dll_status_.find("found") != std::string::npos;

  // --- Readiness card ---
  if (launcher::BeginCard("##status")) {
    launcher::Overline("SYSTEM CHECK");
    ImGui::Dummy(ImVec2(0, 16.0f));

    struct Check {
      const char* label;
      bool ok;
      std::string detail;
    };
    const Check checks[] = {
        {"Game executable", have_game,
         have_game ? detected_game_exe_
                   : std::string("Place pokersmash.exe next to the launcher.")},
        {"Game data", have_data,
         have_data ? settings_.game_data_root
                   : std::string("Set the game folder on the Game page.")},
        {"GPU plugin", have_gpu,
         have_gpu ? std::string("rexgpu-xenos.dll — D3D12 + Vulkan")
                  : std::string("rexgpu-xenos.dll is missing next to the launcher.")},
    };

    const float avail = ImGui::GetContentRegionAvail().x;
    const float chip_w = 132.0f;

    for (size_t i = 0; i < std::size(checks); ++i) {
      const Check& c = checks[i];

      if (i > 0) {
        ImGui::Dummy(ImVec2(0, 14.0f));
        launcher::Hairline();
        ImGui::Dummy(ImVec2(0, 14.0f));
      }

      const ImVec2 row = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // Status dot.
      const ImVec4& dot = c.ok ? palette::kOk : palette::kBad;
      dl->AddCircleFilled(ImVec2(row.x + 5.0f, row.y + 9.0f), 4.0f, ImGui::GetColorU32(dot));

      // Label and detail path.
      dl->AddText(f.emphasis, f.emphasis->LegacySize, ImVec2(row.x + 22.0f, row.y),
                  ImGui::GetColorU32(palette::kText), c.label);
      dl->AddText(f.caption, f.caption->LegacySize, ImVec2(row.x + 22.0f, row.y + 24.0f),
                  ImGui::GetColorU32(palette::kTextFaint), c.detail.c_str());

      // Chip on the right.
      ImGui::SetCursorScreenPos(ImVec2(row.x + avail - chip_w, row.y + 1.0f));
      launcher::StatusChip(c.ok, c.ok ? "Ready" : "Missing");

      // Advance to the bottom of the row. The trailing Dummy submits an item so
      // ImGui grows the card's content bounds; without it the last row trips the
      // "extend window boundaries" assert.
      ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + 46.0f));
      ImGui::Dummy(ImVec2(0, 0));
    }
  }
  launcher::EndCard();

  ImGui::Dummy(ImVec2(0, 20.0f));

  // --- Current configuration card ---
  if (launcher::BeginCard("##config")) {
    launcher::Overline("CURRENT CONFIGURATION");
    ImGui::Dummy(ImVec2(0, 16.0f));

    const char* backend_label = "Auto";
    for (const auto& o : kBackendOptions) {
      if (settings_.gpu_backend == o.value) {
        backend_label = o.label;
        break;
      }
    }
    const char* display_label = "Windowed";
    for (const auto& o : kDisplayOptions) {
      if (settings_.display_mode == o.value) {
        display_label = o.label;
        break;
      }
    }

    launcher::KeyValueRow("Graphics backend", backend_label, palette::kAccentHot);
    ImGui::Dummy(ImVec2(0, 12.0f));
    launcher::KeyValueRow("Display mode", display_label, palette::kText);
    ImGui::Dummy(ImVec2(0, 12.0f));
    launcher::KeyValueRow("Frame limit", settings_.frame_limit.c_str(), palette::kText);
    ImGui::Dummy(ImVec2(0, 12.0f));
    launcher::KeyValueRow("VSync", settings_.vsync ? "On" : "Off", palette::kText);
  }
  launcher::EndCard();
}

void LauncherApp::DrawGraphicsPage() {
  PageHeader("Graphics", "How the game appears on your screen.", Icon::kGraphics);

  if (launcher::BeginCard("##graphics")) {
    launcher::SettingRow("Graphics backend",
                         "Direct3D 12 is the better-tested path. Vulkan is there if "
                         "Direct3D 12 does not work. Auto tries Direct3D 12 first.",
                         240.0f,
                         [&] { ComboSetting("backend", nullptr, kBackendOptions,
                                            std::size(kBackendOptions), settings_.gpu_backend,
                                            240.0f); });

    launcher::SettingRow("Display mode",
                         "Borderless covers the screen without changing the display mode.",
                         240.0f,
                         [&] { ComboSetting("display", nullptr, kDisplayOptions,
                                            std::size(kDisplayOptions), settings_.display_mode,
                                            240.0f); });

    launcher::SettingRow("Render resolution",
                         "Auto uses the window size. A fixed resolution renders at that "
                         "size and scales to fit.",
                         240.0f,
                         [&] { ComboSetting("res", nullptr, kResolutionOptions,
                                            std::size(kResolutionOptions),
                                            settings_.render_resolution, 240.0f); });

    launcher::SettingRow("Vertical sync",
                         "Off allows tearing in exchange for the lowest latency.",
                         46.0f,
                         [&] { launcher::Toggle("vsync", &settings_.vsync); });
  }
  launcher::EndCard();
}

void LauncherApp::DrawPerformancePage() {
  PageHeader("Performance", "Frame pacing and diagnostics.", Icon::kPerformance);

  if (launcher::BeginCard("##perf")) {
    launcher::SettingRow("Frame limit",
                         "Auto follows the display refresh rate. The original game runs "
                         "at 60.",
                         240.0f,
                         [&] { ComboSetting("framelimit", nullptr, kFrameLimitOptions,
                                            std::size(kFrameLimitOptions),
                                            settings_.frame_limit, 240.0f); });

    launcher::SettingRow("Performance overlay",
                         "Draws frame time and counters over the game. Toggle in game "
                         "with F3.",
                         46.0f,
                         [&] {
                           launcher::Toggle("overlay", &settings_.show_performance_overlay);
                         });
  }
  launcher::EndCard();
}

void LauncherApp::DrawGamePage() {
  PageHeader("Game", "Your game dump and language.", Icon::kGame);

  if (launcher::BeginCard("##game")) {
    const launcher::Fonts& f = launcher::GetFonts();

    // --- Game folder ---
    ImGui::PushFont(f.emphasis);
    ImGui::PushStyleColor(ImGuiCol_Text, palette::kText);
    ImGui::TextUnformatted("Game folder");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    launcher::Hint("The folder containing default.xex and the Data/ directory.");
    ImGui::Dummy(ImVec2(0, 12.0f));

    std::array<char, 1024> path_buf{};
    std::snprintf(path_buf.data(), path_buf.size(), "%s", settings_.game_data_root.c_str());
    ImGui::PushFont(f.body, f.body->LegacySize);
    ImGui::SetNextItemWidth(-250.0f);
    if (ImGui::InputText("##gameroot", path_buf.data(), path_buf.size())) {
      settings_.game_data_root = path_buf.data();
    }
    ImGui::PopFont();
    ImGui::SameLine();
    if (launcher::GhostButton("Browse", ImVec2(112, metrics::kButtonHeight))) {
      // Native folder picker; keeps the launcher dependency-free.
#if REX_PLATFORM_WIN32
      BROWSEINFOW bi{};
      bi.lpszTitle = L"Select the folder containing default.xex";
      bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
      LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
      if (pidl) {
        wchar_t wide[MAX_PATH]{};
        if (SHGetPathFromIDListW(pidl, wide)) {
          char utf8[MAX_PATH * 3]{};
          WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
          settings_.game_data_root = utf8;
        }
        CoTaskMemFree(pidl);
      }
#endif
    }
    ImGui::SameLine();
    if (launcher::GhostButton("Open", ImVec2(112, metrics::kButtonHeight))) {
      if (!settings_.game_data_root.empty()) {
#if REX_PLATFORM_WIN32
        ShellExecuteW(nullptr, L"open",
                      std::filesystem::path(settings_.game_data_root).wstring().c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
#endif
      }
    }

    // Detected candidates, offered as one-click fills.
    if (!detected_roots_.empty()) {
      ImGui::Dummy(ImVec2(0, 18.0f));
      launcher::Overline("DETECTED NEXT TO THE LAUNCHER");
      ImGui::Dummy(ImVec2(0, 10.0f));
      for (const auto& root : detected_roots_) {
        const std::string label = root.string();
        const bool selected = settings_.game_data_root == label;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float row_h = 34.0f;
        const float row_w = ImGui::GetContentRegionAvail().x;

        ImGui::InvisibleButton(label.c_str(), ImVec2(row_w, row_h));
        if (ImGui::IsItemHovered()) {
          dl->AddRectFilled(p, ImVec2(p.x + row_w, p.y + row_h),
                            ImGui::GetColorU32(palette::kField), metrics::kRadiusSmall);
        }
        if (selected) {
          dl->AddRectFilled(p, ImVec2(p.x + 3.0f, p.y + row_h),
                            ImGui::GetColorU32(palette::kAccent), 0.0f);
        }
        dl->AddText(f.caption, f.caption->LegacySize, ImVec2(p.x + 14.0f, p.y + (row_h - f.caption->LegacySize) * 0.5f),
                    ImGui::GetColorU32(selected ? palette::kText : palette::kTextMuted),
                    label.c_str());
        if (ImGui::IsItemClicked()) {
          settings_.game_data_root = label;
        }
        ImGui::Dummy(ImVec2(0, 2.0f));
      }
    }

    ImGui::Dummy(ImVec2(0, 18.0f));
    launcher::SettingRow("Game language", "Applies to in-game text and menus.", 240.0f,
                         [&] { ComboSetting("lang", nullptr, kLanguageOptions,
                                            std::size(kLanguageOptions), settings_.language,
                                            240.0f); });
  }
  launcher::EndCard();
}

void LauncherApp::DrawAdvancedPage() {
  PageHeader("Advanced", "Options for troubleshooting.", Icon::kAdvanced);

  if (launcher::BeginCard("##advanced")) {
    const launcher::Fonts& f = launcher::GetFonts();

    launcher::SettingRow("Write a log file",
                         "Writes pokersmash.log next to the game. Useful when reporting "
                         "a problem.",
                         46.0f,
                         [&] { launcher::Toggle("logfile", &settings_.log_to_file); });

    // --- Extra command line ---
    ImGui::Dummy(ImVec2(0, 14.0f));
    launcher::Hairline();
    ImGui::Dummy(ImVec2(0, 14.0f));

    ImGui::PushFont(f.emphasis);
    ImGui::PushStyleColor(ImGuiCol_Text, palette::kText);
    ImGui::TextUnformatted("Extra command line");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    launcher::Hint("Appended verbatim. Leave empty unless you know what you need.");
    ImGui::Dummy(ImVec2(0, 10.0f));

    std::array<char, 512> args_buf{};
    std::snprintf(args_buf.data(), args_buf.size(), "%s", settings_.extra_args.c_str());
    ImGui::PushFont(f.body, f.body->LegacySize);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##extra", args_buf.data(), args_buf.size())) {
      settings_.extra_args = args_buf.data();
    }
    ImGui::PopFont();

    // --- Command preview ---
    ImGui::Dummy(ImVec2(0, 20.0f));
    launcher::Overline("COMMAND PREVIEW");
    ImGui::Dummy(ImVec2(0, 10.0f));

    std::string preview = kGameExeName;
    for (const auto& arg : BuildGameArguments()) {
      preview += " " + arg;
    }

    const ImVec2 box = ImGui::GetCursorScreenPos();
    const float box_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 text_size = f.caption->CalcTextSizeA(f.caption->LegacySize, box_w - 28.0f, 0.0f,
                                                    preview.c_str());
    const float box_h = text_size.y + 22.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(box, ImVec2(box.x + box_w, box.y + box_h), ImGui::GetColorU32(palette::kField),
                      metrics::kRadiusSmall);
    dl->AddRect(box, ImVec2(box.x + box_w, box.y + box_h), ImGui::GetColorU32(palette::kBorderSoft),
                metrics::kRadiusSmall, 0, 1.0f);
    dl->AddText(f.caption, f.caption->LegacySize, ImVec2(box.x + 14.0f, box.y + 11.0f),
                ImGui::GetColorU32(palette::kTextMuted), preview.c_str());
    ImGui::Dummy(ImVec2(box_w, box_h));
  }
  launcher::EndCard();
}

void LauncherApp::DrawBottomBarContents() {
  const launcher::Fonts& f = launcher::GetFonts();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // --- Left: status line ---
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float bar_h = ImGui::GetContentRegionAvail().y;
  const float text_y = origin.y + (bar_h - f.body->LegacySize) * 0.5f;

  if (!launch_error_.empty()) {
    dl->AddCircleFilled(ImVec2(origin.x + 32.0f + 4.0f, text_y + f.body->LegacySize * 0.5f), 4.0f,
                        ImGui::GetColorU32(palette::kBad));
    dl->AddText(f.body, f.body->LegacySize, ImVec2(origin.x + 50.0f, text_y),
                ImGui::GetColorU32(palette::kBad), launch_error_.c_str());
  } else {
    dl->AddCircleFilled(ImVec2(origin.x + 32.0f + 4.0f, text_y + f.body->LegacySize * 0.5f), 4.0f,
                        ImGui::GetColorU32(palette::kOk));
    dl->AddText(f.body, f.body->LegacySize, ImVec2(origin.x + 50.0f, text_y),
                ImGui::GetColorU32(palette::kTextMuted), "Ready to play.");
  }

  // --- Right: Save + PLAY ---
  const float play_w = 208.0f;
  const float save_w = 118.0f;
  const float gap = 12.0f;
  const float margin = 32.0f;
  const float bar_w = ImGui::GetContentRegionAvail().x;
  const float buttons_y = origin.y + (bar_h - metrics::kButtonHeight) * 0.5f;

  ImGui::SetCursorScreenPos(ImVec2(origin.x + bar_w - margin - play_w - gap - save_w, buttons_y));
  if (launcher::GhostButton("Save", ImVec2(save_w, metrics::kButtonHeight))) {
    SaveSettings();
  }

  ImGui::SetCursorScreenPos(ImVec2(origin.x + bar_w - margin - play_w, buttons_y));
  if (launcher::PrimaryButton("PLAY", ImVec2(play_w, metrics::kButtonHeight))) {
    SaveSettings();
    LaunchGame();
  }
}

// ===========================================================================
// Window events
// ===========================================================================

void LauncherApp::OnClosing(rex::ui::UIEvent&) {
  app_context_.RequestDeferredQuit();
}

void LauncherApp::OnKeyDown(rex::ui::KeyEvent& e) {
  // Enter launches, Escape closes. Handy when the window has focus but the
  // mouse is elsewhere.
  if (e.virtual_key() == rex::ui::VirtualKey::kReturn) {
    SaveSettings();
    LaunchGame();
  } else if (e.virtual_key() == rex::ui::VirtualKey::kEscape) {
    app_context_.RequestDeferredQuit();
  }
}

// ===========================================================================
// Settings persistence
// ===========================================================================

std::filesystem::path LauncherApp::LauncherDir() const {
  return rex::filesystem::GetExecutableFolder();
}

std::filesystem::path LauncherApp::SettingsPath() const {
  return LauncherDir() / "launcher.toml";
}

void LauncherApp::LoadSettings() {
  std::ifstream in(SettingsPath());
  if (!in) {
    return;
  }
  std::string line;
  bool first_line = true;
  while (std::getline(in, line)) {
    // Editors on Windows happily write a UTF-8 BOM, which would otherwise end up
    // glued to the first key and silently break parsing.
    if (first_line) {
      first_line = false;
      if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
          static_cast<unsigned char>(line[1]) == 0xBB &&
          static_cast<unsigned char>(line[2]) == 0xBF) {
        line.erase(0, 3);
      }
    }
    auto hash = line.find('#');
    if (hash != std::string::npos) {
      line.erase(hash);
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = Trim(line.substr(0, eq));
    const std::string value = Unquote(line.substr(eq + 1));

    if (key == "game_data_root") {
      settings_.game_data_root = value;
    } else if (key == "gpu_backend") {
      settings_.gpu_backend = value;
    } else if (key == "display_mode") {
      settings_.display_mode = value;
    } else if (key == "vsync") {
      settings_.vsync = (value == "true");
    } else if (key == "render_resolution") {
      settings_.render_resolution = value;
    } else if (key == "frame_limit") {
      settings_.frame_limit = value;
    } else if (key == "show_performance_overlay") {
      settings_.show_performance_overlay = (value == "true");
    } else if (key == "language") {
      settings_.language = value;
    } else if (key == "extra_args") {
      settings_.extra_args = value;
    } else if (key == "log_to_file") {
      settings_.log_to_file = (value == "true");
    } else if (key == "page") {
      const int page = std::atoi(value.c_str());
      if (page >= 0 && page <= static_cast<int>(Page::kAdvanced)) {
        settings_.page = static_cast<Page>(page);
      }
    }
  }
}

void LauncherApp::SaveSettings() const {
  std::ofstream out(SettingsPath(), std::ios::trunc);
  if (!out) {
    REXLOG_WARN("Launcher: could not write {}", SettingsPath().string());
    return;
  }
  out << "# Poker Smash Launcher settings\n";
  out << "# Written automatically; edit through the launcher UI.\n\n";
  out << "game_data_root = " << Quote(settings_.game_data_root) << "\n";
  out << "gpu_backend = " << Quote(settings_.gpu_backend) << "\n";
  out << "display_mode = " << Quote(settings_.display_mode) << "\n";
  out << "vsync = " << (settings_.vsync ? "true" : "false") << "\n";
  out << "render_resolution = " << Quote(settings_.render_resolution) << "\n";
  out << "frame_limit = " << Quote(settings_.frame_limit) << "\n";
  out << "show_performance_overlay = " << (settings_.show_performance_overlay ? "true" : "false")
      << "\n";
  out << "language = " << Quote(settings_.language) << "\n";
  out << "extra_args = " << Quote(settings_.extra_args) << "\n";
  out << "log_to_file = " << (settings_.log_to_file ? "true" : "false") << "\n";
  out << "page = " << static_cast<int>(settings_.page) << "\n";
}

// ===========================================================================
// Game discovery / launching
// ===========================================================================

bool LauncherApp::LooksLikeGameRoot(const std::filesystem::path& dir) {
  std::error_code ec;
  return std::filesystem::is_regular_file(dir / "default.xex", ec) && !ec;
}

std::vector<std::filesystem::path> LauncherApp::DiscoverGameRoots() const {
  std::vector<std::filesystem::path> found;

  // Walk up from the launcher's own directory, checking each ancestor for a
  // folder that looks like an extracted game dump. A fixed list of relative
  // paths would miss the build tree (out/build/<preset>/ is four levels below
  // the repo root) and any layout a user happens to pick.
  const std::filesystem::path subdirs[] = {"extracted", "game_data", "game"};

  std::error_code ec;
  auto current = std::filesystem::weakly_canonical(LauncherDir(), ec);
  if (ec) {
    return found;
  }

  // Six levels is enough to get from any plausible build output directory up to
  // the drive root, and keeps the search from wandering the whole filesystem.
  for (int depth = 0; depth < 6 && !current.empty(); ++depth) {
    for (const auto& sub : subdirs) {
      auto candidate = std::filesystem::weakly_canonical(current / sub, ec);
      if (ec) {
        continue;
      }
      if (LooksLikeGameRoot(candidate) &&
          std::find(found.begin(), found.end(), candidate) == found.end()) {
        found.push_back(candidate);
      }
    }

    // The directory itself may already be the game root (the dist/ layout).
    if (LooksLikeGameRoot(current) &&
        std::find(found.begin(), found.end(), current) == found.end()) {
      found.push_back(current);
    }

    auto parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return found;
}

std::vector<std::string> LauncherApp::BuildGameArguments() const {
  std::vector<std::string> args;

  if (!settings_.game_data_root.empty()) {
    args.push_back("--game_data_root=" + settings_.game_data_root);
  }
  if (!settings_.gpu_backend.empty() && settings_.gpu_backend != "auto") {
    args.push_back("--gpu_backend=" + settings_.gpu_backend);
  }
  if (settings_.log_to_file) {
    args.push_back("--log_file=pokersmash.log");
  }

  // Free-form extras, split on whitespace. Quoted segments are kept together.
  if (!settings_.extra_args.empty()) {
    std::string current;
    bool in_quotes = false;
    for (char c : settings_.extra_args) {
      if (c == '"') {
        in_quotes = !in_quotes;
      } else if (std::isspace(static_cast<unsigned char>(c)) && !in_quotes) {
        if (!current.empty()) {
          args.push_back(current);
          current.clear();
        }
      } else {
        current.push_back(c);
      }
    }
    if (!current.empty()) {
      args.push_back(current);
    }
  }

  return args;
}

bool LauncherApp::LaunchGame() {
  launch_error_.clear();

  const auto game_exe = LauncherDir() / kGameExeName;
  if (!std::filesystem::exists(game_exe)) {
    launch_error_ = std::string("Cannot find ") + kGameExeName +
                    " next to the launcher. Build it first (see README).";
    return false;
  }
  if (settings_.game_data_root.empty() || !LooksLikeGameRoot(settings_.game_data_root)) {
    launch_error_ = "Game folder is not set or does not contain default.xex. "
                    "Set it on the Game page.";
    return false;
  }

#if REX_PLATFORM_WIN32
  // Build a single command line string: "exe" "arg" "arg" ...
  std::wstring command = L"\"" + game_exe.wstring() + L"\"";
  for (const auto& arg : BuildGameArguments()) {
    command += L" \"" + std::filesystem::path(arg).wstring() + L"\"";
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  std::wstring mutable_command = command;
  const BOOL ok = CreateProcessW(game_exe.wstring().c_str(), mutable_command.data(), nullptr,
                                 nullptr, FALSE, 0, nullptr, LauncherDir().wstring().c_str(), &si,
                                 &pi);
  if (!ok) {
    launch_error_ = "Failed to start the game (CreateProcess error " +
                    std::to_string(GetLastError()) + ").";
    return false;
  }

  // The launcher does not need to wait; the game owns its own window.
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  REXLOG_INFO("Launcher: started {}", game_exe.string());

  // Close the launcher once the game is running.
  should_quit_ = true;
  app_context_.RequestDeferredQuit();
  return true;
#else
  launch_error_ = "Launching is only implemented on Windows.";
  return false;
#endif
}

}  // namespace pokersmash_launcher
