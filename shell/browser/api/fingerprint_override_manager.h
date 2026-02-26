// Copyright (c) 2026 FalconBrowser.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_FINGERPRINT_OVERRIDE_MANAGER_H_
#define ELECTRON_SHELL_BROWSER_API_FINGERPRINT_OVERRIDE_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace content {
class BrowserContext;
}

namespace electron {

// Configuration for fingerprint overrides. All fields are optional.
// Omitting a section means the real browser values will be used.
struct FingerprintConfig {
  FingerprintConfig();
  ~FingerprintConfig();
  FingerprintConfig(const FingerprintConfig&);
  FingerprintConfig& operator=(const FingerprintConfig&);

  // --- User Agent ---
  struct UA {
    UA();
    ~UA();
    UA(const UA&);
    UA& operator=(const UA&);

    std::string string;  // Full UA string for HTTP + navigator.userAgent
    std::vector<blink::UserAgentBrandVersion> brands;
    std::vector<blink::UserAgentBrandVersion> full_version_list;
    std::string platform;          // "Windows", "macOS", "Linux"
    std::string platform_version;  // "15.0.0"
    std::string architecture;      // "x86", "arm"
    std::string bitness;           // "64", "32"
    std::string model;             // "" for desktop
    bool mobile = false;
    std::string form_factor;        // "Desktop", "Mobile"
    std::string navigator_platform; // "Win32", "MacIntel"
  };

  // --- Hardware ---
  struct Hardware {
    int cores = 0;
    double ram = 0;
    std::string gpu;         // WebGL UNMASKED_RENDERER_WEBGL
    std::string gpu_vendor;  // WebGL UNMASKED_VENDOR_WEBGL
  };

  // --- Screen & Window ---
  struct Screen {
    int width = 0;
    int height = 0;
    int avail_width = 0;
    int avail_height = 0;
    int color_depth = 24;
    int pixel_depth = 24;
    double dpr = 1.0;
    int max_touch_points = 0;
  };

  // --- Network ---
  struct Network {
    std::string type;            // "wifi", "ethernet"
    std::string effective_type;  // "4g", "3g"
    int rtt = 0;                 // ms
    double downlink = 0;         // Mbps
    bool save_data = false;
  };

  // --- Language ---
  struct Language {
    Language();
    ~Language();
    Language(const Language&);
    Language& operator=(const Language&);

    std::string primary;        // "en-US"
    std::vector<std::string> list;  // ["en-US", "en"]
    std::string accept_header;  // "en-US,en;q=0.9"
  };

  // --- Geolocation ---
  struct Geo {
    double latitude = 0;
    double longitude = 0;
    double accuracy = 0;
  };

  // --- Seeds for deterministic noise ---
  struct Seeds {
    Seeds();
    ~Seeds();
    Seeds(const Seeds&);
    Seeds& operator=(const Seeds&);

    std::optional<double> canvas;
    std::optional<double> audio;
    std::optional<double> webgl;
    std::optional<double> font;
    std::optional<double> hardware;
    std::optional<double> performance;
    std::optional<double> svg;
    std::optional<double> crypto;
    std::optional<double> dom;
    std::optional<double> math;
    std::optional<double> speech;
    std::optional<double> master;
    std::optional<double> network;
    std::optional<double> wasm;
    std::optional<double> cache;
    std::optional<double> worker;
  };

  // --- Fonts ---
  struct Fonts {
    Fonts();
    ~Fonts();
    Fonts(const Fonts&);
    Fonts& operator=(const Fonts&);

    std::vector<std::string> allow_list;
  };

  // --- Battery ---
  struct Battery {
    bool charging = true;
    double level = 1.0;
    double charging_time = 0;
    double discharging_time = 0;
  };

  // All top-level sections are optional.
  std::optional<UA> ua;
  std::optional<Hardware> hardware;
  std::optional<Screen> screen;
  std::optional<Network> network;
  std::optional<Language> language;
  std::optional<std::string> timezone;
  std::optional<Geo> geo;
  std::optional<Seeds> seeds;
  std::optional<Fonts> fonts;
  std::optional<double> storage_quota;
  std::optional<Battery> battery;

  // Granular disable list: e.g., "screen.colorDepth", "audio", etc.
  base::flat_set<std::string> disabled;

  // Check if a specific override is disabled.
  bool IsDisabled(const std::string& override_path) const;

  // Build a blink::UserAgentMetadata from the UA config.
  // Falls back to the provided default metadata for unset fields.
  blink::UserAgentMetadata BuildUserAgentMetadata(
      const blink::UserAgentMetadata& default_metadata) const;

  // Serialize the config to a flat string→string map for IPC transport.
  // Keys are dot-separated paths (e.g., "ua.string", "screen.width").
  base::flat_map<std::string, std::string> SerializeToFlatMap() const;
};

// Process-global singleton managing per-session fingerprint override configs.
// Thread-safe: the network service may access from IO threads.
class FingerprintOverrideManager {
 public:
  static FingerprintOverrideManager& GetInstance();

  // Set the fingerprint config for a browser context (session).
  // Pass nullptr to clear.
  void SetConfig(content::BrowserContext* context,
                 std::unique_ptr<FingerprintConfig> config);

  // Get the current config for a browser context. Returns nullptr if none set.
  const FingerprintConfig* GetConfig(
      content::BrowserContext* context) const;

  // Remove config for a browser context (e.g., on destruction).
  void RemoveConfig(content::BrowserContext* context);

  // Convenience: check if a specific override is disabled for a context.
  bool IsOverrideDisabled(content::BrowserContext* context,
                          const std::string& override_path) const;

  // Get the most recently computed UserAgentMetadata override.
  // Returns nullptr if no session has set a UA override.
  // Used by ElectronBrowserClient::GetUserAgentMetadata() for Client Hints.
  const blink::UserAgentMetadata* GetOverrideMetadata() const;

 private:
  friend class base::NoDestructor<FingerprintOverrideManager>;

  FingerprintOverrideManager();
  ~FingerprintOverrideManager();

  mutable base::Lock lock_;
  std::map<content::BrowserContext*, std::unique_ptr<FingerprintConfig>>
      configs_ GUARDED_BY(lock_);

  // Cached metadata from the most recent SetConfig() call that included UA.
  // Used as the global default for GetUserAgentMetadata().
  std::optional<blink::UserAgentMetadata> override_metadata_ GUARDED_BY(lock_);
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_API_FINGERPRINT_OVERRIDE_MANAGER_H_
