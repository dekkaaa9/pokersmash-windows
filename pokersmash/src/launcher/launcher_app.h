// Poker Smash Launcher
//
// A small standalone GUI (Dear ImGui + SDL3, via the ReXGlue UI layer) that
// configures and starts pokersmash.exe. It shares nothing with the recompiled
// game except the runtime DLL: no guest code, no codegen, no emulated GPU.
//
// The launcher owns its own D3D12 presenter, renders a settings UI, and spawns
// the game as a child process with the chosen arguments.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context.h>

namespace rex::ui {
class GraphicsProvider;
class ImmediateDrawer;
class Presenter;
}  // namespace rex::ui

namespace pokersmash_launcher {

// Which page the sidebar has selected.
enum class Page {
  kHome,
  kGraphics,
  kPerformance,
  kGame,
  kAdvanced,
};

// Everything the launcher persists to launcher.toml and passes to the game.
struct LauncherSettings {
  // --- Folders ---
  // Folder containing default.xex and Data/. Empty means "let the game
  // auto-discover".
  std::string game_data_root;

  // --- Graphics ---
  // "auto", "d3d12", or "vulkan".
  std::string gpu_backend = "auto";
  // "windowed", "borderless", or "fullscreen".
  std::string display_mode = "windowed";
  bool vsync = true;
  // "auto", "720p", "1080p", "1440p", "2160p".
  std::string render_resolution = "auto";

  // --- Performance ---
  // "auto", "30", "60", "120", "unlimited".
  std::string frame_limit = "auto";
  bool show_performance_overlay = false;

  // --- Game ---
  std::string language = "English";

  // --- Advanced ---
  // Extra raw command line appended verbatim.
  std::string extra_args;
  bool log_to_file = false;

  // --- UI state (persisted too, so the launcher reopens where you left it) ---
  Page page = Page::kHome;
};

class LauncherApp final : public rex::ui::WindowedApp,
                          public rex::ui::WindowListener,
                          public rex::ui::WindowInputListener {
 public:
  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& app_context);

  LauncherApp(const LauncherApp&) = delete;
  LauncherApp& operator=(const LauncherApp&) = delete;

  bool OnInitialize() override;

 protected:
  void OnDestroy() override;

  // WindowListener
  void OnClosing(rex::ui::UIEvent& e) override;

  // WindowInputListener
  void OnKeyDown(rex::ui::KeyEvent& e) override;

 private:
  explicit LauncherApp(rex::ui::WindowedAppContext& app_context);
  ~LauncherApp() override;

  bool SetupGraphics();
  bool SetupUi();
  void DrawFrame();
  void DrawSidebarContents();
  void DrawHomePage();
  void DrawGraphicsPage();
  void DrawPerformancePage();
  void DrawGamePage();
  void DrawAdvancedPage();
  void DrawBottomBarContents();

  // --- Settings persistence ---
  void LoadSettings();
  void SaveSettings() const;
  std::filesystem::path SettingsPath() const;

  // --- Game discovery / launching ---
  // Candidate directories (relative to the launcher) that contain default.xex.
  std::vector<std::filesystem::path> DiscoverGameRoots() const;
  // True when `dir` looks like an extracted game root.
  static bool LooksLikeGameRoot(const std::filesystem::path& dir);
  // Builds the argv for the game from the current settings.
  std::vector<std::string> BuildGameArguments() const;
  // Spawns pokersmash.exe. Returns false (and sets `launch_error_`) on failure.
  bool LaunchGame();
  // Directory the launcher executable lives in.
  std::filesystem::path LauncherDir() const;

  rex::ui::WindowedAppContext& app_context_;

  std::unique_ptr<rex::ui::GraphicsProvider> graphics_provider_;
  std::unique_ptr<rex::ui::Presenter> presenter_;
  std::unique_ptr<rex::ui::ImmediateDrawer> immediate_drawer_;
  std::unique_ptr<rex::ui::Window> window_;
  std::unique_ptr<rex::ui::ImGuiDrawer> imgui_drawer_;
  // The launcher draws a full-window UI, so it hooks the drawer's per-frame
  // callback through a trivial dialog rather than a widget tree.
  class FrameDialog final : public rex::ui::ImGuiDialog {
   public:
    FrameDialog(rex::ui::ImGuiDrawer* drawer, LauncherApp* app)
        : rex::ui::ImGuiDialog(drawer), app_(app) {}

   protected:
    void OnDraw(ImGuiIO&) override;

   private:
    LauncherApp* app_;
  };
  std::unique_ptr<FrameDialog> frame_dialog_;

  LauncherSettings settings_;

  // Detected once at startup so the pages can show what was found.
  std::vector<std::filesystem::path> detected_roots_;
  std::string detected_game_exe_;   // absolute path to pokersmash.exe, or empty
  std::string detected_dll_status_; // human-readable backend availability

  // Set when LaunchGame fails; shown as a banner until the next attempt.
  std::string launch_error_;

  // Set when the user clicks PLAY so the message loop can exit cleanly after
  // the child process has been started.
  bool should_quit_ = false;
};

}  // namespace pokersmash_launcher
