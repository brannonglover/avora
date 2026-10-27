// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_UPLOAD_SCHEDULER_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_UPLOAD_SCHEDULER_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace avora::sync {

// Decides when the tab snapshot is uploaded.
//
// Tab changes arrive in bursts -- a page load alone reports its URL, then its
// title, then its title again -- so each change only marks the snapshot dirty,
// and the upload waits until the tabs have been quiet for `quiet_period`, or
// for `max_delay` after the first unsent change if they never go quiet. The
// snapshot is taken when the upload starts, so it is always the full current
// set, and one identical to the last one the server accepted is not sent
// again. At most one upload is in flight; a change during it is sent after it.
// A failed upload is retried with exponential backoff.
class UploadScheduler {
 public:
  // Builds the request body for the tabs as they are now.
  using SnapshotCallback = base::RepeatingCallback<std::string()>;
  // Sends `body`, then runs `done` with whether the server accepted it.
  using UploadCallback =
      base::RepeatingCallback<void(std::string body,
                                   base::OnceCallback<void(bool)> done)>;

  static constexpr base::TimeDelta kDefaultQuietPeriod = base::Seconds(3);
  static constexpr base::TimeDelta kDefaultMaxDelay = base::Seconds(10);
  static constexpr base::TimeDelta kInitialRetryDelay = base::Seconds(15);
  static constexpr base::TimeDelta kMaxRetryDelay = base::Minutes(10);

  UploadScheduler(SnapshotCallback snapshot,
                  UploadCallback upload,
                  base::TimeDelta quiet_period = kDefaultQuietPeriod,
                  base::TimeDelta max_delay = kDefaultMaxDelay);
  UploadScheduler(const UploadScheduler&) = delete;
  UploadScheduler& operator=(const UploadScheduler&) = delete;
  ~UploadScheduler();

  // The tabs changed.
  void MarkDirty();

  // Uploads as soon as nothing is in flight, without waiting for quiet.
  void UploadNow();

  // Drops any pending upload and forgets what was last sent, so the next
  // MarkDirty() always uploads (for example after signing in again). An upload
  // already in flight completes, but its result is ignored.
  void Reset();

  bool upload_pending() const { return dirty_; }
  bool upload_in_flight() const { return in_flight_; }

 private:
  void Arm(base::TimeDelta delay);
  void Fire();
  void OnUploadDone(uint64_t generation, std::string body, bool success);

  SnapshotCallback snapshot_;
  UploadCallback upload_;
  const base::TimeDelta quiet_period_;
  const base::TimeDelta max_delay_;

  // Set by MarkDirty() and cleared when an upload starts.
  bool dirty_ = false;
  // When the oldest unsent change happened, for `max_delay_`.
  base::TimeTicks first_dirty_at_;
  bool in_flight_ = false;
  // The last body the server accepted.
  std::optional<std::string> last_uploaded_;
  // Bumped by Reset() so a stale upload's result is ignored.
  uint64_t generation_ = 0;
  base::TimeDelta retry_delay_;

  base::OneShotTimer timer_;
  base::WeakPtrFactory<UploadScheduler> weak_factory_{this};
};

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_UPLOAD_SCHEDULER_H_
