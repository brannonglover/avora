// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_upload_scheduler.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"

namespace avora::sync {

UploadScheduler::UploadScheduler(SnapshotCallback snapshot,
                                 UploadCallback upload,
                                 base::TimeDelta quiet_period,
                                 base::TimeDelta max_delay)
    : snapshot_(std::move(snapshot)),
      upload_(std::move(upload)),
      quiet_period_(quiet_period),
      max_delay_(max_delay) {}

UploadScheduler::~UploadScheduler() = default;

void UploadScheduler::MarkDirty() {
  const base::TimeTicks now = base::TimeTicks::Now();
  if (!dirty_) {
    dirty_ = true;
    first_dirty_at_ = now;
  }
  if (in_flight_) {
    // OnUploadDone() schedules the follow-up.
    return;
  }
  const base::TimeTicks deadline =
      std::min(now + quiet_period_, first_dirty_at_ + max_delay_);
  Arm(deadline - now);
}

void UploadScheduler::UploadNow() {
  if (!dirty_) {
    dirty_ = true;
    first_dirty_at_ = base::TimeTicks::Now();
  }
  if (in_flight_) {
    return;
  }
  timer_.Stop();
  Fire();
}

void UploadScheduler::Reset() {
  timer_.Stop();
  dirty_ = false;
  in_flight_ = false;
  last_uploaded_.reset();
  retry_delay_ = base::TimeDelta();
  ++generation_;
}

void UploadScheduler::Arm(base::TimeDelta delay) {
  // While backing off after a failure, changes do not bring the retry forward.
  if (timer_.IsRunning() && !retry_delay_.is_zero()) {
    delay = std::max(delay, timer_.desired_run_time() - base::TimeTicks::Now());
  }
  timer_.Start(FROM_HERE, std::max(delay, base::TimeDelta()),
               base::BindOnce(&UploadScheduler::Fire,
                              weak_factory_.GetWeakPtr()));
}

void UploadScheduler::Fire() {
  if (in_flight_ || !dirty_) {
    return;
  }
  dirty_ = false;
  std::string body = snapshot_.Run();
  // An empty body means there is nothing that can be sent right now (for
  // example the device name is not known yet); whoever fixes that marks the
  // snapshot dirty again.
  if (body.empty() || body == last_uploaded_) {
    return;
  }
  in_flight_ = true;
  upload_.Run(body, base::BindOnce(&UploadScheduler::OnUploadDone,
                                   weak_factory_.GetWeakPtr(), generation_,
                                   body));
}

void UploadScheduler::OnUploadDone(uint64_t generation,
                                   std::string body,
                                   bool success) {
  if (generation != generation_) {
    return;
  }
  in_flight_ = false;
  if (success) {
    last_uploaded_ = std::move(body);
    retry_delay_ = base::TimeDelta();
    if (dirty_) {
      MarkDirty();
    }
    return;
  }

  retry_delay_ = retry_delay_.is_zero()
                     ? kInitialRetryDelay
                     : std::min(retry_delay_ * 2, kMaxRetryDelay);
  if (!dirty_) {
    dirty_ = true;
    first_dirty_at_ = base::TimeTicks::Now();
  }
  // Arm() keeps a running retry timer from being brought forward, so stop it
  // first: this is the retry being set.
  timer_.Stop();
  timer_.Start(FROM_HERE, retry_delay_,
               base::BindOnce(&UploadScheduler::Fire,
                              weak_factory_.GetWeakPtr()));
}

}  // namespace avora::sync
