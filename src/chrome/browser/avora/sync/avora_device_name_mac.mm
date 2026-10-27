// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_device_name.h"

#import <SystemConfiguration/SystemConfiguration.h>

#include "base/apple/scoped_cftyperef.h"
#include "base/strings/sys_string_conversions.h"
#include "base/system/sys_info.h"

namespace avora::sync {

std::string GetDeviceNameBlocking() {
  // Not NSHost: it can take seconds (crbug.com/138570).
  base::apple::ScopedCFTypeRef<CFStringRef> computer_name(
      SCDynamicStoreCopyComputerName(/*store=*/nullptr,
                                     /*nameEncoding=*/nullptr));
  if (computer_name) {
    std::string name = base::SysCFStringRefToUTF8(computer_name.get());
    if (!name.empty()) {
      return name;
    }
  }
  std::string model = base::SysInfo::HardwareModelName();
  return model.empty() ? "Mac" : model;
}

}  // namespace avora::sync
