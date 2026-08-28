"use strict";

const fs = require("fs");
const path = require("path");
const vscode = require("vscode");
const {
  BODY_START,
  MAX_NODES,
  createDefaultModel,
  updateCss,
  updateHtml,
  validateModel,
  walkNodes
} = require("./visual_editor_model");

const MODEL_FILE = path.join(".jellyframe", "visual-editor.json");

function isChinese() {
  return /^zh(?:-|$)/i.test(vscode.env.language || "");
}

function readJson(filename) {
  try {
    return JSON.parse(fs.readFileSync(filename, "utf8"));
  } catch (error) {
    if (fs.existsSync(filename)) {
      throw new Error(`${path.basename(filename)} contains invalid JSON: ${error.message}`);
    }
    return undefined;
  }
}

function isPathInside(root, candidate) {
  const relative = path.relative(path.resolve(root), path.resolve(candidate));
  return relative !== "" && relative !== ".." && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
}

function stylesheetHrefs(html) {
  const hrefs = [];
  for (const match of html.matchAll(/<link\b[^>]*>/gi)) {
    const tag = match[0];
    const rel = /\brel\s*=\s*(["'])(.*?)\1/i.exec(tag)?.[2] || "";
    if (!rel.split(/\s+/).some((value) => value.toLowerCase() === "stylesheet")) continue;
    const href = /\bhref\s*=\s*(["'])(.*?)\1/i.exec(tag)?.[2];
    if (href) hrefs.push(href);
  }
  return hrefs;
}

function appFiles(root) {
  const manifestPath = path.join(root, "jellyframe.app.json");
  const legacyPath = path.join(root, "app.json");
  const actualManifest = fs.existsSync(manifestPath) ? manifestPath : legacyPath;
  if (!fs.existsSync(actualManifest)) {
    throw new Error(isChinese() ? "当前目录不包含 JellyFrame App manifest。" : "The selected directory does not contain a JellyFrame App manifest.");
  }
  const manifest = readJson(actualManifest) || {};
  const entryValue = String(manifest.entry || "/index.html").replace(/^[/\\]+/, "");
  const entryPath = path.resolve(root, entryValue);
  if (!isPathInside(root, entryPath) || !fs.existsSync(entryPath)) {
    throw new Error(isChinese() ? "App manifest 的入口文件无效。" : "The App manifest entry is invalid.");
  }
  const html = fs.readFileSync(entryPath, "utf8");
  const stylesheetHref = stylesheetHrefs(html).find((href) => {
    if (/^(?:[a-z]+:|\/\/)/i.test(href)) return false;
    return isPathInside(root, path.resolve(path.dirname(entryPath), href));
  }) || "styles/app.css";
  const stylesheetPath = path.resolve(path.dirname(entryPath), stylesheetHref);
  if (!isPathInside(root, stylesheetPath)) {
    throw new Error(isChinese() ? "App 样式表必须位于包目录内。" : "The App stylesheet must be inside the package root.");
  }
  return { manifest, manifestPath: actualManifest, entryPath, stylesheetPath, stylesheetHref, html };
}

function ensureStylesheet(html, href) {
  const normalized = href.replace(/\\/g, "/").replace(/^\.\//, "");
  if (stylesheetHrefs(html).some((value) => value.replace(/\\/g, "/").replace(/^\.\//, "") === normalized)) return html;
  const link = `  <link rel="stylesheet" href="${href.replace(/\\/g, "/")}">\n`;
  return /<\/head\s*>/i.test(html) ? html.replace(/<\/head\s*>/i, `${link}</head>`) : html;
}

function timestamp() {
  return new Date().toISOString().replace(/[:.]/g, "-");
}

function backupSources(root, files, html, css) {
  const directory = path.join(root, ".jellyframe", "visual-editor-backups", timestamp());
  fs.mkdirSync(directory, { recursive: true });
  fs.writeFileSync(path.join(directory, path.basename(files.entryPath)), html, "utf8");
  if (css !== undefined) fs.writeFileSync(path.join(directory, path.basename(files.stylesheetPath)), css, "utf8");
  return directory;
}

function workspaceText(filename, fallback = "") {
  const resolved = path.resolve(filename);
  const document = vscode.workspace.textDocuments.find((candidate) => path.resolve(candidate.uri.fsPath) === resolved);
  if (document) return document.getText();
  return fs.existsSync(filename) ? fs.readFileSync(filename, "utf8") : fallback;
}

async function replaceDocuments(entries) {
  const documents = await Promise.all(entries.map(async ([filename]) => vscode.workspace.openTextDocument(vscode.Uri.file(filename))));
  const edit = new vscode.WorkspaceEdit();
  for (let index = 0; index < entries.length; index += 1) {
    const document = documents[index];
    const end = document.lineAt(document.lineCount - 1).range.end;
    edit.replace(document.uri, new vscode.Range(new vscode.Position(0, 0), end), entries[index][1]);
  }
  if (!await vscode.workspace.applyEdit(edit)) {
    throw new Error("VS Code refused to update the visual-editor source files");
  }
  const saved = await Promise.all(documents.map((document) => document.save()));
  if (saved.some((outcome) => !outcome)) throw new Error("VS Code could not save every visual-editor source file");
}

function validatePackageAssets(root, model) {
  walkNodes(model.root, (node) => {
    if (node.type !== "image") return;
    const filename = path.resolve(root, String(node.src).replace(/^[/\\]+/, ""));
    if (!isPathInside(root, filename) || !fs.existsSync(filename) || !fs.statSync(filename).isFile()) {
      throw new Error(isChinese()
        ? `图片 ${node.id} 必须引用当前 App 包内已存在的文件。`
        : `Image ${node.id} must reference an existing file inside the current App package.`);
    }
  });
}

async function saveModel(root, files, model, takeoverConfirmed) {
  validateModel(model);
  validatePackageAssets(root, model);
  const currentHtml = workspaceText(files.entryPath);
  const currentCss = workspaceText(files.stylesheetPath);
  let confirmed = takeoverConfirmed;
  if (!currentHtml.includes(BODY_START) && !confirmed) {
    const accept = isChinese() ? "初始化可视化编辑" : "Initialize visual editing";
    const choice = await vscode.window.showWarningMessage(
      isChinese()
        ? "首次保存将由可视化编辑器接管入口页面的 body，并在 .jellyframe/visual-editor-backups 中保存原页面与样式。脚本文件不会被修改。"
        : "The first save lets the visual editor own the entry-page body and stores the original page and stylesheet under .jellyframe/visual-editor-backups. Script files are not modified.",
      { modal: true }, accept);
    if (choice !== accept) return { saved: false, takeoverConfirmed: false };
    const backup = backupSources(root, files, currentHtml, currentCss);
    vscode.window.showInformationMessage(isChinese() ? `原页面已备份：${backup}` : `Original page backed up: ${backup}`);
    confirmed = true;
  }

  const html = updateHtml(ensureStylesheet(currentHtml, files.stylesheetHref), model);
  const css = updateCss(currentCss);
  fs.mkdirSync(path.dirname(files.stylesheetPath), { recursive: true });
  if (!fs.existsSync(files.stylesheetPath)) fs.writeFileSync(files.stylesheetPath, "", "utf8");
  const modelPath = path.join(root, MODEL_FILE);
  fs.mkdirSync(path.dirname(modelPath), { recursive: true });
  if (!fs.existsSync(modelPath)) fs.writeFileSync(modelPath, "", "utf8");
  await replaceDocuments([
    [files.entryPath, html],
    [files.stylesheetPath, css],
    [modelPath, `${JSON.stringify(model, null, 2)}\n`]
  ]);
  return { saved: true, takeoverConfirmed: confirmed };
}

function initialModel(root, files) {
  const stored = readJson(path.join(root, MODEL_FILE));
  if (stored) return validateModel(stored);
  const viewport = files.manifest.viewport || Object.values(files.manifest.targets || {})[0]?.viewport || {};
  return createDefaultModel(viewport, files.manifest.name || path.basename(root));
}

function assetMap(webview, root, model) {
  const assets = {};
  walkNodes(model.root, (node) => {
    if (node.type !== "image" || typeof node.src !== "string") return;
    const relative = node.src.replace(/^[/\\]+/, "");
    const filename = path.resolve(root, relative);
    if (isPathInside(root, filename) && fs.existsSync(filename)) {
      assets[node.src] = webview.asWebviewUri(vscode.Uri.file(filename)).toString();
    }
  });
  return assets;
}

function nonce() {
  return `${Date.now()}${Math.random().toString(16).slice(2)}`;
}

function visualEditorHtml(webview, root, model, assets) {
  const token = nonce();
  const payload = JSON.stringify({ model, assets, chinese: isChinese(), appName: path.basename(root), maxNodes: MAX_NODES }).replace(/</g, "\\u003c");
  return `<!doctype html>
<html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src ${webview.cspSource} data:; style-src 'unsafe-inline'; script-src 'nonce-${token}';">
<style>
* { box-sizing: border-box; }
html, body { width: 100%; height: 100%; margin: 0; overflow: hidden; }
body { color: var(--vscode-foreground); background: var(--vscode-editor-background); font: 13px var(--vscode-font-family); display: grid; grid-template-rows: 42px minmax(0,1fr) 26px; }
button, input, select { font: inherit; }
button { color: var(--vscode-button-foreground); background: var(--vscode-button-background); border: 1px solid var(--vscode-button-border, transparent); min-height: 28px; cursor: pointer; }
button:hover { background: var(--vscode-button-hoverBackground); }
button.secondary { color: var(--vscode-foreground); background: var(--vscode-input-background); border-color: var(--vscode-widget-border); }
button.icon { width: 30px; padding: 0; font-size: 16px; }
header { display: flex; align-items: center; gap: 8px; padding: 0 10px; border-bottom: 1px solid var(--vscode-panel-border); }
#app-name { font-weight: 600; margin-right: auto; }
#workspace { min-width: 0; min-height: 0; display: grid; grid-template-columns: 210px minmax(280px,1fr) 260px; }
aside { min-width: 0; overflow: auto; background: var(--vscode-sideBar-background); }
#palette { border-right: 1px solid var(--vscode-panel-border); }
#inspector { border-left: 1px solid var(--vscode-panel-border); }
.panel-title { height: 34px; display: flex; align-items: center; padding: 0 12px; font-weight: 600; border-bottom: 1px solid var(--vscode-panel-border); position: sticky; top: 0; background: var(--vscode-sideBar-background); z-index: 2; }
.palette-list { padding: 8px; display: grid; grid-template-columns: 1fr 1fr; gap: 6px; }
.palette-item { min-height: 54px; text-align: left; padding: 7px 8px; color: var(--vscode-foreground); background: var(--vscode-list-inactiveSelectionBackground); border: 1px solid transparent; }
.palette-item:hover { border-color: var(--vscode-focusBorder); background: var(--vscode-list-hoverBackground); }
.palette-item strong, .palette-item span { display: block; }
.palette-item span { margin-top: 3px; color: var(--vscode-descriptionForeground); font-size: 11px; }
#stage { min-width: 0; min-height: 0; display: grid; grid-template-rows: 34px minmax(0,1fr); background: var(--vscode-editor-background); }
#stage-toolbar { display: flex; align-items: center; gap: 5px; padding: 0 8px; border-bottom: 1px solid var(--vscode-panel-border); }
#viewport-preset { margin-right: auto; }
select, input[type=text], input[type=number] { min-width: 0; height: 28px; color: var(--vscode-input-foreground); background: var(--vscode-input-background); border: 1px solid var(--vscode-input-border, var(--vscode-widget-border)); padding: 3px 6px; }
#canvas-wrap { min-width: 0; min-height: 0; overflow: auto; display: flex; align-items: center; justify-content: center; padding: 24px; background-image: linear-gradient(45deg, var(--vscode-editorWidget-background) 25%, transparent 25%), linear-gradient(-45deg, var(--vscode-editorWidget-background) 25%, transparent 25%), linear-gradient(45deg, transparent 75%, var(--vscode-editorWidget-background) 75%), linear-gradient(-45deg, transparent 75%, var(--vscode-editorWidget-background) 75%); background-size: 20px 20px; background-position: 0 0, 0 10px, 10px -10px, -10px 0; }
#canvas-shell { flex: 0 0 auto; transform-origin: center; box-shadow: 0 8px 28px rgba(0,0,0,.35); }
#canvas { width: 100%; height: 100%; overflow: hidden; background: #0d141b; color: #f4f7fb; }
#canvas.round { border-radius: 50%; }
.designer-node { position: relative; }
.designer-node.selected { outline: 2px solid #36a3ff; outline-offset: -2px; }
.designer-node.drop-target { outline: 2px dashed #20b486; outline-offset: -4px; }
.image-placeholder { display: flex; align-items: center; justify-content: center; background: #252f3a; color: #9aa9b8; font-size: 12px; }
.progress-fill { display: block; height: 100%; border-radius: inherit; }
#inspector-body { padding: 10px 12px 18px; }
.field { margin-bottom: 10px; }
.field label { display: block; margin-bottom: 4px; color: var(--vscode-descriptionForeground); font-size: 11px; }
.field input, .field select { width: 100%; }
.field.color-row { display: grid; grid-template-columns: 32px 1fr; gap: 6px; }
.field.color-row label { grid-column: 1 / -1; }
.field input[type=color] { width: 32px; padding: 2px; }
#node-actions { display: flex; gap: 5px; margin-bottom: 12px; }
#node-actions button { flex: 1; }
.empty { padding: 16px 12px; color: var(--vscode-descriptionForeground); line-height: 1.5; }
footer { display: flex; align-items: center; padding: 0 10px; border-top: 1px solid var(--vscode-panel-border); color: var(--vscode-descriptionForeground); font-size: 11px; }
#status { margin-right: auto; }
#selection-path { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
</style></head><body>
<header><span id="app-name"></span><button id="undo" class="secondary icon" title="Undo">↶</button><button id="redo" class="secondary icon" title="Redo">↷</button><button id="actual" class="secondary"></button><button id="save"></button></header>
<div id="workspace"><aside id="palette"><div class="panel-title" id="elements-title"></div><div class="palette-list" id="palette-list"></div></aside>
<main id="stage"><div id="stage-toolbar"><select id="viewport-preset"><option value="model"></option><option value="round-300">300 × 300 · Round</option><option value="rect-172x320">172 × 320</option><option value="rect-320x240">320 × 240</option></select><button id="zoom-out" class="secondary icon" title="Zoom out">−</button><button id="zoom-fit" class="secondary">Fit</button><button id="zoom-in" class="secondary icon" title="Zoom in">+</button><span id="zoom-label">100%</span></div><div id="canvas-wrap"><div id="canvas-shell"><div id="canvas"></div></div></div></main>
<aside id="inspector"><div class="panel-title" id="properties-title"></div><div id="inspector-body"></div></aside></div>
<footer><span id="status"></span><span id="selection-path"></span></footer>
<script nonce="${token}">const initial=${payload};
const vscode=acquireVsCodeApi(); let model=structuredClone(initial.model); let selectedId=model.root.id; let history=[]; let future=[]; let zoom='fit'; let dirty=false; const assets={...initial.assets};
const text=initial.chinese?{elements:'元素',properties:'属性',save:'保存源码',actual:'保存并实际调试',status:'受 JellyFrame 支持范围约束',select:'选择一个元素以编辑属性',delete:'删除',duplicate:'复制',up:'上移',down:'下移',root:'页面',container:'容器',text:'文本',button:'按钮',image:'图片',input:'输入框',progress:'进度条',layout:'布局',content:'内容',appearance:'外观',browse:'选择图片',saved:'已保存',saving:'正在保存…',unsaved:'有未保存修改',nodeLimit:'元素数量已达到上限',invalidId:'ID 须以字母开头，且只能包含字母、数字、下划线或连字符',duplicateId:'ID 已被其他元素使用'}:{elements:'Elements',properties:'Properties',save:'Save source',actual:'Save & debug',status:'Bounded to JellyFrame capabilities',select:'Select an element to edit its properties',delete:'Delete',duplicate:'Duplicate',up:'Move up',down:'Move down',root:'Page',container:'Container',text:'Text',button:'Button',image:'Image',input:'Input',progress:'Progress',layout:'Layout',content:'Content',appearance:'Appearance',browse:'Choose image',saved:'Saved',saving:'Saving…',unsaved:'Unsaved changes',nodeLimit:'The element limit has been reached',invalidId:'IDs must start with a letter and contain only letters, numbers, underscores, or hyphens',duplicateId:'This ID is already used by another element'};
const definitions=initial.chinese?[['container',text.container,'横向或纵向弹性布局'],['text',text.text,'标题、标签或段落'],['button',text.button,'可绑定事件的操作控件'],['image',text.image,'App 包内的图片资源'],['input',text.input,'文本输入控件'],['progress',text.progress,'0 到 100 的进度指示']]:[['container',text.container,'Flex row or column'],['text',text.text,'Label or paragraph'],['button',text.button,'Clickable control'],['image',text.image,'Package-local image'],['input',text.input,'Text input'],['progress',text.progress,'Bounded indicator']];
function clone(value){return structuredClone(value)} function walk(node,fn,parent){fn(node,parent);(node.children||[]).forEach(child=>walk(child,fn,node))} function find(id){let hit;walk(model.root,n=>{if(n.id===id)hit=n});return hit} function parentOf(id){let hit;walk(model.root,(n,p)=>{if(n.id===id)hit=p});return hit}
function snapshot(){history.push(JSON.stringify(model));if(history.length>80)history.shift();future=[]} function changed(){dirty=true;document.getElementById('status').textContent=text.unsaved;renderAll()}
function uniqueId(prefix){const ids=new Set();walk(model.root,n=>ids.add(n.id));let i=1;while(ids.has(prefix+'-'+i))i++;return prefix+'-'+i}
function makeNode(type){const id=uniqueId(type);const base={id,type};if(type==='container')return {...base,layout:'column',gap:8,padding:10,width:'100%',height:'96px',background:'transparent',radius:0,align:'stretch',justify:'start',children:[]};if(type==='text')return {...base,text:'Text',fontSize:18,color:'#f4f7fb',weight:'normal',align:'left',width:'auto'};if(type==='button')return {...base,text:'Button',width:'100%',height:'44px',background:'#20b486',color:'#071712',radius:6};if(type==='image')return {...base,src:'',alt:'',width:'100%',height:'96px',fit:'cover',radius:6};if(type==='input')return {...base,placeholder:'Input',value:'',width:'100%',height:'40px',background:'#18212b',color:'#f4f7fb',radius:4};return {...base,value:50,width:'100%',height:'12px',track:'#26313d',fill:'#ffb84d',radius:6}}
 function nodeCount(node){let count=0;walk(node,()=>count++);return count} function report(message){document.getElementById('status').textContent=message}
 function add(type,targetId){if(nodeCount(model.root)>=initial.maxNodes){report(text.nodeLimit);return}snapshot();const target=find(targetId);const container=target?.type==='container'?target:parentOf(targetId)||model.root;const node=makeNode(type);container.children.push(node);selectedId=node.id;changed()}
function removeSelected(){if(selectedId===model.root.id)return;snapshot();const parent=parentOf(selectedId);parent.children=parent.children.filter(n=>n.id!==selectedId);selectedId=parent.id;changed()}
 function duplicateSelected(){const node=find(selectedId);const parent=parentOf(selectedId);if(!node||!parent)return;if(nodeCount(model.root)+nodeCount(node)>initial.maxNodes){report(text.nodeLimit);return}snapshot();const reserved=new Set();walk(model.root,n=>reserved.add(n.id));const next=prefix=>{let i=1;while(reserved.has(prefix+'-'+i))i++;const id=prefix+'-'+i;reserved.add(id);return id};const copy=clone(node);walk(copy,n=>n.id=next(n.type));parent.children.splice(parent.children.indexOf(node)+1,0,copy);selectedId=copy.id;changed()}
function moveSelected(delta){const parent=parentOf(selectedId);if(!parent)return;const index=parent.children.findIndex(n=>n.id===selectedId);const next=Math.max(0,Math.min(parent.children.length-1,index+delta));if(index===next)return;snapshot();const [node]=parent.children.splice(index,1);parent.children.splice(next,0,node);changed()}
function moveNode(id,targetId){if(id===model.root.id||id===targetId)return;const node=find(id),old=parentOf(id),target=find(targetId);if(!node||!old||target?.type!=='container')return;let ancestor=false;walk(node,n=>{if(n.id===targetId)ancestor=true});if(ancestor)return;snapshot();old.children=old.children.filter(n=>n.id!==id);target.children.push(node);selectedId=id;changed()}
function style(node){const s={width:node.width||'auto',height:node.height||'auto'};if(node.type==='container')Object.assign(s,{display:'flex',flexDirection:node.layout==='row'?'row':'column',gap:(node.gap||0)+'px',padding:(node.padding||0)+'px',alignItems:({start:'flex-start',end:'flex-end'}[node.align]||node.align||'stretch'),justifyContent:({start:'flex-start',end:'flex-end'}[node.justify]||node.justify||'flex-start'),background:node.background||'transparent',borderRadius:(node.radius||0)+'px'});if(node.type==='text')Object.assign(s,{fontSize:(node.fontSize||16)+'px',color:node.color||'#fff',fontWeight:node.weight||'normal',textAlign:node.align||'left',overflowWrap:'anywhere'});if(node.type==='button'||node.type==='input')Object.assign(s,{background:node.background,color:node.color,borderRadius:(node.radius||0)+'px',border:'0',padding:'0 12px'});if(node.type==='image')Object.assign(s,{objectFit:node.fit||'cover',borderRadius:(node.radius||0)+'px'});if(node.type==='progress')Object.assign(s,{background:node.track,borderRadius:(node.radius||0)+'px',overflow:'hidden'});return s}
function renderNode(node){let el;if(node.type==='container')el=document.createElement('section');else if(node.type==='text')el=document.createElement('div');else if(node.type==='button'){el=document.createElement('button');el.type='button';el.textContent=node.text}else if(node.type==='image'){el=assets[node.src]?document.createElement('img'):document.createElement('div');if(assets[node.src]){el.src=assets[node.src];el.alt=node.alt||''}else{el.classList.add('image-placeholder');el.textContent=node.src||'Image'}}else if(node.type==='input'){el=document.createElement('input');el.value=node.value||'';el.placeholder=node.placeholder||''}else{el=document.createElement('div');const fill=document.createElement('span');fill.className='progress-fill';fill.style.width=Math.max(0,Math.min(100,Number(node.value)||0))+'%';fill.style.background=node.fill;el.append(fill)}if(node.type==='text')el.textContent=node.text;el.dataset.id=node.id;el.classList.add('designer-node');if(node.id===selectedId)el.classList.add('selected');Object.assign(el.style,style(node));el.draggable=node.id!==model.root.id;el.addEventListener('click',event=>{event.stopPropagation();selectedId=node.id;renderAll()});el.addEventListener('dragstart',event=>{event.stopPropagation();event.dataTransfer.setData('text/plain','move:'+node.id)});if(node.type==='container'){el.addEventListener('dragover',event=>{event.preventDefault();event.stopPropagation();el.classList.add('drop-target')});el.addEventListener('dragleave',()=>el.classList.remove('drop-target'));el.addEventListener('drop',event=>{event.preventDefault();event.stopPropagation();el.classList.remove('drop-target');const data=event.dataTransfer.getData('text/plain');if(data.startsWith('new:'))add(data.slice(4),node.id);if(data.startsWith('move:'))moveNode(data.slice(5),node.id)});node.children.forEach(child=>el.append(renderNode(child)))}return el}
 function field(node,key,label,type='text',options){const wrap=document.createElement('div');wrap.className='field'+(type==='color'?' color-row':'');const lab=document.createElement('label');lab.textContent=label;wrap.append(lab);let input;if(type==='select'){input=document.createElement('select');options.forEach(([value,name])=>{const option=document.createElement('option');option.value=value;option.textContent=name;input.append(option)})}else{input=document.createElement('input');input.type=type;if(type==='number'){input.min=options?.[0]??0;input.max=options?.[1]??999}}const raw=node[key]??'';input.value=type==='color'&& !/^#[0-9a-f]{6}$/i.test(String(raw))?'#000000':raw;const commit=value=>{if(key==='id'){const candidate=String(value).trim();if(!/^[A-Za-z][A-Za-z0-9_-]{0,47}$/.test(candidate)){input.setCustomValidity(text.invalidId);input.reportValidity();return false}let duplicate=false;walk(model.root,n=>{if(n!==node&&n.id===candidate)duplicate=true});if(duplicate){input.setCustomValidity(text.duplicateId);input.reportValidity();return false}input.setCustomValidity('');value=candidate}snapshot();node[key]=type==='number'?Number(value):value;changed();return true};input.addEventListener('change',()=>commit(input.value));wrap.append(input);if(type==='color'){const textInput=document.createElement('input');textInput.type='text';textInput.value=raw;textInput.addEventListener('change',()=>commit(textInput.value));input.addEventListener('change',()=>{textInput.value=input.value});wrap.append(textInput)}return wrap}
function renderInspector(){const body=document.getElementById('inspector-body');body.replaceChildren();const node=find(selectedId);if(!node){body.innerHTML='<div class="empty">'+text.select+'</div>';return}const actions=document.createElement('div');actions.id='node-actions';if(node.id!==model.root.id){[['↑',()=>moveSelected(-1),text.up],['↓',()=>moveSelected(1),text.down],['⧉',duplicateSelected,text.duplicate],['×',removeSelected,text.delete]].forEach(([label,fn,title])=>{const b=document.createElement('button');b.className='secondary';b.textContent=label;b.title=title;b.addEventListener('click',fn);actions.append(b)})}body.append(actions,field(node,'id','ID'));if(node.type==='container'){body.append(field(node,'layout',text.layout,'select',[['column','Column'],['row','Row']]),field(node,'gap','Gap','number',[0,64]),field(node,'padding','Padding','number',[0,64]),field(node,'align','Align','select',[['stretch','Stretch'],['start','Start'],['center','Center'],['end','End']]),field(node,'justify','Justify','select',[['start','Start'],['center','Center'],['end','End'],['space-between','Space between'],['space-around','Space around']]),field(node,'background','Background','color'),field(node,'radius','Radius','number',[0,150]),field(node,'width','Width'),field(node,'height','Height'))}if(node.type==='text')body.append(field(node,'text',text.content),field(node,'fontSize','Font size','number',[8,72]),field(node,'weight','Weight','select',[['normal','Normal'],['bold','Bold']]),field(node,'align','Text align','select',[['left','Left'],['center','Center'],['right','Right']]),field(node,'color','Color','color'));if(node.type==='button')body.append(field(node,'text',text.content),field(node,'background','Background','color'),field(node,'color','Text color','color'),field(node,'radius','Radius','number',[0,64]));if(node.type==='image'){body.append(field(node,'src','Package path'),field(node,'alt','Alt text'),field(node,'fit','Fit','select',[['cover','Cover'],['contain','Contain'],['fill','Fill']]),field(node,'radius','Radius','number',[0,150]));const browse=document.createElement('button');browse.textContent=text.browse;browse.addEventListener('click',()=>vscode.postMessage({type:'choose-image',nodeId:node.id}));body.append(browse)}if(node.type==='input')body.append(field(node,'placeholder','Placeholder'),field(node,'value','Value'),field(node,'background','Background','color'),field(node,'color','Text color','color'),field(node,'radius','Radius','number',[0,64]));if(node.type==='progress')body.append(field(node,'value','Value','number',[0,100]),field(node,'track','Track','color'),field(node,'fill','Fill','color'),field(node,'radius','Radius','number',[0,64]));if(node.type!=='container'&&node.type!=='text')body.append(field(node,'width','Width'),field(node,'height','Height'));if(node.type==='text')body.append(field(node,'width','Width'))}
function renderCanvas(){const canvas=document.getElementById('canvas');canvas.replaceChildren(renderNode(model.root));canvas.className=model.viewport.shape==='round'?'round':'';const shell=document.getElementById('canvas-shell');shell.style.width=model.viewport.width+'px';shell.style.height=model.viewport.height+'px';requestAnimationFrame(applyZoom)}
function pathFor(id){const parts=[];let node=find(id);while(node){parts.unshift(node.id);node=parentOf(node.id)}return parts.join(' / ')}
function renderAll(){renderCanvas();renderInspector();document.getElementById('selection-path').textContent=pathFor(selectedId);document.getElementById('undo').disabled=!history.length;document.getElementById('redo').disabled=!future.length}
function applyZoom(){const wrap=document.getElementById('canvas-wrap'),shell=document.getElementById('canvas-shell');let scale=zoom;if(zoom==='fit')scale=Math.min(1,(wrap.clientWidth-48)/model.viewport.width,(wrap.clientHeight-48)/model.viewport.height);scale=Math.max(.2,Math.min(2,Number(scale)||1));shell.style.transform='scale('+scale+')';document.getElementById('zoom-label').textContent=Math.round(scale*100)+'%'}
document.getElementById('app-name').textContent=initial.appName;document.getElementById('elements-title').textContent=text.elements;document.getElementById('properties-title').textContent=text.properties;document.getElementById('save').textContent=text.save;document.getElementById('actual').textContent=text.actual;document.getElementById('status').textContent=text.status;document.querySelector('#viewport-preset option[value=model]').textContent=model.viewport.width+' × '+model.viewport.height+(model.viewport.shape==='round'?' · Round':'');
const list=document.getElementById('palette-list');definitions.forEach(([type,label,description])=>{const item=document.createElement('button');item.className='palette-item';item.draggable=true;item.innerHTML='<strong>'+label+'</strong><span>'+description+'</span>';item.addEventListener('dragstart',event=>event.dataTransfer.setData('text/plain','new:'+type));item.addEventListener('click',()=>add(type,selectedId));list.append(item)});
document.getElementById('undo').onclick=()=>{if(!history.length)return;future.push(JSON.stringify(model));model=JSON.parse(history.pop());selectedId=find(selectedId)?selectedId:model.root.id;dirty=true;renderAll()};document.getElementById('redo').onclick=()=>{if(!future.length)return;history.push(JSON.stringify(model));model=JSON.parse(future.pop());selectedId=find(selectedId)?selectedId:model.root.id;dirty=true;renderAll()};document.getElementById('zoom-out').onclick=()=>{zoom=zoom==='fit'?0.8:Math.max(.2,Number(zoom)-.1);applyZoom()};document.getElementById('zoom-in').onclick=()=>{zoom=zoom==='fit'?1:Math.min(2,Number(zoom)+.1);applyZoom()};document.getElementById('zoom-fit').onclick=()=>{zoom='fit';applyZoom()};new ResizeObserver(()=>{if(zoom==='fit')applyZoom()}).observe(document.getElementById('canvas-wrap'));
document.getElementById('viewport-preset').onchange=event=>{if(event.target.value==='model')return;snapshot();const presets={'round-300':[300,300,'round'],'rect-172x320':[172,320,'rect'],'rect-320x240':[320,240,'rect']};const p=presets[event.target.value];model.viewport={width:p[0],height:p[1],shape:p[2]};changed()};
function requestSave(debug){document.getElementById('status').textContent=text.saving;vscode.postMessage({type:debug?'save-debug':'save',model})}document.getElementById('save').onclick=()=>requestSave(false);document.getElementById('actual').onclick=()=>requestSave(true);
window.addEventListener('message',event=>{const message=event.data;if(message.type==='saved'){dirty=false;document.getElementById('status').textContent=text.saved;if(message.debug)vscode.postMessage({type:'debug'})}if(message.type==='save-cancelled')document.getElementById('status').textContent=text.unsaved;if(message.type==='asset'){const node=find(message.nodeId);if(node){snapshot();node.src=message.path;assets[message.path]=message.uri;changed()}}if(message.type==='error')document.getElementById('status').textContent=message.message});window.addEventListener('beforeunload',event=>{if(dirty){event.preventDefault();event.returnValue=''}});renderAll();
</script></body></html>`;
}

async function openVisualEditor(context, root) {
  let files;
  let model;
  try {
    files = appFiles(root);
    model = initialModel(root, files);
  } catch (error) {
    vscode.window.showErrorMessage(`${isChinese() ? "无法打开可视化编辑器" : "Unable to open visual editor"}: ${error.message}`);
    return;
  }
  const panel = vscode.window.createWebviewPanel(
    "jellyframeVisualEditor",
    `${isChinese() ? "JellyFrame 可视化编辑" : "JellyFrame Visual Editor"}: ${path.basename(root)}`,
    vscode.ViewColumn.Beside,
    { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [vscode.Uri.file(root)] }
  );
  let takeoverConfirmed = files.html.includes(BODY_START);
  panel.webview.html = visualEditorHtml(panel.webview, root, model, assetMap(panel.webview, root, model));
  panel.webview.onDidReceiveMessage(async (message) => {
    try {
      if (message?.type === "save" || message?.type === "save-debug") {
        const outcome = await saveModel(root, files, message.model, takeoverConfirmed);
        takeoverConfirmed = outcome.takeoverConfirmed;
        if (outcome.saved) {
          files = appFiles(root);
          panel.webview.postMessage({ type: "saved", debug: message.type === "save-debug" });
        } else {
          panel.webview.postMessage({ type: "save-cancelled" });
        }
      } else if (message?.type === "debug") {
        vscode.commands.executeCommand("jellyframe.debug", vscode.Uri.file(root));
      } else if (message?.type === "choose-image" && typeof message.nodeId === "string") {
        const picked = await vscode.window.showOpenDialog({
          defaultUri: vscode.Uri.file(path.join(root, "assets")),
          canSelectFiles: true,
          canSelectFolders: false,
          canSelectMany: false,
          filters: { Images: ["bmp", "png", "jpg", "jpeg", "webp"] },
          openLabel: isChinese() ? "选择包内图片" : "Select package image"
        });
        const filename = picked?.[0]?.fsPath;
        if (!filename) return;
        if (!isPathInside(root, filename)) {
          vscode.window.showErrorMessage(isChinese() ? "图片必须位于当前 App 包内。" : "The image must be inside the current App package.");
          return;
        }
        const relative = `/${path.relative(root, filename).replace(/\\/g, "/")}`;
        panel.webview.postMessage({ type: "asset", nodeId: message.nodeId, path: relative, uri: panel.webview.asWebviewUri(vscode.Uri.file(filename)).toString() });
      }
    } catch (error) {
      panel.webview.postMessage({ type: "error", message: error.message });
      vscode.window.showErrorMessage(`${isChinese() ? "可视化编辑失败" : "Visual editor failed"}: ${error.message}`);
    }
  }, undefined, context.subscriptions);
}

module.exports = {
  appFiles,
  ensureStylesheet,
  initialModel,
  isPathInside,
  openVisualEditor,
  saveModel,
  stylesheetHrefs,
  visualEditorHtml
};
