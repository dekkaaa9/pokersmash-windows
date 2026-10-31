// pokersmash - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>

// Which graphics backend the GPU plugin should use. Registered as a cvar so it
// is settable from the command line (--gpu_backend=vulkan), from pokersmash.toml
// (gpu_backend = "vulkan"), and from the environment, all through the SDK's
// normal config plumbing.
REXCVAR_DEFINE_STRING(gpu_backend, "auto", "GPU",
                      "Graphics backend: auto, d3d12, or vulkan")
    .allowed({"auto", "d3d12", "vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

class PokersmashApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<PokersmashApp>(new PokersmashApp(ctx, "pokersmash",
        PPCImageConfig));
  }

  // ---------------------------------------------------------------------------
  // Automatic configuration
  //
  // The build is meant to be run by double-clicking, so it must work with no
  // command line at all. Resolution order for the game data root:
  //
  //   1. --game_data_root / the `game_data_root` cvar, if the user passed it.
  //   2. A `game_data_root` key in pokersmash.toml next to the executable.
  //   3. Auto-discovery: the first candidate directory (relative to the exe)
  //      that actually contains a `default.xex`.
  //
  // The GPU plugin is likewise enabled automatically when rexgpu-xenos.dll is
  // staged next to the executable, so no `--gpu_plugin` argument is needed.
  // ---------------------------------------------------------------------------

  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) {
      return;  // explicit --game_data_root / cvar wins.
    }

    const auto exe_dir = rex::filesystem::GetExecutableFolder();

    if (auto from_config = ReadGameRootFromConfig(exe_dir); !from_config.empty()) {
      paths.game_data_root = from_config;
      REXLOG_INFO("PokersmashApp: game_data_root from pokersmash.toml: {}",
                  from_config.string());
      return;
    }

    if (auto found = DiscoverGameRoot(exe_dir); !found.empty()) {
      paths.game_data_root = found;
      REXLOG_INFO("PokersmashApp: game_data_root auto-discovered: {}", found.string());
    }
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    const auto exe_dir = rex::filesystem::GetExecutableFolder();

    if (config.gpu_plugin.empty()) {
      if (std::filesystem::exists(exe_dir / "rexgpu-xenos.dll")) {
        config.gpu_plugin = "xenos";
        REXLOG_INFO("PokersmashApp: GPU plugin 'xenos' auto-enabled");
      }
    }

    // Backend selection. The SDK's own loader always passes backend="any",
    // which makes the plugin pick D3D12 whenever it is compiled in, so a
    // Vulkan build would never be reached through the default path. Load the
    // plugin ourselves instead, so the backend can be chosen and so a failing
    // backend can fall back to a working one.
    //
    //   gpu_backend in pokersmash.toml: "d3d12" | "vulkan" | "auto"
    //   default "auto" = try d3d12, then vulkan.
    if (config.graphics || config.gpu_plugin.empty()) {
      return;  // no plugin, or the caller supplied a graphics system itself.
    }

    const std::string requested = REXCVAR_GET(gpu_backend);
    std::vector<std::string> order;
    if (requested == "vulkan") {
      order = {"vulkan", "d3d12"};
    } else if (requested == "d3d12") {
      order = {"d3d12", "vulkan"};
    } else {
      order = {"d3d12", "vulkan"};  // "auto" and anything unrecognized
    }

    for (const auto& backend : order) {
      auto graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, backend);
      if (graphics) {
        config.graphics = std::move(graphics);
        REXLOG_INFO("PokersmashApp: GPU backend '{}' selected", backend);
        return;
      }
      REXLOG_WARN("PokersmashApp: GPU backend '{}' unavailable, trying next", backend);
    }

    // Nothing loaded: leave config.graphics empty and let the SDK's own loader
    // run so its error path (message box + log) reports the failure.
    REXLOG_ERROR("PokersmashApp: no GPU backend could be loaded");
  }

  // Poker Smash opens its content with bare relative paths ("data\textures\",
  // "data\menutextures\", "data\rendermethods\"), which the 360 resolved against
  // the process working directory. The SDK VFS only understands device-rooted
  // paths, so it rejects them ("device not found") and the game stalls on the
  // first asset it cannot read. Alias the relative prefixes the game uses onto
  // the mounted game partition. ResolvePath expands symlinks before it looks up
  // the device, so "data\..." becomes "\Device\Harddisk0\Partition1\Data\...".
  void OnPostSetup() override {
    auto* vfs = runtime()->file_system();
    constexpr const char* kGamePartition = "\\Device\\Harddisk0\\Partition1";
    bool a = vfs->RegisterSymbolicLink("data", std::string(kGamePartition) + "\\Data");
    bool b = vfs->RegisterSymbolicLink("Data", std::string(kGamePartition) + "\\Data");
    REXLOG_INFO("PokersmashApp: registered data symlinks ({} / {})", a, b);
  }

 private:
  // Reads `game_data_root = "..."` from pokersmash.toml next to the executable.
  // Deliberately tiny: one key, one file, no TOML dependency. Returns an empty
  // path when the file or key is absent.
  static std::filesystem::path ReadGameRootFromConfig(
      const std::filesystem::path& exe_dir) {
    return std::filesystem::path(ReadConfigValue(exe_dir, "game_data_root"));
  }

  // Minimal single-key reader for pokersmash.toml: finds the first `key = value`
  // line, strips a trailing comment and surrounding quotes, and returns the
  // value. Empty when the file, key, or value is missing.
  static std::string ReadConfigValue(const std::filesystem::path& exe_dir,
                                     const std::string& wanted_key) {
    std::ifstream in(exe_dir / "pokersmash.toml");
    if (!in) {
      return {};
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
      if (Trim(line.substr(0, eq)) != wanted_key) {
        continue;
      }
      std::string value = Trim(line.substr(eq + 1));
      if (value.size() >= 2 &&
          ((value.front() == '"' && value.back() == '"') ||
           (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
      }
      return value;
    }
    return {};
  }

  // Searches a short list of likely locations for a directory containing
  // default.xex. Handles both a portable layout (game data next to the exe) and
  // the development layout (exe deep inside out/build/... with the extracted
  // game root several levels up).
  static std::filesystem::path DiscoverGameRoot(
      const std::filesystem::path& exe_dir) {
    std::vector<std::filesystem::path> candidates = {
        exe_dir / "extracted",
        exe_dir / "game_data",
        exe_dir / "game",
        exe_dir,
        exe_dir / ".." / "extracted",
        exe_dir / ".." / ".." / "extracted",
        exe_dir / ".." / ".." / ".." / "extracted",
        exe_dir / ".." / ".." / ".." / ".." / "extracted",
    };
    for (const auto& candidate : candidates) {
      std::error_code ec;
      auto normalized = std::filesystem::weakly_canonical(candidate, ec);
      if (ec) {
        continue;
      }
      if (std::filesystem::is_regular_file(normalized / "default.xex", ec) && !ec) {
        return normalized;
      }
    }
    return {};
  }

  static std::string Trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
  }
};
