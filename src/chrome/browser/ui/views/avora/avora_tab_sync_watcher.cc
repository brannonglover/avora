// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_tab_sync_watcher.h"

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "chrome/browser/avora/avora_tab_guid.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_materializer.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/jpeg_codec.h"

namespace avora {

namespace {

using sync::TabSyncService;

// Only ordinary tabbed windows: popups (including the mini window), app and
// DevTools windows hold no tabs the user shares.
bool IsSharingBrowser(BrowserWindowInterface* browser) {
  return browser && browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
         browser->GetProfile() && !browser->GetProfile()->IsOffTheRecord();
}

// Windows closing on the way out are not tabs being closed.
bool IsQuitting() {
  return browser_shutdown::IsTryingToQuit() ||
         browser_shutdown::HasShutdownStarted();
}

TabSyncService* ServiceFor(Profile* profile) {
  return IsQuitting() ? nullptr : TabSyncService::GetForProfileIfExists(profile);
}

TabSyncService::TodayTab ToTodayTab(const AvoraSpaceTabFilter::SharedTab& tab) {
  return {tab.guid, tab.space_id, tab.url, tab.title};
}

AvoraSpaceTabFilter::SharedTab ToSharedTab(
    const TabSyncService::TodayTab& tab) {
  return {tab.guid, tab.space_id, tab.url, tab.title};
}

// Tab strips are only changed from a task of their own, never from inside a
// tab strip notification or a filter's flush (which is where some of these
// requests come from).
bool IsLiveProfile(Profile* profile) {
  return g_browser_process && g_browser_process->profile_manager() &&
         g_browser_process->profile_manager()->IsValidProfile(profile);
}

// docs/SYNC.md: at most 640 px wide and 256 KB.
constexpr int kPreviewWidth = 640;
constexpr size_t kMaxPreviewBytes = 256 * 1024;
// Lets late layout and web fonts settle before the picture is taken.
constexpr base::TimeDelta kPreviewSettleDelay = base::Milliseconds(1500);

// Encodes off the UI thread: a smaller quality if the first is too big.
std::string EncodePreview(SkBitmap bitmap) {
  for (int quality : {60, 40}) {
    std::optional<std::vector<uint8_t>> jpeg =
        gfx::JPEGCodec::Encode(bitmap, quality);
    if (jpeg && jpeg->size() <= kMaxPreviewBytes) {
      return std::string(jpeg->begin(), jpeg->end());
    }
  }
  return std::string();
}

}  // namespace

// A window's front tab: a preview is taken a moment after it is brought to
// the front or finishes loading.
class TabSyncWatcher::FrontTab : public content::WebContentsObserver {
 public:
  FrontTab(TabSyncWatcher* watcher, content::WebContents* contents)
      : content::WebContentsObserver(contents), watcher_(watcher) {
    Schedule();
  }

  void DidStopLoading() override { Schedule(); }

 private:
  void Schedule() {
    timer_.Start(FROM_HERE, kPreviewSettleDelay,
                 base::BindOnce(&TabSyncWatcher::CapturePreview,
                                base::Unretained(watcher_),
                                web_contents()->GetWeakPtr()));
  }

  raw_ptr<TabSyncWatcher> watcher_;
  base::OneShotTimer timer_;
};

// static
void TabSyncWatcher::EnsureStarted() {
  static TabSyncWatcher* instance = nullptr;
  if (instance) {
    return;
  }
  // Never destroyed: it serves for the life of the browser process, and the
  // collections it observes are torn down at shutdown without it.
  instance = new TabSyncWatcher();
}

TabSyncWatcher::TabSyncWatcher() {
  TabSyncService::SetTodayTabs(this);
  AvoraSpaceTabFilter::SetSharedTabsDelegate(this);
  browsers_observation_.Observe(GlobalBrowserCollection::GetInstance());
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* browser) {
        OnBrowserCreated(browser);
        return true;
      });
}

TabSyncWatcher::~TabSyncWatcher() = default;

void TabSyncWatcher::OnBrowserCreated(BrowserWindowInterface* browser) {
  // Creates the profile's service, which starts syncing once its stored
  // session is read.
  if (IsSharingBrowser(browser)) {
    TabSyncService::GetForProfile(browser->GetProfile());
    if (TabStripModel* model = browser->GetTabStripModel();
        model && !front_tabs_.count(model)) {
      model->AddObserver(this);
      WatchFrontTab(model);
    }
  }
}

void TabSyncWatcher::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    WatchFrontTab(tab_strip_model);
  }
}

void TabSyncWatcher::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  front_tabs_.erase(tab_strip_model);
}

void TabSyncWatcher::WatchFrontTab(TabStripModel* model) {
  content::WebContents* active = model->GetActiveWebContents();
  std::unique_ptr<FrontTab>& front = front_tabs_[model];
  if (!active) {
    front.reset();
  } else if (!front || front->web_contents() != active) {
    front = std::make_unique<FrontTab>(this, active);
  }
}

void TabSyncWatcher::CapturePreview(
    base::WeakPtr<content::WebContents> contents) {
  if (!contents || contents->IsLoading() || IsQuitting()) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(contents->GetBrowserContext());
  TabSyncService* service = ServiceFor(profile);
  const std::string guid = GetTabGuid(contents.get());
  if (!service || guid.empty() || !service->WantsPreview(guid)) {
    return;
  }

  // Banks, mail and account pages usually say no-store.
  content::RenderFrameHost* frame = contents->GetPrimaryMainFrame();
  const network::mojom::URLResponseHead* head = frame->GetLastResponseHead();
  if (head && head->headers &&
      head->headers->HasHeaderValue("cache-control", "no-store")) {
    service->OnPreviewNotAllowed(guid);
    return;
  }
  // In an isolated world, so the page's own scripts can't answer for it.
  frame->ExecuteJavaScriptInIsolatedWorld(
      u"!!document.querySelector('input[type=password]')",
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> contents, std::string guid,
             base::Value has_password) {
            if (!contents || IsQuitting()) {
              return;
            }
            TabSyncService* service = ServiceFor(Profile::FromBrowserContext(
                contents->GetBrowserContext()));
            if (!service) {
              return;
            }
            if (!has_password.is_bool() || has_password.GetBool()) {
              service->OnPreviewNotAllowed(guid);
              return;
            }
            content::RenderWidgetHostView* view =
                contents->GetRenderWidgetHostView();
            const gfx::Size size = view ? view->GetViewBounds().size()
                                        : gfx::Size();
            if (size.IsEmpty()) {
              return;
            }
            const int width = std::min(kPreviewWidth, size.width());
            const gfx::Size output(
                width, size.height() * width / std::max(1, size.width()));
            view->CopyFromSurface(
                gfx::Rect(), output, base::Seconds(5),
                base::BindOnce(
                    [](base::WeakPtr<content::WebContents> contents,
                       std::string guid,
                       const content::CopyFromSurfaceResult& result) {
                      if (!result.has_value() || result->bitmap.drawsNothing()) {
                        return;
                      }
                      base::ThreadPool::PostTaskAndReplyWithResult(
                          FROM_HERE, {base::TaskPriority::BEST_EFFORT},
                          base::BindOnce(&EncodePreview, result->bitmap),
                          base::BindOnce(
                              [](base::WeakPtr<content::WebContents> contents,
                                 std::string guid, std::string jpeg) {
                                if (!contents || jpeg.empty()) {
                                  return;
                                }
                                if (TabSyncService* service =
                                        ServiceFor(Profile::FromBrowserContext(
                                            contents->GetBrowserContext()))) {
                                  service->OnPreviewCaptured(guid,
                                                             std::move(jpeg));
                                }
                              },
                              contents, guid));
                    },
                    contents, guid));
          },
          contents, guid),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void TabSyncWatcher::OnBrowserActivated(BrowserWindowInterface* browser) {
  if (IsSharingBrowser(browser)) {
    if (TabSyncService* service = ServiceFor(browser->GetProfile())) {
      service->SyncSoon();
    }
  }
}

void TabSyncWatcher::OnSharedTabOpened(
    Profile* profile,
    const AvoraSpaceTabFilter::SharedTab& tab,
    const std::string& predecessor,
    const std::string& successor) {
  if (TabSyncService* service = ServiceFor(profile)) {
    service->OnTodayTabOpened(ToTodayTab(tab), predecessor, successor);
  }
}

void TabSyncWatcher::OnSharedTabNavigated(
    Profile* profile,
    const AvoraSpaceTabFilter::SharedTab& tab) {
  if (TabSyncService* service = ServiceFor(profile)) {
    service->OnTodayTabNavigated(ToTodayTab(tab));
  }
}

void TabSyncWatcher::OnSharedTabClosed(Profile* profile,
                                       const std::string& guid) {
  if (TabSyncService* service = ServiceFor(profile)) {
    service->OnTodayTabClosed(guid);
  }
}

std::vector<AvoraSpaceTabFilter::SharedTab> TabSyncWatcher::SharedTabsForSpace(
    Profile* profile,
    const std::string& space_id) {
  std::vector<AvoraSpaceTabFilter::SharedTab> tabs;
  if (TabSyncService* service = ServiceFor(profile)) {
    for (const TabSyncService::TodayTab& tab :
         service->TodayTabsForSpace(space_id)) {
      tabs.push_back(ToSharedTab(tab));
    }
  }
  return tabs;
}

bool TabSyncWatcher::IsReady(Profile* profile) {
  if (IsQuitting() || SessionRestore::IsRestoring(profile)) {
    return false;
  }
  bool has_window = false;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        has_window = IsSharingBrowser(browser) &&
                     browser->GetProfile() == profile;
        return !has_window;
      });
  return has_window;
}

std::vector<TabSyncService::TodayTab> TabSyncWatcher::Collect(
    Profile* profile) {
  std::vector<TabSyncService::TodayTab> tabs;
  for (const AvoraSpaceTabFilter::SharedTab& tab :
       AvoraSpaceTabFilter::CollectSharedTabs(profile)) {
    tabs.push_back(ToTodayTab(tab));
  }
  return tabs;
}

void TabSyncWatcher::Upsert(Profile* profile,
                            const TabSyncService::TodayTab& tab,
                            const std::vector<std::string>& before) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](Profile* profile, AvoraSpaceTabFilter::SharedTab tab,
             std::vector<std::string> before) {
            if (IsLiveProfile(profile) && !IsQuitting()) {
              AvoraSpaceTabFilter::UpsertSharedTab(profile, tab, before);
            }
          },
          profile, ToSharedTab(tab), before));
}

void TabSyncWatcher::Remove(Profile* profile, const std::string& guid) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](Profile* profile, std::string guid) {
                       if (IsLiveProfile(profile) && !IsQuitting()) {
                         AvoraSpaceTabFilter::RemoveSharedTab(profile, guid);
                       }
                     },
                     profile, guid));
}

void TabSyncWatcher::RemovePinnedItem(Profile* profile,
                                      const std::string& item_id) {
  // The open tab, if any, stays open as an ordinary tab, as when the user
  // unpins it here.
  PinnedItemsManager pinned_items(profile->GetPrefs());
  PinnedFoldersManager pinned_folders(profile->GetPrefs());
  bool unmarked = false;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        TabStripModel* model = browser->GetTabStripModel();
        if (!IsSharingBrowser(browser) || browser->GetProfile() != profile ||
            !model) {
          return true;
        }
        for (int i = 0; i < model->count(); ++i) {
          content::WebContents* contents = model->GetWebContentsAt(i);
          if (contents && GetPinnedItemIdForTab(contents) == item_id) {
            UnpinAndRemovePinnedItem(model, &pinned_items, &pinned_folders,
                                     contents);
            unmarked = true;
          }
        }
        return true;
      });
  if (!unmarked) {
    pinned_folders.RemoveItemFromAllFolders(item_id);
  }
}

}  // namespace avora
