"use strict";

const PROPERTY_GROUP_BY_CODE = Object.freeze({
  "layout-text-overflow": "layout",
  "visual-horizontal-overflow": "layout",
  "visual-vertical-paint-overflow": "layout",
  "visual-scroll-needed": "layout",
  "visual-scroll-container": "layout",
  "visual-nested-scroll-container": "layout",
  "paint-display-command-density": "appearance",
  "visual-display-command-density": "appearance",
  "paint-offscreen-budget": "appearance",
  "paint-framebuffer-budget": "appearance",
  "paint-transform-budget": "appearance",
  "paint-transient-surface-budget": "appearance",
  "paint-text-backend-failed": "appearance",
  "paint-non-ascii-fallback": "appearance"
});

function detailAttributes(detail) {
  const attributes = {};
  const text = String(detail || "");
  for (const match of text.matchAll(/([A-Za-z][A-Za-z0-9_-]*)="((?:\\.|[^"])*)"/g)) {
    attributes[match[1]] = match[2].replace(/\\"/g, '"').replace(/\\\\/g, "\\");
  }
  return attributes;
}

function modelNodeIds(model) {
  const ids = new Set();
  const visit = (node) => {
    if (!node || typeof node !== "object") return;
    if (typeof node.id === "string") ids.add(node.id);
    if (Array.isArray(node.children)) node.children.forEach(visit);
  };
  visit(model?.root);
  return ids;
}

function findStableNodeId(attributes, ids) {
  const nodeCandidates = [...String(attributes.node || "").matchAll(/#([A-Za-z][A-Za-z0-9_-]{0,47})(?=[.#>\s]|$)/g)]
    .map((match) => match[1]).filter((id) => ids.has(id));
  const pathCandidates = [...String(attributes.path || "").matchAll(/#([A-Za-z][A-Za-z0-9_-]{0,47})(?=[.#>\s]|$)/g)]
    .map((match) => match[1]).filter((id) => ids.has(id));
  const nodeId = new Set(nodeCandidates).size === 1 ? nodeCandidates[0] : undefined;
  // A DOM path includes ancestors; its deepest matching ID is the owner.
  const pathId = pathCandidates.length ? pathCandidates.at(-1) : undefined;
  if (nodeId && pathId && nodeId !== pathId) return undefined;
  return nodeId || pathId;
}

function sourceLocationForId(source, nodeId) {
  if (!nodeId || typeof source !== "string") return undefined;
  const escaped = nodeId.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const match = new RegExp(`\\bid\\s*=\\s*(["'])${escaped}\\1`, "i").exec(source);
  if (!match) return undefined;
  const before = source.slice(0, match.index);
  const line = (before.match(/\n/g) || []).length;
  const lineStart = before.lastIndexOf("\n") + 1;
  return { line, character: match.index - lineStart, length: match[0].length };
}

function attributeDiagnostic(diagnostic, model, source) {
  const entry = diagnostic && typeof diagnostic === "object" ? diagnostic : {};
  const attributes = detailAttributes(entry.detail);
  const ids = modelNodeIds(model);
  const nodeId = findStableNodeId(attributes, ids);
  const propertyGroup = nodeId ? PROPERTY_GROUP_BY_CODE[entry.code] : undefined;
  const sourceLocation = sourceLocationForId(source, nodeId);
  return {
    nodeId,
    propertyGroup,
    sourceLocation,
    domNode: attributes.node,
    domPath: attributes.path,
    attributed: Boolean(nodeId),
    reason: nodeId
      ? (sourceLocation ? "stable-node-id" : "stable-node-id-without-source-match")
      : (attributes.path ? "runtime-path-does-not-identify-a-visual-editor-node" : "diagnostic-has-no-stable-location")
  };
}

module.exports = { attributeDiagnostic, detailAttributes, modelNodeIds, sourceLocationForId };
