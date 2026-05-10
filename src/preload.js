const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("browserApi", {
  newTab: url => ipcRenderer.invoke("tabs:new", url),
  startWindowDrag: () => ipcRenderer.send("window-drag:start"),
  moveWindowDrag: () => ipcRenderer.send("window-drag:move"),
  endWindowDrag: () => ipcRenderer.send("window-drag:end"),
  resizeSidebar: width => ipcRenderer.invoke("sidebar:resize", width),
  activateTab: id => ipcRenderer.invoke("tabs:activate", id),
  closeTab: id => ipcRenderer.invoke("tabs:close", id),
  togglePin: id => ipcRenderer.invoke("tabs:toggle-pin", id),
  setNewTabOverlayVisible: visible => ipcRenderer.invoke("tabs:set-new-tab-overlay-visible", visible),
  navigate: input => ipcRenderer.invoke("nav:go", input),
  back: () => ipcRenderer.invoke("nav:back"),
  forward: () => ipcRenderer.invoke("nav:forward"),
  reload: () => ipcRenderer.invoke("nav:reload"),
  installExtension: () => ipcRenderer.invoke("extensions:install-unpacked"),
  installWebStoreExtension: input => ipcRenderer.invoke("extensions:install-web-store", input),
  listExtensions: () => ipcRenderer.invoke("extensions:list"),
  openExtension: id => ipcRenderer.invoke("extensions:open", id),
  openExternal: url => ipcRenderer.invoke("app:open-external", url),
  onTabsState: callback => {
    const listener = (_event, state) => callback(state);
    ipcRenderer.on("tabs:state", listener);
    return () => ipcRenderer.removeListener("tabs:state", listener);
  },
  onExtensionsState: callback => {
    const listener = (_event, state) => callback(state);
    ipcRenderer.on("extensions:state", listener);
    return () => ipcRenderer.removeListener("extensions:state", listener);
  },
  onDownloadsState: callback => {
    const listener = (_event, state) => callback(state);
    ipcRenderer.on("downloads:state", listener);
    return () => ipcRenderer.removeListener("downloads:state", listener);
  },
  onAddressFocus: callback => {
    const listener = () => callback();
    ipcRenderer.on("address:focus", listener);
    return () => ipcRenderer.removeListener("address:focus", listener);
  },
  onNewTabOpen: callback => {
    const listener = () => callback();
    ipcRenderer.on("new-tab:open", listener);
    return () => ipcRenderer.removeListener("new-tab:open", listener);
  },
});
