// Poker Smash Launcher implementation.

#include "launcher_app.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <imgui.h>

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

// --- Palette (dark, emulator-launcher look) ---
constexpr ImVec4 kBg{0.09f, 0.09f, 0.10f, 1.00f};
constexpr ImVec4 kSidebarBg{0.06f, 0.06f, 0.07f, 1.00f};
constexpr ImVec4 kPanelBg{0.12f, 0.12f, 0.14f, 1.00f};
constexpr ImVec4 kAccent{0.85f, 0.20f, 0.20f, 1.00f};
constexpr ImVec4 kAccentHover{0.95f, 0.30f, 0.30f, 1.00f};
constexpr ImVec4 kTextDim{0.60f, 0.60f, 0.62f, 1.00f};

// Width of the left navigation column.
constexpr float kSidebarWidth = 200.0f;

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

// Draws a labelled combo bound to a string setting. Returns true if changed.
bool ComboSetting(const char* id, const char* label, const Option* options, size_t count,
                  std::string& value, float width = 220.0f) {
  ImGui::PushID(id);
  ImGui::TextUnformatted(label);
  ImGui::SetNextItemWidth(width);

  const char* current = options[0].label;
  for (size_t i = 0; i < count; ++i) {
    if (value == options[i].value) {
      current = options[i].label;
      break;
    }
  }

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
  ImGui::PopID();
  return changed;
}

// A dimmed explanatory line under a control.
void Hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
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
      window_.get(), 64, nullptr,
      [](ImGuiStyle& imgui_style, rex::ui::Style&) {
        // Dark theme with square-ish corners, matching the reference launcher.
        imgui_style.WindowRounding = 0.0f;
        imgui_style.FrameRounding = 2.0f;
        imgui_style.GrabRounding = 2.0f;
        imgui_style.ScrollbarRounding = 2.0f;
        imgui_style.TabRounding = 2.0f;
        imgui_style.WindowBorderSize = 0.0f;
        imgui_style.FrameBorderSize = 0.0f;
        imgui_style.WindowPadding = ImVec2(16, 16);
        imgui_style.FramePadding = ImVec2(10, 6);
        imgui_style.ItemSpacing = ImVec2(8, 10);
        imgui_style.Colors[ImGuiCol_WindowBg] = kBg;
        imgui_style.Colors[ImGuiCol_ChildBg] = kPanelBg;
        imgui_style.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
        imgui_style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        imgui_style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
        imgui_style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.27f, 1.0f);
        imgui_style.Colors[ImGuiCol_Button] = kAccent;
        imgui_style.Colors[ImGuiCol_ButtonHovered] = kAccentHover;
        imgui_style.Colors[ImGuiCol_ButtonActive] = kAccent;
        imgui_style.Colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);
        imgui_style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.32f, 1.0f);
        imgui_style.Colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.93f, 1.0f);
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
  constexpr float kBottomHeight = 68.0f;
  const float content_h = total_h - kBottomHeight;

  // --- Sidebar (fixed column, full height of the content area) ---
  ImGui::SetCursorPos(ImVec2(0, 0));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kSidebarBg);
  ImGui::BeginChild("##sidebar", ImVec2(kSidebarWidth, content_h), false,
                    ImGuiWindowFlags_NoScrollbar);
  DrawSidebarContents();
  ImGui::EndChild();
  ImGui::PopStyleColor();

  // --- Content (right of the sidebar) ---
  const float content_w = total_w - kSidebarWidth;
  ImGui::SetCursorPos(ImVec2(kSidebarWidth, 0));
  ImGui::BeginChild("##content", ImVec2(content_w, content_h), false,
                    ImGuiWindowFlags_NoScrollbar);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
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

  // --- Bottom bar (Save / PLAY) ---
  ImGui::SetCursorPos(ImVec2(0, content_h));
  ImGui::BeginChild("##bottom", ImVec2(total_w, kBottomHeight), false,
                    ImGuiWindowFlags_NoScrollbar);
  DrawBottomBarContents();
  ImGui::EndChild();

  ImGui::End();
}

void LauncherApp::DrawSidebarContents() {
  ImGui::Dummy(ImVec2(0, 18));
  ImGui::SetCursorPosX(20);
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted("POKER SMASH");
  ImGui::PopStyleColor();
  ImGui::SetCursorPosX(20);
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextUnformatted("Xbox 360 recompilation");
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0, 22));

  struct NavEntry {
    const char* label;
    Page page;
  };
  const NavEntry entries[] = {
      {"HOME", Page::kHome},
      {"GRAPHICS", Page::kGraphics},
      {"PERFORMANCE", Page::kPerformance},
      {"GAME", Page::kGame},
      {"ADVANCED", Page::kAdvanced},
  };

  const float row_h = 36.0f;
  for (const auto& entry : entries) {
    const bool active = settings_.page == entry.page;
    const ImVec2 row_pos = ImGui::GetCursorScreenPos();

    if (active) {
      // Accent bar down the left edge, like the reference launcher.
      ImGui::GetWindowDrawList()->AddRectFilled(
          row_pos, ImVec2(row_pos.x + 3.0f, row_pos.y + row_h), ImGui::GetColorU32(kAccent));
    }

    ImGui::PushStyleColor(ImGuiCol_Header, active ? ImVec4(0.16f, 0.16f, 0.18f, 1.0f)
                                                  : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.20f, 0.23f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, active ? kAccent : ImVec4(0.85f, 0.85f, 0.87f, 1.0f));

    ImGui::SetCursorPosX(0);
    if (ImGui::Selectable(entry.label, false, 0, ImVec2(kSidebarWidth, row_h))) {
      settings_.page = entry.page;
    }
    ImGui::PopStyleColor(4);
  }
}

void LauncherApp::DrawHomePage() {
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted("HOME");
  ImGui::PopStyleColor();
  Hint("Start the game and check that everything is in place.");
  ImGui::Dummy(ImVec2(0, 12));

  // --- Status summary ---
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelBg);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
  ImGui::BeginChild("##status", ImVec2(0, 250), true, ImGuiWindowFlags_NoScrollbar);
  ImGui::TextUnformatted("Status");
  ImGui::Separator();
  ImGui::Dummy(ImVec2(0, 8));

  const bool have_game = !detected_game_exe_.empty();
  const bool have_data = !settings_.game_data_root.empty() &&
                         LooksLikeGameRoot(settings_.game_data_root);

  auto status_line = [](const char* label, bool ok, const std::string& detail) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(200);
    ImGui::PushStyleColor(ImGuiCol_Text, ok ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f)
                                            : ImVec4(0.9f, 0.45f, 0.35f, 1.0f));
    ImGui::TextUnformatted(ok ? "OK" : "MISSING");
    ImGui::PopStyleColor();
    if (!detail.empty()) {
      Hint(detail.c_str());
    }
    ImGui::Dummy(ImVec2(0, 4));
  };

  status_line("Game executable", have_game,
              have_game ? detected_game_exe_ : std::string("Place pokersmash.exe next to the launcher."));
  status_line("Game data", have_data,
              have_data ? settings_.game_data_root : std::string("Set the game folder on the GAME page."));
  status_line("GPU plugin", detected_dll_status_.find("found") != std::string::npos,
              detected_dll_status_);

  ImGui::Dummy(ImVec2(0, 8));
  ImGui::TextUnformatted("Current configuration");
  ImGui::Separator();
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::Text("Graphics backend: %s", settings_.gpu_backend.c_str());
  ImGui::Text("Display mode: %s", settings_.display_mode.c_str());
  ImGui::Text("Frame limit: %s", settings_.frame_limit.c_str());

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void LauncherApp::DrawGraphicsPage() {
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted("GRAPHICS");
  ImGui::PopStyleColor();
  Hint("How the game appears on your screen.");
  ImGui::Dummy(ImVec2(0, 12));

  ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelBg);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
  ImGui::BeginChild("##graphics", ImVec2(0, 0), true);

  ComboSetting("backend", "Graphics backend", kBackendOptions,
               std::size(kBackendOptions), settings_.gpu_backend);
  Hint("Direct3D 12 is the better-tested path. Vulkan is available if Direct3D 12 "
       "does not work. Auto tries Direct3D 12 first.");
  ImGui::Dummy(ImVec2(0, 10));

  ComboSetting("display", "Display mode", kDisplayOptions,
               std::size(kDisplayOptions), settings_.display_mode);
  Hint("Borderless covers the screen without changing the display mode.");
  ImGui::Dummy(ImVec2(0, 10));

  ComboSetting("res", "Render resolution", kResolutionOptions,
               std::size(kResolutionOptions), settings_.render_resolution);
  Hint("Auto uses the window size. A fixed resolution renders at that size and "
       "scales to fit.");
  ImGui::Dummy(ImVec2(0, 10));

  ImGui::Checkbox("VSync", &settings_.vsync);
  Hint("Off allows tearing for the lowest latency.");

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void LauncherApp::DrawPerformancePage() {
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted("PERFORMANCE");
  ImGui::PopStyleColor();
  Hint("Frame pacing and diagnostics.");
  ImGui::Dummy(ImVec2(0, 12));

  ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelBg);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
  ImGui::BeginChild("##perf", ImVec2(0, 0), true);

  ComboSetting("framelimit", "Frame limit", kFrameLimitOptions,
               std::size(kFrameLimitOptions), settings_.frame_limit);
  Hint("Auto follows the display refresh rate. The original game runs at 60.");
  ImGui::Dummy(ImVec2(0, 10));

  ImGui::Checkbox("Show performance overlay", &settings_.show_performance_overlay);
  Hint("Draws frame time and counters over the game (toggle in game with F3).");

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void LauncherApp::DrawGamePage() {
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted("GAME");
  ImGui::PopStyleColor();
  Hint("Your game dump and language.");
  ImGui::Dummy(ImVec2(0, 12));

  ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelBg);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
  ImGui::BeginChild("##game", ImVec2(0, 0), true);

  ImGui::TextUnformatted("Game folder");
  Hint("The folder containing default.xex and the Data/ directory.");

  std::array<char, 1024> path_buf{};
  std::snprintf(path_buf.data(), path_buf.size(), "%s", settings_.game_data_root.c_str());
  ImGui::SetNextItemWidth(-200);
  if (ImGui::InputText("##gameroot", path_buf.data(), path_buf.size())) {
    settings_.game_data_root = path_buf.data();
  }
  ImGui::SameLine();
  if (ImGui::Button("Browse...", ImVec2(90, 0))) {
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
  if (ImGui::Button("Open", ImVec2(70, 0))) {
    if (!settings_.game_data_root.empty()) {
#if REX_PLATFORM_WIN32
      ShellExecuteW(nullptr, L"open", std::filesystem::path(settings_.game_data_root).wstring().c_str(),
                    nullptr, nullptr, SW_SHOWNORMAL);
#endif
    }
  }

  // Detected candidates, offered as one-click fills.
  if (!detected_roots_.empty()) {
    ImGui::Dummy(ImVec2(0, 6));
    Hint("Detected next to the launcher:");
    for (const auto& root : detected_roots_) {
      const std::string label = root.string();
      if (ImGui::Selectable(label.c_str(), settings_.game_data_root == label)) {
        settings_.game_data_root = label;
      }
    }
  }

  ImGui::Dummy(ImVec2(0, 14));
  ComboSetting("lang", "Game language", kLanguageOptions,
               std::size(kLanguageOptions), settings_.language);

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void LauncherApp::DrawAdvancedPage() {
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted("ADVANCED");
  ImGui::PopStyleColor();
  Hint("Options for troubleshooting.");
  ImGui::Dummy(ImVec2(0, 12));

  ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelBg);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
  ImGui::BeginChild("##advanced", ImVec2(0, 0), true);

  ImGui::Checkbox("Write a log file", &settings_.log_to_file);
  Hint("Writes pokersmash.log next to the game. Useful when reporting a problem.");
  ImGui::Dummy(ImVec2(0, 10));

  ImGui::TextUnformatted("Extra command line");
  Hint("Appended verbatim. Leave empty unless you know what you need.");
  std::array<char, 512> args_buf{};
  std::snprintf(args_buf.data(), args_buf.size(), "%s", settings_.extra_args.c_str());
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##extra", args_buf.data(), args_buf.size())) {
    settings_.extra_args = args_buf.data();
  }

  ImGui::Dummy(ImVec2(0, 16));
  ImGui::TextUnformatted("Command preview");
  ImGui::Separator();
  ImGui::Dummy(ImVec2(0, 6));
  std::string preview = kGameExeName;
  for (const auto& arg : BuildGameArguments()) {
    preview += " " + arg;
  }
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("%s", preview.c_str());
  ImGui::PopStyleColor();

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void LauncherApp::DrawBottomBarContents() {
  // Left: status / error text, vertically centred against the buttons.
  ImGui::SetCursorPos(ImVec2(24, 26));
  if (!launch_error_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.35f, 1.0f));
    ImGui::TextWrapped("%s", launch_error_.c_str());
    ImGui::PopStyleColor();
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted("Ready.");
    ImGui::PopStyleColor();
  }

  // Right: Save + PLAY, pinned to the bottom-right corner.
  const float play_w = 200.0f;
  const float save_w = 120.0f;
  const float margin = 24.0f;
  const float spacing = 12.0f;

  const float window_w = ImGui::GetWindowWidth();
  const float buttons_y = 14.0f;

  ImGui::SetCursorPos(ImVec2(window_w - margin - play_w - spacing - save_w, buttons_y));
  if (ImGui::Button("Save", ImVec2(save_w, 40))) {
    SaveSettings();
  }

  ImGui::SetCursorPos(ImVec2(window_w - margin - play_w, buttons_y));
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
  if (ImGui::Button("PLAY", ImVec2(play_w, 40))) {
    SaveSettings();
    LaunchGame();
  }
  ImGui::PopStyleColor(3);
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
  while (std::getline(in, line)) {
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
  const auto dir = LauncherDir();
  const std::filesystem::path candidates[] = {
      dir / "extracted",
      dir / "game_data",
      dir / "game",
      dir,
      dir / ".." / "extracted",
      dir / ".." / ".." / "extracted",
      dir / ".." / ".." / ".." / "extracted",
  };

  std::vector<std::filesystem::path> found;
  for (const auto& candidate : candidates) {
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(candidate, ec);
    if (ec || !LooksLikeGameRoot(normalized)) {
      continue;
    }
    if (std::find(found.begin(), found.end(), normalized) == found.end()) {
      found.push_back(normalized);
    }
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
                    "Set it on the GAME page.";
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
