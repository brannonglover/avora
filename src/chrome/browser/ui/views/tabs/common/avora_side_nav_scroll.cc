// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/tabs/common/avora_side_nav_scroll.h"

#include "ui/views/controls/scroll_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace avora {

views::ScrollView* FindEnclosingScrollView(const views::View* view) {
  for (const views::View* ancestor = view ? view->parent() : nullptr; ancestor;
       ancestor = ancestor->parent()) {
    if (const auto* scroll_view =
            views::AsViewClass<views::ScrollView>(ancestor)) {
      return const_cast<views::ScrollView*>(scroll_view);
    }
  }
  return nullptr;
}

}  // namespace avora
