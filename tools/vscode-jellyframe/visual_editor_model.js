"use strict";

const MODEL_FORMAT = "jellyframe.visual-editor";
const MODEL_VERSION = 1;
const BODY_START = "<!-- jellyframe-visual-editor:body:start -->";
const BODY_END = "<!-- jellyframe-visual-editor:body:end -->";
const CSS_START = "/* jellyframe-visual-editor:styles:start */";
const CSS_END = "/* jellyframe-visual-editor:styles:end */";
const MAX_NODES = 128;
const NODE_TYPES = new Set(["container", "text", "button", "image", "input", "progress"]);

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
  const text = String(value ?? "").trim();
  return /^(?:auto|0|[0-9]+(?:\.[0-9]+)?(?:px|%))$/.test(text) ? text : fallback;
}

function cssNumber(value, fallback, minimum, maximum) {
  const number = Number(value);
  return Number.isFinite(number) ? Math.min(maximum, Math.max(minimum, number)) : fallback;
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
  return { ...common, value: 50, width: "100%", height: "12px", track: "#26313d", fill: "#ffb84d", radius: 6 };
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

function validateModel(model) {
  if (!model || model.format !== MODEL_FORMAT || model.formatVersion !== MODEL_VERSION) {
    throw new Error("Unsupported JellyFrame visual-editor model");
  }
  if (!model.root || model.root.type !== "container") {
    throw new Error("Visual-editor model must have a container root");
  }
  const ids = new Set();
  let count = 0;
  walkNodes(model.root, (node) => {
    count += 1;
    if (count > MAX_NODES) throw new Error(`Visual-editor model exceeds ${MAX_NODES} nodes`);
    if (!NODE_TYPES.has(node.type)) throw new Error(`Unsupported visual-editor node type: ${node.type}`);
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,47}$/.test(String(node.id || ""))) throw new Error(`Invalid visual-editor node id: ${node.id || "missing"}`);
    if (ids.has(node.id)) throw new Error(`Duplicate visual-editor node id: ${node.id}`);
    ids.add(node.id);
    if (node.type === "container" && !Array.isArray(node.children)) throw new Error(`Container ${node.id} must have children`);
    if (node.type === "image" && !/^\/(?!\/)[^?#]+$/.test(String(node.src || ""))) {
      throw new Error(`Image ${node.id} must reference a package-local resource`);
    }
    if (node.type === "progress" && !/^(?:0|[0-9]+(?:\.[0-9]+)?px)$/.test(String(node.height || ""))) {
      throw new Error(`Progress ${node.id} height must be a non-negative pixel length`);
    }
  });
  return model;
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
    add("color", cssText(node.color, "#ffffff"));
    add("font-weight", node.weight === "bold" ? "bold" : "normal");
    add("text-align", ["left", "center", "right"].includes(node.align) ? node.align : "left");
    add("overflow-wrap", "anywhere");
  } else if (node.type === "button" || node.type === "input") {
    add("background", cssText(node.background, "#202a34"));
    add("color", cssText(node.color, "#ffffff"));
    add("border-radius", `${cssNumber(node.radius, 0, 0, 64)}px`);
    add("border", "0");
    add("padding", "0 12px");
  } else if (node.type === "image") {
    add("object-fit", ["cover", "contain", "fill"].includes(node.fit) ? node.fit : "cover");
    add("border-radius", `${cssNumber(node.radius, 0, 0, 150)}px`);
  } else if (node.type === "progress") {
    add("background", cssText(node.track, "#26313d"));
    add("border-radius", `${cssNumber(node.radius, 0, 0, 64)}px`);
    add("overflow", "hidden");
  }
  return declarations.join("; ");
}

function renderNode(node, indent = "    ") {
  const id = escapeHtml(node.id);
  const style = escapeHtml(nodeStyle(node));
  if (node.type === "container") {
    const children = node.children.map((child) => renderNode(child, `${indent}  `)).join("\n");
    return `${indent}<section id="${id}" class="jf-visual-container" style="${style}">${children ? `\n${children}\n${indent}` : ""}</section>`;
  }
  if (node.type === "text") return `${indent}<div id="${id}" class="jf-visual-text" style="${style}">${escapeHtml(node.text)}</div>`;
  if (node.type === "button") return `${indent}<button id="${id}" class="jf-visual-button" type="button" style="${style}">${escapeHtml(node.text)}</button>`;
  if (node.type === "image") return `${indent}<img id="${id}" class="jf-visual-image" src="${escapeHtml(node.src)}" alt="${escapeHtml(node.alt)}" style="${style}">`;
  if (node.type === "input") return `${indent}<input id="${id}" class="jf-visual-input" value="${escapeHtml(node.value)}" placeholder="${escapeHtml(node.placeholder)}" style="${style}">`;
  const progress = cssNumber(node.value, 0, 0, 100);
  const fillHeight = cssLength(node.height, "12px");
  return `${indent}<div id="${id}" class="jf-visual-progress" role="progressbar" aria-valuenow="${progress}" aria-valuemin="0" aria-valuemax="100" style="${style}"><span style="width: ${progress}%; height: ${fillHeight}; background: ${escapeHtml(cssText(node.fill, "#ffb84d"))}"></span></div>`;
}

function renderBody(model) {
  validateModel(model);
  return `${BODY_START}\n${renderNode(model.root, "  ")}\n${BODY_END}`;
}

function renderCss() {
  return `${CSS_START}\nhtml, body { width: 100%; height: 100%; margin: 0; overflow: hidden; }\n.jf-visual-container { box-sizing: border-box; min-width: 0; }\n.jf-visual-container:first-child { overflow: hidden; }\n.jf-visual-text { box-sizing: border-box; min-width: 0; }\n.jf-visual-button, .jf-visual-input { box-sizing: border-box; }\n.jf-visual-image { display: block; }\n.jf-visual-progress > span { display: block; }\n${CSS_END}`;
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
  CSS_START,
  MAX_NODES,
  MODEL_FORMAT,
  MODEL_VERSION,
  NODE_TYPES,
  createDefaultModel,
  defaultNode,
  nextId,
  renderBody,
  renderCss,
  updateCss,
  updateHtml,
  validateModel,
  walkNodes
};
