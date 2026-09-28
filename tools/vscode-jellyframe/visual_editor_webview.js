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
  let modelCheckTimer;
  let modelCheckRevision = 0;
  let previewCanvasChildren;
  let previewCanvasScroll;
  let zoom = persisted.zoom === undefined || persisted.zoom === "fit"
    ? "fit"
    : clamp(Number(persisted.zoom) || 1, 0.2, 2);
  let activePanel = persisted.activePanel === "outline" ? "outline" : "components";
  const narrowEditor = typeof window.matchMedia === "function" && window.matchMedia("(max-width: 760px)").matches;
  let leftCollapsed = persisted.leftCollapsed === undefined ? narrowEditor : Boolean(persisted.leftCollapsed);
  let rightCollapsed = persisted.rightCollapsed === undefined ? narrowEditor : Boolean(persisted.rightCollapsed);
  let panX = Number.isFinite(Number(persisted.panX)) ? Number(persisted.panX) : 0;
  let panY = Number.isFinite(Number(persisted.panY)) ? Number(persisted.panY) : 0;
  const collapsedNodes = new Set(Array.isArray(persisted.collapsedNodes) ? persisted.collapsedNodes : []);
  const assets = { ...(initial.assets || {}) };
  const maxNodes = Number(initial.maxNodes) || 128;
  let suppressClick = false;
  let contextMenu;
  let resizeSession;
  let selectionOverlay;

  const t = initial.chinese ? {
    wearableGroup: "穿戴设备组合",
    panelGroup: "面板设备导航",
    functionList: "纵向功能入口", functionListHelp: "可滚动的纵向按钮列表，每项有独立 ID",
    metricScreen: "指标页", metricScreenHelp: "数值、目标、进度与详情入口",
    roundAction: "圆屏操作区", roundActionHelp: "居中数值与主操作，保留边缘留白",
    notificationDetail: "通知详情", notificationDetailHelp: "纵向信息与底部操作，可滚动",
    contentHorizontal: "内容水平对齐", contentVertical: "内容垂直对齐",
    paddingX: "左右内边距", paddingY: "上下内边距", overflowY: "纵向溢出",
    horizontalChildren: "子项水平对齐", verticalChildren: "子项垂直对齐",
    components: "组件",
    outline: "结构",
    layoutGroup: "布局",
    contentGroup: "内容",
    controlsGroup: "控件",
    recipesGroup: "模板组合",
    container: "容器",
    text: "文本",
    button: "按钮",
    image: "图片",
    input: "输入框",
    progress: "进度条",
    divider: "分隔线",
    spacer: "留白",
    select: "选择框",
    list: "列表",
    navigation: "底部导航",
    switch: "开关",
    statusCard: "状态卡",
    settingsRow: "设置行",
    bottomNavigation: "底部导航组合",
    iconTextRow: "图标信息行",
    iconTextRowHelp: "图标、标题、副标题与右侧值组成的紧凑信息行",
    valueRow: "标签数值行",
    valueRowHelp: "适合设置页和状态页的标签与数值行",
    metricBlock: "指标块",
    metricBlockHelp: "适合手表屏幕的数值、单位与说明组合",
    segmentedControl: "分段选择",
    segmentedControlHelp: "由普通按钮组成的横向选项组，业务切换由脚本维护",
    containerHelp: "横向或纵向排列内容",
    textHelp: "标题、标签或说明文字",
    buttonHelp: "可绑定事件的操作按钮",
    imageHelp: "App 包内的图片资源",
    inputHelp: "单行文字输入控件",
    progressHelp: "显示有界数值进度",
    dividerHelp: "在内容之间建立清晰分隔",
    spacerHelp: "为嵌入式布局保留固定空间",
    selectHelp: "受限选项选择，不依赖浏览器弹窗",
    listHelp: "展示短列表或设置项",
    navigationHelp: "适合小屏设备的有限导航项",
    statusCardHelp: "标题、状态和值进度组成的常用状态块",
    settingsRowHelp: "标签与开关组成的设置行",
    bottomNavigationHelp: "包含内容区和底部导航的设备页面骨架",
    switchHelp: "适合设备设置的二态开关",
    viewport: "目标",
    modelViewport: "App 声明尺寸",
    fit: "适应",
    save: "保存",
    saveDebug: "保存并运行",
    saved: "已保存",
    runtimeIdle: "Runtime 未启动",
    runtimeRunning: "Runtime 运行中",
    runtimeReporting: "正在生成 Runtime 报告",
    runtimeStopped: "Runtime 已停止",
    saving: "正在保存...",
    checking: "正在检查当前模型...",
    checked: "模型检查通过",
    checkError: "模型检查失败",
    dirty: "有未保存修改",
    ready: "设计画布为近似预览；实际效果以桌面壳为准",
    saveCancelled: "保存已取消",
    sourceConflict: "生成源码已在编辑器外发生变化；当前画布仍基于模型。保存将用当前模型替换已标记生成区。",
    viewDiff: "查看差异",
    restoreBackup: "恢复最近快照",
    selected: "已选择",
    nodes: "个节点",
    page: "页面",
    identity: "标识",
    content: "内容",
    layout: "布局",
    appearance: "外观",
    interaction: "交互",
    detectedListeners: "已检测到的监听器",
    noListeners: "未检测到本地监听器",
    copyEventSkeleton: "复制事件骨架",
    eventSkeletonHelp: "不会修改脚本；请将骨架粘贴到 App 的本地 JavaScript 中。",
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
    options: "选项",
    selectedOption: "当前选项",
    items: "项目",
    itemHeight: "项目高度",
    activeItem: "当前项目",
    activeColor: "当前颜色",
    addItem: "添加项目",
    removeItem: "删除项目",
    choose: "选择",
    moveUp: "上移",
    moveDown: "下移",
    duplicate: "复制",
    remove: "删除",
    select: "选择",
    copyId: "复制稳定 ID",
    showInOutline: "在结构树中显示",
    close: "关闭",
    resize: "调整尺寸",
    resizeWidth: "调整宽度",
    resizeHeight: "调整高度",
    toggleLeft: "显示或隐藏组件面板",
    toggleRight: "显示或隐藏属性面板",
    undo: "撤销",
    redo: "重做",
    invalidId: "ID 必须以字母开头，只能包含字母、数字、下划线或连字符。",
    duplicateId: "该 ID 已被其他节点使用。",
    invalidValue: "该值不符合此控件的类型或范围。",
    invalidLength: "请输入非负数，并选择 px、% 或 auto。",
    nodeLimit: "节点数量已达到上限。",
    invalidDrop: "不能把节点移动到自身或其后代中。",
    rootProtected: "页面根节点不能移动或删除。",
    emptyContainer: "拖放组件到这里",
    imageEmpty: "选择包内图片",
    error: "编辑器错误"
  } : {
    wearableGroup: "Wearable recipes",
    panelGroup: "Panel navigation",
    functionList: "Function list", functionListHelp: "Scrollable vertical buttons with individual IDs",
    metricScreen: "Metric screen", metricScreenHelp: "Value, goal, progress and detail action",
    roundAction: "Round action", roundActionHelp: "Centered value and action with edge insets",
    notificationDetail: "Notification detail", notificationDetailHelp: "Scrollable message and action",
    contentHorizontal: "Horizontal content", contentVertical: "Vertical content",
    paddingX: "Horizontal padding", paddingY: "Vertical padding", overflowY: "Vertical overflow",
    horizontalChildren: "Horizontal children", verticalChildren: "Vertical children",
    components: "Components",
    outline: "Outline",
    layoutGroup: "Layout",
    contentGroup: "Content",
    controlsGroup: "Controls",
    recipesGroup: "Recipes",
    container: "Container",
    text: "Text",
    button: "Button",
    image: "Image",
    input: "Input",
    progress: "Progress",
    divider: "Divider",
    spacer: "Spacer",
    select: "Select",
    list: "List",
    navigation: "Bottom navigation",
    switch: "Switch",
    statusCard: "Status card",
    settingsRow: "Settings row",
    bottomNavigation: "Bottom navigation",
    iconTextRow: "Icon detail row",
    iconTextRowHelp: "A compact icon, title, subtitle and trailing value row",
    valueRow: "Value row",
    valueRowHelp: "A label and value row for settings and status screens",
    metricBlock: "Metric block",
    metricBlockHelp: "A wearable-friendly value, unit and supporting label block",
    segmentedControl: "Segmented control",
    segmentedControlHelp: "A button-based option group; switching stays in author JavaScript",
    containerHelp: "Arrange content in a row or column",
    textHelp: "Heading, label, or supporting copy",
    buttonHelp: "Action control with a stable event target",
    imageHelp: "Image resource inside the App package",
    inputHelp: "Single-line text input control",
    progressHelp: "Display a bounded numeric value",
    dividerHelp: "Separate content without extra runtime behavior",
    spacerHelp: "Reserve bounded space for embedded layouts",
    selectHelp: "Choose from a small fixed set of options",
    listHelp: "Display a short list or settings rows",
    navigationHelp: "A small-screen navigation row with bounded items",
    statusCardHelp: "A status label, value and bounded progress indicator",
    settingsRowHelp: "A label and switch for a compact settings row",
    bottomNavigationHelp: "A device screen skeleton with content and bottom navigation",
    switchHelp: "A bounded two-state control for device settings",
    viewport: "Target",
    modelViewport: "App manifest size",
    fit: "Fit",
    save: "Save",
    saveDebug: "Save & run",
    saved: "Saved",
    runtimeIdle: "Runtime idle",
    runtimeRunning: "Runtime running",
    runtimeReporting: "Generating Runtime report",
    runtimeStopped: "Runtime stopped",
    saving: "Saving...",
    checking: "Checking the current model...",
    checked: "Model check passed",
    checkError: "Model check failed",
    dirty: "Unsaved changes",
    ready: "The design canvas is approximate; the desktop shell is authoritative",
    saveCancelled: "Save cancelled",
    sourceConflict: "The generated source changed outside the editor; the canvas still follows the model. Save will replace the marked generated region with this model.",
    viewDiff: "View diff",
    restoreBackup: "Restore latest snapshot",
    selected: "Selected",
    nodes: "nodes",
    page: "Page",
    identity: "Identity",
    content: "Content",
    layout: "Layout",
    appearance: "Appearance",
    interaction: "Interaction",
    detectedListeners: "Detected listeners",
    noListeners: "No local listener detected",
    copyEventSkeleton: "Copy event skeleton",
    eventSkeletonHelp: "This does not modify scripts. Paste the skeleton into local App JavaScript.",
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
    options: "Options",
    selectedOption: "Selected option",
    items: "Items",
    itemHeight: "Item height",
    activeItem: "Active item",
    activeColor: "Active color",
    addItem: "Add item",
    removeItem: "Remove item",
    choose: "Choose",
    moveUp: "Move up",
    moveDown: "Move down",
    duplicate: "Duplicate",
    remove: "Delete",
    select: "Select",
    copyId: "Copy stable ID",
    showInOutline: "Show in outline",
    close: "Close",
    resize: "Resize",
    resizeWidth: "Resize width",
    resizeHeight: "Resize height",
    toggleLeft: "Toggle components panel",
    toggleRight: "Toggle inspector",
    undo: "Undo",
    redo: "Redo",
    invalidId: "IDs must start with a letter and contain only letters, numbers, underscores, or hyphens.",
    duplicateId: "This ID is already used by another node.",
    invalidValue: "This value does not match the field type or allowed range.",
    invalidLength: "Enter a non-negative value and select px, %, or auto.",
    nodeLimit: "The node limit has been reached.",
    invalidDrop: "A node cannot be moved into itself or one of its descendants.",
    rootProtected: "The page root cannot be moved or deleted.",
    emptyContainer: "Drop components here",
    imageEmpty: "Choose a package image",
    error: "Editor error"
  };

  const definitions = Array.isArray(initial.registry) ? initial.registry : [
    { type: "container", group: "layoutGroup", label: "container", help: "containerHelp", icon: "□" },
    { type: "text", group: "contentGroup", label: "text", help: "textHelp", icon: "T" },
    { type: "image", group: "contentGroup", label: "image", help: "imageHelp", icon: "▧" },
    { type: "button", group: "controlsGroup", label: "button", help: "buttonHelp", icon: "B" },
    { type: "input", group: "controlsGroup", label: "input", help: "inputHelp", icon: "I" },
    { type: "progress", group: "controlsGroup", label: "progress", help: "progressHelp", icon: "▬" }
  ];
  const registryByType = new Map(definitions.map((definition) => [definition.type, definition]));
  const recipes = Array.isArray(initial.recipes) ? initial.recipes : [];

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

  function find(id, root = model.root) {
    let found;
    walk(root, (node) => {
      if (!found && node.id === id) found = node;
    });
    return found;
  }

  function parentOf(id, root = model.root) {
    let result;
    walk(root, (node, parent) => {
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
    if (type === "progress") return { id, type: "progress", value: 50, width: "100%", height: "12px", track: "#26313d", fill: "#ffb84d", radius: 6 };
    if (type === "divider") return { id, type, width: "100%", height: 1, color: "#344250" };
    if (type === "spacer") return { id, type, width: "100%", height: 12 };
    if (type === "select") return { id, type, options: ["Option 1", "Option 2", "Option 3"], selected: 0, width: "100%", height: "40px", background: "#18212b", color: "#f4f7fb", radius: 4 };
    if (type === "list") return { id, type, items: ["List item 1", "List item 2", "List item 3"], width: "100%", height: "auto", itemHeight: 36, gap: 4, background: "#18212b", color: "#f4f7fb", radius: 6 };
    if (type === "navigation") return { id, type, items: ["Home", "Stats", "Settings"], active: 0, width: "100%", height: "48px", gap: 4, background: "#18212b", color: "#9aa9b8", activeColor: "#20b486", radius: 6 };
    if (type === "switch") return { id, type, checked: true, width: "52px", height: "28px", onColor: "#20b486", offColor: "#26313d", thumbColor: "#f4f7fb", radius: 14 };
    throw new Error(`Unsupported visual-editor node type: ${type}`);
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
    scheduleModelCheck();
  }

  function scheduleModelCheck() {
    if (modelCheckTimer) clearTimeout(modelCheckTimer);
    const revision = ++modelCheckRevision;
    modelCheckTimer = setTimeout(() => {
      modelCheckTimer = undefined;
      vscode.postMessage({ type: "model-check", revision, model });
    }, 180);
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
      panX,
      panY,
      collapsedNodes: [...collapsedNodes]
    });
  }

  function insertNode(node, parentId, index, root = model.root) {
    const parent = find(parentId, root);
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

  function moveNode(id, parentId, index, root = model.root) {
    if (id === root.id) return false;
    const node = find(id, root);
    const targetParent = find(parentId, root);
    if (!node || !targetParent || targetParent.type !== "container" || containsId(node, parentId)) return false;
    const oldParent = parentOf(id, root);
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
    event.dataTransfer.effectAllowed = payload.kind === "new" || payload.kind === "recipe" ? "copy" : "move";
    event.dataTransfer.setData("application/x-jellyframe-node", value);
    event.dataTransfer.setData("text/plain", payload.kind === "move" ? `move:${payload.id}` : `new:${payload.type}`);
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
    if (previewCanvasChildren) {
      $("canvas").replaceChildren(...previewCanvasChildren);
      restoreCanvasScroll(previewCanvasScroll);
      previewCanvasChildren = undefined;
      previewCanvasScroll = undefined;
      renderSelectionOverlay();
    }
  }

  function showDropPreview(payload, targetId, mode) {
    const result = prepareDrop(payload, targetId, mode);
    if (!result || result.error) return;
    const canvas = $("canvas");
    const scrollPositions = captureCanvasScroll();
    previewCanvasScroll = scrollPositions;
    previewCanvasChildren = [...canvas.childNodes];
    renderSelectionOverlay();
    // Render the projected tree so flex sizing and sibling layout match the drop.
    canvas.replaceChildren(renderNode(result.root, true));
    restoreCanvasScroll(scrollPositions);
    const preview = [...canvas.querySelectorAll(".designer-node")]
      .find((element) => element.dataset.nodeId === result.id);
    preview?.classList.add("designer-drop-preview");
  }

  function dropMode(event, element, node) {
    const rect = element.getBoundingClientRect();
    const horizontal = !element.classList.contains("outline-row") && parentOf(node.id)?.layout === "row";
    const relative = horizontal
      ? (event.clientX - rect.left) / Math.max(1, rect.width)
      : (event.clientY - rect.top) / Math.max(1, rect.height);
    if (relative < 0.25 && node.id !== model.root.id) return "before";
    if (relative > 0.75 && node.id !== model.root.id) return "after";
    return node.type === "container" ? "inside" : (relative < 0.5 ? "before" : "after");
  }

  function prepareDrop(payload, targetId, mode) {
    const insertion = insertionFor(targetId, mode);
    if (!payload || !insertion) return;
    const root = clone(model.root);
    let id;
    if (payload.kind === "new") {
      if (nodeCount() >= maxNodes) return { error: t.nodeLimit };
      const node = defaultNode(payload.type);
      if (!insertNode(node, insertion.parentId, insertion.index, root)) return;
      id = node.id;
    } else if (payload.kind === "recipe") {
      const recipe = recipes.find((candidate) => candidate.type === payload.type);
      if (!recipe) return { error: t.invalidDrop };
      const node = recipeNode(recipe);
      if (nodeCount() + nodeCount(node) > maxNodes) return { error: t.nodeLimit };
      const used = new Set();
      walk(model.root, (candidate) => used.add(candidate.id));
      remapIds(node, used);
      if (!insertNode(node, insertion.parentId, insertion.index, root)) return;
      id = node.id;
    } else if (payload.kind === "move") {
      const node = find(payload.id);
      if (!node || containsId(node, insertion.parentId)) return { error: t.invalidDrop };
      if (!moveNode(payload.id, insertion.parentId, insertion.index, root)) return;
      id = payload.id;
    } else return;
    return { root, id };
  }

  function performDrop(payload, targetId, mode) {
    const result = prepareDrop(payload, targetId, mode);
    if (!result) return;
    if (result.error) return report(result.error, "error");
    snapshot();
    model.root = result.root;
    selectedId = result.id;
    markDirty();
    renderAll();
  }

  function bindPointerDrag(element, payload, label) {
    element.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      event.stopPropagation();
      const startX = event.clientX;
      const startY = event.clientY;
      let dragging = false;
      let targetId;
      let targetMode;
      let lastX = startX;
      let lastY = startY;
      const ghost = document.createElement("div");
      ghost.className = "pointer-drag-ghost";
      ghost.textContent = label;

      const updateTarget = (moveEvent) => {
        clearDropIndicators();
        targetId = undefined;
        targetMode = undefined;
        const hit = document.elementFromPoint(moveEvent.clientX, moveEvent.clientY);
        const candidate = hit?.closest(".designer-node, .outline-row");
        if (candidate?.dataset.nodeId) {
          const node = find(candidate.dataset.nodeId);
          if (node) {
            const dragged = payload.kind === "move" ? find(payload.id) : undefined;
            if (dragged && (dragged.id === node.id || containsId(dragged, node.id))) return;
            targetId = node.id;
            targetMode = dropMode(moveEvent, candidate, node);
            if (payload.kind !== "move") candidate.classList.add(`drop-${targetMode}`);
            showDropPreview(payload, targetId, targetMode);
            return;
          }
        }
        if (hit && $("canvas").contains(hit)) {
          targetId = model.root.id;
          targetMode = "inside";
          $("canvas").classList.add("drop-inside");
          showDropPreview(payload, targetId, targetMode);
        }
      };
      const move = (moveEvent) => {
        lastX = moveEvent.clientX;
        lastY = moveEvent.clientY;
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
      const finish = (upEvent) => {
        document.removeEventListener("pointermove", move, true);
        document.removeEventListener("pointerup", finish, true);
        document.removeEventListener("pointercancel", finish, true);
        const cancelled = upEvent?.type === "pointercancel";
        if (dragging && upEvent && !cancelled) {
          lastX = upEvent.clientX;
          lastY = upEvent.clientY;
          updateTarget({ clientX: lastX, clientY: lastY });
        }
        ghost.remove();
        document.body.classList.remove("pointer-dragging");
        if (!dragging) return;
        suppressClick = true;
        clearDropIndicators();
        if (!cancelled && targetId && targetMode) performDrop(payload, targetId, targetMode);
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

  function recipeNode(recipe) {
    const node = clone(model.viewport.shape === "round" && recipe.roundTemplate ? recipe.roundTemplate : recipe.template);
    if (recipe.roundSafeInset && model.viewport.shape === "round") {
      node.paddingX = Math.ceil(model.viewport.width * (1 - Math.SQRT1_2) / 2);
      node.paddingY = Math.ceil(model.viewport.height * (1 - Math.SQRT1_2) / 2);
    }
    return node;
  }

  function addRecipe(recipe) {
    const node = recipeNode(recipe);
    const subtreeSize = nodeCount(node);
    if (nodeCount() + subtreeSize > maxNodes) return report(t.nodeLimit, "error");
    const selected = find(selectedId);
    const parent = selected?.type === "container" ? selected : parentOf(selectedId) || model.root;
    const index = selected?.type === "container" ? parent.children.length : parent.children.findIndex((child) => child.id === selectedId) + 1;
    const used = new Set();
    walk(model.root, (candidate) => used.add(candidate.id));
    remapIds(node, used);
    snapshot();
    if (!insertNode(node, parent.id, index)) return history.pop();
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

  function selectNode(id, options = {}) {
    if (!find(id)) return;
    if (selectedId !== id) {
      selectedId = id;
      renderAll();
    }
    if (options.focusOutline) focusOutlineRow(id);
  }

  function closeContextMenu() {
    contextMenu?.remove();
    contextMenu = undefined;
  }

  function copyNodeId(id) {
    vscode.postMessage({ type: "copy-node-id", text: id });
  }

  function openContextMenu(event, nodeId) {
    event.preventDefault();
    event.stopPropagation();
    const node = find(nodeId);
    if (!node) return;
    if (selectedId !== node.id) selectNode(node.id);
    closeContextMenu();
    const fromOutline = Boolean(event.target?.closest?.(".outline-row"));
    const restoreFocus = () => {
      const selector = fromOutline ? "#outline-tree .outline-row" : "#canvas .designer-node";
      const target = [...document.querySelectorAll(selector)].find((item) => item.dataset.nodeId === selectedId);
      if (target) {
        if (!fromOutline && !target.hasAttribute("tabindex")) target.tabIndex = -1;
        target.focus({ preventScroll: true });
      }
    };

    const menu = document.createElement("div");
    menu.className = "editor-context-menu";
    menu.setAttribute("role", "menu");
    menu.tabIndex = -1;
    const addItem = (label, handler, options = {}) => {
      const item = document.createElement("button");
      item.type = "button";
      item.className = "editor-context-menu-item";
      item.setAttribute("role", "menuitem");
      item.tabIndex = -1;
      item.textContent = label;
      item.disabled = Boolean(options.disabled);
      item.addEventListener("click", () => {
        if (item.disabled) return;
        closeContextMenu();
        handler();
        if (label !== t.showInOutline) restoreFocus();
      });
      menu.append(item);
      return item;
    };
    addItem(t.select, () => selectNode(node.id));
    addItem(t.copyId, () => copyNodeId(node.id));
    addItem(t.showInOutline, () => {
      leftCollapsed = false;
      activePanel = "outline";
      pathNodes(node.id).forEach((ancestor) => collapsedNodes.delete(ancestor.id));
      persistUi();
      renderPanels();
      renderOutline();
      focusOutlineRow(node.id);
    });
    const separator = document.createElement("div");
    separator.className = "editor-context-menu-separator";
    separator.setAttribute("role", "separator");
    menu.append(separator);
    addItem(t.duplicate, duplicateSelected, { disabled: saving || node.id === model.root.id || nodeCount() + nodeCount(node) > maxNodes });
    addItem(t.moveUp, () => moveSelected(-1), { disabled: saving || node.id === model.root.id || !parentOf(node.id)?.children?.length || parentOf(node.id).children[0]?.id === node.id });
    addItem(t.moveDown, () => moveSelected(1), { disabled: saving || node.id === model.root.id || !parentOf(node.id)?.children?.length || parentOf(node.id).children.at(-1)?.id === node.id });
    addItem(t.remove, removeSelected, { disabled: saving || node.id === model.root.id });
    const historySeparator = document.createElement("div");
    historySeparator.className = "editor-context-menu-separator";
    historySeparator.setAttribute("role", "separator");
    menu.append(historySeparator);
    addItem(t.undo, undo, { disabled: !history.length || saving });
    addItem(t.redo, redo, { disabled: !future.length || saving });
    document.body.append(menu);
    const margin = 6;
    const x = clamp(event.clientX, margin, Math.max(margin, window.innerWidth - menu.offsetWidth - margin));
    const y = clamp(event.clientY, margin, Math.max(margin, window.innerHeight - menu.offsetHeight - margin));
    menu.style.left = `${x}px`;
    menu.style.top = `${y}px`;
    contextMenu = menu;
    const enabledItems = () => [...menu.querySelectorAll("button:not(:disabled)")];
    const focusItem = (item) => {
      menu.querySelectorAll("button").forEach((button) => { button.tabIndex = button === item ? 0 : -1; });
      item?.focus({ preventScroll: true });
    };
    menu.addEventListener("keydown", (keyEvent) => {
      keyEvent.stopPropagation();
      const items = enabledItems();
      const index = items.indexOf(document.activeElement);
      if (["ArrowDown", "ArrowUp", "Home", "End"].includes(keyEvent.key)) {
        keyEvent.preventDefault();
        const next = keyEvent.key === "Home" ? 0 : keyEvent.key === "End" ? items.length - 1
          : (index + (keyEvent.key === "ArrowDown" ? 1 : -1) + items.length) % items.length;
        focusItem(items[next]);
      } else if (keyEvent.key === "Escape" || keyEvent.key === "Tab") {
        keyEvent.preventDefault();
        closeContextMenu();
        restoreFocus();
      }
    });
    focusItem(enabledItems()[0]);
  }

  function renderSelectionOverlay() {
    const node = find(selectedId);
    const element = [...$("canvas").querySelectorAll(".designer-node")].find((item) => item.dataset.nodeId === node?.id);
    if (!node || node.id === model.root.id || previewCanvasChildren || saving || !element) {
      selectionOverlay?.remove();
      selectionOverlay = undefined;
      return;
    }
    const shell = $("canvas-shell").getBoundingClientRect();
    const scale = shell.width / model.viewport.width;
    const rect = element.getBoundingClientRect();
    const clip = { left: shell.left, top: shell.top, right: shell.right, bottom: shell.bottom };
    // Clip editor chrome to the same scrolling ancestors without putting it
    // inside controls, editable text, or the generated app layout.
    for (let parent = element.parentElement; parent && parent !== $("canvas-shell"); parent = parent.parentElement) {
      const style = getComputedStyle(parent);
      const bounds = parent.getBoundingClientRect();
      if (style.overflowX !== "visible") {
        clip.left = Math.max(clip.left, bounds.left);
        clip.right = Math.min(clip.right, bounds.right);
      }
      if (style.overflowY !== "visible") {
        clip.top = Math.max(clip.top, bounds.top);
        clip.bottom = Math.min(clip.bottom, bounds.bottom);
      }
    }
    const layer = selectionOverlay || document.createElement("div");
    layer.className = "selection-overlay";
    Object.assign(layer.style, {
      left: `${(clip.left - shell.left) / scale}px`, top: `${(clip.top - shell.top) / scale}px`,
      width: `${Math.max(0, clip.right - clip.left) / scale}px`, height: `${Math.max(0, clip.bottom - clip.top) / scale}px`
    });
    const frame = layer.firstElementChild || document.createElement("div");
    frame.className = "selection-frame";
    Object.assign(frame.style, {
      left: `${(rect.left - clip.left) / scale}px`, top: `${(rect.top - clip.top) / scale}px`,
      width: `${rect.width / scale}px`, height: `${rect.height / scale}px`
    });
    if (!selectionOverlay) ["nw", "n", "ne", "e", "se", "s", "sw", "w"].forEach((edge) => {
      const handle = document.createElement("button");
      handle.type = "button";
      handle.tabIndex = -1;
      handle.className = `resize-handle resize-handle-${edge}`;
      handle.dataset.resizeEdge = edge;
      const horizontal = edge === "e" || edge === "w";
      const vertical = edge === "n" || edge === "s";
      handle.title = horizontal ? t.resizeWidth : vertical ? t.resizeHeight : t.resize;
      handle.setAttribute("aria-label", horizontal ? t.resizeWidth : vertical ? t.resizeHeight : t.resize);
      handle.addEventListener("pointerdown", (event) => {
        const current = find(selectedId);
        const target = [...$("canvas").querySelectorAll(".designer-node")].find((item) => item.dataset.nodeId === selectedId);
        if (current && target) bindResizeHandle(event, current, target, edge);
      });
      handle.addEventListener("click", (event) => { event.preventDefault(); event.stopPropagation(); });
      frame.append(handle);
    });
    if (!selectionOverlay) {
      layer.append(frame);
      $("canvas-shell").append(layer);
    }
    selectionOverlay = layer;
  }

  function bindResizeHandle(event, node, element, edge) {
    if (event.button !== 0 || node.id === model.root.id || saving || resizeSession) return;
    event.preventDefault();
    event.stopPropagation();
    closeContextMenu();
    const rect = element.getBoundingClientRect();
    const scale = $("canvas-shell").getBoundingClientRect().width / model.viewport.width;
    const style = getComputedStyle(element);
    const extra = (axis) => style.boxSizing === "border-box" ? 0
      : (axis === "width" ? ["paddingLeft", "paddingRight", "borderLeftWidth", "borderRightWidth"]
        : ["paddingTop", "paddingBottom", "borderTopWidth", "borderBottomWidth"])
        .reduce((sum, key) => sum + (parseFloat(style[key]) || 0), 0);
    const startWidth = rect.width / scale - extra("width");
    const startHeight = rect.height / scale - extra("height");
    const resizeWidth = edge.includes("e") || edge.includes("w");
    const resizeHeight = edge.includes("n") || edge.includes("s");
    const startX = event.clientX;
    const startY = event.clientY;
    const originalScroll = captureCanvasScroll();
    let changes = {};
    document.body.classList.add("resizing-node");

    const move = (moveEvent) => {
      if (moveEvent.pointerId !== event.pointerId) return;
      changes = {};
      const setDimension = (key, start, delta) => {
        if (Math.abs(delta) < 1) return;
        const field = registryByType.get(node.type)?.fields.find((item) => item.key === key);
        if (!field) return;
        const pixels = Math.round(clamp(start + delta, field.min ?? 0, field.max ?? 10000));
        const typed = typedValue(node, key, field.kind === "number" ? pixels : `${pixels}px`);
        if (!typed.error && typed.value !== node[key]) changes[key] = typed.value;
      };
      if (resizeWidth) setDimension("width", startWidth, (moveEvent.clientX - startX) / scale * (edge.includes("w") ? -1 : 1));
      if (resizeHeight) setDimension("height", startHeight, (moveEvent.clientY - startY) / scale * (edge.includes("n") ? -1 : 1));
      const projected = clone(model.root);
      Object.assign(find(node.id, projected), changes);
      renderCanvas(projected, true);
    };
    const finish = (finishEvent) => {
      if (finishEvent?.pointerId !== undefined && finishEvent.pointerId !== event.pointerId) return;
      document.removeEventListener("pointermove", move, true);
      document.removeEventListener("pointerup", finish, true);
      document.removeEventListener("pointercancel", finish, true);
      window.removeEventListener("blur", cancel);
      resizeSession = undefined;
      document.body.classList.remove("resizing-node");
      if (finishEvent?.type === "pointerup" && Object.keys(changes).length) {
        snapshot();
        Object.assign(node, changes);
        markDirty();
      }
      renderAll();
      if (finishEvent?.type !== "pointerup" || !Object.keys(changes).length) {
        restoreCanvasScroll(originalScroll);
        renderSelectionOverlay();
      }
    };
    const cancel = () => finish();
    resizeSession = { cancel };
    window.addEventListener("blur", cancel);
    document.addEventListener("pointermove", move, true);
    document.addEventListener("pointerup", finish, true);
    document.addEventListener("pointercancel", finish, true);
  }

  function styleLength(value) {
    if (typeof value === "number" && Number.isFinite(value) && value >= 0) return `${value}px`;
    const text = String(value ?? "").trim();
    return /^(?:0|[0-9]+(?:\.[0-9]+)?(?:px|%))$/.test(text) ? text : "";
  }

  function visualLineHeight(fontSize) {
    const size = Math.max(1, Math.round(Number(fontSize) || 16));
    return `${size + Math.max(6, Math.floor(size / 3))}px`;
  }

  function applyCommonStyle(element, node) {
    const width = styleLength(node.width);
    const height = styleLength(node.height);
    if (width) element.style.width = width;
    if (height) element.style.height = height;
    if (node.type === "container") {
      element.style.display = "flex";
      element.style.flexDirection = node.layout === "row" ? "row" : "column";
      element.style.gap = `${Number(node.gap) || 0}px`;
      element.style.padding = `${Number(node.padding) || 0}px`;
      if (node.paddingX !== undefined || node.paddingY !== undefined) {
        element.style.padding = `${node.paddingY ?? node.padding ?? 0}px ${node.paddingX ?? node.padding ?? 0}px`;
      }
      if (node.overflowY) {
        element.style.overflow = node.overflowY === "visible" ? "visible" : "hidden";
        if (node.overflowY === "auto") element.style.overflowY = "auto";
      }
      element.style.alignItems = ({ start: "flex-start", end: "flex-end" })[node.align] || node.align || "stretch";
      element.style.justifyContent = ({ start: "flex-start", end: "flex-end" })[node.justify] || node.justify || "flex-start";
      element.style.background = node.background || "transparent";
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
      element.style.boxSizing = "border-box";
    } else if (node.type === "text") {
      element.style.fontSize = `${Number(node.fontSize) || 16}px`;
      element.style.lineHeight = visualLineHeight(node.fontSize);
      element.style.color = node.color;
      element.style.fontWeight = node.weight === "bold" ? "bold" : "normal";
      element.style.textAlign = node.align || "left";
      element.style.overflowWrap = "anywhere";
    } else if (node.type === "button" || node.type === "input") {
      element.style.fontSize = `${node.fontSize ?? 16}px`;
      element.style.lineHeight = visualLineHeight(node.fontSize);
      element.style.background = node.background;
      element.style.color = node.color;
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
      element.style.border = "0";
      element.style.padding = `${node.paddingY ?? 0}px ${node.paddingX ?? 12}px`;
      element.style.textAlign = node.textAlign || (node.type === "button" ? "center" : "left");
    } else if (node.type === "image") {
      element.style.objectFit = node.fit || "cover";
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
    } else if (node.type === "progress") {
      element.style.background = node.track;
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
      element.style.overflow = "hidden";
    } else if (node.type === "divider") {
      element.style.background = "transparent";
      element.style.setProperty("--divider-color", node.color);
    } else if (node.type === "spacer") {
      element.style.background = "transparent";
    } else if (node.type === "select") {
      element.style.background = node.background;
      element.style.color = node.color;
      element.style.border = "0";
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
      element.style.padding = "0 10px";
    } else if (node.type === "list") {
      element.style.display = "flex";
      element.style.flexDirection = "column";
      element.style.gap = `${Number(node.gap) || 0}px`;
      element.style.margin = "0";
      element.style.padding = "0";
      element.style.listStyle = "none";
      element.style.background = node.background;
      element.style.color = node.color;
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
      element.style.overflow = "hidden";
    } else if (node.type === "navigation") {
      element.style.display = "flex";
      element.style.alignItems = "stretch";
      element.style.gap = `${Number(node.gap) || 0}px`;
      element.style.background = node.background;
      element.style.borderRadius = `${Number(node.radius) || 0}px`;
      element.style.padding = "4px";
      element.style.fontSize = `${Number(node.fontSize) || 9}px`;
      element.style.lineHeight = visualLineHeight(node.fontSize || 9);
      element.style.boxSizing = "border-box";
      element.style.overflow = "hidden";
    } else if (node.type === "switch") {
      element.style.display = "inline-flex";
      element.style.alignItems = "center";
      element.style.justifyContent = node.checked ? "flex-end" : "flex-start";
      element.style.padding = "3px";
      element.style.background = node.checked ? node.onColor : node.offColor;
      element.style.border = "0";
      element.style.borderRadius = `${Number(node.radius) || 14}px`;
      element.style.boxSizing = "border-box";
    }
    if ((node.type === "text" || node.type === "button") && node.verticalAlign !== undefined) {
      element.style.display = "flex";
      element.style.flexDirection = "column";
      element.style.alignItems = "stretch";
      element.style.justifyContent = ({ start: "flex-start", center: "center", end: "flex-end" })[node.verticalAlign];
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

  const designRenderers = {
    container(node, preview) {
      const element = document.createElement("section");
      if (!node.children.length) {
        const empty = document.createElement("div");
        empty.className = "designer-empty";
        empty.textContent = t.emptyContainer;
        element.append(empty);
      } else node.children.forEach((child) => {
        const rendered = renderNode(child, preview);
        if (node.overflowY === "auto") rendered.style.flexShrink = "0";
        element.append(rendered);
      });
      return element;
    },
    text(node) {
      const element = document.createElement("div");
      element.contentEditable = "true";
      element.setAttribute("role", "textbox");
      element.spellcheck = false;
      appendAlignedText(element, node);
      return element;
    },
    button(node) {
      const element = document.createElement("button");
      element.type = "button";
      appendAlignedText(element, node);
      return element;
    },
    image(node) {
      if (node.src && assets[node.src]) {
        const element = document.createElement("img");
        element.src = assets[node.src];
        element.alt = node.alt || "";
        return element;
      }
      const element = document.createElement("div");
      element.className = "image-placeholder";
      element.textContent = t.imageEmpty;
      return element;
    },
    input(node) {
      const element = document.createElement("input");
      element.value = node.value || "";
      element.placeholder = node.placeholder || "";
      element.readOnly = true;
      element.tabIndex = -1;
      return element;
    },
    progress(node) {
      const element = document.createElement("div");
      const fill = document.createElement("span");
      fill.className = "progress-fill";
      fill.style.width = `${clamp(Number(node.value) || 0, 0, 100)}%`;
      fill.style.height = styleLength(node.height) || "12px";
      fill.style.background = node.fill;
      element.append(fill);
      return element;
    },
    divider() {
      const element = document.createElement("div");
      element.setAttribute("role", "separator");
      return element;
    },
    spacer() {
      const element = document.createElement("div");
      element.setAttribute("aria-hidden", "true");
      return element;
    },
    select(node) {
      const element = document.createElement("select");
      node.options.forEach((option, index) => {
        const item = document.createElement("option");
        item.value = String(index);
        item.textContent = option;
        item.selected = index === node.selected;
        element.append(item);
      });
      element.tabIndex = -1;
      return element;
    },
    list(node) {
      const element = document.createElement("ul");
      node.items.forEach((item) => {
        const row = document.createElement("li");
        const content = document.createElement("span");
        content.style.display = "block";
        content.style.width = "100%";
        content.textContent = item;
        row.append(content);
        row.style.minHeight = `${Number(node.itemHeight) || 36}px`;
        row.style.display = "flex";
        row.style.flexDirection = "column";
        row.style.justifyContent = ({ start: "flex-start", center: "center", end: "flex-end" })[node.verticalAlign || "center"];
        row.style.padding = `${node.paddingY ?? 0}px ${node.paddingX ?? 10}px`;
        row.style.textAlign = node.textAlign || "left";
        row.style.fontSize = `${node.fontSize ?? 16}px`;
        row.style.lineHeight = visualLineHeight(node.fontSize);
        row.style.boxSizing = "border-box";
        element.append(row);
      });
      return element;
    },
    navigation(node) {
      const element = document.createElement("nav");
      element.setAttribute("aria-label", t.navigation);
      node.items.forEach((item, index) => {
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = item;
        button.style.flex = "1 1 0";
        button.style.boxSizing = "border-box";
        button.style.minWidth = "0";
        button.style.minHeight = "0";
        button.style.margin = "0";
        button.style.padding = "0";
        button.style.background = "transparent";
        button.style.border = "0";
        button.style.fontSize = `${Number(node.fontSize) || 9}px`;
        button.style.lineHeight = visualLineHeight(node.fontSize || 9);
        button.style.color = index === node.active ? node.activeColor : node.color;
        element.append(button);
      });
      return element;
    },
    switch(node) {
      const element = document.createElement("button");
      const thumb = document.createElement("span");
      element.type = "button";
      element.setAttribute("role", "switch");
      element.setAttribute("aria-checked", String(Boolean(node.checked)));
      thumb.style.display = "block";
      thumb.style.width = styleLength(node.height) || "28px";
      thumb.style.height = styleLength(node.height) || "28px";
      thumb.style.borderRadius = "50%";
      thumb.style.background = node.thumbColor;
      element.append(thumb);
      return element;
    }
  };

  function appendAlignedText(element, node) {
    if (node.verticalAlign === undefined) {
      element.textContent = node.text;
      return;
    }
    const span = document.createElement("span");
    span.style.display = "block";
    span.style.width = "100%";
    span.textContent = node.text;
    element.append(span);
  }

  function renderNode(node, preview = false) {
    const rendererKey = registryByType.get(node.type)?.renderKey;
    const renderer = designRenderers[rendererKey];
    if (!renderer) throw new Error(`Unsupported visual-editor node type: ${node.type}`);
    const element = renderer(node, preview);
    element.classList.add(`jf-visual-${node.type}`, "designer-node");
    element.dataset.nodeId = node.id;
    if (!preview && node.id === selectedId) element.classList.add("selected");
    applyCommonStyle(element, node);
    if (preview) {
      element.contentEditable = "false";
      element.tabIndex = -1;
      element.inert = true;
      return element;
    }
    // Use one pointer-drag path so the browser's native drag lifecycle cannot
    // interrupt the target calculation inside the webview.
    element.draggable = false;
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
    if (node.id !== model.root.id) {
      bindPointerDrag(element, { kind: "move", id: node.id }, nodeLabel(node));
    }
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
    element.addEventListener("contextmenu", (event) => openContextMenu(event, node.id));
    bindDropTarget(element, node);
    return element;
  }

  function captureCanvasScroll() {
    return new Map([...$("canvas").querySelectorAll(".designer-node")].map((element) =>
      [element.dataset.nodeId, { top: element.scrollTop, left: element.scrollLeft }]));
  }

  function restoreCanvasScroll(positions) {
    // Scroll is editor-only view state. Restore after attachment/layout so the
    // browser can clamp each offset if the content or viewport became smaller.
    $("canvas").querySelectorAll(".designer-node").forEach((element) => {
      const position = positions.get(element.dataset.nodeId);
      if (!position) return;
      element.scrollTop = position.top;
      element.scrollLeft = position.left;
    });
  }

  function renderCanvas(root = model.root, preview = false) {
    const canvas = $("canvas");
    const scrollPositions = captureCanvasScroll();
    canvas.replaceChildren(renderNode(root, preview));
    const round = model.viewport.shape === "round";
    canvas.classList.toggle("round", round);
    $("canvas-shell").classList.toggle("round", round);
    $("canvas-shell").style.width = `${model.viewport.width}px`;
    $("canvas-shell").style.height = `${model.viewport.height}px`;
    restoreCanvasScroll(scrollPositions);
    renderSelectionOverlay();
    $("device-caption").textContent = `${model.viewport.width} x ${model.viewport.height}${round ? " · round" : ""}`;
    requestAnimationFrame(applyZoom);
  }

  function bindCanvasDrop() {
    const canvas = $("canvas");
    canvas.addEventListener("dragover", (event) => {
      event.preventDefault();
      clearDropIndicators();
      if (!event.target.closest?.(".designer-node")) canvas.classList.add("drop-inside");
    });
    canvas.addEventListener("dragleave", (event) => {
      if (!canvas.contains(event.relatedTarget)) clearDropIndicators();
    });
    canvas.addEventListener("drop", (event) => {
      event.preventDefault();
      event.stopPropagation();
      const payload = decodeDrag(event);
      clearDropIndicators();
      const target = event.target.closest?.(".designer-node");
      const node = target?.dataset.nodeId ? find(target.dataset.nodeId) : undefined;
      performDrop(payload, node?.id || model.root.id, node ? dropMode(event, target, node) : "inside");
    });
  }

  function bindCanvasPan() {
    const wrap = $("canvas-wrap");
    wrap.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      if (event.target.closest?.("#canvas-floating-toolbar")) return;
      const onViewport = $("canvas-shell").contains(event.target);
      const blank = !onViewport && !event.target.closest?.(".designer-node");
      if (!blank && !(event.ctrlKey || event.metaKey) && onViewport) return;
      if (!blank && !(event.ctrlKey || event.metaKey)) return;
      event.preventDefault();
      let lastX = event.clientX;
      let lastY = event.clientY;
      const move = (moveEvent) => {
        panX += moveEvent.clientX - lastX;
        panY += moveEvent.clientY - lastY;
        lastX = moveEvent.clientX;
        lastY = moveEvent.clientY;
        persistUi();
        applyZoom();
      };
      const finish = () => {
        document.body.classList.remove("panning");
        document.removeEventListener("pointermove", move, true);
        document.removeEventListener("pointerup", finish, true);
        document.removeEventListener("pointercancel", finish, true);
        persistUi();
      };
      document.body.classList.add("panning");
      document.addEventListener("pointermove", move, true);
      document.addEventListener("pointerup", finish, true);
      document.addEventListener("pointercancel", finish, true);
    });
  }

  function renderSourceNotice() {
    const notice = $("source-notice");
    notice.hidden = !initial.sourceConflict;
    notice.replaceChildren();
    if (!initial.sourceConflict) return;
    const message = document.createElement("span");
    message.className = "source-notice-copy";
    message.textContent = t.sourceConflict;
    const diff = document.createElement("button");
    diff.type = "button";
    diff.className = "quiet source-notice-action";
    diff.textContent = t.viewDiff;
    diff.addEventListener("click", () => vscode.postMessage({ type: "show-source-diff" }));
    const restore = document.createElement("button");
    restore.type = "button";
    restore.className = "quiet source-notice-action";
    restore.textContent = t.restoreBackup;
    restore.addEventListener("click", () => vscode.postMessage({ type: "restore-backup" }));
    notice.append(message, diff, restore);
  }

  function renderPalette() {
    const groups = [...new Set(definitions.map((definition) => definition.group))];
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
        button.dataset.componentType = definition.type;
        button.draggable = false;
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
    for (const group of [...new Set(recipes.map((recipe) => recipe.group))]) {
      const section = document.createElement("section");
      section.className = "palette-group palette-recipes";
      const heading = document.createElement("h3");
      heading.className = "group-label";
      heading.textContent = t[group] || group;
      section.append(heading);
      recipes.filter((recipe) => recipe.group === group).forEach((recipe) => {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "palette-item recipe-item";
        button.dataset.recipeType = recipe.type;
        button.draggable = false;
        const icon = document.createElement("span");
        icon.className = "palette-icon";
        icon.textContent = recipe.icon;
        const copy = document.createElement("span");
        copy.className = "palette-copy";
        const strong = document.createElement("strong");
        strong.textContent = t[recipe.label] || recipe.type;
        const help = document.createElement("span");
        help.textContent = t[recipe.help] || "";
        copy.append(strong, help);
        button.append(icon, copy);
        button.title = help.textContent;
        bindPointerDrag(button, { kind: "recipe", type: recipe.type }, t[recipe.label] || recipe.type);
        button.addEventListener("click", (event) => { if (allowClick(event)) addRecipe(recipe); });
        section.append(button);
      });
      if (group === "wearableGroup") list.prepend(section); else list.append(section);
    }
  }

  function renderOutlineBranch(node, depth) {
    const branch = document.createElement("div");
    branch.className = "outline-branch";
    const row = document.createElement("div");
    row.className = `outline-row${node.id === selectedId ? " selected" : ""}`;
    row.style.setProperty("--depth", depth);
    row.dataset.nodeId = node.id;
    row.draggable = false;
    row.setAttribute("role", "treeitem");
    row.setAttribute("aria-level", String(depth + 1));
    row.setAttribute("aria-selected", String(node.id === selectedId));
    row.setAttribute("aria-label", nodeLabel(node));
    row.tabIndex = node.id === selectedId ? 0 : -1;
    const toggle = document.createElement("button");
    toggle.type = "button";
    toggle.className = "outline-toggle";
    const hasChildren = node.type === "container" && node.children.length > 0;
    toggle.textContent = hasChildren ? (collapsedNodes.has(node.id) ? "›" : "⌄") : "";
    toggle.disabled = !hasChildren;
    toggle.tabIndex = -1;
    toggle.setAttribute("aria-hidden", "true");
    if (hasChildren) row.setAttribute("aria-expanded", String(!collapsedNodes.has(node.id)));
    toggle.addEventListener("pointerdown", (event) => event.stopPropagation());
    toggle.addEventListener("click", (event) => {
      event.stopPropagation();
      if (collapsedNodes.has(node.id)) collapsedNodes.delete(node.id); else collapsedNodes.add(node.id);
      persistUi();
      renderOutline();
      focusOutlineRow(node.id);
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
    row.addEventListener("click", (event) => { if (allowClick(event)) selectNode(node.id, { focusOutline: true }); });
    row.addEventListener("contextmenu", (event) => openContextMenu(event, node.id));
    if (node.id !== model.root.id) {
      bindPointerDrag(row, { kind: "move", id: node.id }, nodeLabel(node));
    }
    row.addEventListener("dragstart", (event) => {
      event.stopPropagation();
      setDrag(event, { kind: "move", id: node.id });
    });
    bindDropTarget(row, node);
    branch.append(row);
    if (hasChildren && !collapsedNodes.has(node.id)) {
      const children = document.createElement("div");
      children.className = "outline-children";
      children.id = `outline-children-${node.id}`;
      children.setAttribute("role", "group");
      row.setAttribute("aria-owns", children.id);
      node.children.forEach((child) => children.append(renderOutlineBranch(child, depth + 1)));
      branch.append(children);
    }
    return branch;
  }

  function renderOutline() {
    const tree = $("outline-tree");
    const hadFocus = tree.contains(document.activeElement);
    tree.replaceChildren(renderOutlineBranch(model.root, 0));
    const rows = visibleOutlineRows();
    const visibleIds = new Set(rows.map((row) => row.dataset.nodeId));
    // A selected canvas node can be inside a collapsed branch. Keep a Tab entry.
    const entry = pathNodes(selectedId).reverse().find((node) => visibleIds.has(node.id));
    rows.forEach((row) => { row.tabIndex = row.dataset.nodeId === entry?.id ? 0 : -1; });
    if (hadFocus) focusOutlineRow(entry?.id);
  }

  function visibleOutlineRows() {
    return [...document.querySelectorAll("#outline-tree .outline-row")];
  }

  function focusOutlineRow(id = selectedId) {
    const rows = visibleOutlineRows();
    const row = rows.find((candidate) => candidate.dataset.nodeId === id);
    if (row) rows.forEach((candidate) => { candidate.tabIndex = candidate === row ? 0 : -1; });
    row?.focus();
  }

  function moveOutlineSelection(id) {
    if (!id || !find(id)) return;
    selectNode(id, { focusOutline: true });
  }

  function handleOutlineKeydown(event) {
    if (event.altKey || event.ctrlKey || event.metaKey || event.shiftKey || event.isComposing) return false;
    const row = event.target.closest?.("#outline-tree .outline-row");
    if (!row || event.target.closest("input, select, textarea")) return false;
    const node = find(row.dataset.nodeId);
    if (!node) return false;
    const rows = visibleOutlineRows();
    const index = rows.indexOf(row);
    if (index < 0) return false;
    const hasChildren = node.type === "container" && node.children.length > 0;
    if (event.key === "Home" || event.key === "End") {
      moveOutlineSelection(rows[event.key === "Home" ? 0 : rows.length - 1].dataset.nodeId);
      event.preventDefault();
      return true;
    }
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      const nextIndex = event.key === "ArrowDown" ? index + 1 : index - 1;
      if (rows[nextIndex]) moveOutlineSelection(rows[nextIndex].dataset.nodeId);
      event.preventDefault();
      return true;
    }
    if (event.key === "ArrowRight") {
      if (hasChildren && collapsedNodes.has(node.id)) {
        collapsedNodes.delete(node.id);
        persistUi();
        renderAll();
        focusOutlineRow(node.id);
      } else if (hasChildren) {
        moveOutlineSelection(node.children[0].id);
      }
      event.preventDefault();
      return true;
    }
    if (event.key === "ArrowLeft") {
      if (hasChildren && !collapsedNodes.has(node.id)) {
        collapsedNodes.add(node.id);
        persistUi();
        renderAll();
        focusOutlineRow(node.id);
      } else {
        const parent = parentOf(node.id);
        if (parent) moveOutlineSelection(parent.id);
      }
      event.preventDefault();
      return true;
    }
    if (event.key === "Enter" || event.key === " ") {
      moveOutlineSelection(node.id);
      event.preventDefault();
      return true;
    }
    return false;
  }

  function section(title) {
    const element = document.createElement("section");
    element.className = "inspector-section";
    const heading = document.createElement("h3");
    heading.textContent = title;
    element.append(heading);
    return element;
  }

  function typedValue(node, key, rawValue) {
    const field = registryByType.get(node.type)?.fields?.find((candidate) => candidate.key === key);
    if (!field) return { value: rawValue };
    if (field.kind === "number") {
      const value = Number(rawValue);
      if (!Number.isFinite(value)) return { error: t.invalidValue };
      const dynamicMax = key === "selected" && Array.isArray(node.options)
        ? node.options.length - 1
        : key === "active" && Array.isArray(node.items)
          ? node.items.length - 1
          : field.max;
      const bounded = clamp(value, field.min ?? -Infinity, dynamicMax ?? Infinity);
      return { value: field.integer ? Math.round(bounded) : bounded };
    }
    if (field.kind === "enum") {
      const value = String(rawValue);
      return field.values?.includes(value) ? { value } : { error: t.invalidValue };
    }
    if (field.kind === "boolean") {
      return typeof rawValue === "boolean" ? { value: rawValue } : { error: t.invalidValue };
    }
    if (field.kind === "string-list") {
      if (!Array.isArray(rawValue) || rawValue.length < field.minItems || rawValue.length > field.maxItems ||
          rawValue.some((item) => typeof item !== "string" || item.length > field.maxLength || /[{};\r\n]/.test(item))) {
        return { error: t.invalidValue };
      }
      return { value: rawValue.map((item) => item.trim()) };
    }
    if (field.kind === "length") {
      const value = String(rawValue ?? "auto").trim();
      return /^(?:auto|0|[0-9]+(?:\.[0-9]+)?(?:px|%))$/.test(value)
        ? { value }
        : { error: t.invalidLength };
    }
    if (field.kind === "color") {
      const value = String(rawValue ?? "").trim();
      return value && !/[{};\r\n]/.test(value) ? { value } : { error: t.invalidValue };
    }
    return { value: String(rawValue ?? "") };
  }

  function commitValue(node, key, rawValue, validate) {
    const typed = typedValue(node, key, rawValue);
    if (typed.error) return typed.error;
    const value = typed.value;
    const error = validate?.(value);
    if (error) return error;
    const changes = [[key, value]];
    if (key === "padding" && node.type === "container") {
      changes.push(["paddingX", value], ["paddingY", value]);
    } else if (key === "options" && node.type === "select") {
      changes.push(["selected", clamp(Number(node.selected) || 0, 0, Math.max(0, value.length - 1))]);
    } else if (key === "items" && node.type === "navigation") {
      changes.push(["active", clamp(Number(node.active) || 0, 0, Math.max(0, value.length - 1))]);
    }
    if (changes.every(([changeKey, changeValue]) => node[changeKey] === changeValue)) return undefined;
    snapshot();
    const oldId = node.id;
    changes.forEach(([changeKey, changeValue]) => { node[changeKey] = changeValue; });
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

  function stringListField(node, field, label) {
    const row = fieldRow(label);
    row.classList.add("field-list");
    const list = document.createElement("div");
    list.className = "string-list-control";
    const values = Array.isArray(node[field.key]) ? node[field.key] : [];
    const commitItems = (next) => {
      const typed = typedValue(node, field.key, next);
      if (typed.error) return showFieldError(row, typed.error);
      showFieldError(row);
      commitValue(node, field.key, typed.value);
    };
    values.forEach((value, index) => {
      const item = document.createElement("div");
      item.className = "string-list-item";
      const input = document.createElement("input");
      input.type = "text";
      input.maxLength = String(field.maxLength);
      input.value = value;
      input.addEventListener("change", () => {
        const next = values.slice();
        next[index] = input.value;
        commitItems(next);
      });
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "icon-button quiet list-item-remove";
      remove.textContent = "×";
      remove.title = t.removeItem;
      remove.setAttribute("aria-label", t.removeItem);
      remove.disabled = values.length <= field.minItems;
      remove.addEventListener("click", () => commitItems(values.filter((_, itemIndex) => itemIndex !== index)));
      item.append(input, remove);
      list.append(item);
    });
    const add = document.createElement("button");
    add.type = "button";
    add.className = "quiet list-item-add";
    add.textContent = `+ ${t.addItem}`;
    add.disabled = values.length >= field.maxItems;
    add.addEventListener("click", () => commitItems([...values, ""]));
    list.append(add);
    row.append(list);
    return row;
  }

  function booleanField(node, key, label) {
    const row = fieldRow(label);
    const toggle = document.createElement("button");
    toggle.type = "button";
    toggle.className = "inspector-toggle";
    toggle.setAttribute("role", "switch");
    toggle.setAttribute("aria-checked", String(Boolean(node[key])));
    toggle.textContent = node[key] ? (initial.chinese ? "开" : "On") : (initial.chinese ? "关" : "Off");
    toggle.addEventListener("click", () => commitValue(node, key, !node[key]));
    row.append(toggle);
    return row;
  }

  function numberField(node, key, label, minimum, maximum) {
    const row = fieldRow(label);
    const input = document.createElement("input");
    input.type = "number";
    input.min = String(minimum);
    input.max = String(maximum);
    input.step = "1";
    input.value = String(fieldValue(node, key) ?? minimum);
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
    select.value = fieldValue(node, key);
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
      button.className = fieldValue(node, key) === value ? "active" : "";
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
    input.addEventListener("change", () => {
      const error = commitValue(node, key, input.value.trim() || "transparent");
      showFieldError(row, error);
    });
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

  function registryChoiceLabel(value) {
    const labels = {
      "space-between": t.between,
      "space-around": t.around
    };
    return labels[value] || t[value] || value;
  }

  function fieldValue(node, key) {
    const field = registryByType.get(node.type)?.fields.find((candidate) => candidate.key === key);
    return node[key] ?? node[field?.fallbackKey] ?? field?.default;
  }

  function registryField(node, field) {
    let label = t[field.label] || field.label || field.key;
    if (node.type === "container" && (field.key === "align" || field.key === "justify")) {
      const horizontal = (node.layout === "row") === (field.key === "justify");
      label = horizontal ? t.horizontalChildren : t.verticalChildren;
    }
    if (field.kind === "text") return textField(node, field.key, label);
    if (field.kind === "number") return numberField(node, field.key, label, field.min ?? 0, field.max ?? 100);
    if (field.kind === "length") return lengthField(node, field.key, label);
    if (field.kind === "color") return colorField(node, field.key, label);
    if (field.kind === "resource") return resourceField(node);
    if (field.kind === "string-list") return stringListField(node, field, label);
    if (field.kind === "boolean") return booleanField(node, field.key, label);
    if (field.kind === "enum") {
      const vertical = field.key === "verticalAlign" || label === t.verticalChildren;
      const horizontal = label === t.horizontalChildren;
      const choices = (field.values || []).map((value) => {
        let name = registryChoiceLabel(value);
        if (value === "start" || value === "end") {
          if (vertical) name = value === "start" ? (initial.chinese ? "上" : "Top") : (initial.chinese ? "下" : "Bottom");
          if (horizontal) name = value === "start" ? t.left : t.right;
        }
        return [value, name];
      });
      return field.control === "segmented"
        ? segmentedField(node, field.key, label, choices)
        : selectField(node, field.key, label, choices);
    }
    return undefined;
  }

  function defaultEventFor(node) {
    if (node.type === "input" || node.type === "select" || node.type === "switch") return "change";
    return "click";
  }

  function eventSkeleton(node, eventName) {
    return `document.getElementById(${JSON.stringify(node.id)})?.addEventListener(${JSON.stringify(eventName)}, (event) => {\n  // Update local App state here.\n});\n`;
  }

  function interactionSection(node) {
    const element = section(t.interaction);
    const listeners = Array.isArray(initial.interactions?.[node.id]) ? initial.interactions[node.id] : [];
    const heading = document.createElement("div");
    heading.className = "interaction-heading";
    heading.textContent = t.detectedListeners;
    element.append(heading);
    if (listeners.length) {
      const list = document.createElement("ul");
      list.className = "listener-list";
      listeners.forEach((listener) => {
        const item = document.createElement("li");
        const eventName = document.createElement("code");
        eventName.textContent = listener.event;
        const source = document.createElement("span");
        source.textContent = listener.source;
        item.append(eventName, source);
        list.append(item);
      });
      element.append(list);
    } else {
      const empty = document.createElement("p");
      empty.className = "interaction-empty";
      empty.textContent = t.noListeners;
      element.append(empty);
    }
    const copy = document.createElement("button");
    copy.type = "button";
    copy.className = "quiet interaction-copy";
    copy.textContent = t.copyEventSkeleton;
    copy.addEventListener("click", () => {
      vscode.postMessage({ type: "copy-event-skeleton", code: eventSkeleton(node, defaultEventFor(node)) });
    });
    const help = document.createElement("p");
    help.className = "interaction-help";
    help.textContent = t.eventSkeletonHelp;
    element.append(copy, help);
    return element;
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

    const groups = new Map();
    for (const field of registryByType.get(node.type)?.fields || []) {
      const control = registryField(node, field);
      if (!control) continue;
      if (!groups.has(field.group)) groups.set(field.group, []);
      groups.get(field.group).push(control);
    }
    for (const [group, controls] of groups) {
      const element = section(t[group] || group);
      element.append(...controls);
      body.append(element);
    }
    body.append(interactionSection(node));
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
    if (resizeSession) { resizeSession.cancel(); return; }
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
      const style = getComputedStyle(wrap);
      const horizontalPadding = parseFloat(style.paddingLeft) + parseFloat(style.paddingRight);
      const verticalPadding = parseFloat(style.paddingTop) + parseFloat(style.paddingBottom);
      scale = Math.min(1,
        (wrap.clientWidth - horizontalPadding) / model.viewport.width,
        (wrap.clientHeight - verticalPadding) / model.viewport.height);
    }
    scale = clamp(Number(scale) || 1, 0.2, 2);
    wrap.style.setProperty("--pan-x", `${panX}px`);
    wrap.style.setProperty("--pan-y", `${panY}px`);
    $("canvas-shell").style.transform = `translate3d(${panX}px, ${panY}px, 0) scale(${scale})`;
    $("zoom-label").textContent = `${Math.round(scale * 100)}%`;
    renderSelectionOverlay();
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
    resizeSession?.cancel();
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
    $("runtime-state").textContent = t.runtimeIdle;
    $("components-tab").textContent = t.components;
    $("outline-tab").textContent = t.outline;
    $("viewport-label").textContent = t.viewport;
    const modelViewportOption = $("viewport-preset").querySelector("option[value=model]");
    modelViewportOption.textContent = initial.chinese ? "App 声明尺寸" : "App declared size";
    modelViewportOption.title = `${t.modelViewport} · ${model.viewport.width} x ${model.viewport.height}`;
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
    const canvasToolLabels = {
      "canvas-tool-undo": t.undo,
      "canvas-tool-redo": t.redo,
      "canvas-tool-fit": t.fit,
      "canvas-tool-zoom-out": initial.chinese ? "缩小" : "Zoom out",
      "canvas-tool-zoom-in": initial.chinese ? "放大" : "Zoom in",
      "canvas-tool-outline": initial.chinese ? "显示结构" : "Show structure",
      "canvas-tool-save": t.save
    };
    Object.entries(canvasToolLabels).forEach(([id, label]) => {
      $(id).title = label;
      $(id).setAttribute("aria-label", label);
    });
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
  $("canvas-tool-undo").addEventListener("click", undo);
  $("canvas-tool-redo").addEventListener("click", redo);
  $("canvas-tool-fit").addEventListener("click", () => { zoom = "fit"; persistUi(); applyZoom(); });
  $("canvas-tool-zoom-out").addEventListener("click", () => adjustZoom(-0.1));
  $("canvas-tool-zoom-in").addEventListener("click", () => adjustZoom(0.1));
  $("canvas-tool-outline").addEventListener("click", () => {
    leftCollapsed = false;
    setPanel("outline");
    persistUi();
    renderPanels();
    if (zoom === "fit") applyZoom();
  });
  $("canvas-tool-save").addEventListener("click", () => requestSave(false));
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
  bindCanvasPan();
  new ResizeObserver(() => { if (zoom === "fit") applyZoom(); }).observe($("canvas-wrap"));

  document.addEventListener("pointerdown", (event) => {
    resizeSession?.cancel();
    if (contextMenu && !contextMenu.contains(event.target)) closeContextMenu();
  }, true);
  document.addEventListener("scroll", (event) => {
    if (!contextMenu?.contains(event.target)) closeContextMenu();
  }, true);
  $("canvas-wrap").addEventListener("scroll", renderSelectionOverlay, true);

  document.addEventListener("keydown", (event) => {
    if (resizeSession) {
      if (event.key === "Escape") resizeSession.cancel();
      event.preventDefault();
      return;
    }
    const editing = ["INPUT", "SELECT", "TEXTAREA"].includes(document.activeElement?.tagName)
      || document.activeElement?.isContentEditable;
    if (event.key === "ContextMenu" || (event.shiftKey && event.key === "F10")) {
      const target = event.target.closest?.(".outline-row, .designer-node");
      if (target) {
        const bounds = target.getBoundingClientRect();
        openContextMenu({ target, clientX: bounds.left, clientY: bounds.top,
          preventDefault: () => event.preventDefault(), stopPropagation: () => event.stopPropagation() }, target.dataset.nodeId);
        return;
      }
    }
    if (event.key === "Escape" && contextMenu) {
      closeContextMenu();
      event.preventDefault();
      return;
    }
    if (!editing && handleOutlineKeydown(event)) return;
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
      initial.interactions = message.interactions || initial.interactions || {};
      setSaveState("saved", t.saved);
      renderAll();
      if (message.debug) vscode.postMessage({ type: "debug" });
    } else if (message.type === "runtime-state") {
      const labels = {
        running: t.runtimeRunning,
        reporting: t.runtimeReporting,
        stopped: t.runtimeStopped
      };
      const state = labels[message.state] ? message.state : "idle";
      $("runtime-state").textContent = labels[state] || t.runtimeIdle;
      $("runtime-state").dataset.runtimeState = state;
    } else if (message.type === "save-cancelled") {
      saving = false;
      setSaveState("dirty", t.saveCancelled);
      renderAll();
    } else if (message.type === "model-check-result") {
      if (message.revision !== modelCheckRevision) return;
      if (message.ok) {
        document.body.dataset.modelCheck = "ok";
        if (dirty && !saving) report(t.checked, "dirty");
      } else {
        document.body.dataset.modelCheck = "error";
        setSaveState("error", `${t.checkError}: ${message.message || "unknown error"}`);
        renderAll();
      }
    } else if (message.type === "source-conflict") {
      initial.sourceConflict = Boolean(message.conflict);
      if (message.interactions) initial.interactions = message.interactions;
      renderSourceNotice();
      if (initial.sourceConflict && !saving) setSaveState("dirty", t.sourceConflict);
      renderInspector();
    } else if (message.type === "interactions") {
      initial.interactions = message.interactions || {};
      renderInspector();
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
