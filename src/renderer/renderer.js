const tabsList = document.querySelector("#tabsList");
const pinnedTabsList = document.querySelector("#pinnedTabsList");
const addressForm = document.querySelector("#addressForm");
const addressInput = document.querySelector("#addressInput");
const backButton = document.querySelector("#backButton");
const forwardButton = document.querySelector("#forwardButton");
const reloadButton = document.querySelector("#reloadButton");
const newTabButton = document.querySelector("#newTabButton");
const newTabTopButton = document.querySelector("#newTabTopButton");
const extensionsButton = document.querySelector("#extensionsButton");
const extensionsTray = document.querySelector("#extensionsTray");
const downloadStack = document.querySelector("#downloadStack");
const newTabOverlay = document.querySelector("#newTabOverlay");
const newTabForm = document.querySelector("#newTabForm");
const newTabInput = document.querySelector("#newTabInput");
const extensionInstallOverlay = document.querySelector("#extensionInstallOverlay");
const extensionInstallForm = document.querySelector("#extensionInstallForm");
const extensionInstallInput = document.querySelector("#extensionInstallInput");
const loadUnpackedButton = document.querySelector("#loadUnpackedButton");
const toolbar = document.querySelector(".toolbar");
const sidebarResizer = document.querySelector("#sidebarResizer");

let currentState = {
  activeTabId: null,
  tabs: [],
};

let extensionsState = {
  extensions: [],
};

let downloadsState = {
  downloads: [],
};

let pendingWindowDrag = null;
let pendingSidebarResize = null;

const SIDEBAR_MIN_WIDTH = 184;
const SIDEBAR_MAX_WIDTH = 360;
const CONTENT_MIN_WIDTH = 320;
const CONTENT_INSET = 0;
const SIDEBAR_WIDTH_STORAGE_KEY = "avora.sidebarWidth";

function maxSidebarWidthForWindow() {
  return Math.max(
    SIDEBAR_MIN_WIDTH,
    Math.min(SIDEBAR_MAX_WIDTH, window.innerWidth - CONTENT_MIN_WIDTH - CONTENT_INSET * 2)
  );
}

function clampSidebarWidth(width) {
  const parsedWidth = Number(width);
  const safeWidth = Number.isFinite(parsedWidth) ? parsedWidth : 236;
  return Math.round(Math.max(SIDEBAR_MIN_WIDTH, Math.min(maxSidebarWidthForWindow(), safeWidth)));
}

function setSidebarWidth(width, options = {}) {
  const nextWidth = clampSidebarWidth(width);
  document.documentElement.style.setProperty("--sidebar-width", `${nextWidth}px`);
  sidebarResizer.setAttribute("aria-valuenow", String(nextWidth));

  if (options.persist !== false) {
    window.localStorage.setItem(SIDEBAR_WIDTH_STORAGE_KEY, String(nextWidth));
  }

  if (options.notify !== false) {
    window.browserApi.resizeSidebar(nextWidth);
  }

  return nextWidth;
}

function storedSidebarWidth() {
  const storedValue = window.localStorage.getItem(SIDEBAR_WIDTH_STORAGE_KEY);
  const value = storedValue === null ? NaN : Number(storedValue);
  return Number.isFinite(value) ? value : 236;
}

function canStartToolbarDrag(target) {
  return !target.closest("button, .extensions-tray, .new-tab-overlay");
}

function shouldFocusAddressAfterToolbarClick(target) {
  return Boolean(target.closest("#addressInput, .address-form"));
}

function stopWindowDrag() {
  if (!pendingWindowDrag) {
    return;
  }

  if (pendingWindowDrag.active) {
    window.browserApi.endWindowDrag();
  }

  pendingWindowDrag = null;
  document.body.classList.remove("dragging-window");
}

function stopSidebarResize() {
  if (!pendingSidebarResize) {
    return;
  }

  if (sidebarResizer.hasPointerCapture(pendingSidebarResize.pointerId)) {
    sidebarResizer.releasePointerCapture(pendingSidebarResize.pointerId);
  }

  document.body.classList.remove("resizing-sidebar");
  pendingSidebarResize = null;
}

function selectAddressValue() {
  requestAnimationFrame(() => {
    addressInput.select();
  });
}

function activeTab() {
  return currentState.tabs.find(tab => tab.id === currentState.activeTabId);
}

function shortTitle(tab) {
  if (!tab) {
    return "New tab";
  }

  if (tab.title && tab.title !== "New tab") {
    return tab.title;
  }

  try {
    const url = new URL(tab.url);
    return url.hostname || tab.url;
  } catch {
    return tab.url || "New tab";
  }
}

function tabFallbackLabel(tab) {
  const title = shortTitle(tab).trim();
  if (!title) {
    return "?";
  }

  try {
    const url = new URL(tab.url);
    return url.hostname.replace(/^www\./, "").charAt(0).toUpperCase();
  } catch {
    return title.charAt(0).toUpperCase();
  }
}

function createFavicon(tab, className) {
  if (!tab.faviconUrl) {
    const fallback = document.createElement("span");
    fallback.className = `${className} fallback`;
    fallback.textContent = tabFallbackLabel(tab);
    return fallback;
  }

  const img = document.createElement("img");
  img.className = className;
  img.src = tab.faviconUrl;
  img.alt = "";
  img.referrerPolicy = "no-referrer";
  img.addEventListener("error", () => {
    const fallback = document.createElement("span");
    fallback.className = `${className} fallback`;
    fallback.textContent = tabFallbackLabel(tab);
    img.replaceWith(fallback);
  });
  return img;
}

function extensionFallbackLabel(extension) {
  return (extension.name || "Extension").trim().charAt(0).toUpperCase();
}

function createExtensionIcon(extension) {
  const button = document.createElement("button");
  button.className = "extension-action";
  button.type = "button";
  button.title = `${extension.name} ${extension.version}`;
  button.setAttribute("aria-label", extension.name);
  button.addEventListener("click", async () => {
    const result = await window.browserApi.openExtension(extension.id);
    if (result?.error) {
      window.alert(result.error);
    }
  });

  if (extension.iconUrl) {
    const img = document.createElement("img");
    img.className = "extension-icon";
    img.src = extension.iconUrl;
    img.alt = "";
    img.addEventListener("error", () => {
      const fallback = document.createElement("span");
      fallback.className = "extension-icon fallback";
      fallback.textContent = extensionFallbackLabel(extension);
      img.replaceWith(fallback);
    });
    button.append(img);
  } else {
    const fallback = document.createElement("span");
    fallback.className = "extension-icon fallback";
    fallback.textContent = extensionFallbackLabel(extension);
    button.append(fallback);
  }

  return button;
}

function renderExtensions(state) {
  extensionsState = state;
  extensionsTray.textContent = "";
  extensionsState.extensions.forEach(extension => {
    extensionsTray.append(createExtensionIcon(extension));
  });
}

function downloadStatusText(download) {
  if (download.status === "downloading") {
    if (download.totalBytes) {
      const percent = Math.round((download.progress || 0) * 100);
      return `Downloading ${percent}%`;
    }
    return "Downloading";
  }

  if (download.status === "unpacking") {
    return "Unpacking";
  }

  if (download.status === "installing") {
    return "Installing";
  }

  if (download.status === "complete") {
    return "Installed";
  }

  if (download.status === "error") {
    return download.error || "Install failed";
  }

  return "Starting";
}

function renderDownloads(state) {
  downloadsState = state;
  downloadStack.textContent = "";
  downloadStack.hidden = downloadsState.downloads.length === 0;

  downloadsState.downloads.forEach(download => {
    const item = document.createElement("section");
    item.className = `download-item ${download.status}`;

    const label = document.createElement("div");
    label.className = "download-label";
    label.textContent = download.label || download.extensionId || "Chrome extension";

    const status = document.createElement("div");
    status.className = "download-status";
    status.textContent = downloadStatusText(download);

    const progressTrack = document.createElement("div");
    progressTrack.className = "download-progress";

    const progressBar = document.createElement("div");
    progressBar.className = "download-progress-bar";
    progressBar.style.width = `${Math.max(0, Math.min(1, download.progress || 0)) * 100}%`;

    progressTrack.append(progressBar);
    item.append(label, status, progressTrack);
    downloadStack.append(item);
  });
}

function renderTabs() {
  tabsList.textContent = "";
  pinnedTabsList.textContent = "";

  currentState.tabs
    .filter(tab => tab.pinned)
    .forEach(tab => {
      const item = document.createElement("li");
      item.className = `pinned-tab-item${tab.id === currentState.activeTabId ? " active" : ""}`;

      const activate = document.createElement("button");
      activate.className = "pinned-tab-main";
      activate.type = "button";
      activate.title = shortTitle(tab);
      activate.setAttribute("aria-label", shortTitle(tab));
      activate.addEventListener("click", () => window.browserApi.activateTab(tab.id));

      const loading = document.createElement("span");
      loading.className = `pinned-loading${tab.isLoading ? " visible" : ""}`;
      loading.setAttribute("aria-hidden", "true");

      const unpin = document.createElement("button");
      unpin.className = "pinned-unpin";
      unpin.type = "button";
      unpin.title = "Unpin tab";
      unpin.setAttribute("aria-label", `Unpin ${shortTitle(tab)}`);
      unpin.textContent = "−";
      unpin.addEventListener("click", event => {
        event.stopPropagation();
        window.browserApi.togglePin(tab.id);
      });

      activate.append(createFavicon(tab, "pinned-favicon"), loading);
      item.append(activate, unpin);
      pinnedTabsList.append(item);
    });

  currentState.tabs
    .filter(tab => !tab.pinned)
    .forEach(tab => {
      const item = document.createElement("li");
      item.className = `tab-item${tab.id === currentState.activeTabId ? " active" : ""}`;

      const activate = document.createElement("button");
      activate.className = "tab-main";
      activate.type = "button";
      activate.title = tab.url;
      activate.addEventListener("click", () => window.browserApi.activateTab(tab.id));

      const status = document.createElement("span");
      status.className = `tab-status${tab.isLoading ? " loading" : ""}`;
      status.setAttribute("aria-hidden", "true");

      const title = document.createElement("span");
      title.className = "tab-label";
      title.textContent = shortTitle(tab);

      const pin = document.createElement("button");
      pin.className = "tab-pin";
      pin.type = "button";
      pin.title = "Pin tab";
      pin.setAttribute("aria-label", `Pin ${shortTitle(tab)}`);
      pin.textContent = "P";
      pin.addEventListener("click", event => {
        event.stopPropagation();
        window.browserApi.togglePin(tab.id);
      });

      const close = document.createElement("button");
      close.className = "tab-close";
      close.type = "button";
      close.title = "Close tab";
      close.setAttribute("aria-label", `Close ${shortTitle(tab)}`);
      close.textContent = "×";
      close.addEventListener("click", event => {
        event.stopPropagation();
        window.browserApi.closeTab(tab.id);
      });

      activate.append(status, title);
      item.append(activate, pin, close);
      tabsList.append(item);
    });
}

function renderControls() {
  const tab = activeTab();

  if (tab && document.activeElement !== addressInput) {
    addressInput.value = tab.url || "";
  }

  backButton.disabled = !tab?.canGoBack;
  forwardButton.disabled = !tab?.canGoForward;
  reloadButton.disabled = !tab;
}

function render(state) {
  currentState = state;
  renderTabs();
  renderControls();
}

function openNewTabOverlay() {
  newTabOverlay.hidden = false;
  newTabInput.value = "";
  window.browserApi.setNewTabOverlayVisible(true);
  requestAnimationFrame(() => {
    newTabInput.focus();
  });
}

function closeNewTabOverlay() {
  newTabOverlay.hidden = true;
  newTabInput.value = "";
  window.browserApi.setNewTabOverlayVisible(false);
}

function openExtensionInstallOverlay() {
  extensionInstallOverlay.hidden = false;
  extensionInstallInput.value = "";
  window.browserApi.setNewTabOverlayVisible(true);
  requestAnimationFrame(() => {
    extensionInstallInput.focus();
  });
}

function closeExtensionInstallOverlay() {
  extensionInstallOverlay.hidden = true;
  extensionInstallInput.value = "";
  window.browserApi.setNewTabOverlayVisible(false);
}

addressForm.addEventListener("submit", event => {
  event.preventDefault();
  window.browserApi.navigate(addressInput.value);
  addressInput.blur();
});

addressInput.addEventListener("focus", selectAddressValue);
addressInput.addEventListener("mouseup", event => {
  if (pendingWindowDrag && !pendingWindowDrag.active) {
    event.preventDefault();
    selectAddressValue();
  }
});

newTabForm.addEventListener("submit", event => {
  event.preventDefault();
  const value = newTabInput.value.trim();
  closeNewTabOverlay();
  window.browserApi.newTab(value || undefined);
});

newTabOverlay.addEventListener("click", event => {
  if (event.target === newTabOverlay) {
    closeNewTabOverlay();
  }
});

extensionInstallForm.addEventListener("submit", async event => {
  event.preventDefault();
  const value = extensionInstallInput.value.trim();
  if (!value) {
    return;
  }

  closeExtensionInstallOverlay();
  const result = await window.browserApi.installWebStoreExtension(value);
  if (result?.error) {
    window.alert(result.error);
  }
});

extensionInstallOverlay.addEventListener("click", event => {
  if (event.target === extensionInstallOverlay) {
    closeExtensionInstallOverlay();
  }
});

loadUnpackedButton.addEventListener("click", async () => {
  closeExtensionInstallOverlay();
  const result = await window.browserApi.installExtension();
  if (result?.error) {
    window.alert(result.error);
  }
});

toolbar.addEventListener("pointerdown", event => {
  if (event.button !== 0 || !canStartToolbarDrag(event.target)) {
    return;
  }

  pendingWindowDrag = {
    active: false,
    pointerId: event.pointerId,
    focusAddressOnClick: shouldFocusAddressAfterToolbarClick(event.target),
    x: event.screenX,
    y: event.screenY,
  };

  if (pendingWindowDrag.focusAddressOnClick) {
    event.preventDefault();
  }

  toolbar.setPointerCapture(event.pointerId);
});

window.addEventListener("pointermove", event => {
  if (pendingSidebarResize) {
    if (pendingSidebarResize.pointerId !== event.pointerId) {
      return;
    }

    event.preventDefault();
    setSidebarWidth(pendingSidebarResize.startWidth + event.clientX - pendingSidebarResize.startX);
    return;
  }

  if (!pendingWindowDrag) {
    return;
  }

  if (pendingWindowDrag.pointerId !== event.pointerId) {
    return;
  }

  const movedEnough =
    Math.abs(event.screenX - pendingWindowDrag.x) > 4 ||
    Math.abs(event.screenY - pendingWindowDrag.y) > 4;

  if (!pendingWindowDrag.active && movedEnough) {
    pendingWindowDrag.active = true;
    document.body.classList.add("dragging-window");
    document.activeElement?.blur();
    window.browserApi.startWindowDrag();
  }

  if (pendingWindowDrag.active) {
    event.preventDefault();
    window.browserApi.moveWindowDrag();
  }
});

window.addEventListener("pointerup", event => {
  if (pendingSidebarResize?.pointerId === event.pointerId) {
    stopSidebarResize();
    return;
  }

  if (pendingWindowDrag?.pointerId !== event.pointerId) {
    return;
  }

  if (pendingWindowDrag?.focusAddressOnClick && !pendingWindowDrag.active) {
    addressInput.focus();
    selectAddressValue();
  }

  if (pendingWindowDrag?.pointerId === event.pointerId && toolbar.hasPointerCapture(event.pointerId)) {
    toolbar.releasePointerCapture(event.pointerId);
  }

  stopWindowDrag();
});
toolbar.addEventListener("lostpointercapture", stopWindowDrag);
window.addEventListener("blur", stopWindowDrag);
sidebarResizer.addEventListener("pointerdown", event => {
  if (event.button !== 0) {
    return;
  }

  event.preventDefault();
  pendingSidebarResize = {
    pointerId: event.pointerId,
    startX: event.clientX,
    startWidth: Number.parseFloat(getComputedStyle(document.documentElement).getPropertyValue("--sidebar-width")),
  };
  document.body.classList.add("resizing-sidebar");
  sidebarResizer.setPointerCapture(event.pointerId);
});
sidebarResizer.addEventListener("lostpointercapture", stopSidebarResize);
sidebarResizer.addEventListener("keydown", event => {
  const step = event.shiftKey ? 24 : 12;
  const currentWidth = Number(sidebarResizer.getAttribute("aria-valuenow")) || storedSidebarWidth();

  if (event.key === "ArrowLeft") {
    event.preventDefault();
    setSidebarWidth(currentWidth - step);
  }

  if (event.key === "ArrowRight") {
    event.preventDefault();
    setSidebarWidth(currentWidth + step);
  }
});
window.addEventListener("resize", () => {
  setSidebarWidth(storedSidebarWidth(), { persist: false });
});

backButton.addEventListener("click", () => window.browserApi.back());
forwardButton.addEventListener("click", () => window.browserApi.forward());
reloadButton.addEventListener("click", () => window.browserApi.reload());
newTabButton.addEventListener("click", openNewTabOverlay);
newTabTopButton.addEventListener("click", openNewTabOverlay);
extensionsButton.addEventListener("click", openExtensionInstallOverlay);

window.addEventListener("keydown", event => {
  if (event.key === "Escape" && !newTabOverlay.hidden) {
    event.preventDefault();
    closeNewTabOverlay();
    return;
  }

  if (event.key === "Escape" && !extensionInstallOverlay.hidden) {
    event.preventDefault();
    closeExtensionInstallOverlay();
    return;
  }

  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "l") {
    event.preventDefault();
    addressInput.select();
    addressInput.focus();
  }

  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "t") {
    event.preventDefault();
    openNewTabOverlay();
  }

  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "w") {
    event.preventDefault();
    const tab = activeTab();
    if (tab) {
      window.browserApi.closeTab(tab.id);
    }
  }
});

window.browserApi.onTabsState(render);
window.browserApi.onExtensionsState(renderExtensions);
window.browserApi.onDownloadsState(renderDownloads);
window.browserApi.onAddressFocus(() => {
  addressInput.select();
  addressInput.focus();
});
window.browserApi.onNewTabOpen(openNewTabOverlay);
window.browserApi.listExtensions().then(renderExtensions);
setSidebarWidth(storedSidebarWidth());
