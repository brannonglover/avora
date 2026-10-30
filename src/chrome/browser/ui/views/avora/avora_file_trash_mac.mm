// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_file_trash_mac.h"

#import <Foundation/Foundation.h>

#include "base/apple/foundation_util.h"
#include "base/files/file_path.h"

namespace avora {

bool MoveFileToTrash(const base::FilePath& path) {
  return [NSFileManager.defaultManager
      trashItemAtURL:base::apple::FilePathToNSURL(path)
      resultingItemURL:nil
                 error:nil];
}

}  // namespace avora
