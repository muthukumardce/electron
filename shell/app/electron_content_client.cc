// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/app/electron_content_client.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/command_line.h"
#include "base/containers/extend.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "content/public/common/buildflags.h"
#include "electron/buildflags/buildflags.h"
#include "electron/fuses.h"
#include "extensions/common/constants.h"
#include "pdf/buildflags.h"
#include "shell/common/options_switches.h"
#include "shell/common/process_util.h"
#include "third_party/widevine/cdm/buildflags.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/url_constants.h"

#if BUILDFLAG(ENABLE_WIDEVINE)
#include "base/native_library.h"
#include "content/public/common/cdm_info.h"
#include "media/base/cdm_capability.h"
#include "media/base/video_codecs.h"
#include "third_party/widevine/cdm/widevine_cdm_common.h"
#endif  // BUILDFLAG(ENABLE_WIDEVINE)

#if BUILDFLAG(ENABLE_PDF_VIEWER)
#include "components/pdf/common/constants.h"  // nogncheck
#include "shell/common/electron_constants.h"
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

#if BUILDFLAG(ENABLE_PLUGINS)
#include "content/public/common/webplugininfo.h"
#endif  // BUILDFLAG(ENABLE_PLUGINS)

namespace electron {

namespace {

enum class WidevineCdmFileCheck {
  kNotChecked,
  kFound,
  kNotFound,
};

#if BUILDFLAG(ENABLE_WIDEVINE)
bool IsWidevineAvailable(
    base::FilePath* cdm_path,
    media::CdmCapability::VideoCodecMap* video_codecs_supported,
    base::flat_set<media::CdmSessionType>* session_types_supported,
    base::flat_set<media::EncryptionScheme>* encryption_schemes_supported) {
  static WidevineCdmFileCheck widevine_cdm_file_check =
      WidevineCdmFileCheck::kNotChecked;

  if (widevine_cdm_file_check == WidevineCdmFileCheck::kNotChecked) {
    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    *cdm_path = command_line->GetSwitchValuePath(switches::kWidevineCdmPath);
    if (!cdm_path->empty()) {
      *cdm_path = cdm_path->AppendASCII(
          base::GetNativeLibraryName(kWidevineCdmLibraryName));
      widevine_cdm_file_check = base::PathExists(*cdm_path)
                                    ? WidevineCdmFileCheck::kFound
                                    : WidevineCdmFileCheck::kNotFound;
    }
  }

  if (widevine_cdm_file_check == WidevineCdmFileCheck::kFound) {
    // Add the supported codecs as if they came from the component manifest.
    video_codecs_supported->emplace(media::VideoCodec::kVP8,
                                    media::VideoCodecInfo{});
    video_codecs_supported->emplace(media::VideoCodec::kVP9,
                                    media::VideoCodecInfo{});
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
    video_codecs_supported->emplace(media::VideoCodec::kH264,
                                    media::VideoCodecInfo{});
#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)

    session_types_supported->insert(media::CdmSessionType::kTemporary);
#if BUILDFLAG(IS_CHROMEOS)
    session_types_supported->insert(media::CdmSessionType::kPersistentLicense);
#endif  // BUILDFLAG(IS_CHROMEOS)

    encryption_schemes_supported->insert(media::EncryptionScheme::kCenc);

    return true;
  }

  return false;
}
#endif  // BUILDFLAG(ENABLE_WIDEVINE)

}  // namespace

ElectronContentClient::ElectronContentClient() = default;

ElectronContentClient::~ElectronContentClient() = default;

std::u16string ElectronContentClient::GetLocalizedString(int message_id) {
  return l10n_util::GetStringUTF16(message_id);
}

std::string_view ElectronContentClient::GetDataResource(
    int resource_id,
    ui::ResourceScaleFactor scale_factor) {
  return ui::ResourceBundle::GetSharedInstance().GetRawDataResourceForScale(
      resource_id, scale_factor);
}

gfx::Image& ElectronContentClient::GetNativeImageNamed(int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().GetNativeImageNamed(
      resource_id);
}

base::RefCountedMemory* ElectronContentClient::GetDataResourceBytes(
    int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().LoadDataResourceBytes(
      resource_id);
}

void ElectronContentClient::AddAdditionalSchemes(Schemes* schemes) {
  // Browser Process registration happens in
  // `api::Protocol::RegisterSchemesAsPrivileged`
  //
  // Renderer Process registration happens in `RendererClientBase`
  //
  // We use this for registration to network utility process
  if (IsUtilityProcess()) {
    const auto& cmd = *base::CommandLine::ForCurrentProcess();
    auto append_cli_schemes = [&cmd](auto& appendme, const auto key) {
      base::Extend(appendme, base::SplitString(cmd.GetSwitchValueASCII(key),
                                               ",", base::TRIM_WHITESPACE,
                                               base::SPLIT_WANT_NONEMPTY));
    };

    using namespace switches;
    append_cli_schemes(schemes->cors_enabled_schemes, kCORSSchemes);
    append_cli_schemes(schemes->csp_bypassing_schemes, kBypassCSPSchemes);
    append_cli_schemes(schemes->secure_schemes, kSecureSchemes);
    append_cli_schemes(schemes->service_worker_schemes, kServiceWorkerSchemes);
    append_cli_schemes(schemes->standard_schemes, kStandardSchemes);
  }

  if (electron::fuses::IsGrantFileProtocolExtraPrivilegesEnabled()) {
    schemes->service_worker_schemes.emplace_back(url::kFileScheme);
  }

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  schemes->standard_schemes.push_back(extensions::kExtensionScheme);
  schemes->savable_schemes.push_back(extensions::kExtensionScheme);
  schemes->secure_schemes.push_back(extensions::kExtensionScheme);
  schemes->service_worker_schemes.push_back(extensions::kExtensionScheme);
  schemes->cors_enabled_schemes.push_back(extensions::kExtensionScheme);
  schemes->csp_bypassing_schemes.push_back(extensions::kExtensionScheme);
#endif
}

void ElectronContentClient::AddPlugins(
    std::vector<content::WebPluginInfo>* plugins) {
#if BUILDFLAG(ENABLE_PDF_VIEWER)
  static constexpr char16_t kPDFPluginName[] = u"Chromium PDF Plugin";
  static constexpr char16_t kPDFPluginDescription[] = u"Built-in PDF viewer";
  static constexpr char kPDFPluginExtension[] = "pdf";
  static constexpr char kPDFPluginExtensionDescription[] =
      "Portable Document Format";

  content::WebPluginInfo pdf_info;
  pdf_info.name = kPDFPluginName;
  // This isn't a real file path; it's just used as a unique identifier.
  static constexpr std::string_view kPdfPluginPath = "internal-pdf-viewer";
  pdf_info.path = base::FilePath::FromASCII(kPdfPluginPath);
  pdf_info.desc = kPDFPluginDescription;
  content::WebPluginMimeType pdf_mime_type(pdf::kInternalPluginMimeType,
                                           kPDFPluginExtension,
                                           kPDFPluginExtensionDescription);
  pdf_info.mime_types.push_back(pdf_mime_type);
  pdf_info.type = content::WebPluginInfo::PLUGIN_TYPE_BROWSER_INTERNAL_PLUGIN;
  plugins->push_back(pdf_info);
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)
}

void ElectronContentClient::AddContentDecryptionModules(
    std::vector<content::CdmInfo>* cdms,
    std::vector<media::CdmHostFilePath>* cdm_host_file_paths) {
  if (cdms) {
#if BUILDFLAG(ENABLE_WIDEVINE)
    base::FilePath cdm_path;
    media::CdmCapability::VideoCodecMap video_codecs_supported;
    base::flat_set<media::CdmSessionType> session_types_supported;
    base::flat_set<media::EncryptionScheme> encryption_schemes_supported;

    // Try to find the actual CDM binary on disk
    IsWidevineAvailable(&cdm_path, &video_codecs_supported,
                        &session_types_supported,
                        &encryption_schemes_supported);

    // Always register Widevine with at least minimal capabilities so
    // navigator.requestMediaKeySystemAccess("com.widevine.alpha") resolves.
    // The CDM binary is only needed for actual DRM playback, not for the
    // capability check that fingerprinting services use.
    if (video_codecs_supported.empty()) {
      video_codecs_supported.emplace(media::VideoCodec::kVP8,
                                     media::VideoCodecInfo{});
      video_codecs_supported.emplace(media::VideoCodec::kVP9,
                                     media::VideoCodecInfo{});
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
      video_codecs_supported.emplace(media::VideoCodec::kH264,
                                     media::VideoCodecInfo{});
#endif
      session_types_supported.insert(media::CdmSessionType::kTemporary);
      encryption_schemes_supported.insert(media::EncryptionScheme::kCenc);
    }

    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    auto cdm_version_string =
        command_line->GetSwitchValueASCII(switches::kWidevineCdmVersion);
    const base::Version version(
        cdm_version_string.empty() ? "4.10.2934.0" : cdm_version_string);

    media::CdmCapability capability(
        {}, std::move(video_codecs_supported),
        std::move(encryption_schemes_supported),
        std::move(session_types_supported), version);

    cdms->push_back(content::CdmInfo(
        kWidevineKeySystem, content::CdmInfo::Robustness::kSoftwareSecure,
        std::move(capability), false, kWidevineCdmDisplayName,
        kWidevineCdmType, version, cdm_path));
#endif  // BUILDFLAG(ENABLE_WIDEVINE)
  }
}

}  // namespace electron
