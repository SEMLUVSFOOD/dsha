// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// "DS Dashboard" panel: arrange the pages and tiles shown by the DSHA app on a
// Nintendo DS. Plain web component without build step or dependencies.
//
// Drag and drop uses pointer events, so it works with a mouse and with touch
// screens. Everything can also be done without dragging (tap "+" to add, tile
// menu to move/remove), which is easier on small phones.

const TILES_PER_PAGE = 8;
const MAX_PAGES = 16;
const MAX_PAGE_NAME = 20;
const MAX_TITLE = 31;
const MAX_TILE_NAME = 40;
const DRAG_THRESHOLD = 6;

// Domains the DS knows how to show and control
const SUPPORTED = new Set([
  "light", "switch", "input_boolean", "fan", "cover", "lock", "climate",
  "media_player", "scene", "script", "automation", "button", "input_button",
  "vacuum", "siren", "humidifier", "number", "input_number", "sensor",
  "binary_sensor", "person", "weather",
]);

const FILTERS = [
  { id: "all", label: "All", domains: null },
  { id: "light", label: "Lights", domains: ["light"] },
  { id: "switch", label: "Switches", domains: ["switch", "input_boolean", "fan", "siren", "humidifier"] },
  { id: "automation", label: "Automations", domains: ["automation"] },
  { id: "script", label: "Scripts & scenes", domains: ["script", "scene", "button", "input_button"] },
  { id: "climate", label: "Climate", domains: ["climate"] },
  { id: "cover", label: "Covers & locks", domains: ["cover", "lock", "vacuum"] },
  { id: "media", label: "Media", domains: ["media_player"] },
  { id: "sensor", label: "Sensors", domains: ["sensor", "binary_sensor", "weather", "person", "number", "input_number"] },
];

const DOMAIN_COLORS = {
  light: "#fdba30", climate: "#f97316", humidifier: "#f97316",
  cover: "#34c759", lock: "#34c759", vacuum: "#34c759",
  media_player: "#a78bfa", scene: "#a78bfa", script: "#a78bfa",
  button: "#a78bfa", input_button: "#a78bfa",
};

const ACTIONS = [
  { id: "default", label: "Default (toggle / run / press)" },
  { id: "trigger", label: "Trigger (run the automation now)" },
  { id: "none", label: "Read only (no action)" },
];

const domainOf = (entityId) => entityId.split(".")[0];
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({
  "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
}[c]));
const clone = (o) => JSON.parse(JSON.stringify(o));

function emptyPage(name) {
  return { name, tiles: new Array(TILES_PER_PAGE).fill(null) };
}

class DshaPanel extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._layout = null;
    this._saved = null;      // JSON of the last saved layout
    this._page = 0;
    this._selected = null;   // Index of the tile being edited
    this._filter = "all";
    this._search = "";
    this._drag = null;
    this._status = "";
    this._error = false;
    this._loadError = null;
    this._onPointerMove = this._onPointerMove.bind(this);
    this._onPointerUp = this._onPointerUp.bind(this);
  }

  set hass(hass) {
    const first = !this._hass;
    this._hass = hass;
    if (first) {
      this._load();
    } else if (this._layout && !this._drag) {
      this._scheduleStateUpdate();
    }
    const menu = this.shadowRoot.querySelector("ha-menu-button");
    if (menu) menu.hass = hass;
  }

  set narrow(narrow) {
    this._narrow = narrow;
    const menu = this.shadowRoot.querySelector("ha-menu-button");
    if (menu) menu.narrow = narrow;
  }

  set panel(panel) {
    this._panel = panel;
  }

  // Data
  // ----

  async _load() {
    this._render();
    try {
      const layout = await this._hass.callWS({ type: "dsha/layout/get" });
      delete layout.revision;
      this._layout = layout;
      this._saved = JSON.stringify(layout);
    } catch (err) {
      this._loadError = err.message || String(err);
    }
    this._render();
  }

  get _dirty() {
    return this._layout && JSON.stringify(this._layout) !== this._saved;
  }

  async _save() {
    this._setStatus("Saving...");
    try {
      const saved = await this._hass.callWS({
        type: "dsha/layout/save",
        layout: this._layout,
      });
      delete saved.revision;
      this._layout = saved;
      this._saved = JSON.stringify(saved);
      this._setStatus("Saved! Press START on the DS to load the new layout.");
    } catch (err) {
      this._setStatus(`Couldn't save: ${err.message || err}`, true);
    }
    this._render();
  }

  _setStatus(text, error = false) {
    this._status = text;
    this._error = error;
    const el = this.shadowRoot.querySelector(".status");
    if (el) {
      el.textContent = text;
      el.classList.toggle("error", error);
    }
  }

  _entities() {
    const states = this._hass.states;
    const filter = FILTERS.find((f) => f.id === this._filter);
    const search = this._search.trim().toLowerCase();
    const placed = new Set();
    for (const page of this._layout.pages) {
      for (const tile of page.tiles) if (tile) placed.add(tile.entity_id);
    }

    return Object.keys(states)
      .filter((id) => SUPPORTED.has(domainOf(id)))
      .filter((id) => !filter.domains || filter.domains.includes(domainOf(id)))
      .map((id) => ({
        id,
        name: states[id].attributes.friendly_name || id,
        placed: placed.has(id),
      }))
      .filter((e) => !search || e.name.toLowerCase().includes(search)
                    || e.id.includes(search))
      .sort((a, b) => a.name.localeCompare(b.name));
  }

  _stateText(entityId) {
    const st = this._hass.states[entityId];
    if (!st) return "not found";
    if (this._hass.formatEntityState) {
      try {
        return this._hass.formatEntityState(st);
      } catch (e) {
        // Fall back to the raw state
      }
    }
    const unit = st.attributes.unit_of_measurement;
    return unit ? `${st.state} ${unit}` : st.state;
  }

  _isOn(entityId) {
    const st = this._hass.states[entityId];
    if (!st) return false;
    return ["on", "open", "unlocked", "playing", "heat", "cool", "auto",
            "heat_cool", "cleaning", "home"].includes(st.state);
  }

  _tileName(tile) {
    if (tile.name) return tile.name;
    const st = this._hass.states[tile.entity_id];
    return (st && st.attributes.friendly_name) || tile.entity_id;
  }

  // Layout operations
  // -----------------

  _currentTiles() {
    return this._layout.pages[this._page].tiles;
  }

  _firstEmpty(page) {
    return this._layout.pages[page].tiles.findIndex((t) => t === null);
  }

  _addEntity(entityId, page = this._page, slot = null) {
    if (slot === null) {
      slot = this._firstEmpty(page);
      if (slot < 0) {
        // Page full: continue on a new page after it
        if (this._layout.pages.length >= MAX_PAGES) {
          this._setStatus("All pages are full.", true);
          return;
        }
        const base = this._layout.pages[page].name.replace(/ \d+$/, "");
        this._layout.pages.splice(page + 1, 0,
          emptyPage(`${base} 2`.slice(0, MAX_PAGE_NAME)));
        page += 1;
        slot = 0;
        this._page = page;
      }
    }
    this._layout.pages[page].tiles[slot] = { entity_id: entityId, name: "",
                                             action: "default" };
    this._setStatus("");
  }

  _moveTile(from, to) {
    const tiles = this._currentTiles();
    [tiles[from], tiles[to]] = [tiles[to], tiles[from]];
    if (this._selected === from) this._selected = to;
    else if (this._selected === to) this._selected = from;
  }

  _moveTileToPage(from, page) {
    if (page === this._page) return;
    const slot = this._firstEmpty(page);
    if (slot < 0) {
      this._setStatus(`"${this._layout.pages[page].name}" is full.`, true);
      return;
    }
    this._layout.pages[page].tiles[slot] = this._currentTiles()[from];
    this._currentTiles()[from] = null;
    this._selected = null;
  }

  _removeTile(index) {
    this._currentTiles()[index] = null;
    if (this._selected === index) this._selected = null;
  }

  _addPage(name = null) {
    if (this._layout.pages.length >= MAX_PAGES) {
      this._setStatus(`At most ${MAX_PAGES} pages.`, true);
      return;
    }
    this._layout.pages.push(emptyPage(name || `Page ${this._layout.pages.length + 1}`));
    this._page = this._layout.pages.length - 1;
    this._selected = null;
  }

  _deletePage() {
    const page = this._layout.pages[this._page];
    if (page.tiles.some((t) => t) && !confirm(`Delete the page "${page.name}"?`)) return;
    this._layout.pages.splice(this._page, 1);
    if (!this._layout.pages.length) this._layout.pages.push(emptyPage("Home"));
    this._page = Math.min(this._page, this._layout.pages.length - 1);
    this._selected = null;
  }

  _movePage(delta) {
    const to = this._page + delta;
    if (to < 0 || to >= this._layout.pages.length) return;
    const pages = this._layout.pages;
    [pages[this._page], pages[to]] = [pages[to], pages[this._page]];
    this._page = to;
  }

  // Creates pages with the entities of a Home Assistant area
  _addArea(areaId) {
    const hass = this._hass;
    const area = hass.areas && hass.areas[areaId];
    if (!area) return;
    const devices = hass.devices || {};
    const ids = Object.values(hass.entities || {})
      .filter((e) => !e.hidden && !e.entity_category)
      .filter((e) => (e.area_id || (devices[e.device_id] || {}).area_id) === areaId)
      .map((e) => e.entity_id)
      .filter((id) => SUPPORTED.has(domainOf(id)) && hass.states[id])
      // Controls first, sensors last
      .sort((a, b) => {
        const order = (id) => ["sensor", "binary_sensor"].includes(domainOf(id)) ? 1 : 0;
        return order(a) - order(b) || a.localeCompare(b);
      });
    if (!ids.length) {
      this._setStatus(`No supported entities in "${area.name}".`, true);
      return;
    }
    const pagesNeeded = Math.ceil(ids.length / TILES_PER_PAGE);
    if (this._layout.pages.length + pagesNeeded > MAX_PAGES) {
      this._setStatus("Not enough room for more pages.", true);
      return;
    }
    // Reuse the current page if it's empty
    let start = this._layout.pages.length;
    if (!this._currentTiles().some((t) => t)) {
      this._layout.pages.splice(this._page, 1);
      start = this._page;
    }
    for (let p = 0; p < pagesNeeded; p++) {
      const name = (p === 0 ? area.name : `${area.name} ${p + 1}`).slice(0, MAX_PAGE_NAME);
      const page = emptyPage(name);
      ids.slice(p * TILES_PER_PAGE, (p + 1) * TILES_PER_PAGE).forEach((id, i) => {
        page.tiles[i] = { entity_id: id, name: "", action: "default" };
      });
      this._layout.pages.splice(start + p, 0, page);
    }
    this._page = start;
    this._selected = null;
    this._setStatus(`Added ${ids.length} entities from "${area.name}".`);
  }

  // Drag and drop
  // -------------

  _onPointerDown(ev) {
    if (ev.button !== undefined && ev.button !== 0) return;
    const el = ev.target.closest("[data-drag]");
    if (!el || ev.target.closest("button, input, select")) return;
    this._drag = {
      kind: el.dataset.drag,           // "entity" or "tile"
      value: el.dataset.value,
      startX: ev.clientX,
      startY: ev.clientY,
      started: false,
      ghost: null,
      label: el.dataset.label || "",
    };
    window.addEventListener("pointermove", this._onPointerMove);
    window.addEventListener("pointerup", this._onPointerUp);
    window.addEventListener("pointercancel", this._onPointerUp);
  }

  _onPointerMove(ev) {
    const d = this._drag;
    if (!d) return;
    if (!d.started) {
      if (Math.hypot(ev.clientX - d.startX, ev.clientY - d.startY) < DRAG_THRESHOLD) return;
      d.started = true;
      d.ghost = document.createElement("div");
      d.ghost.className = "ghost";
      d.ghost.textContent = d.label;
      this.shadowRoot.appendChild(d.ghost);
      this.shadowRoot.querySelector(".editor").classList.add("dragging");
    }
    ev.preventDefault();
    d.ghost.style.left = `${ev.clientX + 8}px`;
    d.ghost.style.top = `${ev.clientY + 8}px`;

    const target = this._dropTarget(ev.clientX, ev.clientY);
    for (const el of this.shadowRoot.querySelectorAll(".over")) {
      if (el !== target) el.classList.remove("over");
    }
    if (target) target.classList.add("over");
  }

  _dropTarget(x, y) {
    const el = this.shadowRoot.elementFromPoint(x, y);
    if (!el) return null;
    return el.closest("[data-slot], [data-page-tab], [data-trash]");
  }

  _onPointerUp(ev) {
    window.removeEventListener("pointermove", this._onPointerMove);
    window.removeEventListener("pointerup", this._onPointerUp);
    window.removeEventListener("pointercancel", this._onPointerUp);
    const d = this._drag;
    this._drag = null;
    if (!d || !d.started) return; // A click, handled by the click handlers
    d.ghost.remove();
    this._suppressClick = true;
    setTimeout(() => { this._suppressClick = false; }, 0);

    const target = ev.type === "pointerup" ? this._dropTarget(ev.clientX, ev.clientY) : null;
    if (target) {
      if (target.dataset.slot !== undefined) {
        const slot = Number(target.dataset.slot);
        if (d.kind === "entity") {
          this._addEntity(d.value, this._page, slot);
          this._selected = slot;
        } else {
          this._moveTile(Number(d.value), slot);
        }
      } else if (target.dataset.pageTab !== undefined) {
        const page = Number(target.dataset.pageTab);
        if (d.kind === "entity") {
          this._addEntity(d.value, page);
          this._page = page;
        } else {
          this._moveTileToPage(Number(d.value), page);
        }
      } else if (target.dataset.trash !== undefined && d.kind === "tile") {
        this._removeTile(Number(d.value));
      }
    }
    this._render();
  }

  // Rendering
  // ---------

  _scheduleStateUpdate() {
    if (this._stateTimer) return;
    this._stateTimer = setTimeout(() => {
      this._stateTimer = null;
      for (const el of this.shadowRoot.querySelectorAll("[data-state-of]")) {
        el.textContent = this._stateText(el.dataset.stateOf);
      }
    }, 500);
  }

  _render() {
    const root = this.shadowRoot;
    // Keep the focus and the cursor of the search box across renders
    const active = root.activeElement;
    const focusId = active && active.id;
    const caret = active && active.selectionStart;
    const listScroll = root.querySelector(".entity-list")?.scrollTop || 0;

    root.innerHTML = `<style>${STYLE}</style>${this._html()}`;

    const menu = root.querySelector("ha-menu-button");
    if (menu) {
      menu.hass = this._hass;
      menu.narrow = this._narrow;
    }
    const list = root.querySelector(".entity-list");
    if (list) list.scrollTop = listScroll;
    if (focusId) {
      const el = root.getElementById(focusId);
      if (el) {
        el.focus();
        if (caret !== null && caret !== undefined && el.setSelectionRange) {
          el.setSelectionRange(caret, caret);
        }
      }
    }
    this._bind();
  }

  _html() {
    const header = `
      <div class="toolbar">
        <ha-menu-button></ha-menu-button>
        <div class="main-title">DS Dashboard</div>
      </div>`;

    if (this._loadError) {
      return `${header}<div class="content"><div class="card error-card">
        Couldn't load the layout: ${esc(this._loadError)}</div></div>`;
    }
    if (!this._layout || !this._hass) {
      return `${header}<div class="content">Loading...</div>`;
    }

    const layout = this._layout;
    const page = layout.pages[this._page];
    const areas = Object.values(this._hass.areas || {})
      .sort((a, b) => a.name.localeCompare(b.name));

    const tabs = layout.pages.map((p, i) => `
      <button class="tab ${i === this._page ? "active" : ""}" data-page-tab="${i}"
              data-action="page" data-index="${i}">${esc(p.name)}
        <span class="count">${p.tiles.filter((t) => t).length}</span></button>`).join("");

    const slots = page.tiles.map((tile, i) => {
      if (!tile) {
        return `<div class="slot empty" data-slot="${i}">
          <span>Drop here</span></div>`;
      }
      const domain = domainOf(tile.entity_id);
      const color = DOMAIN_COLORS[domain] || "#03a9f4";
      const on = this._isOn(tile.entity_id);
      const badge = tile.action === "trigger" ? "trigger"
                  : tile.action === "none" ? "read only" : "";
      return `<div class="slot tile ${on ? "on" : ""} ${this._selected === i ? "selected" : ""}"
                   style="--domain:${color}" data-slot="${i}" data-drag="tile"
                   data-value="${i}" data-label="${esc(this._tileName(tile))}"
                   data-action="select-tile" data-index="${i}">
          <div class="tile-name">${esc(this._tileName(tile))}</div>
          <div class="tile-state" data-state-of="${esc(tile.entity_id)}">${esc(this._stateText(tile.entity_id))}</div>
          ${badge ? `<div class="badge">${badge}</div>` : ""}
        </div>`;
    }).join("");

    const selected = this._selected !== null ? page.tiles[this._selected] : null;
    const editor = selected ? `
      <div class="card tile-editor">
        <div class="row">
          <strong>${esc(selected.entity_id)}</strong>
          <button class="text" data-action="close-editor">Done</button>
        </div>
        <label for="tile-name">Name on the DS (empty = the entity's name)</label>
        <input id="tile-name" maxlength="${MAX_TILE_NAME}" value="${esc(selected.name)}"
               placeholder="${esc(this._hass.states[selected.entity_id]?.attributes.friendly_name || "")}">
        <label for="tile-action">When tapped</label>
        <select id="tile-action">
          ${ACTIONS.filter((a) => a.id !== "trigger" || domainOf(selected.entity_id) === "automation")
            .map((a) => `<option value="${a.id}" ${a.id === selected.action ? "selected" : ""}>${a.label}</option>`).join("")}
        </select>
        <div class="row buttons">
          <select id="tile-move-page">
            <option value="">Move to page...</option>
            ${layout.pages.map((p, i) => i === this._page ? "" : `<option value="${i}">${esc(p.name)}</option>`).join("")}
          </select>
          <button class="danger" data-action="remove-tile">Remove</button>
        </div>
      </div>` : `
      <p class="hint">Drag entities from the list into the slots, or tap
        <b>+</b>. Drag tiles to move them, onto a page tab to move them to
        that page, or back to the list to remove them. Tap a tile to rename
        it.</p>`;

    const entities = this._entities();
    const list = entities.length ? entities.slice(0, 300).map((e) => `
      <div class="entity" data-drag="entity" data-value="${esc(e.id)}" data-label="${esc(e.name)}">
        <span class="handle" title="Drag">&#8942;&#8942;</span>
        <span class="dot" style="background:${DOMAIN_COLORS[domainOf(e.id)] || "#03a9f4"}"></span>
        <span class="entity-text">
          <span class="entity-name">${esc(e.name)}${e.placed ? ' <span class="placed">&#10003; on the DS</span>' : ""}</span>
          <span class="entity-id">${esc(e.id)}</span>
        </span>
        <button class="add" data-action="add-entity" data-value="${esc(e.id)}" title="Add to this page">+</button>
      </div>`).join("") : `<p class="hint">No entities match.</p>`;

    return `${header}
      <div class="content editor">
        <div class="card top-bar">
          <label for="title">Title on the DS</label>
          <input id="title" maxlength="${MAX_TITLE}" value="${esc(layout.title)}"
                 placeholder="Home Assistant">
          <button class="primary" data-action="save" ${this._dirty ? "" : "disabled"}>
            ${this._dirty ? "Save" : "Saved"}</button>
          <span class="status ${this._error ? "error" : ""}">${esc(this._status)}</span>
        </div>

        <div class="columns">
          <div class="preview-col">
            <div class="tabs">${tabs}
              <button class="tab add-page" data-action="add-page" title="Add a page">+ Page</button>
            </div>
            <div class="page-tools">
              <input id="page-name" maxlength="${MAX_PAGE_NAME}" value="${esc(page.name)}" aria-label="Page name">
              <button class="icon" data-action="page-left" title="Move page left" ${this._page === 0 ? "disabled" : ""}>&#9664;</button>
              <button class="icon" data-action="page-right" title="Move page right" ${this._page === layout.pages.length - 1 ? "disabled" : ""}>&#9654;</button>
              <button class="icon danger" data-action="delete-page" title="Delete page">&#10005;</button>
            </div>

            <div class="ds">
              <div class="ds-screen">
                <div class="grid">${slots}</div>
                <div class="ds-footer"><span>&#9664;</span>
                  <span>${esc(page.name)}<br><small>${this._page + 1}/${layout.pages.length}</small></span>
                  <span>&#9654;</span></div>
              </div>
            </div>
            ${editor}
          </div>

          <div class="card list-col" data-trash>
            <div class="list-head">
              <input id="search" type="search" placeholder="Search entities, automations..." value="${esc(this._search)}">
              ${areas.length ? `<select id="area">
                  <option value="">Add a page from an area...</option>
                  ${areas.map((a) => `<option value="${esc(a.area_id)}">${esc(a.name)}</option>`).join("")}
                </select>` : ""}
              <div class="chips">${FILTERS.map((f) => `
                <button class="chip ${f.id === this._filter ? "active" : ""}" data-action="filter" data-value="${f.id}">${f.label}</button>`).join("")}
              </div>
            </div>
            <div class="entity-list">${list}</div>
            <div class="trash-hint">Drop a tile here to remove it</div>
          </div>
        </div>
      </div>`;
  }

  _bind() {
    const root = this.shadowRoot;
    root.querySelector(".editor")?.addEventListener("pointerdown", (ev) => this._onPointerDown(ev));

    root.querySelectorAll("[data-action]").forEach((el) => {
      el.addEventListener("click", (ev) => {
        if (this._suppressClick) return;
        ev.stopPropagation();
        this._action(el.dataset.action, el.dataset);
      });
    });

    const on = (id, event, fn) => root.getElementById(id)?.addEventListener(event, fn);
    on("title", "input", (ev) => {
      this._layout.title = ev.target.value;
      this._updateSaveButton();
    });
    on("page-name", "input", (ev) => {
      const name = ev.target.value;
      if (!name.trim()) return;
      this._layout.pages[this._page].name = name;
      const tab = root.querySelector(`.tab[data-page-tab="${this._page}"]`);
      if (tab) tab.firstChild.textContent = name;
      this._updateSaveButton();
    });
    on("page-name", "change", () => this._render());
    on("search", "input", (ev) => {
      this._search = ev.target.value;
      this._render();
    });
    on("area", "change", (ev) => {
      if (ev.target.value) this._addArea(ev.target.value);
      this._render();
    });
    on("tile-name", "input", (ev) => {
      this._currentTiles()[this._selected].name = ev.target.value;
      const tile = root.querySelector(`.slot[data-slot="${this._selected}"] .tile-name`);
      if (tile) tile.textContent = this._tileName(this._currentTiles()[this._selected]);
      this._updateSaveButton();
    });
    on("tile-action", "change", (ev) => {
      this._currentTiles()[this._selected].action = ev.target.value;
      this._render();
    });
    on("tile-move-page", "change", (ev) => {
      if (ev.target.value !== "") this._moveTileToPage(this._selected, Number(ev.target.value));
      this._render();
    });
  }

  _updateSaveButton() {
    const btn = this.shadowRoot.querySelector('[data-action="save"]');
    if (!btn) return;
    btn.disabled = !this._dirty;
    btn.textContent = this._dirty ? "Save" : "Saved";
  }

  _action(action, data) {
    switch (action) {
      case "save":
        this._save();
        return;
      case "page":
        this._page = Number(data.index);
        this._selected = null;
        break;
      case "add-page":
        this._addPage();
        break;
      case "delete-page":
        this._deletePage();
        break;
      case "page-left":
        this._movePage(-1);
        break;
      case "page-right":
        this._movePage(1);
        break;
      case "filter":
        this._filter = data.value;
        break;
      case "add-entity":
        this._addEntity(data.value);
        break;
      case "select-tile":
        this._selected = Number(data.index);
        break;
      case "close-editor":
        this._selected = null;
        break;
      case "remove-tile":
        this._removeTile(this._selected);
        break;
      default:
        return;
    }
    this._render();
  }
}

const STYLE = `
  :host {
    display: block;
    height: 100%;
    background: var(--primary-background-color, #111827);
    color: var(--primary-text-color, #f3f4f6);
    font-family: var(--paper-font-body1_-_font-family, system-ui, sans-serif);
    --accent: var(--primary-color, #03a9f4);
    --card: var(--card-background-color, #1f2937);
    --muted: var(--secondary-text-color, #9ca3af);
    --line: var(--divider-color, #374151);
  }
  .toolbar {
    display: flex; align-items: center; height: 56px; padding: 0 12px;
    background: var(--app-header-background-color, var(--card));
    color: var(--app-header-text-color, inherit);
    border-bottom: 1px solid var(--line); box-sizing: border-box;
  }
  .main-title { font-size: 20px; margin-left: 8px; }
  .content { padding: 16px; max-width: 1200px; margin: 0 auto; box-sizing: border-box; }
  .card {
    background: var(--card); border-radius: var(--ha-card-border-radius, 12px);
    border: 1px solid var(--line); padding: 12px; box-sizing: border-box;
  }
  .top-bar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 16px; }
  .top-bar label { color: var(--muted); font-size: 14px; }
  .top-bar input { flex: 1 1 200px; max-width: 320px; }
  .status { color: var(--muted); font-size: 14px; flex-basis: 100%; min-height: 1em; }
  .status.error, .error-card { color: var(--error-color, #ef4444); }
  input, select {
    font: inherit; color: inherit; background: var(--primary-background-color, #111827);
    border: 1px solid var(--line); border-radius: 8px; padding: 8px 10px; box-sizing: border-box;
  }
  button {
    font: inherit; color: inherit; background: var(--secondary-background-color, #374151);
    border: 0; border-radius: 8px; padding: 8px 12px; cursor: pointer;
  }
  button:disabled { opacity: .45; cursor: default; }
  button.primary { background: var(--accent); color: var(--text-primary-color, #fff); min-width: 90px; }
  button.danger { color: var(--error-color, #ef4444); }
  button.text { background: none; color: var(--accent); }
  button.icon { min-width: 36px; padding: 8px; }
  .columns { display: grid; grid-template-columns: minmax(300px, 420px) 1fr; gap: 16px; align-items: start; }
  @media (max-width: 800px) { .columns { grid-template-columns: 1fr; } }
  .tabs { display: flex; flex-wrap: wrap; gap: 6px; margin-bottom: 8px; }
  .tab { background: var(--card); border: 1px solid var(--line); }
  .tab.active { background: var(--accent); color: var(--text-primary-color, #fff); border-color: var(--accent); }
  .tab .count { opacity: .7; font-size: 12px; margin-left: 4px; }
  .add-page { border-style: dashed; }
  .page-tools { display: flex; gap: 6px; margin-bottom: 10px; }
  .page-tools input { flex: 1; }
  .ds {
    background: #3a3f47; border-radius: 18px; padding: 14px;
    box-shadow: inset 0 0 0 2px #2a2e34;
  }
  .ds-screen { background: #111827; border-radius: 4px; padding: 4px; aspect-ratio: 256 / 192; display: flex; flex-direction: column; }
  .grid { flex: 1; display: grid; grid-template-columns: 1fr 1fr; grid-template-rows: repeat(4, 1fr); gap: 4px; }
  .slot {
    border-radius: 6px; padding: 4px 6px; font-size: 13px; overflow: hidden; position: relative;
    min-height: 0; box-sizing: border-box; user-select: none; -webkit-user-select: none;
  }
  .slot.empty {
    border: 1px dashed #4b5563; color: #6b7280; display: flex;
    align-items: center; justify-content: center; font-size: 12px;
  }
  .slot.tile {
    background: #1f2937; color: #f3f4f6; border-left: 3px solid var(--domain);
    cursor: grab; touch-action: none; display: flex; flex-direction: column; justify-content: space-between;
  }
  .slot.tile.on { background: var(--domain); color: #111827; }
  .slot.tile.selected { outline: 2px solid #fff; }
  .slot.over, .tab.over { outline: 2px solid var(--accent); outline-offset: 1px; }
  .slot.empty.over { background: rgba(3, 169, 244, .2); }
  .tile-name { font-weight: 600; line-height: 1.15; overflow: hidden; display: -webkit-box;
               -webkit-line-clamp: 2; -webkit-box-orient: vertical; }
  .tile-state { font-size: 11px; opacity: .85; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .badge { position: absolute; top: 2px; right: 4px; font-size: 9px; opacity: .8; text-transform: uppercase; }
  .ds-footer { display: flex; justify-content: space-between; align-items: center; color: #d1d5db;
               font-size: 12px; text-align: center; padding: 4px 8px 0; line-height: 1.1; }
  .hint { color: var(--muted); font-size: 14px; line-height: 1.4; }
  .tile-editor { margin-top: 12px; display: flex; flex-direction: column; gap: 6px; }
  .tile-editor label { color: var(--muted); font-size: 13px; margin-top: 6px; }
  .row { display: flex; justify-content: space-between; align-items: center; gap: 8px; }
  .row.buttons { margin-top: 8px; }
  .row.buttons select { flex: 1; }
  .list-col { display: flex; flex-direction: column; max-height: calc(100vh - 220px); min-height: 300px; }
  .list-head { display: flex; flex-direction: column; gap: 8px; }
  .chips { display: flex; flex-wrap: wrap; gap: 6px; }
  .chip { font-size: 13px; padding: 4px 10px; border-radius: 16px; }
  .chip.active { background: var(--accent); color: var(--text-primary-color, #fff); }
  .entity-list { overflow-y: auto; margin-top: 8px; flex: 1; }
  .entity {
    display: flex; align-items: center; gap: 8px; padding: 6px 4px;
    border-bottom: 1px solid var(--line); user-select: none; -webkit-user-select: none; cursor: grab;
  }
  .handle { color: var(--muted); touch-action: none; letter-spacing: -3px; padding: 6px 4px; }
  .dot { width: 8px; height: 8px; border-radius: 4px; flex: none; }
  .entity-text { display: flex; flex-direction: column; flex: 1; min-width: 0; }
  .entity-name { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .entity-id { color: var(--muted); font-size: 12px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .placed { color: var(--success-color, #34c759); font-size: 12px; }
  button.add { width: 34px; height: 34px; padding: 0; font-size: 20px; flex: none; }
  .trash-hint { display: none; text-align: center; color: var(--error-color, #ef4444); padding: 10px; }
  .dragging .list-col { outline: 2px dashed var(--line); }
  .dragging .trash-hint { display: block; }
  .dragging, .dragging * { cursor: grabbing !important; }
  .ghost {
    position: fixed; z-index: 1000; pointer-events: none; padding: 6px 10px;
    background: var(--accent); color: #fff; border-radius: 8px; font-size: 13px;
    box-shadow: 0 4px 12px rgba(0,0,0,.4); max-width: 220px; white-space: nowrap;
    overflow: hidden; text-overflow: ellipsis;
  }
`;

if (!customElements.get("dsha-panel")) {
  customElements.define("dsha-panel", DshaPanel);
}
