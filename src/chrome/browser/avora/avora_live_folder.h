// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_H_
#define CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_H_

#include <string>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"

namespace avora {

// Identifies the backend that owns a Live Folder's contents.  Stored as a
// string in prefs so an unknown provider read from a newer profile degrades to
// kUnknown instead of corrupting the folder list.
enum class LiveFolderProviderId {
  kUnknown = 0,
  kGitHub = 1,
};

std::string LiveFolderProviderIdToString(LiveFolderProviderId id);
LiveFolderProviderId LiveFolderProviderIdFromString(const std::string& value);

// Lifecycle of a provider-owned item.  Only kOpen and kDraft are displayed;
// the terminal states exist so a sync can tell "this PR merged" apart from
// "this PR fell out of the query", which is what drives the removal animation
// and the unseen badge.
enum class LiveFolderItemStatus {
  kOpen = 0,
  kDraft = 1,
  kMerged = 2,
  kClosed = 3,
};

std::string LiveFolderItemStatusToString(LiveFolderItemStatus status);
LiveFolderItemStatus LiveFolderItemStatusFromString(const std::string& value);

// True for the states a Live Folder still shows in the sidebar.
bool IsActiveLiveFolderItemStatus(LiveFolderItemStatus status);

// Provider-specific detail shown in the row subtitle and used for filtering.
// Kept as a flat struct rather than a generic dictionary because every field
// here is rendered, and a typo in a string key would fail silently at runtime.
struct LiveFolderItemMetadata {
  // "owner/name", e.g. "avora/browser".
  std::string repo;

  // Login of the user who opened the item.
  std::string author;

  // Pull request number within |repo|.  Zero when the provider has no
  // equivalent concept.
  int number = 0;

  base::DictValue ToDict() const;
  static LiveFolderItemMetadata FromDict(const base::DictValue& dict);
};

// One row inside a Live Folder.
//
// Unlike a SidebarItem, the user does not own this record: it is created,
// updated, and deleted by the provider on every sync.  Nothing here should be
// user-editable, because the next sync would overwrite it.
struct LiveFolderItem {
  // Stable identity assigned by the remote service (GitHub's node/database id).
  // Sync matches on this, never on URL or title, so a renamed PR keeps its
  // position and its "already seen" state.
  std::string external_id;

  std::string title;
  std::string url;

  LiveFolderItemStatus status = LiveFolderItemStatus::kOpen;

  LiveFolderItemMetadata metadata;

  // Remote last-modified time, used for ordering.  Newest first.
  base::Time updated_at;

  // False until the row has been shown to the user while the folder was
  // expanded.  Drives the "something new appeared" affordance on a collapsed
  // folder.
  bool seen = false;

  base::DictValue ToDict() const;
  static LiveFolderItem FromDict(const base::DictValue& dict);
};

// The saved query that defines a Live Folder's contents.
//
// An all-false struct is meaningless (it would match nothing), so the store
// treats it as "use the provider's default involvement set" rather than
// returning an empty folder.
struct LiveFolderQuery {
  // Restrict to these "owner/name" repositories.  Empty means all repositories
  // visible to the connected account.
  std::vector<std::string> repositories;

  // Involvement filters.  These are OR-ed together: an item matching any
  // enabled filter appears in the folder.
  bool created_by_me = false;
  bool assigned_to_me = false;
  bool review_requested = false;
  bool mentioned = false;

  // Review requests addressed to a team the account belongs to, rather than to
  // the account directly.
  bool team_review_requested = false;

  // When false, draft pull requests are filtered out after fetching.
  bool include_drafts = true;

  // True when no involvement filter is set, meaning the provider should
  // substitute its default set.
  bool HasNoInvolvementFilter() const;

  // The involvement set Arc ships by default: anything that touches you, plus
  // review requests (which |involves:| does not cover).
  static LiveFolderQuery DefaultForGitHub();

  base::DictValue ToDict() const;
  static LiveFolderQuery FromDict(const base::DictValue& dict);
};

// A sidebar folder whose contents are owned by a provider rather than by the
// user.
//
// Space-scoped like every other sidebar concept: |space_id| is the owning
// Space, and switching Spaces swaps the visible folder set wholesale.
struct LiveFolder {
  std::string id;
  std::string space_id;

  std::string name;

  LiveFolderProviderId provider = LiveFolderProviderId::kUnknown;

  LiveFolderQuery query;

  // Provider-owned contents from the last successful sync.  Persisted so the
  // sidebar renders instantly at startup instead of blank-then-populate.
  std::vector<LiveFolderItem> items;

  bool expanded = true;

  int order = 0;

  // Completion time of the last successful sync.  Null when never synced.
  base::Time last_synced_at;

  // Number of active items the user has not seen yet.
  int UnseenCount() const;

  // Items still worth displaying, newest first.
  std::vector<LiveFolderItem> ActiveItems() const;

  base::DictValue ToDict() const;
  static LiveFolder FromDict(const base::DictValue& dict);
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_H_
