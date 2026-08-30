#include "examples/shared/main.h"

#if defined(OS_WIN)
#if defined(CEF_USE_BOOTSTRAP)
#include "include/cef_sandbox_win.h"

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE hInstance,
                                    LPTSTR lpCmdLine,
                                    int nCmdShow,
                                    void* sandbox_info,
                                    cef_version_info_t* /*version_info*/) {
  return shared::wWinMain(hInstance, sandbox_info);
}

#else  // !defined(CEF_USE_BOOTSTRAP)

int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPTSTR lpCmdLine,
                      int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);
  UNREFERENCED_PARAMETER(nCmdShow);
  return shared::wWinMain(hInstance, /*sandbox_info=*/nullptr);
}

#endif  // !defined(CEF_USE_BOOTSTRAP)
#else   // !defined(OS_WIN)
int main(int argc, char* argv[]) {
  return shared::main(argc, argv);
}
#endif  // !defined(OS_WIN)
