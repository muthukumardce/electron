// Copyright (c) 2026 FalconBrowser contributors.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/renderer/electron_chrome_app_extension.h"

#include "v8/include/v8-extension.h"
#include "v8/include/v8-function-callback.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-primitive.h"
#include "v8/include/v8-template.h"

namespace electron {

namespace {

constexpr const char* kExtensionName = "v8/ChromeApp";

// The JS source that runs once per renderer context to install chrome.app.
//
// Notes on the shape:
//   - `var chrome; if (!chrome) chrome = {};` — same idiom LoadTimesExtension
//     uses, so we share the same chrome global (the order in which the two
//     extensions run doesn't matter; whichever runs first creates it,
//     whichever runs second sees it already populated).
//   - The four methods declare `native function GetXxx();` and immediately
//     call it. V8 treats functions defined inside an extension script
//     specially: their Function.prototype.toString returns the native
//     placeholder, which is what bot detectors check.
//   - Property values mirror what stock Chrome on a non-app page exposes.
//     Notably: isInstalled is `false` (not undefined), the enums carry the
//     exact string literals, getIsInstalled returns false, runningState
//     returns the string 'cannot_run', installState's callback is invoked
//     with 'disabled'.
constexpr const char* kExtensionScript =
    "var chrome;"
    "if (!chrome) chrome = {};"
    "chrome.app = {"
    "  isInstalled: false,"
    "  InstallState: {"
    "    DISABLED: 'disabled',"
    "    INSTALLED: 'installed',"
    "    NOT_INSTALLED: 'not_installed'"
    "  },"
    "  RunningState: {"
    "    CANNOT_RUN: 'cannot_run',"
    "    READY_TO_RUN: 'ready_to_run',"
    "    RUNNING: 'running'"
    "  },"
    "  getDetails: function() {"
    "    native function GetDetails();"
    "    return GetDetails();"
    "  },"
    "  getIsInstalled: function() {"
    "    native function GetIsInstalled();"
    "    return GetIsInstalled();"
    "  },"
    "  installState: function(callback) {"
    "    native function GetInstallState();"
    "    return GetInstallState(callback);"
    "  },"
    "  runningState: function() {"
    "    native function GetRunningState();"
    "    return GetRunningState();"
    "  }"
    "};";

// chrome.app.getDetails() on a non-app page returns null in real Chrome.
void GetDetails(const v8::FunctionCallbackInfo<v8::Value>& args) {
  args.GetReturnValue().SetNull();
}

// chrome.app.getIsInstalled() returns false on any page that isn't a hosted
// app (which is every page in our case — we don't host packaged apps).
void GetIsInstalled(const v8::FunctionCallbackInfo<v8::Value>& args) {
  args.GetReturnValue().Set(v8::Boolean::New(args.GetIsolate(), false));
}

// chrome.app.installState(callback) invokes the callback asynchronously with
// the install-state string. For a non-app page, the value is 'disabled'.
// We schedule the callback as a microtask via v8::Function::Call from a
// Promise-then to avoid synchronous calls (real Chrome's implementation
// goes through an IPC round-trip; observable users would notice a sync
// callback as anomalous).
void GetInstallState(const v8::FunctionCallbackInfo<v8::Value>& args) {
  if (args.Length() < 1 || !args[0]->IsFunction())
    return;

  v8::Isolate* isolate = args.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Function> callback = args[0].As<v8::Function>();

  // Build a resolved Promise whose .then handler invokes the user callback
  // with the install-state string. This achieves microtask-deferred
  // dispatch without us needing to plumb a TaskRunner into the renderer.
  v8::Local<v8::Promise::Resolver> resolver;
  if (!v8::Promise::Resolver::New(context).ToLocal(&resolver))
    return;
  v8::Local<v8::String> state_str =
      v8::String::NewFromUtf8(isolate, "disabled",
                              v8::NewStringType::kNormal)
          .ToLocalChecked();
  if (resolver->Resolve(context, state_str).IsNothing())
    return;
  // Resolved promise; the user callback is wired as the .then handler.
  v8::Local<v8::Promise> promise = resolver->GetPromise();
  if (promise->Then(context, callback).IsEmpty())
    return;
}

// chrome.app.runningState() returns one of 'running' / 'ready_to_run' /
// 'cannot_run'. On a non-app page in real Chrome it's 'cannot_run'.
void GetRunningState(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::Local<v8::String> value =
      v8::String::NewFromUtf8(isolate, "cannot_run",
                              v8::NewStringType::kNormal)
          .ToLocalChecked();
  args.GetReturnValue().Set(value);
}

class ChromeAppExtensionWrapper final : public v8::Extension {
 public:
  ChromeAppExtensionWrapper()
      : v8::Extension(kExtensionName, kExtensionScript) {}

  v8::Local<v8::FunctionTemplate> GetNativeFunctionTemplate(
      v8::Isolate* isolate,
      v8::Local<v8::String> name) override {
    auto matches = [&](const char* needle) {
      v8::Local<v8::String> n =
          v8::String::NewFromUtf8(isolate, needle,
                                  v8::NewStringType::kInternalized)
              .ToLocalChecked();
      return name->StringEquals(n);
    };

    if (matches("GetDetails"))
      return v8::FunctionTemplate::New(isolate, GetDetails);
    if (matches("GetIsInstalled"))
      return v8::FunctionTemplate::New(isolate, GetIsInstalled);
    if (matches("GetInstallState"))
      return v8::FunctionTemplate::New(isolate, GetInstallState);
    if (matches("GetRunningState"))
      return v8::FunctionTemplate::New(isolate, GetRunningState);
    return v8::Local<v8::FunctionTemplate>();
  }
};

}  // namespace

// static
std::unique_ptr<v8::Extension> ElectronChromeAppExtension::Get() {
  return std::make_unique<ChromeAppExtensionWrapper>();
}

}  // namespace electron
