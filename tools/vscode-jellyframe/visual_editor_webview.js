"use strict";

(() => {
  const initialElement = document.getElementById("visual-editor-initial");
  if (!initialElement) throw new Error("Visual editor initial state is missing");

  const initial = JSON.parse(initialElement.textContent || "{}");
  const vscode = typeof acquireVsCodeApi === "function"
    ? acquireVsCodeApi()
    : { postMessage() {}, getState() { return undefined; }, setState() {} };
  const persisted = vscode.getState() || {};
  const clone = (value) => JSON.parse(JSON.stringify(value));
  let model = clone(initial.model);
  let selectedId = model.root.id;
  let history = [];
  let future = [];
  let dirty = false;
  let saving = false;
  let zoom = persisted.zoom === undefined || persisted.zoom === "fit"
    ? "fit"
    : clamp(Number(persisted.zoom) || 1, 0.2, 2);
  let activePanel = persisted.activePanel === "outline" ? "outline" : "components";
  const narrowEditor = typeof window.matchMedia === "function" && window.matchMedia("(max-width: 760px)").matches;
  let leftCollapsed = persisted.leftCollapsed === undefined ? narrowEditor : Boolean(persisted.leftCollapsed);
  let rightCollapsed = persisted.rightCollapsed === undefined ? narrowEditor : Boolean(persisted.rightCollapsed);
  const collapsedNodes = new Set(Array.isArray(persisted.collapsedNodes) ? persisted.collapsedNodes : []);
  const assets = { ...(initial.assets || {}) };
  const maxNodes = Number(initial.maxNodes) || 128;
  let suppressClick = false;

  const t = initial.chinese ? {
    components: "组件",
    outline: "结构",
    layoutGroup: "布局",
    contentGroup: "内容",
    controlsGroup: "控件",
    container: "容器",
    text: "文本",
    button: "按钮",
    image: "图片",
    input: "输入框",
    progress: "进度条",
    containerHelp: "横向或纵向排列内容",
    textHelp: "标题、标签或说明文字",
    buttonHelp: "可绑定事件的操作按钮",
    imageHelp: "App 包内的图片资源",
    inputHelp: "单行文字输入控件",
    progressHelp: "显示有界数值进度",
    viewport: "目标",
    modelViewport: "App 声明尺寸",
    fit: "适应",
    save: "保存",
    saveDebug: "保存并运行",
    saved: "已保存",
    saving: "正在保存...",
    dirty: "有未保存修改",
    ready: "设计画布为近似预览；实际效果以桌面壳为准",
    saveCancelled: "保存已取消",
    sourceConflict: "生成源码已在编辑器外发生变化；当前画布仍基于模型。保存将用当前模型替换已标记生成区。",
    selected: "已选择",
    nodes: "个节点",
    page: "页面",
    identity: "标识",
    content: "内容",
    layout: "布局",
    appearance: "外观",
    id: "稳定 ID",
    textValue: "文本",
    placeholder: "占位文字",
    value: "值",
    source: "图片路径",
    alt: "替代文字",
    direction: "方向",
    column: "纵向",
    row: "横向",
    width: "宽度",
    height: "高度",
    gap: "间距",
    padding: "内边距",
    align: "交叉轴",
    justify: "主轴",
    start: "起点",
    center: "居中",
    end: "终点",
    stretch: "拉伸",
    between: "两端",
    around: "环绕",
    background: "背景",
    color: "文字颜色",
    radius: "圆角",
    fontSize: "字号",
    weight: "字重",
    normal: "常规",
    bold: "加粗",
    textAlign: "文字对齐",
    left: "左",
    right: "右",
    fitMode: "填充方式",
    cover: "裁切填充",
    contain: "完整显示",
    fill: "拉伸",
    track: "轨道颜色",
    fillColor: "进度颜色",
    choose: "选择",
    moveUp: "上移",
    moveDown: "下移",
    duplicate: "复制",
    remove: "删除",
    toggleLeft: "显示或隐藏组件面板",
    toggleRight: "显示或隐藏属性面板",
    undo: "撤销",
    redo: "重做",
    invalidId: "ID 必须以字母开头，只能包含字母、数字、下划线或连字符。",
    duplicateId: "该 ID 已被其他节点使用。",
    invalidLength: "请输入非负数，并选择 px、% 或 auto。",
    nodeLimit: "节点数量已达到上限。",
    invalidDrop: "不能把节点移动到自身或其后代中。",
    rootProtected: "页面根节点不能移动或删除。",
    emptyContainer: "拖放组件到这里",
    imageEmpty: "选择包内图片",
    error: "编辑器错误"
  } : {
    components: "Components",
    outline: "Outline",
    layoutGroup: "Layout",
    contentGroup: "Content",
    controlsGroup: "Controls",
    container: "Container",
    text: "Text",
    button: "Button",
    image: "Image",
    input: "Input",
    progress: "Progress",
    containerHelp: "Arrange content in a row or column",
    textHelp: "Heading, label, or supporting copy",
    buttonHelp: "Action control with a stable event target",
    imageHelp: "Image resource inside the App package",
    inputHelp: "Single-line text input control",
    progressHelp: "Display a bounded numeric value",
    viewport: "Target",
    modelViewport: "App manifest size",
    fit: "Fit",
    save: "Save",
    saveDebug: "Save & run",
    saved: "Saved",
    saving: "Saving...",
    dirty: "Unsaved changes",
    ready: "The design canvas is approximate; the desktop shell is authoritative",
    saveCancelled: "Save cancelled",
    sourceConflict: "The generated source changed outside the editor; the canvas still follows the model. Save will replace the marked generated region with this model.",
    selected: "Selected",
    nodes: "nodes",
    page: "Page",
    identity: "Identity",
    content: "Content",
    layout: "Layout",
    appearance: "Appearance",
    id: "Stable ID",
    textValue: "Text",
    placeholder: "Placeholder",
    value: "Value",
    source: "Image path",
    alt: "Alt text",
    direction: "Direction",
    column: "Column",
    row: "Row",
    width: "Width",
    height: "Height",
    gap: "Gap",
    padding: "Padding",
    align: "Cross axis",
    justify: "Main axis",
    start: "Start",
    center: "Center",
    end: "End",
    stretch: "Stretch",
    between: "Between",
    around: "Around",
    background: "Background",
    color: "Text color",
    radius: "Radius",
    fontSize: "Font size",
    weight: "Weight",
    normal: "Normal",
    bold: "Bold",
    textAlign: "Text align",
    left: "Left",
    right: "Right",
    fitMode: "Image fit",
    cover: "Cover",
    contain: "Contain",
    fill: "Fill",
    track: "Track",
    fillColor: "Progress",
    choose: "Choose",
    moveUp: "Move up",
    moveDown: "Move down",
    duplicate: "Duplicate",
    remove: "Delete",
    toggleLeft: "Toggle components panel",
    toggleRight: "Toggle inspector",
    undo: "Undo",
    redo: "Redo",
    invalidId: "IDs must start with a letter and contain only letters, numbers, underscores, or hyphens.",
    duplicateId: "This ID is already used by another node.",
    invalidLength: "Enter a non-negative value and select px, %, or auto.",
    nodeLimit: "The node limit has been reached.",
    invalidDrop: "A node cannot be moved into itself or one of its descendants.",
    rootProtected: "The page root cannot be moved or deleted.",
    emptyContainer: "Drop components here",
    imageEmpty: "Choose a package image",
    error: "Editor error"
  };

  const definitions = [
    { type: "container", group: "layoutGroup", label: "container", help: "containerHelp", icon: "□" },
    { type: "text", group: "contentGroup", label: "text", help: "textHelp", icon: "T" },
    { type: "image", group: "contentGroup", label: "image", help: "imageHelp", icon: "▧" },
    { type: "button", group: "controlsGroup", label: "button", help: "buttonHelp", icon: "B" },
    { type: "input", group: "controlsGroup", label: "input", help: "inputHelp", icon: "I" },
    { type: "progress", group: "controlsGroup", label: "progress", help: "progressHelp", icon: "▬" }
  ];

  const $ = (id) => document.getElementById(id);

  function clamp(value, minimum, maximum) {
    return Math.min(maximum, Math.max(minimum, value));
  }

  function walk(node, visitor, parent = null, depth = 0) {
    if (!node) return;
    visitor(node, parent, depth);
    if (Array.isArray(node.children)) {
      node.children.forEach((child) => walk(child, visitor, node, depth + 1));
    }
  }

  function find(id) {
    let found;
    walk(model.root, (node) => {
      if (!found && node.id === id) found = node;
    });
    return found;
  }

  function parentOf(id) {
    let result;
    walk(model.root, (node, parent) => {
      if (!result && node.id === id) result = parent;
    });
    return result;
  }

  function nodeCount(node = model.root) {
    let count = 0;
    walk(node, () => { count += 1; });
    return count;
  }

  function containsId(node, id) {
    let result = false;
    walk(node, (candidate) => {
      if (candidate.id === id) result = true;
    });
    return result;
  }

  function pathNodes(id) {
    const result = [];
    let node = find(id);
    while (node) {
      result.unshift(node);
      node = parentOf(node.id);
    }
    return result;
  }

  function nextId(prefix) {
    const used = new Set();
    walk(model.root, (node) => used.add(node.id));
    let index = 1;
    while (used.has(`${prefix}-${index}`)) index += 1;
    return `${prefix}-${index}`;
  }

  function defaultNode(type) {
    const id = nextId(type);
    if (type === "container") return { id, type, layout: "column", gap: 10, padding: 12, width: "100%", height: "96px", background: "transparent", radius: 0, align: "stretch", justify: "start", children: [] };
    if (type === "text") return { id, type, text: t.text, fontSize: 18, color: "#f4f7fb", weight: "normal", align: "left", width: "auto" };
    if (type === "button") return { id, type, text: t.button, width: "100%", height: "44px", background: "#20b486", color: "#071712", radius: 6 };
    if (type === "image") return { id, type, src: "", alt: "", width: "100%", height: "96px", fit: "cover", radius: 6 };
    if (type === "input") return { id, type, placeholder: t.input, value: "", width: "100%", height: "40px", background: "#18212b", color: "#f4f7fb", radius: 4 };
    return { id, type: "progress", value: 50, width: "100%", height: "12px", track: "#26313d", fill: "#ffb84d", radius: 6 };
  }

  function snapshot() {
    history.push(JSON.stringify(model));
    if (history.length > 100) history.shift();
    future = [];
  }

  function restore(serialized) {
    model = JSON.parse(serialized);
    if (!find(selectedId)) selectedId = model.root.id;
    markDirty();
    renderAll();
  }

  function undo() {
    if (!history.length || saving) return;
    future.push(JSON.stringify(model));
    restore(history.pop());
  }

  function redo() {
    if (!future.length || saving) return;
    history.push(JSON.stringify(model));
    restore(future.pop());
  }

  function markDirty(message = t.dirty) {
    dirty = true;
    setSaveState("dirty", message);
  }

  function setSaveState(state, message) {
    document.body.dataset.saveState = state;
    $("document-state").textContent = message;
    $("status").textContent = message;
    $("save").disabled = state === "saving";
    $("actual").disabled = state === "saving";
  }

  function report(message, state = dirty ? "dirty" : "ready") {
    $("status").textContent = message;
    if (state === "error") document.body.dataset.saveState = "error";
  }

  function persistUi() {
    const styles = getComputedStyle(document.documentElement);
    vscode.setState({
      zoom,
      activePanel,
      leftCollapsed,
      rightCollapsed,
      leftWidth: styles.getPropertyValue("--left-width").trim(),
      rightWidth: styles.getPropertyValue("--right-width").trim(),
      collapsedNodes: [...collapsedNodes]
    });
  }

  function insertNode(node, parentId, index) {
    const parent = find(parentId);
    if (!parent || parent.type !== "container" || !Array.isArray(parent.children)) return false;
    const bounded = clamp(Number(index), 0, parent.children.length);
    parent.children.splice(bounded, 0, node);
    return true;
  }

  function removeNode(id) {
    const parent = parentOf(id);
    if (!parent) return undefined;
    const index = parent.children.findIndex((node) => node.id === id);
    if (index < 0) return undefined;
    return { node: parent.children.splice(index, 1)[0], parent, index };
  }

  function moveNode(id, parentId, index) {
    if (id === model.root.id) return false;
    const node = find(id);
    const targetParent = find(parentId);
    if (!node || !targetParent || targetParent.type !== "container" || containsId(node, parentId)) return false;
    const oldParent = parentOf(id);
    const oldIndex = oldParent.children.findIndex((child) => child.id === id);
    let nextIndex = clamp(Number(index), 0, targetParent.children.length);
    if (oldParent.id === targetParent.id && oldIndex < nextIndex) nextIndex -= 1;
    oldParent.children.splice(oldIndex, 1);
    targetParent.children.splice(nextIndex, 0, node);
    return true;
  }

  function insertionFor(targetId, mode) {
    const target = find(targetId);
    if (!target) return undefined;
    if (mode === "inside" && target.type === "container") {
      return { parentId: target.id, index: target.children.length };
    }
    const parent = parentOf(target.id);
    if (!parent) return target.type === "container" ? { parentId: target.id, index: target.children.length } : undefined;
    const index = parent.children.findIndex((node) => node.id === target.id);
    return { parentId: parent.id, index: index + (mode === "after" ? 1 : 0) };
  }

  function decodeDrag(event) {
    const raw = event.dataTransfer?.getData("application/x-jellyframe-node") || event.dataTransfer?.getData("text/plain") || "";
    if (raw.startsWith("new:")) return { kind: "new", type: raw.slice(4) };
    if (raw.startsWith("move:")) return { kind: "move", id: raw.slice(5) };
    try { return JSON.parse(raw); } catch { return undefined; }
  }

  function setDrag(event, payload) {
    const value = JSON.stringify(payload);
    event.dataTransfer.effectAllowed = payload.kind === "new" ? "copy" : "move";
    event.dataTransfer.setData("application/x-jellyframe-node", value);
    event.dataTransfer.setData("text/plain", payload.kind === "new" ? `new:${payload.type}` : `move:${payload.id}`);
  }

  function allowClick(event) {
    if (!suppressClick) return true;
    suppressClick = false;
    event.preventDefault();
    event.stopPropagation();
    return false;
  }

  function clearDropIndicators() {
    document.querySelectorAll(".drop-before,.drop-after,.drop-inside").forEach((element) => {
      element.classList.remove("drop-before", "drop-after", "drop-inside");
    });
  }

  function dropMode(event, element, node) {
    const rect = element.getBoundingClientRect();
    const relative = (event.clientY - rect.top) / Math.max(1, rect.height);
    if (relative < 0.25 && node.id !== model.root.id) return "before";
    if (relative > 0.75 && node.id !== model.root.id) return "after";
    return node.type === "container" ? "inside" : (relative < 0.5 ? "before" : "after");
  }

  function performDrop(payload, targetId, mode) {
    const insertion = insertionFor(targetId, mode);
    if (!payload || !insertion) return;
    if (payload.kind === "new") {
      if (nodeCount() >= maxNodes) return report(t.nodeLimit, "error");
      const node = defaultNode(payload.type);
      snapshot();
      if (!insertNode(node, insertion.parentId, insertion.index)) return history.pop();
      selectedId = node.id;
    } else if (payload.kind === "move") {
      const node = find(payload.id);
      if (!node || containsId(node, insertion.parentId)) return report(t.invalidDrop, "error");
      snapshot();
      if (!moveNode(payload.id, insertion.parentId, insertion.index)) return history.pop();
      selectedId = payload.id;
    } else return;
    markDirty();
    renderAll();
  }

  function bindPointerDrag(element, payload, label) {
    element.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      const startX = event.clientX;
      const startY = event.clientY;
      let dragging = false;
      let targetId;
      let targetMode;
      const ghost = document.createElement("div");
      ghost.className = "pointer-drag-ghost";
      ghost.textContent = label;

      const updateTarget = (moveEvent) => {
        clearDropIndicators();
        const hit = document.elementFromPoint(moveEvent.clientX, moveEvent.clientY);
        const candidate = hit?.closest(".designer-node, .outline-row");
        if (candidate?.dataset.nodeId) {
          const node = find(candidate.dataset.nodeId);
          if (node) {
            targetId = node.id;
            targetMode = dropMode(moveEvent, candidate, node);
            candidate.classList.add(`drop-${targetMode}`);
            return;
          }
        }
        if (hit && $("canvas").contains(hit)) {
          targetId = model.root.id;
          targetMode = "inside";
          $("canvas").classList.add("drop-inside");
        }
      };
      const move = (moveEvent) => {
        if (!dragging && Math.hypot(moveEvent.clientX - startX, moveEvent.clientY - startY) < 6) return;
        if (!dragging) {
          dragging = true;
          document.body.classList.add("pointer-dragging");
          document.body.append(ghost);
        }
        ghost.style.left = `${moveEvent.clientX}px`;
        ghost.style.top = `${moveEvent.clientY}px`;
        updateTarget(moveEvent);
      };
      const finish = () => {
        document.removeEventListener("pointermove", move, true);
        document.removeEventListener("pointerup", finish, true);
        document.removeEventListener("pointercancel", finish, true);
        document.body.classList.remove("pointer-dragging");
        ghost.remove();
        if (!dragging) return;
        suppressClick = true;
        clearDropIndicators();
        if (targetId && targetMode) performDrop(payload, targetId, targetMode);
      };
      document.addEventListener("pointermove", move, true);
      document.addEventListener("pointerup", finish, true);
      document.addEventListener("pointercancel", finish, true);
    });
  }

  function addNode(type) {
    if (nodeCount() >= maxNodes) return report(t.nodeLimit, "error");
    const selected = find(selectedId);
    const parent = selected?.type === "container" ? selected : parentOf(selectedId) || model.root;
    const index = selected?.type === "container" ? parent.children.length : parent.children.findIndex((node) => node.id === selectedId) + 1;
    const node = defaultNode(type);
    snapshot();
    insertNode(node, parent.id, index);
    selectedId = node.id;
    markDirty();
    renderAll();
  }

  function removeSelected() {
    if (selectedId === model.root.id) return report(t.rootProtected, "error");
    const parent = parentOf(selectedId);
    snapshot();
    removeNode(selectedId);
    selectedId = parent.id;
    markDirty();
    renderAll();
  }

  function remapIds(node, used) {
    let index = 1;
    while (used.has(`${node.type}-${index}`)) index += 1;
    node.id = `${node.type}-${index}`;
    used.add(node.id);
    if (Array.isArray(node.children)) node.children.forEach((child) => remapIds(child, used));
  }

  function duplicateSelected() {
    if (nodeCount() >= maxNodes || nodeCount(find(selectedId)) + nodeCount() > maxNodes) return report(t.nodeLimit, "error");
    const source = find(selectedId);
    const parent = parentOf(selectedId);
    if (!source || !parent) return;
    const duplicate = clone(source);
    const used = new Set();
    walk(model.root, (node) => used.add(node.id));
    remapIds(duplicate, used);
    const index = parent.children.findIndex((node) => node.id === source.id) + 1;
    snapshot();
    insertNode(duplicate, parent.id, index);
    selectedId = duplicate.id;
    markDirty();
    renderAll();
  }

  function moveSelected(delta) {
    const parent = parentOf(selectedId);
    if (!parent) return;
    const index = parent.children.findIndex((node) => node.id === selectedId);
    const target = clamp(index + delta, 0, parent.children.length - 1);
    if (target === index) return;
    snapshot();
    const [node] = parent.children.splice(index, 1);
    parent.children.splice(target, 0, node);
    markDirty();
    renderAll();
  }

  function duplicateSubtreeCount() {
    const selected = find(selectedId);
    return selected ? nodeCount(selected) : 0;
  }

  function nodeLabel(node) {
    if (node.id === model.root.id) return t.page;
    if (node.type === "text" || node.type === "button") return String(node.text || t[node.type]);
    if (node.type === "input") return String(node.placeholder || t.input);
    return t[node.type] || node.type;
  }

  function selectNode(id) {
    if (!find(id)) return;
    if (selectedId === id) return;
    selectedId = id;
    renderAll();
  }

  function applyCommonStyle(element, node) {
    if (node.width && node.width !== "auto") element.style.width = node.width;
    if (node.height && node.height !== "auto") element.style.height = node.height;
    if (node.type === "container") {
      element.style.display = "flex";
      element.style.flexDirection = node.layout === "row" ? "row" : "column";
      element.style.gap = `${Number(node.gap) || 0}px`;
      element.style.padding = `${Number(node.padding) || 0}px`;
      element.style.alignItems = ({ start: "flex-start", end: "flex-end" })[node.align] || node.align || "stretch";
      element.style.justifyContent = ({ start: "flex-start", end: "flex-end" })[node.justify] || node.justify || "flex-start";
      element.style.background = node.background || "transparent";
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
      element.style.boxSizing = "border-box";
    } else if (node.type === "text") {
      element.style.fontSize = `${Number(node.fontSize) || 16}px`;
      element.style.color = node.color;
      element.style.fontWeight = node.weight === "bold" ? "bold" : "normal";
      element.style.textAlign = node.align || "left";
      element.style.overflowWrap = "anywhere";
    } else if (node.type === "button" || node.type === "input") {
      element.style.background = node.background;
      element.style.color = node.color;
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
      element.style.border = "0";
      element.style.padding = "0 12px";
    } else if (node.type === "image") {
      element.style.objectFit = node.fit || "cover";
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
    } else if (node.type === "progress") {
      element.style.background = node.track;
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
      element.style.overflow = "hidden";
    }
  }

  function bindDropTarget(element, node) {
    element.addEventListener("dragover", (event) => {
      event.preventDefault();
      event.stopPropagation();
      clearDropIndicators();
      const mode = dropMode(event, element, node);
      element.classList.add(`drop-${mode}`);
      event.dataTransfer.dropEffect = "move";
    });
    element.addEventListener("dragleave", (event) => {
      if (!element.contains(event.relatedTarget)) clearDropIndicators();
    });
    element.addEventListener("drop", (event) => {
      event.preventDefault();
      event.stopPropagation();
      const mode = dropMode(event, element, node);
      const payload = decodeDrag(event);
      clearDropIndicators();
      performDrop(payload, node.id, mode);
    });
  }

  function renderNode(node) {
    let element;
    if (node.type === "container") {
      element = document.createElement("section");
      if (!node.children.length) {
        const empty = document.createElement("div");
        empty.className = "designer-empty";
        empty.textContent = t.emptyContainer;
        element.append(empty);
      } else node.children.forEach((child) => element.append(renderNode(child)));
    } else if (node.type === "text") {
      element = document.createElement("div");
      element.contentEditable = "true";
      element.setAttribute("role", "textbox");
      element.spellcheck = false;
      element.textContent = node.text;
    } else if (node.type === "button") {
      element = document.createElement("button");
      element.type = "button";
      element.textContent = node.text;
    } else if (node.type === "image" && node.src && assets[node.src]) {
      element = document.createElement("img");
      element.src = assets[node.src];
      element.alt = node.alt || "";
    } else if (node.type === "image") {
      element = document.createElement("div");
      element.className = "image-placeholder";
      element.textContent = t.imageEmpty;
    } else if (node.type === "input") {
      element = document.createElement("input");
      element.value = node.value || "";
      element.placeholder = node.placeholder || "";
      element.readOnly = true;
      element.tabIndex = -1;
    } else {
      element = document.createElement("div");
      const fill = document.createElement("span");
      fill.className = "progress-fill";
      fill.style.width = `${clamp(Number(node.value) || 0, 0, 100)}%`;
      fill.style.height = node.height || "12px";
      fill.style.background = node.fill;
      element.append(fill);
    }
    element.classList.add(`jf-visual-${node.type}`, "designer-node");
    element.dataset.nodeId = node.id;
    if (node.id === selectedId) element.classList.add("selected");
    applyCommonStyle(element, node);
    element.draggable = node.id !== model.root.id;
    element.addEventListener("dragstart", (event) => {
      event.stopPropagation();
      setDrag(event, { kind: "move", id: node.id });
    });
    element.addEventListener("dragend", clearDropIndicators);
    element.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      if (allowClick(event)) selectNode(node.id);
    });
    bindPointerDrag(element, { kind: "move", id: node.id }, nodeLabel(node));
    if (node.type === "text") {
      element.addEventListener("blur", () => {
        const value = element.textContent || "";
        if (value !== node.text) commitValue(node, "text", value);
      });
      element.addEventListener("keydown", (event) => {
        if (event.key === "Escape") {
          event.preventDefault();
          element.textContent = node.text;
          element.blur();
        }
        if (event.key === "Enter" && !event.shiftKey) {
          event.preventDefault();
          element.blur();
        }
      });
    }
    bindDropTarget(element, node);
    return element;
  }

  function renderCanvas() {
    const canvas = $("canvas");
    canvas.replaceChildren(renderNode(model.root));
    const round = model.viewport.shape === "round";
    canvas.classList.toggle("round", round);
    $("canvas-shell").classList.toggle("round", round);
    $("canvas-shell").style.width = `${model.viewport.width}px`;
    $("canvas-shell").style.height = `${model.viewport.height}px`;
    $("device-caption").textContent = `${model.viewport.width} x ${model.viewport.height}${round ? " · round" : ""}`;
    requestAnimationFrame(applyZoom);
  }

  function bindCanvasDrop() {
    const canvas = $("canvas");
    canvas.addEventListener("dragover", (event) => {
      event.preventDefault();
      if (event.target !== canvas) return;
      clearDropIndicators();
      canvas.classList.add("drop-inside");
    });
    canvas.addEventListener("dragleave", (event) => {
      if (event.target === canvas && !canvas.contains(event.relatedTarget)) clearDropIndicators();
    });
    canvas.addEventListener("drop", (event) => {
      if (event.target !== canvas) return;
      event.preventDefault();
      const payload = decodeDrag(event);
      clearDropIndicators();
      performDrop(payload, model.root.id, "inside");
    });
  }

  function renderSourceNotice() {
    const notice = $("source-notice");
    notice.hidden = !initial.sourceConflict;
    notice.textContent = initial.sourceConflict ? t.sourceConflict : "";
  }

  function renderPalette() {
    const groups = ["layoutGroup", "contentGroup", "controlsGroup"];
    const list = $("palette-list");
    list.replaceChildren();
    groups.forEach((group) => {
      const section = document.createElement("section");
      section.className = "palette-group";
      const heading = document.createElement("h3");
      heading.className = "group-label";
      heading.textContent = t[group];
      section.append(heading);
      definitions.filter((item) => item.group === group).forEach((definition) => {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "palette-item";
        button.draggable = true;
        const icon = document.createElement("span");
        icon.className = "palette-icon";
        icon.textContent = definition.icon;
        const copy = document.createElement("span");
        copy.className = "palette-copy";
        const strong = document.createElement("strong");
        strong.textContent = t[definition.label];
        const help = document.createElement("span");
        help.textContent = t[definition.help];
        copy.append(strong, help);
        button.append(icon, copy);
        button.addEventListener("dragstart", (event) => setDrag(event, { kind: "new", type: definition.type }));
        bindPointerDrag(button, { kind: "new", type: definition.type }, t[definition.label]);
        button.addEventListener("click", (event) => { if (allowClick(event)) addNode(definition.type); });
        section.append(button);
      });
      list.append(section);
    });
  }

  function renderOutlineBranch(node, depth) {
    const branch = document.createElement("div");
    branch.className = "outline-branch";
    const row = document.createElement("div");
    row.className = `outline-row${node.id === selectedId ? " selected" : ""}`;
    row.style.setProperty("--depth", depth);
    row.dataset.nodeId = node.id;
    row.draggable = node.id !== model.root.id;
    row.setAttribute("role", "treeitem");
    row.setAttribute("aria-level", String(depth + 1));
    row.setAttribute("aria-selected", String(node.id === selectedId));
    const toggle = document.createElement("button");
    toggle.type = "button";
    toggle.className = "outline-toggle";
    const hasChildren = node.type === "container" && node.children.length > 0;
    toggle.textContent = hasChildren ? (collapsedNodes.has(node.id) ? "›" : "⌄") : "";
    toggle.disabled = !hasChildren;
    toggle.addEventListener("pointerdown", (event) => event.stopPropagation());
    toggle.addEventListener("click", (event) => {
      event.stopPropagation();
      if (collapsedNodes.has(node.id)) collapsedNodes.delete(node.id); else collapsedNodes.add(node.id);
      persistUi();
      renderOutline();
    });
    const icon = document.createElement("span");
    icon.className = "outline-icon";
    icon.textContent = definitions.find((item) => item.type === node.type)?.icon || "·";
    const name = document.createElement("span");
    name.className = "outline-name";
    name.textContent = nodeLabel(node);
    const kind = document.createElement("span");
    kind.className = "outline-kind";
    kind.textContent = node.id;
    row.append(toggle, icon, name, kind);
    row.addEventListener("click", (event) => { if (allowClick(event)) selectNode(node.id); });
    bindPointerDrag(row, { kind: "move", id: node.id }, nodeLabel(node));
    row.addEventListener("dragstart", (event) => {
      event.stopPropagation();
      setDrag(event, { kind: "move", id: node.id });
    });
    bindDropTarget(row, node);
    branch.append(row);
    if (hasChildren && !collapsedNodes.has(node.id)) {
      node.children.forEach((child) => branch.append(renderOutlineBranch(child, depth + 1)));
    }
    return branch;
  }

  function renderOutline() {
    const tree = $("outline-tree");
    tree.replaceChildren(renderOutlineBranch(model.root, 0));
  }

  function section(title) {
    const element = document.createElement("section");
    element.className = "inspector-section";
    const heading = document.createElement("h3");
    heading.textContent = title;
    element.append(heading);
    return element;
  }

  function commitValue(node, key, value, validate) {
    const error = validate?.(value);
    if (error) return error;
    if (node[key] === value) return undefined;
    snapshot();
    const oldId = node.id;
    node[key] = value;
    if (key === "id") {
      if (selectedId === oldId) selectedId = value;
      if (collapsedNodes.delete(oldId)) collapsedNodes.add(value);
    }
    markDirty();
    renderAll();
    return undefined;
  }

  function fieldRow(label) {
    const row = document.createElement("div");
    row.className = "field";
    const fieldLabel = document.createElement("label");
    fieldLabel.textContent = label;
    row.append(fieldLabel);
    return row;
  }

  function showFieldError(row, message) {
    row.querySelector(".field-error")?.remove();
    if (!message) return;
    const error = document.createElement("div");
    error.className = "field-error";
    error.textContent = message;
    row.append(error);
  }

  function textField(node, key, label, options = {}) {
    const row = fieldRow(label);
    const input = document.createElement("input");
    input.type = "text";
    input.value = String(node[key] ?? "");
    input.placeholder = options.placeholder || "";
    input.addEventListener("change", () => {
      let value = input.value;
      let error;
      if (key === "id") {
        value = value.trim();
        if (!/^[A-Za-z][A-Za-z0-9_-]{0,47}$/.test(value)) error = t.invalidId;
        else {
          const existing = find(value);
          if (existing && existing !== node) error = t.duplicateId;
        }
      }
      showFieldError(row, error);
      if (!error) commitValue(node, key, value);
    });
    row.append(input);
    return row;
  }

  function numberField(node, key, label, minimum, maximum) {
    const row = fieldRow(label);
    const input = document.createElement("input");
    input.type = "number";
    input.min = String(minimum);
    input.max = String(maximum);
    input.step = "1";
    input.value = String(node[key] ?? minimum);
    input.addEventListener("change", () => {
      const value = clamp(Number(input.value), minimum, maximum);
      commitValue(node, key, value);
    });
    row.append(input);
    return row;
  }

  function selectField(node, key, label, choices) {
    const row = fieldRow(label);
    const select = document.createElement("select");
    choices.forEach(([value, name]) => {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = name;
      select.append(option);
    });
    select.value = node[key];
    select.addEventListener("change", () => commitValue(node, key, select.value));
    row.append(select);
    return row;
  }

  function segmentedField(node, key, label, choices) {
    const row = fieldRow(label);
    const control = document.createElement("div");
    control.className = "segmented";
    choices.forEach(([value, name]) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = node[key] === value ? "active" : "";
      button.textContent = name;
      button.title = name;
      button.addEventListener("click", () => commitValue(node, key, value));
      control.append(button);
    });
    row.append(control);
    return row;
  }

  function parseLength(value) {
    const text = String(value ?? "auto").trim();
    if (text === "auto") return { value: "", unit: "auto" };
    if (text === "0") return { value: "0", unit: "px" };
    const match = /^([0-9]+(?:\.[0-9]+)?)(px|%)$/.exec(text);
    return match ? { value: match[1], unit: match[2] } : { value: "", unit: "auto" };
  }

  function lengthField(node, key, label) {
    const row = fieldRow(label);
    const control = document.createElement("div");
    control.className = "field-composite";
    const parsed = parseLength(node[key]);
    const input = document.createElement("input");
    input.type = "number";
    input.min = "0";
    input.step = "1";
    input.value = parsed.value;
    const unit = document.createElement("select");
    [["px", "px"], ["%", "%"], ["auto", "auto"]].forEach(([value, name]) => {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = name;
      unit.append(option);
    });
    unit.value = parsed.unit;
    input.disabled = unit.value === "auto";
    const apply = () => {
      input.disabled = unit.value === "auto";
      if (unit.value === "auto") return commitValue(node, key, "auto");
      const numeric = Number(input.value);
      const error = !Number.isFinite(numeric) || numeric < 0 ? t.invalidLength : undefined;
      showFieldError(row, error);
      if (!error) commitValue(node, key, `${numeric}${unit.value}`);
    };
    input.addEventListener("change", apply);
    unit.addEventListener("change", apply);
    control.append(input, unit);
    row.append(control);
    return row;
  }

  function colorField(node, key, label) {
    const row = fieldRow(label);
    const control = document.createElement("div");
    control.className = "color-control";
    const swatch = document.createElement("input");
    swatch.type = "color";
    swatch.value = /^#[0-9a-f]{6}$/i.test(node[key]) ? node[key] : "#000000";
    const input = document.createElement("input");
    input.type = "text";
    input.value = node[key] || "transparent";
    swatch.addEventListener("change", () => commitValue(node, key, swatch.value));
    input.addEventListener("change", () => commitValue(node, key, input.value.trim() || "transparent"));
    control.append(swatch, input);
    row.append(control);
    return row;
  }

  function resourceField(node) {
    const row = fieldRow(t.source);
    const control = document.createElement("div");
    control.className = "resource-control";
    const input = document.createElement("input");
    input.type = "text";
    input.value = node.src || "";
    input.readOnly = true;
    const choose = document.createElement("button");
    choose.type = "button";
    choose.textContent = t.choose;
    choose.addEventListener("click", () => vscode.postMessage({ type: "choose-image", nodeId: node.id }));
    control.append(input, choose);
    row.append(control);
    return row;
  }

  function renderInspector() {
    const node = find(selectedId);
    const body = $("inspector-body");
    body.replaceChildren();
    $("selected-type").textContent = node ? t[node.type] : "";
    $("selected-name").textContent = node ? node.id : "";
    const actions = $("node-actions");
    actions.replaceChildren();
    if (!node) {
      const empty = document.createElement("div");
      empty.className = "inspector-empty";
      empty.textContent = t.ready;
      body.append(empty);
      return;
    }
    if (node.id !== model.root.id) {
      [["↑", t.moveUp, () => moveSelected(-1)], ["↓", t.moveDown, () => moveSelected(1)], ["⧉", t.duplicate, duplicateSelected], ["×", t.remove, removeSelected]].forEach(([label, title, handler]) => {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "quiet";
        button.textContent = label;
        button.title = title;
        button.setAttribute("aria-label", title);
        button.addEventListener("click", handler);
        actions.append(button);
      });
    }

    const identity = section(t.identity);
    identity.append(textField(node, "id", t.id));
    body.append(identity);

    if (["text", "button", "input", "image", "progress"].includes(node.type)) {
      const content = section(t.content);
      if (node.type === "text" || node.type === "button") content.append(textField(node, "text", t.textValue));
      if (node.type === "input") content.append(textField(node, "placeholder", t.placeholder), textField(node, "value", t.value));
      if (node.type === "image") content.append(resourceField(node), textField(node, "alt", t.alt));
      if (node.type === "progress") content.append(numberField(node, "value", t.value, 0, 100));
      body.append(content);
    }

    const layout = section(t.layout);
    if (node.type === "container") {
      layout.append(
        segmentedField(node, "layout", t.direction, [["column", t.column], ["row", t.row]]),
        numberField(node, "gap", t.gap, 0, 64),
        numberField(node, "padding", t.padding, 0, 64),
        selectField(node, "align", t.align, [["stretch", t.stretch], ["start", t.start], ["center", t.center], ["end", t.end]]),
        selectField(node, "justify", t.justify, [["start", t.start], ["center", t.center], ["end", t.end], ["space-between", t.between], ["space-around", t.around]])
      );
    }
    layout.append(lengthField(node, "width", t.width));
    if (node.type !== "text") layout.append(lengthField(node, "height", t.height));
    body.append(layout);

    const appearance = section(t.appearance);
    if (node.type === "container" || node.type === "button" || node.type === "input") appearance.append(colorField(node, "background", t.background));
    if (node.type === "text" || node.type === "button" || node.type === "input") appearance.append(colorField(node, "color", t.color));
    if (node.type === "text") appearance.append(numberField(node, "fontSize", t.fontSize, 8, 72), segmentedField(node, "weight", t.weight, [["normal", t.normal], ["bold", t.bold]]), segmentedField(node, "align", t.textAlign, [["left", t.left], ["center", t.center], ["right", t.right]]));
    if (node.type === "image") appearance.append(selectField(node, "fit", t.fitMode, [["cover", t.cover], ["contain", t.contain], ["fill", t.fill]]));
    if (node.type === "progress") appearance.append(colorField(node, "track", t.track), colorField(node, "fill", t.fillColor));
    if (["container", "button", "image", "input", "progress"].includes(node.type)) appearance.append(numberField(node, "radius", t.radius, 0, 150));
    body.append(appearance);
  }

  function renderBreadcrumbs() {
    const container = $("breadcrumbs");
    container.replaceChildren();
    pathNodes(selectedId).forEach((node, index, nodes) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "breadcrumb";
      button.textContent = node.id;
      button.addEventListener("click", () => selectNode(node.id));
      container.append(button);
      if (index < nodes.length - 1) {
        const separator = document.createElement("span");
        separator.className = "breadcrumb-separator";
        separator.textContent = "/";
        container.append(separator);
      }
    });
  }

  function renderPanels() {
    document.body.classList.toggle("left-collapsed", leftCollapsed);
    document.body.classList.toggle("right-collapsed", rightCollapsed);
    $("components-tab").classList.toggle("active", activePanel === "components");
    $("outline-tab").classList.toggle("active", activePanel === "outline");
    $("components-tab").setAttribute("aria-selected", String(activePanel === "components"));
    $("outline-tab").setAttribute("aria-selected", String(activePanel === "outline"));
    $("components-panel").hidden = activePanel !== "components";
    $("outline-panel").hidden = activePanel !== "outline";
    $("toggle-left").setAttribute("aria-pressed", String(!leftCollapsed));
    $("toggle-right").setAttribute("aria-pressed", String(!rightCollapsed));
  }

  function renderAll() {
    renderPanels();
    renderSourceNotice();
    renderCanvas();
    renderOutline();
    renderInspector();
    renderBreadcrumbs();
    $("undo").disabled = !history.length || saving;
    $("redo").disabled = !future.length || saving;
    $("node-count").textContent = `${nodeCount()} / ${maxNodes} ${t.nodes}`;
  }

  function applyZoom() {
    const wrap = $("canvas-wrap");
    let scale = zoom;
    if (zoom === "fit") {
      scale = Math.min(1, (wrap.clientWidth - 68) / model.viewport.width, (wrap.clientHeight - 84) / model.viewport.height);
    }
    scale = clamp(Number(scale) || 1, 0.2, 2);
    $("canvas-shell").style.transform = `scale(${scale})`;
    $("zoom-label").textContent = `${Math.round(scale * 100)}%`;
  }

  function adjustZoom(delta) {
    const current = zoom === "fit" ? 1 : Number(zoom);
    zoom = clamp(Math.round((current + delta) * 10) / 10, 0.2, 2);
    persistUi();
    applyZoom();
  }

  function setPanel(panel) {
    activePanel = panel;
    persistUi();
    renderPanels();
  }

  function setupResizer(element, side) {
    element.addEventListener("pointerdown", (event) => {
      event.preventDefault();
      element.setPointerCapture(event.pointerId);
      element.classList.add("dragging");
      document.body.classList.add("resizing");
      const startX = event.clientX;
      const property = side === "left" ? "--left-width" : "--right-width";
      const initialWidth = parseFloat(getComputedStyle(document.documentElement).getPropertyValue(property));
      const move = (moveEvent) => {
        const delta = moveEvent.clientX - startX;
        const width = side === "left" ? initialWidth + delta : initialWidth - delta;
        document.documentElement.style.setProperty(property, `${clamp(width, 180, 420)}px`);
        if (zoom === "fit") applyZoom();
      };
      const finish = () => {
        element.classList.remove("dragging");
        document.body.classList.remove("resizing");
        element.removeEventListener("pointermove", move);
        element.removeEventListener("pointerup", finish);
        element.removeEventListener("pointercancel", finish);
        persistUi();
      };
      element.addEventListener("pointermove", move);
      element.addEventListener("pointerup", finish);
      element.addEventListener("pointercancel", finish);
    });
  }

  function requestSave(debug) {
    if (saving) return;
    saving = true;
    setSaveState("saving", t.saving);
    renderAll();
    vscode.postMessage({ type: debug ? "save-debug" : "save", model });
  }

  function initializeUi() {
    if (persisted.leftWidth && /^\d+(?:\.\d+)?px$/.test(persisted.leftWidth)) document.documentElement.style.setProperty("--left-width", persisted.leftWidth);
    if (persisted.rightWidth && /^\d+(?:\.\d+)?px$/.test(persisted.rightWidth)) document.documentElement.style.setProperty("--right-width", persisted.rightWidth);
    $("app-name").textContent = initial.appName;
    $("components-tab").textContent = t.components;
    $("outline-tab").textContent = t.outline;
    $("viewport-label").textContent = t.viewport;
    $("viewport-preset").querySelector("option[value=model]").textContent = `${t.modelViewport} · ${model.viewport.width} x ${model.viewport.height}`;
    $("zoom-fit").textContent = t.fit;
    $("save").textContent = t.save;
    $("actual").textContent = t.saveDebug;
    $("toggle-left").title = t.toggleLeft;
    $("toggle-left").setAttribute("aria-label", t.toggleLeft);
    $("toggle-right").title = t.toggleRight;
    $("toggle-right").setAttribute("aria-label", t.toggleRight);
    $("undo").title = t.undo;
    $("undo").setAttribute("aria-label", t.undo);
    $("redo").title = t.redo;
    $("redo").setAttribute("aria-label", t.redo);
    renderPalette();
    setSaveState("ready", t.ready);
    renderAll();
  }

  $("components-tab").addEventListener("click", () => setPanel("components"));
  $("outline-tab").addEventListener("click", () => setPanel("outline"));
  $("toggle-left").addEventListener("click", () => { leftCollapsed = !leftCollapsed; persistUi(); renderPanels(); if (zoom === "fit") applyZoom(); });
  $("toggle-right").addEventListener("click", () => { rightCollapsed = !rightCollapsed; persistUi(); renderPanels(); if (zoom === "fit") applyZoom(); });
  $("undo").addEventListener("click", undo);
  $("redo").addEventListener("click", redo);
  $("zoom-out").addEventListener("click", () => adjustZoom(-0.1));
  $("zoom-in").addEventListener("click", () => adjustZoom(0.1));
  $("zoom-fit").addEventListener("click", () => { zoom = "fit"; persistUi(); applyZoom(); });
  $("save").addEventListener("click", () => requestSave(false));
  $("actual").addEventListener("click", () => requestSave(true));
  $("viewport-preset").addEventListener("change", (event) => {
    if (event.target.value === "model") return;
    const presets = {
      "round-300": { width: 300, height: 300, shape: "round" },
      "rect-172x320": { width: 172, height: 320, shape: "rect" },
      "rect-320x240": { width: 320, height: 240, shape: "rect" }
    };
    const viewport = presets[event.target.value];
    if (!viewport) return;
    snapshot();
    model.viewport = viewport;
    markDirty();
    renderAll();
  });
  $("canvas-wrap").addEventListener("wheel", (event) => {
    if (!event.ctrlKey && !event.metaKey) return;
    event.preventDefault();
    adjustZoom(event.deltaY > 0 ? -0.1 : 0.1);
  }, { passive: false });
  setupResizer($("left-resizer"), "left");
  setupResizer($("right-resizer"), "right");
  bindCanvasDrop();
  new ResizeObserver(() => { if (zoom === "fit") applyZoom(); }).observe($("canvas-wrap"));

  document.addEventListener("keydown", (event) => {
    const editing = ["INPUT", "SELECT", "TEXTAREA"].includes(document.activeElement?.tagName);
    const command = event.ctrlKey || event.metaKey;
    if (command && event.key.toLowerCase() === "s") {
      event.preventDefault();
      requestSave(false);
    } else if (command && event.key.toLowerCase() === "d" && !editing) {
      event.preventDefault();
      duplicateSelected();
    } else if (command && event.key.toLowerCase() === "z" && !editing) {
      event.preventDefault();
      if (event.shiftKey) redo(); else undo();
    } else if (command && event.key.toLowerCase() === "y" && !editing) {
      event.preventDefault();
      redo();
    } else if ((event.key === "Delete" || event.key === "Backspace") && !editing) {
      event.preventDefault();
      removeSelected();
    } else if (event.altKey && event.key === "ArrowUp" && !editing) {
      event.preventDefault();
      moveSelected(-1);
    } else if (event.altKey && event.key === "ArrowDown" && !editing) {
      event.preventDefault();
      moveSelected(1);
    }
  });

  window.addEventListener("message", (event) => {
    const message = event.data || {};
    if (message.type === "saved") {
      dirty = false;
      saving = false;
      initial.sourceConflict = false;
      setSaveState("saved", t.saved);
      renderAll();
      if (message.debug) vscode.postMessage({ type: "debug" });
    } else if (message.type === "save-cancelled") {
      saving = false;
      setSaveState("dirty", t.saveCancelled);
      renderAll();
    } else if (message.type === "asset") {
      const node = find(message.nodeId);
      if (node?.type === "image") {
        snapshot();
        node.src = message.path;
        assets[message.path] = message.uri;
        markDirty();
        renderAll();
      }
    } else if (message.type === "error") {
      saving = false;
      setSaveState("error", `${t.error}: ${message.message}`);
      renderAll();
    }
  });

  window.addEventListener("beforeunload", (event) => {
    if (!dirty) return;
    event.preventDefault();
    event.returnValue = "";
  });

  initializeUi();
})();
