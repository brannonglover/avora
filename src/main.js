const { app, BrowserWindow, WebContentsView, dialog, ipcMain, screen, session, shell } = require("electron");
const fs = require("node:fs");
const fsp = require("node:fs/promises");
const https = require("node:https");
const path = require("node:path");
const extract = require("extract-zip");

const UI = {
  toolbarHeight: 54,
  sidebarWidth: 236,
  minSidebarWidth: 184,
  maxSidebarWidth: 360,
  contentInset: 14,
  contentRadius: 16,
  minContentWidth: 320,
  minContentHeight: 240,
};

let window;
let activeTabId = null;
let nextTabId = 1;
let nextDownloadId = 1;
let extensionPopupWindow = null;
const tabs = new Map();
const downloads = new Map();
let windowDrag = null;

function ignorePipeError(error) {
  if (error?.code !== "EPIPE") {
    throw error;
  }
}

process.stdout.on("error", ignorePipeError);
process.stderr.on("error", ignorePipeError);

function safeLog(message) {
  try {
    console.log(message);
  } catch (error) {
    if (error?.code !== "EPIPE") {
      throw error;
    }
  }
}

function browserSession() {
  return session.fromPartition("persist:main");
}

function extensionsRoot() {
  return path.join(app.getPath("userData"), "web-store-extensions");
}

function crxPathFor(extensionId) {
  return path.join(extensionsRoot(), `${extensionId}.crx`);
}

function unpackedPathFor(extensionId) {
  return path.join(extensionsRoot(), extensionId);
}

function chromeWebStoreDownloadUrl(extensionId) {
  const chromeVersion = process.versions.chrome || "120.0.0.0";
  const updateArgs = new URLSearchParams({
    response: "redirect",
    prodversion: chromeVersion,
    acceptformat: "crx2,crx3",
    x: `id=${extensionId}&installsource=ondemand&uc`,
  });

  return `https://clients2.google.com/service/update2/crx?${updateArgs}`;
}

function parseChromeExtensionId(input) {
  const value = String(input || "").trim();
  const shortcuts = new Map([["1password", "aeblfdkhhhdcdjpifhhbdiojplfjncoa"]]);

  if (shortcuts.has(value.toLowerCase())) {
    return shortcuts.get(value.toLowerCase());
  }

  const directMatch = value.match(/^[a-p]{32}$/i);

  if (directMatch) {
    return directMatch[0].toLowerCase();
  }

  try {
    const url = new URL(value);
    const pathMatch = url.pathname.match(/\/([a-p]{32})(?:\/)?$/i);
    const queryMatch = url.search.match(/[?&]id=([a-p]{32})/i);
    const id = pathMatch?.[1] || queryMatch?.[1];
    return id ? id.toLowerCase() : "";
  } catch {
    const looseMatch = value.match(/[a-p]{32}/i);
    return looseMatch ? looseMatch[0].toLowerCase() : "";
  }
}

function chromeWebStoreExtensionIdFromUrl(rawUrl) {
  try {
    const url = new URL(rawUrl);
    if (url.hostname !== "chromewebstore.google.com") {
      return "";
    }

    const pathMatch = url.pathname.match(/\/detail\/(?:[^/]+\/)?([a-p]{32})(?:\/|$)/i);
    return pathMatch ? pathMatch[1].toLowerCase() : "";
  } catch {
    return "";
  }
}

function setDownload(downloadId, patch) {
  const previous = downloads.get(downloadId) || {};
  downloads.set(downloadId, { ...previous, ...patch });
  emitDownloads();
}

function emitDownloads() {
  if (!window || window.isDestroyed()) {
    return;
  }

  window.webContents.send("downloads:state", {
    downloads: Array.from(downloads.values()),
  });
}

function extensionIconUrl(extension) {
  const icons = extension.manifest?.icons || {};
  const iconPath = Object.entries(icons)
    .map(([size, relativePath]) => ({ size: Number(size), relativePath }))
    .filter(icon => Number.isFinite(icon.size) && typeof icon.relativePath === "string")
    .sort((a, b) => b.size - a.size)[0]?.relativePath;

  if (!iconPath) {
    return "";
  }

  return `${extension.url}/${iconPath.replace(/^\/+/, "")}`;
}

function extensionSnapshot(extension) {
  return {
    id: extension.id,
    name: extension.name,
    version: extension.version,
    iconUrl: extensionIconUrl(extension),
  };
}

function extensionPageUrl(extension, relativePath) {
  if (!relativePath) {
    return "";
  }

  return `${extension.url}/${String(relativePath).replace(/^\/+/, "")}`;
}

function extensionPopupUrl(extension) {
  const manifest = extension.manifest || {};
  const action = manifest.action || manifest.browser_action || manifest.page_action || {};

  return (
    extensionPageUrl(extension, action.default_popup) ||
    extensionPageUrl(extension, manifest.options_ui?.page) ||
    extensionPageUrl(extension, manifest.options_page) ||
    extension.url
  );
}

function emitExtensions() {
  if (!window || window.isDestroyed()) {
    return;
  }

  window.webContents.send("extensions:state", {
    extensions: browserSession().extensions.getAllExtensions().map(extensionSnapshot),
  });
}

async function injectChromeWebStoreInstaller(contents) {
  const extensionId = chromeWebStoreExtensionIdFromUrl(contents.getURL());
  if (!extensionId) {
    return;
  }

  const script = `
    (() => {
      const extensionId = ${JSON.stringify(extensionId)};
      const installUrl = "avora-extension-install://" + extensionId;

      function textOf(node) {
        return (node?.innerText || node?.textContent || "").trim().toLowerCase();
      }

      function isInstallButton(node) {
        const text = textOf(node);
        return text.includes("add to chrome") || text.includes("add extension");
      }

      function patchButton(button) {
        if (!button || button.dataset.avoraInstallButton === "true") {
          return;
        }

        button.dataset.avoraInstallButton = "true";
        button.removeAttribute("disabled");
        button.removeAttribute("aria-disabled");
        button.disabled = false;
        button.style.pointerEvents = "auto";
        button.style.cursor = "pointer";
        button.style.opacity = "1";

        button.addEventListener(
          "click",
          event => {
            event.preventDefault();
            event.stopPropagation();
            window.location.href = installUrl;
          },
          true
        );
      }

      function patchAll() {
        document
          .querySelectorAll("button, [role='button']")
          .forEach(button => {
            if (isInstallButton(button)) {
              patchButton(button);
            }
          });
      }

      patchAll();
      if (!window.__avoraChromeStoreObserver) {
        window.__avoraChromeStoreObserver = new MutationObserver(patchAll);
        window.__avoraChromeStoreObserver.observe(document.documentElement, {
          childList: true,
          subtree: true,
          attributes: true,
          attributeFilter: ["disabled", "aria-disabled", "style", "class"],
        });
      }
    })();
  `;

  try {
    await contents.executeJavaScript(script);
  } catch {
    // Store pages may navigate while the injection is pending; the next page event retries.
  }
}

function openExtensionPopup(extensionId) {
  if (!window || window.isDestroyed()) {
    return { error: "Browser window is not available." };
  }

  const extension = browserSession().extensions.getExtension(extensionId);
  if (!extension) {
    return { error: "That extension is not loaded." };
  }

  const url = extensionPopupUrl(extension);
  if (!url) {
    return { error: "That extension does not expose a popup or options page." };
  }

  if (extensionPopupWindow && !extensionPopupWindow.isDestroyed()) {
    extensionPopupWindow.close();
  }

  const bounds = window.getBounds();
  extensionPopupWindow = new BrowserWindow({
    width: 380,
    height: 560,
    x: bounds.x + bounds.width - 404,
    y: bounds.y + 62,
    parent: window,
    modal: false,
    frame: false,
    resizable: false,
    show: false,
    backgroundColor: "#181c20",
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
      partition: "persist:main",
    },
  });

  extensionPopupWindow.webContents.on("console-message", (_event, level, message, line, sourceId) => {
    safeLog(`[extension:${extension.name}] ${level} ${sourceId}:${line} ${message}`);
  });

  extensionPopupWindow.webContents.on("did-fail-load", (_event, code, description, failedUrl) => {
    safeLog(`[extension:${extension.name}] failed to load ${failedUrl}: ${code} ${description}`);
  });

  extensionPopupWindow.webContents.setWindowOpenHandler(({ url: targetUrl }) => {
    createTab(targetUrl, true);
    extensionPopupWindow?.close();
    return { action: "deny" };
  });

  extensionPopupWindow.on("blur", () => {
    if (extensionPopupWindow && !extensionPopupWindow.isDestroyed()) {
      extensionPopupWindow.close();
    }
  });

  extensionPopupWindow.on("closed", () => {
    extensionPopupWindow = null;
  });

  extensionPopupWindow.loadURL(url);
  extensionPopupWindow.once("ready-to-show", () => {
    extensionPopupWindow?.show();
  });

  return { ok: true };
}

function zipOffsetFromCrx(buffer) {
  const isZip = buffer.subarray(0, 4).toString("utf8") === "PK\u0003\u0004";
  if (isZip) {
    return 0;
  }

  const magic = buffer.subarray(0, 4).toString("utf8");
  if (magic !== "Cr24") {
    throw new Error("Downloaded file is not a CRX package.");
  }

  const version = buffer.readUInt32LE(4);
  if (version === 2) {
    const publicKeyLength = buffer.readUInt32LE(8);
    const signatureLength = buffer.readUInt32LE(12);
    return 16 + publicKeyLength + signatureLength;
  }

  if (version === 3) {
    const headerLength = buffer.readUInt32LE(8);
    return 12 + headerLength;
  }

  throw new Error(`Unsupported CRX version ${version}.`);
}

async function unpackCrx(crxPath, destinationPath) {
  const buffer = await fsp.readFile(crxPath);
  const zipOffset = zipOffsetFromCrx(buffer);
  const zipPath = `${crxPath}.zip`;

  await fsp.rm(destinationPath, { recursive: true, force: true });
  await fsp.mkdir(destinationPath, { recursive: true });
  await fsp.writeFile(zipPath, buffer.subarray(zipOffset));

  try {
    await extract(zipPath, { dir: destinationPath });
  } finally {
    await fsp.rm(zipPath, { force: true });
  }
}

function downloadFile(url, destinationPath, downloadId, redirectCount = 0) {
  return new Promise((resolve, reject) => {
    if (redirectCount > 5) {
      reject(new Error("Too many redirects while downloading extension."));
      return;
    }

    const request = https.get(
      url,
      {
        headers: {
          "User-Agent": `Mozilla/5.0 Chrome/${process.versions.chrome || "120.0.0.0"}`,
        },
      },
      response => {
        const statusCode = response.statusCode || 0;

        if ([301, 302, 303, 307, 308].includes(statusCode) && response.headers.location) {
          response.resume();
          const redirectUrl = new URL(response.headers.location, url).toString();
          downloadFile(redirectUrl, destinationPath, downloadId, redirectCount + 1)
            .then(resolve)
            .catch(reject);
          return;
        }

        if (statusCode < 200 || statusCode >= 300) {
          response.resume();
          reject(new Error(`Chrome Web Store returned HTTP ${statusCode}.`));
          return;
        }

        const totalBytes = Number(response.headers["content-length"] || 0);
        let receivedBytes = 0;
        const file = fs.createWriteStream(destinationPath);

        response.on("data", chunk => {
          receivedBytes += chunk.length;
          setDownload(downloadId, {
            receivedBytes,
            totalBytes,
            progress: totalBytes ? receivedBytes / totalBytes : 0,
            status: "downloading",
          });
        });

        response.pipe(file);
        file.on("finish", () => file.close(resolve));
        file.on("error", reject);
      }
    );

    request.on("error", reject);
  });
}

async function installWebStoreExtension(input) {
  const extensionId = parseChromeExtensionId(input);

  if (!extensionId) {
    return {
      error: "Paste a Chrome Web Store URL or a 32-character extension ID.",
    };
  }

  const downloadId = nextDownloadId;
  nextDownloadId += 1;

  const crxPath = crxPathFor(extensionId);
  const destinationPath = unpackedPathFor(extensionId);

  setDownload(downloadId, {
    id: downloadId,
    extensionId,
    label: extensionId,
    status: "starting",
    receivedBytes: 0,
    totalBytes: 0,
    progress: 0,
  });

  try {
    await fsp.mkdir(extensionsRoot(), { recursive: true });
    await downloadFile(chromeWebStoreDownloadUrl(extensionId), crxPath, downloadId);

    setDownload(downloadId, {
      status: "unpacking",
      progress: 1,
    });

    await unpackCrx(crxPath, destinationPath);

    setDownload(downloadId, {
      status: "installing",
    });

    const extension = await browserSession().extensions.loadExtension(destinationPath, {
      allowFileAccess: false,
    });

    setDownload(downloadId, {
      label: extension.name,
      status: "complete",
      progress: 1,
    });
    emitExtensions();

    setTimeout(() => {
      downloads.delete(downloadId);
      emitDownloads();
    }, 5000);

    return { extension: extensionSnapshot(extension) };
  } catch (error) {
    setDownload(downloadId, {
      status: "error",
      error: error instanceof Error ? error.message : String(error),
    });
    return {
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

function normalizeUrl(input) {
  const value = String(input || "").trim();

  if (!value) {
    return "about:blank";
  }

  if (/^(https?:|file:|about:|chrome:)/i.test(value)) {
    return value;
  }

  if (value.includes(".") && !value.includes(" ")) {
    return `https://${value}`;
  }

  const query = encodeURIComponent(value);
  return `https://www.google.com/search?q=${query}`;
}

function viewBounds() {
  if (!window) {
    return { x: 0, y: 0, width: UI.minContentWidth, height: UI.minContentHeight };
  }

  const [width, height] = window.getContentSize();
  return {
    x: UI.sidebarWidth + UI.contentInset,
    y: UI.toolbarHeight + UI.contentInset,
    width: Math.max(UI.minContentWidth, width - UI.sidebarWidth - UI.contentInset * 2),
    height: Math.max(UI.minContentHeight, height - UI.toolbarHeight - UI.contentInset * 2),
  };
}

function clampSidebarWidth(width) {
  if (!window || window.isDestroyed()) {
    return UI.sidebarWidth;
  }

  const [windowWidth] = window.getContentSize();
  const maxWidth = Math.max(
    UI.minSidebarWidth,
    Math.min(UI.maxSidebarWidth, windowWidth - UI.minContentWidth - UI.contentInset * 2)
  );
  return Math.round(Math.max(UI.minSidebarWidth, Math.min(maxWidth, Number(width) || UI.sidebarWidth)));
}

function resizeSidebar(width) {
  UI.sidebarWidth = clampSidebarWidth(width);
  const tab = activeTab();
  if (tab) {
    tab.view.setBounds(viewBounds());
  }
  return { width: UI.sidebarWidth };
}

function tabSnapshot(tab) {
  const history = tab.view.webContents.navigationHistory;

  return {
    id: tab.id,
    title: tab.title,
    url: tab.url,
    faviconUrl: tab.faviconUrl,
    pinned: tab.pinned,
    isLoading: tab.isLoading,
    canGoBack: history.canGoBack(),
    canGoForward: history.canGoForward(),
  };
}

function emitTabs() {
  if (!window || window.isDestroyed()) {
    return;
  }

  window.webContents.send("tabs:state", {
    activeTabId,
    tabs: Array.from(tabs.values()).map(tabSnapshot),
  });
}

function attachTabEvents(tab) {
  const contents = tab.view.webContents;

  contents.on("before-input-event", (event, input) => {
    handleShortcutInput(event, input);
  });

  contents.on("will-navigate", async (event, url) => {
    if (!url.startsWith("avora-extension-install://")) {
      return;
    }

    event.preventDefault();
    await installWebStoreExtension(url.replace("avora-extension-install://", ""));
  });

  contents.setWindowOpenHandler(({ url }) => {
    createTab(url, true);
    return { action: "deny" };
  });

  contents.on("page-title-updated", (_event, title) => {
    tab.title = title || tab.url;
    emitTabs();
  });

  contents.on("page-favicon-updated", (_event, favicons) => {
    tab.faviconUrl = favicons.at(-1) || favicons[0] || "";
    emitTabs();
  });

  contents.on("did-start-loading", () => {
    tab.isLoading = true;
    emitTabs();
  });

  contents.on("did-stop-loading", () => {
    tab.isLoading = false;
    tab.url = contents.getURL();
    tab.title = contents.getTitle() || tab.url;
    injectChromeWebStoreInstaller(contents);
    emitTabs();
  });

  contents.on("did-navigate", (_event, url) => {
    tab.url = url;
    injectChromeWebStoreInstaller(contents);
    emitTabs();
  });

  contents.on("did-navigate-in-page", (_event, url) => {
    tab.url = url;
    injectChromeWebStoreInstaller(contents);
    emitTabs();
  });

  contents.on("did-fail-load", (_event, _code, description, failingUrl) => {
    tab.isLoading = false;
    tab.title = description || "Load failed";
    tab.url = failingUrl || tab.url;
    emitTabs();
  });
}

function createTab(url = "https://www.google.com", activate = true) {
  const id = nextTabId;
  nextTabId += 1;

  const view = new WebContentsView({
    webPreferences: {
      contextIsolation: true,
      sandbox: true,
      nodeIntegration: false,
      partition: "persist:main",
    },
  });
  view.setBorderRadius(UI.contentRadius);
  view.setBackgroundColor("#ffffff");

  const tab = {
    id,
    view,
    title: "New tab",
    url: normalizeUrl(url),
    faviconUrl: "",
    pinned: false,
    isLoading: false,
  };

  tabs.set(id, tab);
  attachTabEvents(tab);
  view.webContents.loadURL(tab.url);

  if (activate) {
    activateTab(id);
  } else {
    emitTabs();
  }

  return tab;
}

function activateTab(id) {
  const tab = tabs.get(id);
  if (!window || !tab) {
    return;
  }

  if (activeTabId !== null) {
    const previous = tabs.get(activeTabId);
    if (previous) {
      window.contentView.removeChildView(previous.view);
    }
  }

  activeTabId = id;
  window.contentView.addChildView(tab.view);
  tab.view.setBounds(viewBounds());
  emitTabs();
}

function closeTab(id) {
  const tab = tabs.get(id);
  if (!window || !tab) {
    return;
  }

  const wasActive = activeTabId === id;
  if (wasActive) {
    window.contentView.removeChildView(tab.view);
  }

  tab.view.webContents.destroy();
  tabs.delete(id);

  if (tabs.size === 0) {
    createTab("https://www.google.com", true);
    return;
  }

  if (wasActive) {
    const remainingIds = Array.from(tabs.keys());
    activateTab(remainingIds[Math.max(0, remainingIds.length - 1)]);
  } else {
    emitTabs();
  }
}

function activeTab() {
  return activeTabId === null ? null : tabs.get(activeTabId);
}

function setActiveTabVisible(visible) {
  const tab = activeTab();
  if (tab) {
    tab.view.setVisible(visible);
  }
}

function handleShortcutInput(event, input) {
  if (input.type !== "keyDown" || (!input.meta && !input.control)) {
    return;
  }

  const key = input.key.toLowerCase();

  if (key === "t") {
    event.preventDefault();
    window?.webContents.focus();
    window?.webContents.send("new-tab:open");
  }

  if (key === "l") {
    event.preventDefault();
    window?.webContents.focus();
    window?.webContents.send("address:focus");
  }

  if (key === "w") {
    event.preventDefault();
    const tab = activeTab();
    if (tab) {
      closeTab(tab.id);
    }
  }
}

function createWindow() {
  window = new BrowserWindow({
    width: 1280,
    height: 820,
    minWidth: 760,
    minHeight: 520,
    title: "Unnamed Browser",
    backgroundColor: "#101316",
    titleBarStyle: "hiddenInset",
    trafficLightPosition: { x: 18, y: 18 },
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });

  window.loadFile(path.join(__dirname, "renderer", "index.html"));

  window.webContents.on("before-input-event", (event, input) => {
    handleShortcutInput(event, input);
  });

  window.on("resize", () => {
    const tab = activeTab();
    if (tab) {
      tab.view.setBounds(viewBounds());
    }
  });

  window.on("closed", () => {
    window = null;
  });

  window.webContents.on("did-finish-load", () => {
    createTab("https://www.google.com", true);
    emitExtensions();
  });
}

app.whenReady().then(() => {
  browserSession().extensions.on("extension-loaded", emitExtensions);
  browserSession().extensions.on("extension-unloaded", emitExtensions);
  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") {
    app.quit();
  }
});

ipcMain.on("window-drag:start", () => {
  if (!window || window.isDestroyed()) {
    return;
  }

  const [windowX, windowY] = window.getPosition();
  const cursor = screen.getCursorScreenPoint();
  windowDrag = {
    cursorX: cursor.x,
    cursorY: cursor.y,
    windowX,
    windowY,
  };
});

ipcMain.on("window-drag:move", () => {
  if (!window || window.isDestroyed() || !windowDrag) {
    return;
  }

  const cursor = screen.getCursorScreenPoint();
  window.setPosition(
    windowDrag.windowX + cursor.x - windowDrag.cursorX,
    windowDrag.windowY + cursor.y - windowDrag.cursorY,
    false
  );
});

ipcMain.on("window-drag:end", () => {
  windowDrag = null;
});

ipcMain.handle("tabs:new", (_event, url) => {
  createTab(url || "https://www.google.com", true);
});

ipcMain.handle("tabs:activate", (_event, id) => {
  activateTab(Number(id));
});

ipcMain.handle("tabs:close", (_event, id) => {
  closeTab(Number(id));
});

ipcMain.handle("tabs:toggle-pin", (_event, id) => {
  const tab = tabs.get(Number(id));
  if (tab) {
    tab.pinned = !tab.pinned;
    emitTabs();
  }
});

ipcMain.handle("tabs:set-new-tab-overlay-visible", (_event, visible) => {
  setActiveTabVisible(!visible);
});

ipcMain.handle("sidebar:resize", (_event, width) => resizeSidebar(width));

ipcMain.handle("extensions:list", () => ({
  extensions: browserSession().extensions.getAllExtensions().map(extensionSnapshot),
}));

ipcMain.handle("extensions:install-unpacked", async () => {
  const result = await dialog.showOpenDialog(window, {
    title: "Choose an unpacked Chrome extension folder",
    buttonLabel: "Load Extension",
    properties: ["openDirectory"],
  });

  if (result.canceled || !result.filePaths[0]) {
    return { canceled: true };
  }

  const extensionPath = result.filePaths[0];
  const manifestPath = path.join(extensionPath, "manifest.json");

  if (!fs.existsSync(manifestPath)) {
    return {
      canceled: false,
      error: "That folder does not contain a manifest.json file.",
    };
  }

  try {
    const extension = await browserSession().extensions.loadExtension(extensionPath, {
      allowFileAccess: false,
    });
    emitExtensions();
    return { canceled: false, extension: extensionSnapshot(extension) };
  } catch (error) {
    return {
      canceled: false,
      error: error instanceof Error ? error.message : String(error),
    };
  }
});

ipcMain.handle("extensions:install-web-store", async (_event, input) => {
  return installWebStoreExtension(input);
});

ipcMain.handle("extensions:open", (_event, extensionId) => openExtensionPopup(extensionId));

ipcMain.handle("nav:go", (_event, input) => {
  const tab = activeTab();
  if (tab) {
    tab.view.webContents.loadURL(normalizeUrl(input));
  }
});

ipcMain.handle("nav:back", () => {
  const tab = activeTab();
  const history = tab?.view.webContents.navigationHistory;
  if (history?.canGoBack()) {
    history.goBack();
  }
});

ipcMain.handle("nav:forward", () => {
  const tab = activeTab();
  const history = tab?.view.webContents.navigationHistory;
  if (history?.canGoForward()) {
    history.goForward();
  }
});

ipcMain.handle("nav:reload", () => {
  activeTab()?.view.webContents.reload();
});

ipcMain.handle("app:open-external", (_event, url) => {
  shell.openExternal(url);
});
