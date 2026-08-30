#include "examples/shared/app_factory.h"

#include "include/wrapper/cef_message_router.h"

namespace avora {

class RendererApp : public CefApp, public CefRenderProcessHandler {
 public:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  void OnWebKitInitialized() override {
    CefMessageRouterConfig config;
    message_router_ = CefMessageRouterRendererSide::Create(config);
  }

  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override {
    message_router_->OnContextCreated(browser, frame, context);
  }

  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override {
    message_router_->OnContextReleased(browser, frame, context);
  }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override {
    return message_router_->OnProcessMessageReceived(browser, frame,
                                                     source_process, message);
  }

 private:
  CefRefPtr<CefMessageRouterRendererSide> message_router_;
  IMPLEMENT_REFCOUNTING(RendererApp);
};

}  // namespace avora

namespace shared {

CefRefPtr<CefApp> CreateRendererProcessApp() {
  return new avora::RendererApp();
}

}  // namespace shared
