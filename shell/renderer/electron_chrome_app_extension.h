// Copyright (c) 2026 FalconBrowser contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_ELECTRON_CHROME_APP_EXTENSION_H_
#define ELECTRON_SHELL_RENDERER_ELECTRON_CHROME_APP_EXTENSION_H_

#include <memory>

namespace v8 {
class Extension;
}  // namespace v8

namespace electron {

// Installs `chrome.app` on the global `chrome` object of every renderer.
//
// Why this exists: Electron + the upstream LoadTimesExtension only attach
// the non-enumerable `loadTimes`/`csi` methods to window.chrome. That makes
// `Object.keys(window.chrome)` return `[]` and `JSON.stringify(window.chrome)`
// return `"{}"`, which Akamai's bot sensor treats as a "this isn't real
// Chrome" tell — real Chrome ships an enumerable `chrome.app` stub on every
// page (regardless of whether an actual hosted app is involved).
//
// The shape we install matches what stock Chrome exposes on a regular,
// non-extension top-level page:
//
//   chrome.app = {
//     isInstalled: false,
//     InstallState: { DISABLED: "disabled", ... },
//     RunningState: { CANNOT_RUN: "cannot_run", ... },
//     getDetails:     function () { [native code] },
//     getIsInstalled: function () { [native code] },
//     installState:   function (cb) { [native code] },
//     runningState:   function () { [native code] },
//   }
//
// Implemented as a v8::Extension so the four methods toString as native
// (Function.prototype.toString on extension-defined functions returns the
// `[native code]` form — same mechanism LoadTimesExtension uses for
// chrome.loadTimes / chrome.csi).
class ElectronChromeAppExtension {
 public:
  static std::unique_ptr<v8::Extension> Get();
};

}  // namespace electron

#endif  // ELECTRON_SHELL_RENDERER_ELECTRON_CHROME_APP_EXTENSION_H_
