// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_upload_scheduler.h"

#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora::sync {
namespace {

constexpr base::TimeDelta kQuiet = base::Seconds(3);
constexpr base::TimeDelta kMax = base::Seconds(10);

class AvoraUploadSchedulerTest : public testing::Test {
 protected:
  AvoraUploadSchedulerTest()
      : scheduler_(base::BindRepeating(&AvoraUploadSchedulerTest::Snapshot,
                                       base::Unretained(this)),
                   base::BindRepeating(&AvoraUploadSchedulerTest::Upload,
                                       base::Unretained(this)),
                   kQuiet,
                   kMax) {}

  std::string Snapshot() {
    ++snapshots_;
    return body_;
  }

  void Upload(std::string body, base::OnceCallback<void(bool)> done) {
    uploads_.push_back(std::move(body));
    pending_done_ = std::move(done);
  }

  // Answers the upload in flight.
  void Complete(bool success) {
    ASSERT_TRUE(pending_done_);
    std::move(pending_done_).Run(success);
  }

  void FastForward(base::TimeDelta delta) {
    task_environment_.FastForwardBy(delta);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::string body_ = "tabs-1";
  int snapshots_ = 0;
  std::vector<std::string> uploads_;
  base::OnceCallback<void(bool)> pending_done_;
  UploadScheduler scheduler_;
};

TEST_F(AvoraUploadSchedulerTest, BurstOfChangesIsOneUploadAfterQuiet) {
  scheduler_.MarkDirty();
  FastForward(base::Seconds(1));
  scheduler_.MarkDirty();
  FastForward(base::Seconds(1));
  scheduler_.MarkDirty();

  FastForward(kQuiet - base::Milliseconds(1));
  EXPECT_TRUE(uploads_.empty());
  EXPECT_EQ(snapshots_, 0);

  FastForward(base::Milliseconds(1));
  EXPECT_EQ(uploads_, std::vector<std::string>{"tabs-1"});
  EXPECT_EQ(snapshots_, 1);
}

TEST_F(AvoraUploadSchedulerTest, NeverQuietStillUploadsByMaxDelay) {
  for (int i = 0; i < 9; ++i) {
    scheduler_.MarkDirty();
    FastForward(base::Seconds(1));
  }
  EXPECT_TRUE(uploads_.empty());
  scheduler_.MarkDirty();
  FastForward(base::Seconds(1));
  EXPECT_EQ(uploads_.size(), 1u);
}

TEST_F(AvoraUploadSchedulerTest, SnapshotIsTakenWhenTheUploadStarts) {
  scheduler_.MarkDirty();
  body_ = "tabs-2";
  FastForward(kQuiet);
  EXPECT_EQ(uploads_, std::vector<std::string>{"tabs-2"});
}

TEST_F(AvoraUploadSchedulerTest, UnchangedSnapshotIsNotSentAgain) {
  scheduler_.MarkDirty();
  FastForward(kQuiet);
  Complete(true);

  // A change that ends where it started (say, a title flickering back).
  scheduler_.MarkDirty();
  FastForward(kQuiet);
  EXPECT_EQ(uploads_.size(), 1u);
  EXPECT_EQ(snapshots_, 2);

  body_ = "tabs-2";
  scheduler_.MarkDirty();
  FastForward(kQuiet);
  EXPECT_EQ(uploads_, (std::vector<std::string>{"tabs-1", "tabs-2"}));
}

TEST_F(AvoraUploadSchedulerTest, ChangeDuringUploadIsSentAfterIt) {
  scheduler_.MarkDirty();
  FastForward(kQuiet);
  ASSERT_EQ(uploads_.size(), 1u);

  body_ = "tabs-2";
  scheduler_.MarkDirty();
  FastForward(kMax * 2);
  // Never two in flight.
  EXPECT_EQ(uploads_.size(), 1u);

  Complete(true);
  FastForward(kQuiet);
  EXPECT_EQ(uploads_, (std::vector<std::string>{"tabs-1", "tabs-2"}));
}

TEST_F(AvoraUploadSchedulerTest, FailureIsRetriedWithBackoff) {
  scheduler_.MarkDirty();
  FastForward(kQuiet);
  Complete(false);

  // A new change does not bring the retry forward.
  scheduler_.MarkDirty();
  FastForward(UploadScheduler::kInitialRetryDelay - base::Seconds(1));
  EXPECT_EQ(uploads_.size(), 1u);
  FastForward(base::Seconds(1));
  ASSERT_EQ(uploads_.size(), 2u);

  Complete(false);
  FastForward(UploadScheduler::kInitialRetryDelay * 2 - base::Seconds(1));
  EXPECT_EQ(uploads_.size(), 2u);
  FastForward(base::Seconds(1));
  ASSERT_EQ(uploads_.size(), 3u);

  Complete(true);
  // Backoff is over: the next change goes out after the usual quiet period.
  body_ = "tabs-2";
  scheduler_.MarkDirty();
  FastForward(kQuiet);
  EXPECT_EQ(uploads_.size(), 4u);
}

TEST_F(AvoraUploadSchedulerTest, UploadNowSkipsTheWait) {
  scheduler_.UploadNow();
  EXPECT_EQ(uploads_.size(), 1u);
}

TEST_F(AvoraUploadSchedulerTest, EmptySnapshotSendsNothing) {
  body_.clear();
  scheduler_.MarkDirty();
  FastForward(kMax);
  EXPECT_TRUE(uploads_.empty());
  EXPECT_FALSE(scheduler_.upload_pending());
}

TEST_F(AvoraUploadSchedulerTest, ResetDropsPendingWorkAndForgetsLastBody) {
  scheduler_.MarkDirty();
  FastForward(kQuiet);
  Complete(true);

  scheduler_.MarkDirty();
  scheduler_.Reset();
  FastForward(kMax);
  EXPECT_EQ(uploads_.size(), 1u);

  // The same body is sent again after a reset (a new sign-in).
  scheduler_.MarkDirty();
  FastForward(kQuiet);
  EXPECT_EQ(uploads_.size(), 2u);
}

TEST_F(AvoraUploadSchedulerTest, ResultOfUploadFromBeforeResetIsIgnored) {
  scheduler_.MarkDirty();
  FastForward(kQuiet);
  base::OnceCallback<void(bool)> stale = std::move(pending_done_);
  scheduler_.Reset();

  std::move(stale).Run(false);
  FastForward(UploadScheduler::kMaxRetryDelay);
  // No retry of the stale upload.
  EXPECT_EQ(uploads_.size(), 1u);
}

}  // namespace
}  // namespace avora::sync
