// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_DEVICE_NAME_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_DEVICE_NAME_H_

#include <string>

namespace avora::sync {

// The name this computer goes by, for the phone's section title: on macOS the
// Computer Name from Sharing settings ("Brannon's MacBook Pro"). Blocking;
// call it from a thread that allows blocking.
std::string GetDeviceNameBlocking();

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_DEVICE_NAME_H_
