// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_updater.h"

#import <Cocoa/Cocoa.h>

#include <utility>
#include <vector>

#include "base/apple/bundle_locations.h"
#include "base/check.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/strings/sys_string_conversions.h"

// The parts of Sparkle used here.  The framework is added at packaging time and
// loaded at runtime, so its headers aren't available to the build.

@protocol AvoraSparkleUpdater
@property(nonatomic, readonly) BOOL sessionInProgress;
- (void)checkForUpdateInformation;
@end

@protocol AvoraSparkleController
@property(nonatomic, readonly) id<AvoraSparkleUpdater> updater;
- (instancetype)initWithStartingUpdater:(BOOL)startUpdater
                        updaterDelegate:(id)updaterDelegate
                     userDriverDelegate:(id)userDriverDelegate;
- (void)checkForUpdates:(id)sender;
@end

@protocol AvoraSparkleUpdate
@property(nonatomic, readonly) NSString* displayVersionString;
@end

@class AvoraUpdaterDelegate;

namespace avora {
namespace {

// Sparkle's code for a check that found nothing newer.
constexpr NSInteger kNoUpdateError = 1001;

class Updater {
 public:
  static Updater& Get() {
    static base::NoDestructor<Updater> updater;
    return *updater;
  }

  id<AvoraSparkleController> controller() { return controller_; }

  // Sparkle only holds its delegate weakly.
  void SetController(id<AvoraSparkleController> controller,
                     AvoraUpdaterDelegate* delegate) {
    controller_ = controller;
    delegate_ = delegate;
  }

  const std::u16string& waiting_version() const { return waiting_version_; }
  void SetWaitingVersion(std::u16string version) {
    if (version == waiting_version_) {
      return;
    }
    waiting_version_ = std::move(version);
    observers_.Notify(&UpdaterObserver::OnWaitingUpdateChanged);
  }

  base::ObserverList<UpdaterObserver>& observers() { return observers_; }

  // Whether a CheckForUpdateInformation() is waiting on Sparkle.
  bool checking() const { return !check_callbacks_.empty(); }
  void AddCheckCallback(UpdateCheckCallback callback) {
    check_callbacks_.push_back(std::move(callback));
  }

  void set_found_update() { found_update_ = true; }

  // Every check ends here, whoever started it.  Callers of
  // CheckForUpdateInformation() hear about the first one to end, which is
  // theirs: Sparkle won't start one while another is in progress.
  void FinishCheck(NSError* error) {
    UpdateCheckResult result = UpdateCheckResult::kUpToDate;
    std::u16string message;
    if (error && error.code != kNoUpdateError) {
      result = UpdateCheckResult::kFailed;
      message = base::SysNSStringToUTF16(error.localizedDescription);
    } else if (!error && found_update_) {
      result = UpdateCheckResult::kUpdateAvailable;
    }
    found_update_ = false;

    std::vector<UpdateCheckCallback> callbacks = std::move(check_callbacks_);
    check_callbacks_.clear();
    for (UpdateCheckCallback& callback : callbacks) {
      std::move(callback).Run(result, message);
    }
  }

 private:
  id<AvoraSparkleController> __strong controller_ = nil;
  AvoraUpdaterDelegate* __strong delegate_ = nil;
  std::u16string waiting_version_;
  base::ObserverList<UpdaterObserver> observers_;
  std::vector<UpdateCheckCallback> check_callbacks_;
  bool found_update_ = false;
};

NSBundle* SparkleBundle() {
  NSURL* frameworks = base::apple::OuterBundle().privateFrameworksURL;
  return [NSBundle
      bundleWithURL:[frameworks URLByAppendingPathComponent:@"Sparkle.framework"
                                                isDirectory:YES]];
}

}  // namespace
}  // namespace avora

// Sparkle's updater and user driver delegate, and its version formatter.
// Sparkle calls all of these on the main thread.
@interface AvoraUpdaterDelegate : NSObject
@end

@implementation AvoraUpdaterDelegate

- (void)updater:(id)updater didFindValidUpdate:(id)item {
  avora::Updater::Get().set_found_update();
}

- (void)updater:(id)updater
    didFinishUpdateCycleForUpdateCheck:(NSInteger)updateCheck
                                 error:(NSError*)error {
  avora::Updater::Get().FinishCheck(error);
}

- (BOOL)supportsGentleScheduledUpdateReminders {
  return YES;
}

// Sparkle would otherwise put its window over the page at a moment of its
// choosing.  A check the user asked for doesn't come through here.
- (BOOL)standardUserDriverShouldHandleShowingScheduledUpdate:(id)update
                                         andInImmediateFocus:
                                             (BOOL)immediateFocus {
  return NO;
}

- (void)standardUserDriverWillHandleShowingUpdate:(BOOL)handleShowingUpdate
                                        forUpdate:
                                            (id<AvoraSparkleUpdate>)update
                                            state:(id)state {
  if (!handleShowingUpdate) {
    avora::Updater::Get().SetWaitingVersion(
        base::SysNSStringToUTF16(update.displayVersionString));
  }
}

// The update was installed, put off or skipped.
- (void)standardUserDriverWillFinishUpdateSession {
  avora::Updater::Get().SetWaitingVersion(std::u16string());
}

// Sparkle names the installed copy from CFBundleShortVersionString, which is
// Chromium's version and has to stay that way.  Packaging puts Avora's in
// CFBundleVersion.
- (id)standardUserDriverRequestsVersionDisplayer {
  return self;
}

- (NSString*)
    formatUpdateDisplayVersionFromUpdate:(id<AvoraSparkleUpdate>)update
                 andBundleDisplayVersion:
                     (NSString* __autoreleasing*)inOutBundleDisplayVersion
                       withBundleVersion:(NSString*)bundleVersion {
  *inOutBundleDisplayVersion = bundleVersion;
  return update.displayVersionString;
}

- (NSString*)formatBundleDisplayVersion:(NSString*)bundleDisplayVersion
                      withBundleVersion:(NSString*)bundleVersion
                         matchingUpdate:(id)matchingUpdate {
  return bundleVersion;
}

@end

namespace avora {

bool IsUpdaterEnabled() {
  NSBundle* bundle = base::apple::OuterBundle();
  return [bundle objectForInfoDictionaryKey:@"SUFeedURL"] &&
         [bundle objectForInfoDictionaryKey:@"SUPublicEDKey"] &&
         SparkleBundle();
}

void StartUpdater() {
  if (!IsUpdaterEnabled()) {
    return;
  }

  NSError* error = nil;
  NSBundle* sparkle = SparkleBundle();
  if (![sparkle loadAndReturnError:&error]) {
    LOG(ERROR) << "Sparkle failed to load: "
               << base::SysNSStringToUTF8(error.localizedDescription);
    return;
  }

  Class controller_class = NSClassFromString(@"SPUStandardUpdaterController");
  CHECK(controller_class);
  AvoraUpdaterDelegate* delegate = [[AvoraUpdaterDelegate alloc] init];
  Updater::Get().SetController([[controller_class alloc]
                                   initWithStartingUpdater:YES
                                           updaterDelegate:delegate
                                        userDriverDelegate:delegate],
                               delegate);
}

void CheckForUpdates() {
  [Updater::Get().controller() checkForUpdates:nil];
}

std::u16string WaitingUpdateVersion() {
  return Updater::Get().waiting_version();
}

void CheckForUpdateInformation(UpdateCheckCallback callback) {
  Updater& state = Updater::Get();
  id<AvoraSparkleUpdater> updater = state.controller().updater;
  if (!updater) {
    std::move(callback).Run(UpdateCheckResult::kFailed,
                            u"Avora's updater didn't start.");
    return;
  }
  if (state.checking()) {
    state.AddCheckCallback(std::move(callback));
    return;
  }
  if (updater.sessionInProgress) {
    std::move(callback).Run(UpdateCheckResult::kBusy, std::u16string());
    return;
  }
  state.AddCheckCallback(std::move(callback));
  [updater checkForUpdateInformation];
}

void AddUpdaterObserver(UpdaterObserver* observer) {
  Updater::Get().observers().AddObserver(observer);
}

void RemoveUpdaterObserver(UpdaterObserver* observer) {
  Updater::Get().observers().RemoveObserver(observer);
}

}  // namespace avora
