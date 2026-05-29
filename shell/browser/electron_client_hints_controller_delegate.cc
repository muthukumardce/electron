// Copyright (c) 2026 Electron / FalconBrowser contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_client_hints_controller_delegate.h"

#include "shell/browser/electron_browser_client.h"
#include "third_party/blink/public/common/client_hints/enabled_client_hints.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace electron {

ElectronClientHintsControllerDelegate::ElectronClientHintsControllerDelegate(
    ElectronBrowserContext* browser_context)
    : browser_context_(browser_context) {}

ElectronClientHintsControllerDelegate::
    ~ElectronClientHintsControllerDelegate() = default;

network::NetworkQualityTracker*
ElectronClientHintsControllerDelegate::GetNetworkQualityTracker() {
  // Electron doesn't run a Network Quality Estimator. Returning nullptr is
  // safe — Chromium guards the NQT-derived hints (RTT, Downlink, ECT) with
  // a null check; those hints simply won't be added on outgoing requests.
  // The low-entropy UA hints we care about don't consult the tracker at all.
  return nullptr;
}

void ElectronClientHintsControllerDelegate::GetAllowedClientHintsFromSource(
    const url::Origin& origin,
    blink::EnabledClientHints* client_hints) {
  // No per-origin Accept-CH persistence store. The unconditional
  // low-entropy hints (Sec-CH-UA, Sec-CH-UA-Mobile, Sec-CH-UA-Platform)
  // are added by content/browser/client_hints/client_hints.cc regardless
  // of whether they appear in this set — that's intentional, see the
  // comment above AddUAHeader(kUA) in that file. We still surface any
  // hints set via SetAdditionalClientHints so runtime-injected ones (used
  // by some Chromium internals) take effect.
  for (auto hint : additional_hints_) {
    client_hints->SetIsEnabled(hint, true);
  }
}

bool ElectronClientHintsControllerDelegate::IsJavaScriptAllowed(
    const GURL& url,
    content::RenderFrameHost* parent_rfh) {
  // Intentionally permissive: Electron doesn't expose a per-origin
  // JavaScript toggle, so always returning true matches real-world
  // behavior. This is also load-bearing — ShouldAddClientHints() in
  // client_hints.cc gates ALL hint emission on this returning true, so
  // returning false here is equivalent to disabling client hints entirely
  // (which is the bug we're fixing).
  return true;
}

blink::UserAgentMetadata
ElectronClientHintsControllerDelegate::GetUserAgentMetadata() {
  // Call directly into ElectronBrowserClient (via its static Get()) instead
  // of routing through content::GetContentClient(), which isn't exported
  // for use outside content/. ElectronBrowserClient::GetUserAgentMetadata()
  // is the single source of truth — it consults FingerprintOverrideManager
  // for any active per-session override and otherwise returns the embedder
  // default with the "Google Chrome" brand appended. Keeping the delegate
  // in lock-step with that one accessor means an override applied via
  // setFingerprintOverrides flows automatically into the Sec-CH-UA headers.
  return ElectronBrowserClient::Get()->GetUserAgentMetadata();
}

void ElectronClientHintsControllerDelegate::PersistClientHints(
    const url::Origin& primary_origin,
    content::RenderFrameHost* parent_rfh,
    const std::vector<network::mojom::WebClientHintsType>& client_hints) {
  // No-op: Electron doesn't persist per-origin Accept-CH preferences.
  // The high-entropy hints get added only on requests where the calling
  // page has explicitly delivered Accept-CH on a prior response in the
  // same browsing session (handled by Chromium without our involvement).
}

void ElectronClientHintsControllerDelegate::SetAdditionalClientHints(
    const std::vector<network::mojom::WebClientHintsType>& hints) {
  additional_hints_ = hints;
}

void ElectronClientHintsControllerDelegate::ClearAdditionalClientHints() {
  additional_hints_.clear();
}

void ElectronClientHintsControllerDelegate::
    SetMostRecentMainFrameViewportSize(const gfx::Size& viewport_size) {
  viewport_size_ = viewport_size;
}

gfx::Size
ElectronClientHintsControllerDelegate::GetMostRecentMainFrameViewportSize() {
  return viewport_size_;
}

}  // namespace electron
