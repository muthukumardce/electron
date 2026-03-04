// Copyright (c) 2026 FalconBrowser.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/fingerprint_override_manager.h"

#include "base/no_destructor.h"
#include "components/embedder_support/user_agent_utils.h"
#include "content/public/browser/browser_context.h"

namespace electron {

// --- Out-of-line constructors/destructors for Chromium style compliance ---

FingerprintConfig::FingerprintConfig() = default;
FingerprintConfig::~FingerprintConfig() = default;
FingerprintConfig::FingerprintConfig(const FingerprintConfig&) = default;
FingerprintConfig& FingerprintConfig::operator=(const FingerprintConfig&) =
    default;

FingerprintConfig::UA::UA() = default;
FingerprintConfig::UA::~UA() = default;
FingerprintConfig::UA::UA(const UA&) = default;
FingerprintConfig::UA& FingerprintConfig::UA::operator=(const UA&) = default;

FingerprintConfig::Language::Language() = default;
FingerprintConfig::Language::~Language() = default;
FingerprintConfig::Language::Language(const Language&) = default;
FingerprintConfig::Language& FingerprintConfig::Language::operator=(
    const Language&) = default;

FingerprintConfig::Seeds::Seeds() = default;
FingerprintConfig::Seeds::~Seeds() = default;
FingerprintConfig::Seeds::Seeds(const Seeds&) = default;
FingerprintConfig::Seeds& FingerprintConfig::Seeds::operator=(const Seeds&) =
    default;

FingerprintConfig::Fonts::Fonts() = default;
FingerprintConfig::Fonts::~Fonts() = default;
FingerprintConfig::Fonts::Fonts(const Fonts&) = default;
FingerprintConfig::Fonts& FingerprintConfig::Fonts::operator=(const Fonts&) =
    default;

// --- FingerprintConfig ---

bool FingerprintConfig::IsDisabled(const std::string& override_path) const {
  if (disabled.empty())
    return false;

  // Check exact match first: "screen.colorDepth"
  if (disabled.contains(override_path))
    return true;

  // Check category-level match: "screen" disables all screen.* overrides
  auto dot_pos = override_path.find('.');
  if (dot_pos != std::string::npos) {
    std::string category = override_path.substr(0, dot_pos);
    if (disabled.contains(category))
      return true;
  }

  return false;
}

blink::UserAgentMetadata FingerprintConfig::BuildUserAgentMetadata(
    const blink::UserAgentMetadata& default_metadata) const {
  if (!ua.has_value())
    return default_metadata;

  blink::UserAgentMetadata metadata;
  const auto& ua_config = ua.value();

  // Brands (low-entropy Sec-CH-UA)
  if (!ua_config.brands.empty()) {
    metadata.brand_version_list = ua_config.brands;
  } else {
    metadata.brand_version_list = default_metadata.brand_version_list;
  }

  // Full version list (high-entropy Sec-CH-UA-Full-Version-List)
  if (!ua_config.full_version_list.empty()) {
    metadata.brand_full_version_list = ua_config.full_version_list;
  } else {
    metadata.brand_full_version_list = default_metadata.brand_full_version_list;
  }

  // Full version string (high-entropy Sec-CH-UA-Full-Version)
  if (!ua_config.full_version_list.empty()) {
    // Use the Chrome/Chromium entry's version as the full_version
    for (const auto& brand : ua_config.full_version_list) {
      if (brand.brand == "Google Chrome" || brand.brand == "Chromium") {
        metadata.full_version = brand.version;
        break;
      }
    }
    if (metadata.full_version.empty() && !ua_config.full_version_list.empty()) {
      metadata.full_version = ua_config.full_version_list[0].version;
    }
  } else {
    metadata.full_version = default_metadata.full_version;
  }

  // Platform
  metadata.platform = ua_config.platform.empty()
                          ? default_metadata.platform
                          : ua_config.platform;

  // Platform version
  metadata.platform_version = ua_config.platform_version.empty()
                                  ? default_metadata.platform_version
                                  : ua_config.platform_version;

  // Architecture
  metadata.architecture = ua_config.architecture.empty()
                              ? default_metadata.architecture
                              : ua_config.architecture;

  // Bitness
  metadata.bitness = ua_config.bitness.empty()
                         ? default_metadata.bitness
                         : ua_config.bitness;

  // Model
  metadata.model = ua_config.model.empty()
                       ? default_metadata.model
                       : ua_config.model;

  // Mobile
  metadata.mobile = ua_config.mobile;

  // Form factors
  if (!ua_config.form_factor.empty()) {
    metadata.form_factors = {ua_config.form_factor};
  } else {
    metadata.form_factors = default_metadata.form_factors;
  }

  return metadata;
}

// --- FingerprintOverrideManager ---

FingerprintOverrideManager::FingerprintOverrideManager() = default;
FingerprintOverrideManager::~FingerprintOverrideManager() = default;

FingerprintOverrideManager& FingerprintOverrideManager::GetInstance() {
  static base::NoDestructor<FingerprintOverrideManager> instance;
  return *instance;
}

void FingerprintOverrideManager::SetConfig(
    content::BrowserContext* context,
    std::unique_ptr<FingerprintConfig> config) {
  base::AutoLock lock(lock_);
  if (config) {
    // If this config has a UA section, compute and cache the metadata
    // for use by ElectronBrowserClient::GetUserAgentMetadata().
    if (config->ua.has_value()) {
      blink::UserAgentMetadata default_metadata =
          embedder_support::GetUserAgentMetadata();
      override_metadata_ = config->BuildUserAgentMetadata(default_metadata);
    }
    configs_[context] = std::move(config);
  } else {
    configs_.erase(context);
    // Clear cached metadata if no configs remain with UA overrides
    bool has_ua_override = false;
    for (const auto& [ctx, cfg] : configs_) {
      if (cfg && cfg->ua.has_value()) {
        has_ua_override = true;
        break;
      }
    }
    if (!has_ua_override) {
      override_metadata_.reset();
    }
  }
}

const FingerprintConfig* FingerprintOverrideManager::GetConfig(
    content::BrowserContext* context) const {
  base::AutoLock lock(lock_);
  auto it = configs_.find(context);
  if (it != configs_.end())
    return it->second.get();
  return nullptr;
}

void FingerprintOverrideManager::RemoveConfig(
    content::BrowserContext* context) {
  base::AutoLock lock(lock_);
  configs_.erase(context);
}

bool FingerprintOverrideManager::IsOverrideDisabled(
    content::BrowserContext* context,
    const std::string& override_path) const {
  const FingerprintConfig* config = GetConfig(context);
  if (!config)
    return false;
  return config->IsDisabled(override_path);
}

const blink::UserAgentMetadata*
FingerprintOverrideManager::GetOverrideMetadata() const {
  base::AutoLock lock(lock_);
  if (override_metadata_.has_value())
    return &override_metadata_.value();
  return nullptr;
}

// --- FingerprintConfig serialization ---

namespace {
void AddString(base::flat_map<std::string, std::string>& m,
               const std::string& key, const std::string& value) {
  if (!value.empty())
    m[key] = value;
}
void AddInt(base::flat_map<std::string, std::string>& m,
            const std::string& key, int value) {
  m[key] = std::to_string(value);
}
void AddDouble(base::flat_map<std::string, std::string>& m,
               const std::string& key, double value) {
  m[key] = std::to_string(value);
}
void AddBool(base::flat_map<std::string, std::string>& m,
             const std::string& key, bool value) {
  m[key] = value ? "true" : "false";
}
void AddOptDouble(base::flat_map<std::string, std::string>& m,
                  const std::string& key,
                  const std::optional<double>& value) {
  if (value.has_value())
    m[key] = std::to_string(value.value());
}
// Serialize brand pairs as "brand1|ver1,brand2|ver2"
std::string SerializeBrands(
    const std::vector<blink::UserAgentBrandVersion>& brands) {
  std::string result;
  for (size_t i = 0; i < brands.size(); i++) {
    if (i > 0) result += ",";
    result += brands[i].brand + "|" + brands[i].version;
  }
  return result;
}
// Serialize string list as "a,b,c"
std::string SerializeStringList(const std::vector<std::string>& list) {
  std::string result;
  for (size_t i = 0; i < list.size(); i++) {
    if (i > 0) result += ",";
    result += list[i];
  }
  return result;
}
}  // namespace

base::flat_map<std::string, std::string>
FingerprintConfig::SerializeToFlatMap() const {
  base::flat_map<std::string, std::string> m;

  if (ua.has_value()) {
    const auto& u = ua.value();
    AddString(m, "ua.string", u.string);
    AddString(m, "ua.platform", u.platform);
    AddString(m, "ua.platformVersion", u.platform_version);
    AddString(m, "ua.architecture", u.architecture);
    AddString(m, "ua.bitness", u.bitness);
    AddString(m, "ua.model", u.model);
    AddBool(m, "ua.mobile", u.mobile);
    AddString(m, "ua.formFactor", u.form_factor);
    AddString(m, "ua.navigatorPlatform", u.navigator_platform);
    if (!u.brands.empty())
      m["ua.brands"] = SerializeBrands(u.brands);
    if (!u.full_version_list.empty())
      m["ua.fullVersionList"] = SerializeBrands(u.full_version_list);
    // Derive uaFullVersion from full_version_list
    if (!u.full_version_list.empty()) {
      for (const auto& b : u.full_version_list) {
        if (b.brand == "Google Chrome" || b.brand == "Chromium") {
          m["ua.uaFullVersion"] = b.version;
          break;
        }
      }
      if (m.find("ua.uaFullVersion") == m.end())
        m["ua.uaFullVersion"] = u.full_version_list[0].version;
    }
  }

  if (hardware.has_value()) {
    const auto& h = hardware.value();
    AddInt(m, "hardware.cores", h.cores);
    AddDouble(m, "hardware.ram", h.ram);
    AddString(m, "hardware.gpu", h.gpu);
    AddString(m, "hardware.gpuVendor", h.gpu_vendor);
  }

  if (screen.has_value()) {
    const auto& s = screen.value();
    AddInt(m, "screen.width", s.width);
    AddInt(m, "screen.height", s.height);
    AddInt(m, "screen.availWidth", s.avail_width);
    AddInt(m, "screen.availHeight", s.avail_height);
    AddInt(m, "screen.colorDepth", s.color_depth);
    AddInt(m, "screen.pixelDepth", s.pixel_depth);
    AddDouble(m, "screen.dpr", s.dpr);
    AddInt(m, "screen.maxTouchPoints", s.max_touch_points);
  }

  if (network.has_value()) {
    const auto& n = network.value();
    AddString(m, "network.type", n.type);
    AddString(m, "network.effectiveType", n.effective_type);
    AddInt(m, "network.rtt", n.rtt);
    AddDouble(m, "network.downlink", n.downlink);
    AddBool(m, "network.saveData", n.save_data);
  }

  if (language.has_value()) {
    const auto& l = language.value();
    AddString(m, "language.primary", l.primary);
    if (!l.list.empty())
      m["language.list"] = SerializeStringList(l.list);
    AddString(m, "language.acceptHeader", l.accept_header);
  }

  if (timezone.has_value())
    m["timezone"] = timezone.value();

  if (geo.has_value()) {
    const auto& g = geo.value();
    AddDouble(m, "geo.latitude", g.latitude);
    AddDouble(m, "geo.longitude", g.longitude);
    AddDouble(m, "geo.accuracy", g.accuracy);
  }

  if (seeds.has_value()) {
    const auto& s = seeds.value();
    AddOptDouble(m, "seeds.canvas", s.canvas);
    AddOptDouble(m, "seeds.audio", s.audio);
    AddOptDouble(m, "seeds.webgl", s.webgl);
    AddOptDouble(m, "seeds.font", s.font);
    AddOptDouble(m, "seeds.hardware", s.hardware);
    AddOptDouble(m, "seeds.performance", s.performance);
    AddOptDouble(m, "seeds.svg", s.svg);
    AddOptDouble(m, "seeds.crypto", s.crypto);
    AddOptDouble(m, "seeds.dom", s.dom);
    AddOptDouble(m, "seeds.math", s.math);
    AddOptDouble(m, "seeds.speech", s.speech);
    AddOptDouble(m, "seeds.master", s.master);
    AddOptDouble(m, "seeds.network", s.network);
    AddOptDouble(m, "seeds.wasm", s.wasm);
    AddOptDouble(m, "seeds.cache", s.cache);
    AddOptDouble(m, "seeds.worker", s.worker);
  }

  if (fonts.has_value() && !fonts->allow_list.empty())
    m["fonts.allowList"] = SerializeStringList(fonts->allow_list);

  if (storage_quota.has_value())
    m["storageQuota"] = std::to_string(storage_quota.value());
  if (storage_usage.has_value())
    m["storageUsage"] = std::to_string(storage_usage.value());

  if (battery.has_value()) {
    const auto& b = battery.value();
    AddBool(m, "battery.charging", b.charging);
    AddDouble(m, "battery.level", b.level);
    AddDouble(m, "battery.chargingTime", b.charging_time);
    AddDouble(m, "battery.dischargingTime", b.discharging_time);
  }

  if (!disabled.empty()) {
    std::string disabled_str;
    for (const auto& d : disabled) {
      if (!disabled_str.empty()) disabled_str += ",";
      disabled_str += d;
    }
    m["disabled"] = disabled_str;
  }

  return m;
}

}  // namespace electron
