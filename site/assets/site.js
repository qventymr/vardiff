"use strict";

document.documentElement.classList.add("js");

const tabs = Array.from(document.querySelectorAll('[role="tab"]'));
const panels = Array.from(document.querySelectorAll('[role="tabpanel"]'));

function selectTab(tab, focus = false) {
  tabs.forEach((item) => {
    const selected = item === tab;
    item.setAttribute("aria-selected", String(selected));
    item.tabIndex = selected ? 0 : -1;
  });
  panels.forEach((panel) => {
    panel.hidden = panel.id !== tab.getAttribute("aria-controls");
  });
  if (focus) tab.focus();
}

tabs.forEach((tab, index) => {
  tab.addEventListener("click", () => selectTab(tab));
  tab.addEventListener("keydown", (event) => {
    let next;
    if (event.key === "ArrowRight") next = (index + 1) % tabs.length;
    if (event.key === "ArrowLeft") next = (index + tabs.length - 1) % tabs.length;
    if (event.key === "Home") next = 0;
    if (event.key === "End") next = tabs.length - 1;
    if (next !== undefined) {
      event.preventDefault();
      selectTab(tabs[next], true);
    }
  });
});

const status = document.getElementById("copy-status");
const timers = new WeakMap();

if (navigator.clipboard && navigator.clipboard.writeText) {
  document.querySelectorAll("[data-copy]").forEach((button) => {
    const code = document.getElementById(button.dataset.copy);
    if (!code) return;
    button.hidden = false;
    button.addEventListener("click", async () => {
      const label = button.querySelector("span");
      const original = button.dataset.label;
      try {
        await navigator.clipboard.writeText(code.textContent.trim());
        clearTimeout(timers.get(button));
        label.textContent = button.dataset.success;
        status.textContent = button.dataset.success;
        timers.set(button, setTimeout(() => {
          label.textContent = original;
          status.textContent = "";
        }, 1800));
      } catch {
        status.textContent = button.dataset.error;
      }
    });
  });
}
