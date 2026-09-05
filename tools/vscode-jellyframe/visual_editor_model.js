"use strict";

const MODEL_FORMAT = "jellyframe.visual-editor";
const MODEL_VERSION = 2;
const BODY_START = "<!-- jellyframe-visual-editor:body:start -->";
const BODY_END = "<!-- jellyframe-visual-editor:body:end -->";
const CSS_START = "/* jellyframe-visual-editor:styles:start */";
const CSS_END = "/* jellyframe-visual-editor:styles:end */";
const MAX_NODES = 128;
const NODE_TYPES = new Set(["container", "text", "button", "image", "input", "progress", "divider", "spacer", "select", "list", "navigation", "switch"]);

// This is the editor's serializable component contract. Rendering remains
// deliberately local to the editor, but labels, placement and field types no
// longer need to be rediscovered by each UI surface.
const COMPONENT_REGISTRY = Object.freeze([
  { type: "container", renderKey: "container", group: "layoutGroup", label: "container", help: "containerHelp", icon: "□", canContain: true,
    fields: [
      { key: "layout", group: "layout", label: "direction", kind: "enum", control: "segmented", values: ["column", "row"] },
      { key: "gap", group: "layout", label: "gap", kind: "number", min: 0, max: 64 },
      { key: "padding", group: "layout", label: "padding", kind: "number", min: 0, max: 64 },
      { key: "align", group: "layout", label: "align", kind: "enum", values: ["stretch", "start", "center", "end"] },
      { key: "justify", group: "layout", label: "justify", kind: "enum", values: ["start", "center", "end", "space-between", "space-around"] },
      { key: "width", group: "layout", label: "width", kind: "length" },
      { key: "height", group: "layout", label: "height", kind: "length" },
      { key: "background", group: "appearance", label: "background", kind: "color" },
      { key: "radius", group: "appearance", label: "radius", kind: "number", min: 0, max: 150 }
    ] },
  { type: "text", renderKey: "text", group: "contentGroup", label: "text", help: "textHelp", icon: "T", fields: [
    { key: "text", group: "content", label: "textValue", kind: "text" },
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "color", group: "appearance", label: "color", kind: "color" },
    { key: "fontSize", group: "appearance", label: "fontSize", kind: "number", min: 8, max: 72 },
    { key: "weight", group: "appearance", label: "weight", kind: "enum", control: "segmented", values: ["normal", "bold"] },
    { key: "align", group: "appearance", label: "textAlign", kind: "enum", control: "segmented", values: ["left", "center", "right"] }
  ] },
  { type: "image", renderKey: "image", group: "contentGroup", label: "image", help: "imageHelp", icon: "▧", fields: [
    { key: "src", group: "content", label: "source", kind: "resource" },
    { key: "alt", group: "content", label: "alt", kind: "text" },
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "length" },
    { key: "fit", group: "appearance", label: "fitMode", kind: "enum", values: ["cover", "contain", "fill"] },
    { key: "radius", group: "appearance", label: "radius", kind: "number", min: 0, max: 150 }
  ] },
  { type: "button", renderKey: "button", group: "controlsGroup", label: "button", help: "buttonHelp", icon: "B", fields: [
    { key: "text", group: "content", label: "textValue", kind: "text" },
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "length" },
    { key: "background", group: "appearance", label: "background", kind: "color" },
    { key: "color", group: "appearance", label: "color", kind: "color" },
    { key: "radius", group: "appearance", label: "radius", kind: "number", min: 0, max: 64 }
  ] },
  { type: "input", renderKey: "input", group: "controlsGroup", label: "input", help: "inputHelp", icon: "I", fields: [
    { key: "placeholder", group: "content", label: "placeholder", kind: "text" },
    { key: "value", group: "content", label: "value", kind: "text" },
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "length" },
    { key: "background", group: "appearance", label: "background", kind: "color" },
    { key: "color", group: "appearance", label: "color", kind: "color" },
    { key: "radius", group: "appearance", label: "radius", kind: "number", min: 0, max: 64 }
  ] },
  { type: "progress", renderKey: "progress", group: "controlsGroup", label: "progress", help: "progressHelp", icon: "▬", fields: [
    { key: "value", group: "content", label: "value", kind: "number", min: 0, max: 100 },
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "length" },
    { key: "track", group: "appearance", label: "track", kind: "color" },
    { key: "fill", group: "appearance", label: "fillColor", kind: "color" },
    { key: "radius", group: "appearance", label: "radius", kind: "number", min: 0, max: 64 }
  ] },
  { type: "divider", renderKey: "divider", group: "layoutGroup", label: "divider", help: "dividerHelp", icon: "—", fields: [
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "number", min: 1, max: 8 },
    { key: "color", group: "appearance", label: "color", kind: "color" }
  ] },
  { type: "spacer", renderKey: "spacer", group: "layoutGroup", label: "spacer", help: "spacerHelp", icon: "↕", fields: [
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "number", min: 0, max: 256 }
  ] },
  { type: "select", renderKey: "select", group: "controlsGroup", label: "select", help: "selectHelp", icon: "▾", fields: [
    { key: "options", group: "content", label: "options", kind: "string-list", minItems: 1, maxItems: 6, maxLength: 32 },
    { key: "selected", group: "content", label: "selectedOption", kind: "number", min: 0, max: 5, integer: true },
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "length" },
    { key: "background", group: "appearance", label: "background", kind: "color" },
    { key: "color", group: "appearance", label: "color", kind: "color" },
    { key: "radius", group: "appearance", label: "radius", kind: "number", min: 0, max: 64 }
  ] },
  { type: "list", renderKey: "list", group: "contentGroup", label: "list", help: "listHelp", icon: "☷", fields: [
    { key: "items", group: "content", label: "items", kind: "string-list", minItems: 1, maxItems: 8, maxLength: 40 },
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "length" },
    { key: "itemHeight", group: "layout", label: "itemHeight", kind: "number", min: 20, max: 96 },
    { key: "gap", group: "layout", label: "gap", kind: "number", min: 0, max: 24 },
    { key: "background", group: "appearance", label: "background", kind: "color" },
    { key: "color", group: "appearance", label: "color", kind: "color" },
    { key: "radius", group: "appearance", label: "radius", kind: "number", min: 0, max: 64 }
  ] },
  { type: "navigation", renderKey: "navigation", group: "controlsGroup", label: "navigation", help: "navigationHelp", icon: "≡", fields: [
    { key: "items", group: "content", label: "items", kind: "string-list", minItems: 2, maxItems: 4, maxLength: 16 },
    { key: "active", group: "content", label: "activeItem", kind: "number", min: 0, max: 3, integer: true },
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "length" },
    { key: "gap", group: "layout", label: "gap", kind: "number", min: 0, max: 16 },
    { key: "background", group: "appearance", label: "background", kind: "color" },
    { key: "color", group: "appearance", label: "color", kind: "color" },
    { key: "activeColor", group: "appearance", label: "activeColor", kind: "color" },
    { key: "fontSize", group: "appearance", label: "fontSize", kind: "number", min: 8, max: 24 },
    { key: "radius", group: "appearance", label: "radius", kind: "number", min: 0, max: 64 }
  ] },
  { type: "switch", renderKey: "switch", group: "controlsGroup", label: "switch", help: "switchHelp", icon: "●", fields: [
    { key: "checked", group: "content", label: "checked", kind: "boolean" },
    { key: "width", group: "layout", label: "width", kind: "length" },
    { key: "height", group: "layout", label: "height", kind: "length" },
    { key: "onColor", group: "appearance", label: "onColor", kind: "color" },
    { key: "offColor", group: "appearance", label: "offColor", kind: "color" },
    { key: "thumbColor", group: "appearance", label: "thumbColor", kind: "color" },
    { key: "radius", group: "appearance", label: "radius", kind: "number", min: 0, max: 64 }
  ] }
]);

// Recipes are insertion templates only. They expand into the same ordinary
// nodes as the component palette before entering the model.
const RECIPE_REGISTRY = Object.freeze([
  { type: "status-card", group: "recipesGroup", label: "statusCard", help: "statusCardHelp", icon: "▤", template: {
    id: "status-card", type: "container", layout: "column", gap: 6, padding: 12, width: "100%", height: "104px", background: "#18212b", radius: 10, align: "stretch", justify: "start", children: [
      { id: "status-title", type: "text", text: "Status", fontSize: 16, color: "#f4f7fb", weight: "bold", align: "left", width: "100%" },
      { id: "status-value", type: "text", text: "Ready", fontSize: 14, color: "#9aa9b8", weight: "normal", align: "left", width: "100%" },
      { id: "status-progress", type: "progress", value: 68, width: "100%", height: "8px", track: "#26313d", fill: "#20b486", radius: 4 }
    ]
  } },
  { type: "settings-row", group: "recipesGroup", label: "settingsRow", help: "settingsRowHelp", icon: "⚙", template: {
    id: "settings-row", type: "container", layout: "row", gap: 8, padding: 8, width: "100%", height: "48px", background: "transparent", radius: 0, align: "center", justify: "space-between", children: [
      { id: "setting-label", type: "text", text: "Setting", fontSize: 15, color: "#f4f7fb", weight: "normal", align: "left", width: "auto" },
      { id: "setting-toggle", type: "switch", checked: true, width: "52px", height: "28px", onColor: "#20b486", offColor: "#26313d", thumbColor: "#f4f7fb", radius: 14 }
    ]
  } },
  { type: "bottom-navigation", group: "recipesGroup", label: "bottomNavigation", help: "bottomNavigationHelp", icon: "≡", template: {
    id: "bottom-navigation-page", type: "container", layout: "column", gap: 8, padding: 12, width: "100%", height: "100%", background: "#000000", radius: 0, align: "stretch", justify: "start", children: [
      { id: "bottom-navigation-title", type: "text", text: "Overview", fontSize: 20, color: "#f4f7fb", weight: "bold", align: "left", width: "100%" },
      { id: "bottom-navigation-list", type: "list", items: ["Battery 82%", "Activity Ready"], width: "100%", height: "auto", itemHeight: 34, gap: 4, background: "#101418", color: "#f4f7fb", radius: 6 },
      { id: "bottom-navigation-space", type: "spacer", width: "100%", height: 72 },
      { id: "bottom-navigation-divider", type: "divider", width: "100%", height: 1, color: "#26313d" },
      { id: "bottom-navigation-nav", type: "navigation", items: ["Home", "Stats", "More"], active: 0, width: "100%", height: "48px", gap: 4, background: "#101418", color: "#9aa9b8", activeColor: "#20b486", fontSize: 9, radius: 6 }
    ]
  } }
]);

function escapeHtml(value) {
  return String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function cssText(value, fallback = "") {
  const text = String(value ?? fallback).trim();
  return text.replace(/[{};\r\n]/g, "");
}

function cssLength(value, fallback = "auto") {
  if (typeof value === "number" && Number.isFinite(value) && value >= 0) return `${value}px`;
  const text = String(value ?? "").trim();
  return /^(?:auto|0|[0-9]+(?:\.[0-9]+)?(?:px|%))$/.test(text) ? text : fallback;
}

function cssNumber(value, fallback, minimum, maximum) {
  const number = Number(value);
  return Number.isFinite(number) ? Math.min(maximum, Math.max(minimum, number)) : fallback;
}

function componentRegistry() {
  return JSON.parse(JSON.stringify(COMPONENT_REGISTRY));
}

function recipeRegistry() {
  return JSON.parse(JSON.stringify(RECIPE_REGISTRY));
}

function migrateModel(input) {
  if (!input || input.format !== MODEL_FORMAT) {
    throw new Error("Unsupported JellyFrame visual-editor model");
  }
  const model = JSON.parse(JSON.stringify(input));
  const version = Number(model.formatVersion);
  if (version === 1) {
    model.formatVersion = MODEL_VERSION;
  } else if (version !== MODEL_VERSION) {
    throw new Error("Unsupported JellyFrame visual-editor model");
  }
  return model;
}

function defaultLineHeight(fontSize) {
  const size = Math.max(1, Math.round(cssNumber(fontSize, 16, 8, 72)));
  return size + Math.max(6, Math.floor(size / 3));
}

function nextId(model, prefix = "node") {
  const used = new Set();
  walkNodes(model.root, (node) => used.add(node.id));
  let index = 1;
  while (used.has(`${prefix}-${index}`)) index += 1;
  return `${prefix}-${index}`;
}

function defaultNode(type, id) {
  const common = { id, type };
  if (type === "container") {
    return { ...common, layout: "column", gap: 10, padding: 12, width: "100%", height: "96px", background: "transparent", radius: 0, align: "stretch", justify: "start", children: [] };
  }
  if (type === "text") {
    return { ...common, text: "Text", fontSize: 18, color: "#f4f7fb", weight: "normal", align: "left", width: "auto" };
  }
  if (type === "button") {
    return { ...common, text: "Button", width: "100%", height: "44px", background: "#20b486", color: "#071712", radius: 6 };
  }
  if (type === "image") {
    return { ...common, src: "", alt: "", width: "100%", height: "96px", fit: "cover", radius: 6 };
  }
  if (type === "input") {
    return { ...common, placeholder: "Input", value: "", width: "100%", height: "40px", background: "#18212b", color: "#f4f7fb", radius: 4 };
  }
  if (type === "progress") {
    return { ...common, value: 50, width: "100%", height: "12px", track: "#26313d", fill: "#ffb84d", radius: 6 };
  }
  if (type === "divider") return { ...common, width: "100%", height: 1, color: "#344250" };
  if (type === "spacer") return { ...common, width: "100%", height: 12 };
  if (type === "select") return { ...common, options: ["Option 1", "Option 2", "Option 3"], selected: 0, width: "100%", height: "40px", background: "#18212b", color: "#f4f7fb", radius: 4 };
  if (type === "list") return { ...common, items: ["List item 1", "List item 2", "List item 3"], width: "100%", height: "auto", itemHeight: 36, gap: 4, background: "#18212b", color: "#f4f7fb", radius: 6 };
  if (type === "navigation") return { ...common, items: ["Home", "Stats", "More"], active: 0, width: "100%", height: "48px", gap: 4, background: "#18212b", color: "#9aa9b8", activeColor: "#20b486", fontSize: 9, radius: 6 };
  if (type === "switch") return { ...common, checked: true, width: "52px", height: "28px", onColor: "#20b486", offColor: "#26313d", thumbColor: "#f4f7fb", radius: 14 };
  throw new Error(`Unsupported visual-editor node type: ${type}`);
}

function createDefaultModel(viewport = {}, appName = "JellyFrame App") {
  const width = Math.round(cssNumber(viewport.width ?? viewport.designWidth, 300, 64, 2048));
  const height = Math.round(cssNumber(viewport.height ?? viewport.designHeight, 300, 64, 2048));
  const root = defaultNode("container", "page");
  root.height = "100%";
  root.padding = 18;
  root.gap = 12;
  root.background = "#0d141b";
  root.justify = "center";
  root.children = [
    { ...defaultNode("text", "title"), text: appName, fontSize: 24, weight: "bold", align: "center", width: "100%" },
    { ...defaultNode("text", "subtitle"), text: "Designed with JellyFrame", fontSize: 14, color: "#9aa9b8", align: "center", width: "100%" }
  ];
  return {
    format: MODEL_FORMAT,
    formatVersion: MODEL_VERSION,
    viewport: { width, height, shape: viewport.shape === "round" ? "round" : "rect" },
    root
  };
}

function walkNodes(node, visitor, parent) {
  if (!node || typeof node !== "object") return;
  visitor(node, parent);
  if (Array.isArray(node.children)) {
    for (const child of node.children) walkNodes(child, visitor, node);
  }
}

function validateRegistryFields(node) {
  const definition = COMPONENT_REGISTRY.find((component) => component.type === node.type);
  for (const field of definition?.fields || []) {
    if (node[field.key] === undefined) continue;
    const value = node[field.key];
    if (field.kind === "number" &&
        (!Number.isFinite(value) || (field.integer && !Number.isInteger(value)) || value < field.min || value > field.max)) {
      throw new Error(`Invalid ${field.key} on ${node.id}: expected a number from ${field.min} to ${field.max}`);
    }
    if (field.kind === "enum" && !field.values.includes(value)) {
      throw new Error(`Invalid ${field.key} on ${node.id}: unsupported value`);
    }
    if (field.kind === "string-list" &&
        (!Array.isArray(value) || value.length < field.minItems || value.length > field.maxItems ||
         value.some((item) => typeof item !== "string" || item.length > field.maxLength || /[{};\r\n]/.test(item)))) {
      throw new Error(`Invalid ${field.key} on ${node.id}: expected ${field.minItems}-${field.maxItems} safe text items`);
    }
    if (field.kind === "boolean" && typeof value !== "boolean") {
      throw new Error(`Invalid ${field.key} on ${node.id}: expected a boolean`);
    }
    if (field.kind === "length" && !/^(?:auto|0|[0-9]+(?:\.[0-9]+)?(?:px|%))$/.test(String(value))) {
      throw new Error(`Invalid ${field.key} on ${node.id}: expected px, %, or auto`);
    }
    if ((field.kind === "text" || field.kind === "color") &&
        (typeof value !== "string" || /[{};\r\n]/.test(value))) {
      throw new Error(`Invalid ${field.key} on ${node.id}: unsafe text value`);
    }
  }
  if (node.type === "select" && node.selected >= node.options.length) {
    throw new Error(`Invalid selected on ${node.id}: selected option is out of range`);
  }
  if (node.type === "navigation" && node.active >= node.items.length) {
    throw new Error(`Invalid active on ${node.id}: active item is out of range`);
  }
}

function validateModel(model) {
  const migrated = migrateModel(model);
  if (!migrated.root || migrated.root.type !== "container") {
    throw new Error("Visual-editor model must have a container root");
  }
  const ids = new Set();
  let count = 0;
  walkNodes(migrated.root, (node) => {
    count += 1;
    if (count > MAX_NODES) throw new Error(`Visual-editor model exceeds ${MAX_NODES} nodes`);
    if (!NODE_TYPES.has(node.type)) throw new Error(`Unsupported visual-editor node type: ${node.type}`);
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,47}$/.test(String(node.id || ""))) throw new Error(`Invalid visual-editor node id: ${node.id || "missing"}`);
    if (ids.has(node.id)) throw new Error(`Duplicate visual-editor node id: ${node.id}`);
    ids.add(node.id);
    if (node.type === "container" && !Array.isArray(node.children)) throw new Error(`Container ${node.id} must have children`);
    if (node.type === "image" && node.src && !/^\/(?!\/)[^?#]+$/.test(String(node.src))) {
      throw new Error(`Image ${node.id} must reference a package-local resource`);
    }
    if (node.type === "progress" && !/^(?:0|[0-9]+(?:\.[0-9]+)?px)$/.test(String(node.height || ""))) {
      throw new Error(`Progress ${node.id} height must be a non-negative pixel length`);
    }
    validateRegistryFields(node);
  });
  return migrated;
}

// Editor edits are checked locally and remain bounded by the same model limit;
// this deliberately does not start a package or Render Core process.
function boundedModelCheck(model) {
  const validated = validateModel(model);
  let nodeCount = 0;
  walkNodes(validated.root, () => { nodeCount += 1; });
  const generated = renderBody(validated);
  return {
    model: validated,
    nodeCount,
    generatedBytes: Buffer.byteLength(generated, "utf8")
  };
}

function nodeStyle(node) {
  const declarations = [];
  const add = (name, value) => declarations.push(`${name}: ${value}`);
  if (node.width && node.width !== "auto") add("width", cssLength(node.width));
  if (node.height && node.height !== "auto") add("height", cssLength(node.height));
  if (node.type === "container") {
    add("display", "flex");
    add("flex-direction", node.layout === "row" ? "row" : "column");
    add("gap", `${cssNumber(node.gap, 0, 0, 64)}px`);
    add("padding", `${cssNumber(node.padding, 0, 0, 64)}px`);
    add("align-items", ["start", "center", "end", "stretch"].includes(node.align) ? node.align.replace("start", "flex-start").replace("end", "flex-end") : "stretch");
    add("justify-content", ["start", "center", "end", "space-between", "space-around"].includes(node.justify) ? node.justify.replace("start", "flex-start").replace("end", "flex-end") : "flex-start");
    add("background", cssText(node.background, "transparent"));
    add("border-radius", `${cssNumber(node.radius, 0, 0, 150)}px`);
  } else if (node.type === "text") {
    add("font-size", `${cssNumber(node.fontSize, 16, 8, 72)}px`);
    add("line-height", `${defaultLineHeight(node.fontSize)}px`);
    add("color", cssText(node.color, "#ffffff"));
    add("font-weight", node.weight === "bold" ? "bold" : "normal");
    add("text-align", ["left", "center", "right"].includes(node.align) ? node.align : "left");
    add("overflow-wrap", "anywhere");
  } else if (node.type === "button" || node.type === "input") {
    add("font-size", "16px");
    add("line-height", `${defaultLineHeight(node.fontSize)}px`);
    add("background", cssText(node.background, "#202a34"));
    add("color", cssText(node.color, "#ffffff"));
    add("border-radius", `${cssNumber(node.radius, 0, 0, 64)}px`);
    add("border", "0");
    add("padding", "0 12px");
    add("text-align", node.type === "button" ? "center" : "left");
  } else if (node.type === "image") {
    add("object-fit", ["cover", "contain", "fill"].includes(node.fit) ? node.fit : "cover");
    add("border-radius", `${cssNumber(node.radius, 0, 0, 150)}px`);
  } else if (node.type === "progress") {
    add("background", cssText(node.track, "#26313d"));
    add("border-radius", `${cssNumber(node.radius, 0, 0, 64)}px`);
    add("overflow", "hidden");
  } else if (node.type === "divider") {
    add("background", cssText(node.color, "#344250"));
    add("min-height", `${cssNumber(node.height, 1, 1, 8)}px`);
  } else if (node.type === "spacer") {
    add("background", "transparent");
  } else if (node.type === "select") {
    add("background", cssText(node.background, "#18212b"));
    add("color", cssText(node.color, "#f4f7fb"));
    add("border", "0");
    add("border-radius", `${cssNumber(node.radius, 0, 0, 64)}px`);
    add("padding", "0 10px");
  } else if (node.type === "list") {
    add("display", "flex");
    add("flex-direction", "column");
    add("gap", `${cssNumber(node.gap, 0, 0, 24)}px`);
    add("margin", "0");
    add("padding", "0");
    add("list-style", "none");
    add("background", cssText(node.background, "#18212b"));
    add("color", cssText(node.color, "#f4f7fb"));
    add("border-radius", `${cssNumber(node.radius, 0, 0, 64)}px`);
    add("overflow", "hidden");
  } else if (node.type === "navigation") {
    add("display", "flex");
    add("align-items", "stretch");
    add("gap", `${cssNumber(node.gap, 0, 0, 16)}px`);
    add("background", cssText(node.background, "#18212b"));
    add("border-radius", `${cssNumber(node.radius, 0, 0, 64)}px`);
    add("padding", "4px");
    add("font-size", `${cssNumber(node.fontSize, 9, 8, 24)}px`);
    add("line-height", `${defaultLineHeight(node.fontSize ?? 9)}px`);
    add("box-sizing", "border-box");
    add("overflow", "hidden");
  } else if (node.type === "switch") {
    add("display", "inline-flex");
    add("align-items", "center");
    add("justify-content", node.checked ? "flex-end" : "flex-start");
    add("padding", "3px");
    add("background", cssText(node.checked ? node.onColor : node.offColor, "#26313d"));
    add("border", "0");
    add("border-radius", `${cssNumber(node.radius, 14, 0, 64)}px`);
  }
  return declarations.join("; ");
}

const sourceRenderers = {
  container(node, indent, id, style) {
    const children = node.children.map((child) => renderNode(child, `${indent}  `)).join("\n");
    return `${indent}<section id="${id}" class="jf-visual-container" style="${style}">${children ? `\n${children}\n${indent}` : ""}</section>`;
  },
  text(node, indent, id, style) {
    return `${indent}<div id="${id}" class="jf-visual-text" style="${style}">${escapeHtml(node.text)}</div>`;
  },
  button(node, indent, id, style) {
    return `${indent}<button id="${id}" class="jf-visual-button" type="button" style="${style}">${escapeHtml(node.text)}</button>`;
  },
  image(node, indent, id, style) {
    return `${indent}<img id="${id}" class="jf-visual-image" src="${escapeHtml(node.src)}" alt="${escapeHtml(node.alt)}" style="${style}">`;
  },
  input(node, indent, id, style) {
    return `${indent}<input id="${id}" class="jf-visual-input" value="${escapeHtml(node.value)}" placeholder="${escapeHtml(node.placeholder)}" style="${style}">`;
  },
  progress(node, indent, id, style) {
    const progress = cssNumber(node.value, 0, 0, 100);
    const fillHeight = cssLength(node.height, "12px");
    return `${indent}<div id="${id}" class="jf-visual-progress" role="progressbar" aria-valuenow="${progress}" aria-valuemin="0" aria-valuemax="100" style="${style}"><span style="width: ${progress}%; height: ${fillHeight}; background: ${escapeHtml(cssText(node.fill, "#ffb84d"))}"></span></div>`;
  },
  divider(node, indent, id, style) {
    return `${indent}<div id="${id}" class="jf-visual-divider" role="separator" style="${style}"></div>`;
  },
  spacer(node, indent, id, style) {
    return `${indent}<div id="${id}" class="jf-visual-spacer" aria-hidden="true" style="${style}"></div>`;
  },
  select(node, indent, id, style) {
    const options = node.options.map((option, index) => `<option value="${index}"${index === node.selected ? " selected" : ""}>${escapeHtml(option)}</option>`).join("");
    return `${indent}<select id="${id}" class="jf-visual-select" style="${style}">${options}</select>`;
  },
  list(node, indent, id, style) {
    const itemStyle = `min-height: ${cssNumber(node.itemHeight, 36, 20, 96)}px; display: flex; align-items: center; padding: 0 10px;`;
    const items = node.items.map((item) => `${indent}  <li style="${itemStyle}">${escapeHtml(item)}</li>`).join("\n");
    return `${indent}<ul id="${id}" class="jf-visual-list" style="${style}">${items ? `\n${items}\n${indent}` : ""}</ul>`;
  },
  navigation(node, indent, id, style) {
    const fontSize = cssNumber(node.fontSize, 9, 8, 24);
    const lineHeight = defaultLineHeight(fontSize);
    const items = node.items.map((item, index) => `${indent}  <button type="button" data-jf-navigation-index="${index}" style="color: ${escapeHtml(index === node.active ? cssText(node.activeColor, "#20b486") : cssText(node.color, "#9aa9b8"))}; font-size: ${fontSize}px; line-height: ${lineHeight}px">${escapeHtml(item)}</button>`).join("\n");
    return `${indent}<nav id="${id}" class="jf-visual-navigation" aria-label="Navigation" style="${style}">${items ? `\n${items}\n${indent}` : ""}</nav>`;
  },
  switch(node, indent, id, style) {
    const thumbStyle = `display: block; width: ${cssLength(node.height, "28px")}; height: ${cssLength(node.height, "28px")}; border-radius: 50%; background: ${escapeHtml(cssText(node.thumbColor, "#f4f7fb"))};`;
    return `${indent}<button id="${id}" class="jf-visual-switch" type="button" role="switch" aria-checked="${node.checked ? "true" : "false"}" style="${style}"><span style="${thumbStyle}"></span></button>`;
  }
};

function renderNode(node, indent = "    ") {
  const definition = COMPONENT_REGISTRY.find((component) => component.type === node.type);
  const renderer = sourceRenderers[definition?.renderKey];
  if (!renderer) throw new Error(`No source renderer registered for visual-editor node type: ${node.type}`);
  return renderer(node, indent, escapeHtml(node.id), escapeHtml(nodeStyle(node)));
}

function renderBody(model) {
  validateModel(model);
  return `${BODY_START}\n${renderNode(model.root, "  ")}\n${BODY_END}`;
}

function renderCss() {
  return `${CSS_START}\nhtml, body { width: 100%; height: 100%; margin: 0; overflow: hidden; }\n.jf-visual-container { box-sizing: border-box; min-width: 0; }\n.jf-visual-container:first-child { overflow: hidden; }\n.jf-visual-text { box-sizing: border-box; min-width: 0; }\n.jf-visual-button, .jf-visual-input { box-sizing: border-box; }\n.jf-visual-button { text-align: center; }\n.jf-visual-input { text-align: left; }\n.jf-visual-image { display: block; }\n.jf-visual-progress > span { display: block; }\n.jf-visual-list li { box-sizing: border-box; }\n.jf-visual-navigation { box-sizing: border-box; overflow: hidden; }\n.jf-visual-navigation button { box-sizing: border-box; flex: 1 1 0; min-width: 0; min-height: 0; margin: 0; padding: 0; background: transparent; border: 0; }\n.jf-visual-switch { box-sizing: border-box; }\n.jf-visual-switch > span { display: block; }\n${CSS_END}`;
}

function replaceMarked(source, start, end, replacement) {
  const startIndex = source.indexOf(start);
  const endIndex = source.indexOf(end);
  if (startIndex < 0 || endIndex < startIndex) return undefined;
  return source.slice(0, startIndex) + replacement + source.slice(endIndex + end.length);
}

function updateHtml(source, model) {
  const body = renderBody(model);
  const marked = replaceMarked(source, BODY_START, BODY_END, body);
  if (marked !== undefined) return marked;
  const match = /<body(?:\s[^>]*)?>([\s\S]*?)<\/body\s*>/i.exec(source);
  if (!match) throw new Error("App entry does not contain a replaceable body element");
  const scripts = [...match[1].matchAll(/<script\b[^>]*>[\s\S]*?<\/script\s*>/gi)].map((item) => item[0]);
  const openTagEnd = source.indexOf(">", match.index) + 1;
  const closeTagStart = match.index + match[0].lastIndexOf("</body");
  const scriptBlock = scripts.length ? `\n${scripts.map((script) => `  ${script}`).join("\n")}` : "";
  return `${source.slice(0, openTagEnd)}\n${body}${scriptBlock}\n${source.slice(closeTagStart)}`;
}

function updateCss(source) {
  const generated = renderCss();
  const marked = replaceMarked(source, CSS_START, CSS_END, generated);
  if (marked !== undefined) return marked;
  const separator = source && !source.endsWith("\n") ? "\n\n" : (source ? "\n" : "");
  return `${source}${separator}${generated}\n`;
}

module.exports = {
  BODY_START,
  BODY_END,
  COMPONENT_REGISTRY,
  RECIPE_REGISTRY,
  CSS_START,
  MAX_NODES,
  MODEL_FORMAT,
  MODEL_VERSION,
  NODE_TYPES,
  createDefaultModel,
  boundedModelCheck,
  componentRegistry,
  defaultNode,
  nextId,
  migrateModel,
  renderBody,
  renderCss,
  recipeRegistry,
  updateCss,
  updateHtml,
  validateModel,
  walkNodes
};
