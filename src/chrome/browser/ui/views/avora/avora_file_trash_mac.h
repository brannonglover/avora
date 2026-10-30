// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_FILE_TRASH_MAC_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_FILE_TRASH_MAC_H_

namespace base {
class FilePath;
}  // namespace base

namespace avora {

// Moves the file or folder at |path| to the Trash, returning whether it got
// there.  Fails rather than deleting on a volume with no Trash.  Blocks on
// disk, so it runs on the thread pool.
bool MoveFileToTrash(const base::FilePath& path);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_FILE_TRASH_MAC_H_
