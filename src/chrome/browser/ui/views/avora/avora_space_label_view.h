// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACE_LABEL_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACE_LABEL_VIEW_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace views {
class Label;
}

namespace avora {

// The active Space's name, shown between the favorites grid and the daily tab
// list.  Replaces what used to be a hardcoded "Default Space" string.
//
// When a WindowSpaceState is provided (the normal multi-window path), the view
// reads the active Space from it and observes it for changes.  Otherwise it
// falls back to SpaceManager::GetActiveSpace() for backward compatibility.
class AvoraSpaceLabelView : public views::View,
                            public SpaceManagerObserver,
                            public WindowSpaceState::Observer {
  METADATA_HEADER(AvoraSpaceLabelView, views::View)

 public:
  AvoraSpaceLabelView(BrowserWindowInterface* browser,
                      WindowSpaceState* window_space_state);
  AvoraSpaceLabelView(const AvoraSpaceLabelView&) = delete;
  AvoraSpaceLabelView& operator=(const AvoraSpaceLabelView&) = delete;
  ~AvoraSpaceLabelView() override;

  SpaceManager* GetSpaceManager() const { return space_manager_.get(); }

  // SpaceManagerObserver:
  void OnSpacesChanged() override;
  void OnActiveSpaceChanged(const std::string& space_id) override;

  // WindowSpaceState::Observer:
  void OnWindowActiveSpaceChanged(const std::string& space_id) override;

 private:
  void UpdateLabel();

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<WindowSpaceState> window_space_state_ = nullptr;
  std::unique_ptr<SpaceManager> space_manager_;
  raw_ptr<views::Label> label_ = nullptr;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACE_LABEL_VIEW_H_
