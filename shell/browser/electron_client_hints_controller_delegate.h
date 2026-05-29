// Copyright (c) 2026 Electron / FalconBrowser contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_ELECTRON_CLIENT_HINTS_CONTROLLER_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_ELECTRON_CLIENT_HINTS_CONTROLLER_DELEGATE_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/client_hints_controller_delegate.h"
#include "ui/gfx/geometry/size.h"

namespace blink {
class EnabledClientHints;
struct UserAgentMetadata;
}  // namespace blink

namespace electron {

class ElectronBrowserContext;

// Minimal ClientHintsControllerDelegate so the network stack actually emits
// the low-entropy User-Agent client hints (Sec-CH-UA, Sec-CH-UA-Mobile,
// Sec-CH-UA-Platform) on outgoing requests.
//
// Stock Electron returns nullptr from
// ElectronBrowserContext::GetClientHintsControllerDelegate(), which makes
// content/browser/client_hints/client_hints.cc short-circuit and emit NO
// client hints at all — even the ones that real Chrome attaches
// unconditionally after UA Reduction (Chrome 110+, 2023). That single
// omission is enough for Akamai / DataDome / PerimeterX to classify the
// connection as "not Chrome" regardless of how good the rest of the
// fingerprint is.
//
// We don't ship a per-origin Accept-CH persistence store, so high-entropy
// hints (Sec-CH-UA-Arch / Platform-Version / Full-Version-List) are still
// only delivered when a site explicitly opts in via Accept-CH on a prior
// response, exactly like Chrome. The brand list itself comes from
// ElectronBrowserClient::GetUserAgentMetadata(), which the
// FingerprintOverrideManager already populates with the right brands.
class ElectronClientHintsControllerDelegate
    : public content::ClientHintsControllerDelegate {
 public:
  explicit ElectronClientHintsControllerDelegate(
      ElectronBrowserContext* browser_context);
  ~ElectronClientHintsControllerDelegate() override;

  ElectronClientHintsControllerDelegate(
      const ElectronClientHintsControllerDelegate&) = delete;
  ElectronClientHintsControllerDelegate& operator=(
      const ElectronClientHintsControllerDelegate&) = delete;

  // content::ClientHintsControllerDelegate:
  network::NetworkQualityTracker* GetNetworkQualityTracker() override;
  void GetAllowedClientHintsFromSource(
      const url::Origin& origin,
      blink::EnabledClientHints* client_hints) override;
  bool IsJavaScriptAllowed(const GURL& url,
                           content::RenderFrameHost* parent_rfh) override;
  blink::UserAgentMetadata GetUserAgentMetadata() override;
  void PersistClientHints(
      const url::Origin& primary_origin,
      content::RenderFrameHost* parent_rfh,
      const std::vector<network::mojom::WebClientHintsType>& client_hints)
      override;
  void SetAdditionalClientHints(
      const std::vector<network::mojom::WebClientHintsType>& hints) override;
  void ClearAdditionalClientHints() override;
  void SetMostRecentMainFrameViewportSize(
      const gfx::Size& viewport_size) override;
  gfx::Size GetMostRecentMainFrameViewportSize() override;

 private:
  raw_ptr<ElectronBrowserContext> const browser_context_;

  // Runtime-injected hints from SetAdditionalClientHints(). Folded into the
  // EnabledClientHints set returned by GetAllowedClientHintsFromSource so
  // they take effect on the very next request without persistence.
  std::vector<network::mojom::WebClientHintsType> additional_hints_;

  // Most recent main-frame viewport size. Used by the network stack for
  // Sec-CH-Viewport-* hints on requests where the live viewport isn't
  // available (prefetch, restore). Updated by the embedder when the view
  // resizes; safe default is empty (Chromium falls back gracefully).
  gfx::Size viewport_size_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_ELECTRON_CLIENT_HINTS_CONTROLLER_DELEGATE_H_
