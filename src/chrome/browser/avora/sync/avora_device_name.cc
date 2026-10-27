// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_device_name.h"

#include "net/base/network_interfaces.h"

namespace avora::sync {

std::string GetDeviceNameBlocking() {
  std::string name = net::GetHostName();
  return name.empty() ? "Avora" : name;
}

}  // namespace avora::sync
