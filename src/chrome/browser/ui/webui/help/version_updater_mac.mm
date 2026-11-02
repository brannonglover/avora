// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Avora: the About page reports Sparkle's view of updates.  Upstream asks
// Google's updater, which isn't in a Chromium build, so every visit to About
// ended in "An error occurred while checking for updates: 0".

#include "chrome/browser/ui/webui/help/version_updater.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/avora/avora_updater.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

void ReportStatus(const VersionUpdater::StatusCallback& status_callback,
                  VersionUpdater::Status status,
                  const std::u16string& message = std::u16string()) {
  status_callback.Run(status, /*progress=*/0, /*rollback=*/false,
                      /*powerwash=*/false, std::string(),
                      /*update_size=*/0, message);
}

void OnUpdateInformation(VersionUpdater::StatusCallback status_callback,
                         avora::UpdateCheckResult result,
                         const std::u16string& error) {
  switch (result) {
    case avora::UpdateCheckResult::kUpToDate:
      ReportStatus(status_callback, VersionUpdater::UPDATED);
      return;
    case avora::UpdateCheckResult::kUpdateAvailable:
      // Sparkle's window offers it, with the notes and the choice to install,
      // so the page has nothing to add.
      avora::CheckForUpdates();
      ReportStatus(status_callback, VersionUpdater::DISABLED);
      return;
    case avora::UpdateCheckResult::kBusy:
      if (!avora::WaitingUpdateVersion().empty()) {
        avora::CheckForUpdates();
      }
      ReportStatus(status_callback, VersionUpdater::DISABLED);
      return;
    case avora::UpdateCheckResult::kFailed:
      ReportStatus(status_callback, VersionUpdater::FAILED,
                   l10n_util::GetStringFUTF16(
                       IDS_ABOUT_BOX_ERROR_DURING_UPDATE_CHECK, error));
      return;
  }
}

class VersionUpdaterMac : public VersionUpdater {
 public:
  VersionUpdaterMac() = default;
  VersionUpdaterMac(const VersionUpdaterMac&) = delete;
  VersionUpdaterMac& operator=(const VersionUpdaterMac&) = delete;
  ~VersionUpdaterMac() override = default;

  // VersionUpdater implementation.  DISABLED hides the update line, which is
  // right for a build from out/Default: it has no updater to ask.
  void CheckForUpdate(StatusCallback status_callback,
                      PromoteCallback promote_callback) override {
    if (!avora::IsUpdaterEnabled()) {
      ReportStatus(status_callback, DISABLED);
      return;
    }
    ReportStatus(status_callback, CHECKING);
    avora::CheckForUpdateInformation(
        base::BindOnce(&OnUpdateInformation, status_callback));
  }
  void PromoteUpdater() override {}
};

}  // namespace

std::unique_ptr<VersionUpdater> VersionUpdater::Create(
    content::WebContents* /* web_contents */) {
  return base::WrapUnique(new VersionUpdaterMac());
}
