const addressForm = document.querySelector("#addressForm");
const addressInput = document.querySelector("#addressInput");
const backButton = document.querySelector("#backButton");
const forwardButton = document.querySelector("#forwardButton");
const reloadButton = document.querySelector("#reloadButton");
const sidebarResizer = document.querySelector("#sidebarResizer");
const tabsList = document.querySelector("#tabsList");

const SIDEBAR_MIN_WIDTH = 184;
const SIDEBAR_MAX_WIDTH = 360;
const CONTENT_MIN_WIDTH = 320;
const CONTENT_INSET = 0;
const SIDEBAR_WIDTH_STORAGE_KEY = "avora.sidebarWidth";

let pendingSidebarResize = null;

function avoraQuery(request) {
  if (typeof window.cefQuery !== "function") {
    return Promise.resolve();
  }

  return new Promise((resolve, reject) => {
    window.cefQuery({
      request,
      onSuccess: resolve,
      onFailure: (_code, message) => reject(new Error(message)),
    });
  });
}

function maxSidebarWidthForWindow() {
  return Math.max(
    SIDEBAR_MIN_WIDTH,
    Math.min(SIDEBAR_MAX_WIDTH, window.innerWidth - CONTENT_MIN_WIDTH - CONTENT_INSET * 2),
  );
}

function clampSidebarWidth(width) {
  const parsedWidth = Number(width);
  const safeWidth = Number.isFinite(parsedWidth) ? parsedWidth : 236;
  return Math.round(
    Math.max(SIDEBAR_MIN_WIDTH, Math.min(maxSidebarWidthForWindow(), safeWidth)),
  );
}

function setSidebarWidth(width, options = {}) {
  const nextWidth = clampSidebarWidth(width);
  document.documentElement.style.setProperty("--sidebar-width", `${nextWidth}px`);
  sidebarResizer.setAttribute("aria-valuenow", String(nextWidth));

  if (options.persist !== false) {
    window.localStorage.setItem(SIDEBAR_WIDTH_STORAGE_KEY, String(nextWidth));
  }

  if (options.notify !== false) {
    avoraQuery(`sidebar:resize:${nextWidth}`);
  }

  return nextWidth;
}

function storedSidebarWidth() {
  const storedValue = window.localStorage.getItem(SIDEBAR_WIDTH_STORAGE_KEY);
  const value = storedValue === null ? NaN : Number(storedValue);
  return Number.isFinite(value) ? value : 236;
}

function normalizeUrl(value) {
  const trimmed = value.trim();
  if (!trimmed) {
    return "https://www.google.com";
  }
  if (/^https?:\/\//i.test(trimmed)) {
    return trimmed;
  }
  if (trimmed.includes(".") && !trimmed.includes(" ")) {
    return `https://${trimmed}`;
  }
  return `https://www.google.com/search?q=${encodeURIComponent(trimmed)}`;
}

function renderTabs() {
  tabsList.innerHTML = "";
  const item = document.createElement("li");
  item.className = "tab-item active";
  item.innerHTML = '<span class="tab-label">Google</span>';
  tabsList.appendChild(item);
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

renderTabs();
setSidebarWidth(storedSidebarWidth(), { notify: true });

addressForm.addEventListener("submit", event => {
  event.preventDefault();
  avoraQuery(`navigate:${normalizeUrl(addressInput.value)}`);
});

reloadButton.addEventListener("click", () => {
  avoraQuery(`navigate:${normalizeUrl(addressInput.value || "https://www.google.com")}`);
});

backButton.addEventListener("click", () => {
  avoraQuery("navigate:back");
});

forwardButton.addEventListener("click", () => {
  avoraQuery("navigate:forward");
});

sidebarResizer.addEventListener("pointerdown", event => {
  if (event.button !== 0) {
    return;
  }

  pendingSidebarResize = {
    pointerId: event.pointerId,
    startX: event.clientX,
    startWidth: Number.parseFloat(
      getComputedStyle(document.documentElement).getPropertyValue("--sidebar-width"),
    ),
  };
  document.body.classList.add("resizing-sidebar");
  sidebarResizer.setPointerCapture(event.pointerId);
});

sidebarResizer.addEventListener("lostpointercapture", stopSidebarResize);

window.addEventListener("pointermove", event => {
  if (!pendingSidebarResize || event.pointerId !== pendingSidebarResize.pointerId) {
    return;
  }

  const delta = event.clientX - pendingSidebarResize.startX;
  setSidebarWidth(pendingSidebarResize.startWidth + delta, { notify: true });
});

window.addEventListener("pointerup", event => {
  if (!pendingSidebarResize || event.pointerId !== pendingSidebarResize.pointerId) {
    return;
  }
  stopSidebarResize();
});

sidebarResizer.addEventListener("keydown", event => {
  const step = event.shiftKey ? 24 : 12;
  const currentWidth = Number(sidebarResizer.getAttribute("aria-valuenow")) || storedSidebarWidth();

  if (event.key === "ArrowLeft") {
    event.preventDefault();
    setSidebarWidth(currentWidth - step, { notify: true });
  } else if (event.key === "ArrowRight") {
    event.preventDefault();
    setSidebarWidth(currentWidth + step, { notify: true });
  }
});

window.addEventListener("resize", () => {
  setSidebarWidth(storedSidebarWidth(), { notify: true });
});

addressInput.value = "https://www.google.com";
