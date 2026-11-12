// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PAGE_EDGE_HANDLE_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PAGE_EDGE_HANDLE_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"

namespace avora {

// A grab strip on the left or right edge of the page.  Dragging it narrows
// or widens the page around its centre, inside a window that stays the same
// size; the space the page gives up shows black.  That is how a site's
// responsive layout gets checked in Avora: pull the page in to a phone's
// width and read the size off the readout.
//
// The strip lies just outside the page -- in the gap between the page and
// the window edge, or in the black space once the page is narrower -- so it
// never takes a click or a scrollbar drag from the page.
class AvoraPageEdgeHandle : public views::View {
  METADATA_HEADER(AvoraPageEdgeHandle, views::View)

 public:
  enum class Edge { kLeft, kRight };

  class Delegate {
   public:
    virtual void OnPageEdgeDragStarted() = 0;
    // |outward| is how far the edge has moved away from the page's centre
    // since the drag began, in DIPs; negative is inward.
    virtual void OnPageEdgeDragged(int outward) = 0;
    virtual void OnPageEdgeDragEnded() = 0;
    // Double-click: the page fills its space again.
    virtual void OnPageEdgeReset() = 0;

   protected:
    virtual ~Delegate() = default;
  };

  explicit AvoraPageEdgeHandle(Edge edge);
  AvoraPageEdgeHandle(const AvoraPageEdgeHandle&) = delete;
  AvoraPageEdgeHandle& operator=(const AvoraPageEdgeHandle&) = delete;
  ~AvoraPageEdgeHandle() override;

  Edge edge() const { return edge_; }
  void SetDelegate(Delegate* delegate) { delegate_ = delegate; }

  // Whether the page is narrowed.  The grip only shows then, or while it is
  // being dragged; at full width the strip is still there to grab -- the
  // cursor shows it -- but draws nothing.
  void SetPageNarrowed(bool narrowed);
  bool IsGripShown() const { return page_narrowed_ || dragging_; }

  // views::View:
  ui::Cursor GetCursor(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  void EndDrag();

  const Edge edge_;
  raw_ptr<Delegate> delegate_ = nullptr;
  bool dragging_ = false;
  bool hovered_ = false;
  bool page_narrowed_ = false;
  gfx::Point drag_start_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PAGE_EDGE_HANDLE_H_
