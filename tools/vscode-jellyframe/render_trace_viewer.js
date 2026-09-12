const MAX_TRACE_BYTES = 8 * 1024 * 1024;
const MAX_TRACE_LINES = 2000;

function parseRenderTrace(text) {
  const source = String(text || "");
  if (Buffer.byteLength(source, "utf8") > MAX_TRACE_BYTES) {
    throw new Error(`render trace exceeds ${MAX_TRACE_BYTES} byte viewer limit`);
  }
  const session = {};
  const frames = [];
  const errors = [];
  let previousFrame;
  const lines = source.split(/\r?\n/).filter((line) => line.trim());
  if (lines.length > MAX_TRACE_LINES) {
    throw new Error(`render trace exceeds ${MAX_TRACE_LINES} record viewer limit`);
  }
  for (let index = 0; index < lines.length; index += 1) {
    let record;
    try {
      record = JSON.parse(lines[index]);
    } catch (error) {
      errors.push(`line ${index + 1}: invalid JSON (${error.message})`);
      continue;
    }
    if (record?.format !== "jellyframe.render.trace.v0") {
      errors.push(`line ${index + 1}: unsupported trace format`);
      continue;
    }
    if (record.type === "session") {
      if (Object.keys(session).length > 0) {
        errors.push(`line ${index + 1}: duplicate session record`);
      } else {
        Object.assign(session, record);
      }
      continue;
    }
    if (record.type !== "frame") {
      errors.push(`line ${index + 1}: unsupported record type`);
      continue;
    }
    if (!Number.isSafeInteger(record.frame) || record.frame < 0) {
      errors.push(`line ${index + 1}: frame must be a non-negative integer`);
      continue;
    }
    if (previousFrame !== undefined && record.frame <= previousFrame) {
      errors.push(`line ${index + 1}: frame numbers must be strictly increasing`);
      continue;
    }
    if (!Number.isSafeInteger(record.totalUs) || record.totalUs < 0) {
      errors.push(`line ${index + 1}: totalUs must be a non-negative integer`);
      continue;
    }
    if (!record.stagesUs || typeof record.stagesUs !== "object" || Array.isArray(record.stagesUs)) {
      errors.push(`line ${index + 1}: stagesUs must be an object`);
      continue;
    }
    const invalidStage = Object.entries(record.stagesUs).some(([, value]) =>
      !Number.isSafeInteger(value) || value < 0
    );
    if (invalidStage) {
      errors.push(`line ${index + 1}: stage times must be non-negative integers`);
      continue;
    }
    frames.push(record);
    previousFrame = record.frame;
  }
  if (frames.length === 0) {
    errors.push("trace contains no usable frame records");
  }
  return { session, frames, errors };
}

function normalizeCommandSample(item) {
  if (!item || typeof item !== "object" || typeof item.type !== "string" ||
      !Number.isSafeInteger(item.us) || item.us < 0 ||
      !Number.isSafeInteger(item.pixels) || item.pixels < 0) {
    return null;
  }
  const samples = Number.isSafeInteger(item.samples) && item.samples > 0 ? item.samples : 1;
  const owner = typeof item.owner === "string" && item.owner.trim()
    ? item.owner.trim()
    : (typeof item.nodeId === "string" && item.nodeId.trim() ? item.nodeId.trim() : "unattributed");
  const command = typeof item.command === "string" && item.command.trim() ? item.command.trim() : item.type;
  return { command, owner, samples, us: item.us, pixels: item.pixels };
}

function addAggregateSample(map, name, totalUs, count, sampleUs) {
  let entry = map.get(name);
  if (!entry) {
    entry = { name, count: 0, totalUs: 0, distribution: [] };
    map.set(name, entry);
  }
  entry.count = Math.min(Number.MAX_SAFE_INTEGER, entry.count + count);
  entry.totalUs = Math.min(Number.MAX_SAFE_INTEGER, entry.totalUs + totalUs);
  entry.distribution.push({ count, us: sampleUs });
}

function finalizeAggregates(map) {
  return Array.from(map.values()).map((entry) => {
    const distribution = entry.distribution.slice().sort((left, right) => left.us - right.us);
    const target = Math.max(1, Math.ceil(entry.count * 0.95));
    let seen = 0;
    let p95Us = 0;
    for (const sample of distribution) {
      seen += sample.count;
      if (seen >= target) {
        p95Us = sample.us;
        break;
      }
    }
    return {
      name: entry.name,
      count: entry.count,
      totalUs: entry.totalUs,
      p95Us: Math.round(p95Us)
    };
  }).sort((left, right) => right.totalUs - left.totalUs || right.count - left.count || left.name.localeCompare(right.name));
}

function aggregateTrace(parsed) {
  const command = new Map();
  const stage = new Map();
  const owner = new Map();
  let invalidCommandSamples = 0;
  let commandsTruncatedFrames = 0;
  let nodesTruncatedFrames = 0;
  for (const frame of parsed?.frames || []) {
    if (frame.commandsTruncated === true) commandsTruncatedFrames += 1;
    if (frame.nodesTruncated === true) nodesTruncatedFrames += 1;
    for (const [name, value] of Object.entries(frame.stagesUs || {})) {
      addAggregateSample(stage, name, value, 1, value);
    }
    let invalidCommandsInFrame = 0;
    for (const item of Array.isArray(frame.commands) ? frame.commands : []) {
      const sample = normalizeCommandSample(item);
      if (!sample) {
        invalidCommandsInFrame += 1;
        continue;
      }
      const perCallUs = sample.us / sample.samples;
      addAggregateSample(command, sample.command, sample.us, sample.samples, perCallUs);
      addAggregateSample(owner, sample.owner, sample.us, sample.samples, perCallUs);
    }
    const declaredInvalid = Number.isSafeInteger(frame.commandInvalidSamples) && frame.commandInvalidSamples > 0
      ? frame.commandInvalidSamples : 0;
    invalidCommandSamples += Math.max(invalidCommandsInFrame, declaredInvalid);
  }
  const owners = finalizeAggregates(owner);
  const unattributed = owners.find((entry) => entry.name === "unattributed") || {
    name: "unattributed", count: 0, totalUs: 0, p95Us: 0
  };
  return {
    frameCount: (parsed?.frames || []).length,
    command: finalizeAggregates(command),
    stage: finalizeAggregates(stage),
    owner: owners,
    missingAttribution: unattributed,
    invalidCommandSamples,
    commandsTruncatedFrames,
    nodesTruncatedFrames
  };
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;");
}

function safeJson(value) {
  return JSON.stringify(value)
    .replace(/</g, "\\u003c")
    .replace(/>/g, "\\u003e")
    .replace(/&/g, "\\u0026")
    .replace(/\u2028/g, "\\u2028")
    .replace(/\u2029/g, "\\u2029");
}

function renderTraceHtml(parsed, chinese, title, options = {}) {
  const frameImages = options && typeof options.frameImages === "object" ? options.frameImages : {};
  const data = safeJson({ ...parsed, frameImages, aggregate: aggregateTrace(parsed) });
  const labels = chinese ? {
    title: "Render Trace",
    frame: "帧",
    total: "总耗时",
    fps: "等效 FPS",
    action: "更新动作",
    reason: "原因",
    dirty: "脏区",
    stages: "阶段耗时",
    commands: "绘制命令归因",
    none: "无",
    noAttribution: "当前帧没有可归因的实际 raster 调用",
    allUnattributed: "当前帧的命令均无法归因（unattributed）",
    partialAttribution: "当前帧有部分命令无法归因（unattributed）",
    slowestFrame: "跳转到最慢帧",
    commandTimingNote: "命令时间只覆盖实际 raster 调用；rounded composite、transform 与宿主绘制等未拆分工作仍不归因。",
    commandsTruncated: "命令聚合已截断，列表不是完整排名。",
    nodesTruncated: "元素归因已达到上限，其余命令可能显示为 unattributed。",
    invalidCommandSamples: "有无效命令计时样本",
    timingNote: "阶段计时是桌面壳的部分归因；timingComplete=false，阶段之和不保证等于总耗时。",
    sourceNote: "此视图不代表 MCU、DMA、panel 或真实设备 FPS。",
    session: "会话",
    errors: "数据问题",
    pixels: "像素",
    owner: "归因对象",
    type: "类型",
    time: "耗时",
    samples: "调用",
    area: "面积",
    pipeline: "管线计数",
    capture: "当前帧截图",
    noCapture: "没有找到与当前帧关联的截图。请确认 trace 与 frame_*.bmp 位于同一输出目录。",
    dirtyCoverage: "脏区覆盖",
    dirtyRects: "脏区矩形",
    dirtyRectsTruncated: "脏区矩形过多，已截断",
    dirtyOverlay: "截图中的脏区",
    aggregate: "跨帧聚合",
    aggregateNote: "按有效 trace 样本累计；p95 是单次调用耗时的第 95 百分位。被截断的帧只代表已记录的下界。",
    aggregateCommand: "命令",
    aggregateStage: "阶段",
    aggregateOwner: "归因对象",
    aggregateCount: "调用数",
    aggregateTotal: "总耗时",
    aggregateP95: "p95",
    aggregateMissing: "缺失归因",
    aggregateMissingNote: "有效命令没有 owner/nodeId，已计入 unattributed；无效计时样本未纳入聚合。",
    aggregateInvalid: "无效命令样本",
    aggregateTruncated: "截断帧"
  } : {
    title: "Render Trace",
    frame: "Frame",
    total: "Total",
    fps: "Effective FPS",
    action: "Action",
    reason: "Reason",
    dirty: "Dirty region",
    stages: "Stage timing",
    commands: "Paint command attribution",
    none: "none",
    noAttribution: "This frame has no attributable raster invocations",
    allUnattributed: "All commands in this frame are unattributed",
    partialAttribution: "Some commands in this frame are unattributed",
    slowestFrame: "Jump to slowest frame",
    commandTimingNote: "Command time covers actual raster invocations only; unsplit rounded composite, transforms and host painting remain unattributed.",
    commandsTruncated: "Command aggregation was truncated; this is not a complete ranking.",
    nodesTruncated: "Element attribution reached its limit; remaining commands may be unattributed.",
    invalidCommandSamples: "Invalid command timing samples",
    timingNote: "Stage timing is partial desktop-shell attribution; timingComplete=false and stages do not necessarily sum to total.",
    sourceNote: "This view is not an MCU, DMA, panel or real-device FPS measurement.",
    session: "Session",
    errors: "Data issues",
    pixels: "Pixels",
    owner: "Owner",
    type: "Type",
    time: "Time",
    samples: "Samples",
    area: "Area",
    pipeline: "Pipeline counters",
    capture: "Frame capture",
    noCapture: "No capture is associated with this frame. Keep the trace and frame_*.bmp files in the same output directory.",
    dirtyCoverage: "Dirty coverage",
    dirtyRects: "Dirty rectangles",
    dirtyRectsTruncated: "Too many dirty rectangles; list truncated",
    dirtyOverlay: "Dirty regions in capture",
    aggregate: "Cross-frame aggregation",
    aggregateNote: "Totals include valid trace samples; p95 is the 95th percentile of per-call time. Truncated frames are lower bounds.",
    aggregateCommand: "Command",
    aggregateStage: "Stage",
    aggregateOwner: "Owner",
    aggregateCount: "Count",
    aggregateTotal: "Total",
    aggregateP95: "p95",
    aggregateMissing: "Missing attribution",
    aggregateMissingNote: "Valid commands without owner/nodeId are grouped as unattributed; invalid timing samples are excluded.",
    aggregateInvalid: "Invalid command samples",
    aggregateTruncated: "Truncated frames"
  };
  const sessionJson = escapeHtml(JSON.stringify(parsed.session || {}));
  const titleText = escapeHtml(title || labels.title);
  const initialError = parsed.errors.length ? `<section class="notice error"><strong>${labels.errors}</strong><ul>${parsed.errors.map((item) => `<li>${escapeHtml(item)}</li>`).join("")}</ul></section>` : "";
  return `<!doctype html>
<html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src ${options.cspSource || "data:"} data:; style-src 'unsafe-inline'; script-src 'nonce-jellyframe-trace';">
<style>
body{font-family:var(--vscode-font-family);color:var(--vscode-foreground);padding:16px;line-height:1.4}h1{font-size:18px;margin:0 0 8px}h2{font-size:14px;margin:20px 0 8px}.muted{color:var(--vscode-descriptionForeground)}.notice{border:1px solid var(--vscode-panel-border);padding:8px;margin:10px 0}.error{color:var(--vscode-errorForeground)}.controls{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.controls input{flex:1;min-width:180px}.metric-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin-top:12px}.metric{border:1px solid var(--vscode-panel-border);padding:8px}.metric b{display:block;font-size:16px}.stage{display:grid;grid-template-columns:minmax(90px,1fr) 3fr 80px;gap:8px;align-items:center;margin:5px 0}.bar{height:8px;background:var(--vscode-editorWidget-background);border-radius:2px;overflow:hidden}.bar i{display:block;height:100%;background:var(--vscode-charts-blue)}.capture{border:1px solid var(--vscode-panel-border);padding:8px;margin-top:12px}.capture-stage{position:relative;display:inline-block;max-width:100%;margin-top:8px}.capture-stage img{display:block;max-width:100%;max-height:420px;object-fit:contain;background:var(--vscode-editor-background)}.dirty-overlay{position:absolute;border:1px solid var(--vscode-charts-orange);background:var(--vscode-editorWarning-foreground);opacity:.25;box-sizing:border-box;pointer-events:none}.dirty-overlay-label{font-size:11px;margin:8px 0 0}.dirty{display:grid;grid-template-columns:minmax(110px,auto) 1fr auto;gap:8px;align-items:center;margin:8px 0}.dirty .bar i{background:var(--vscode-charts-orange)}.dirty-rect{display:grid;grid-template-columns:minmax(80px,1fr) auto;gap:8px;align-items:center;margin:4px 0}.dirty-rect .bar i{background:var(--vscode-charts-orange)}.aggregate{border:1px solid var(--vscode-panel-border);padding:8px;margin-top:16px}.aggregate table{margin-top:8px}table{border-collapse:collapse;width:100%;font-size:12px}th,td{text-align:left;border-bottom:1px solid var(--vscode-panel-border);padding:5px}code{color:var(--vscode-textPreformat-foreground)}ul{margin:5px 0;padding-left:20px}.hidden{display:none}.pill{border:1px solid var(--vscode-panel-border);padding:1px 5px}
</style></head><body><h1>${titleText}</h1>
<p class="muted">${labels.timingNote}<br>${labels.sourceNote}</p>
<div class="controls"><label>${labels.frame} <output id="frameNumber"></output></label><input id="frameSlider" type="range" min="0" max="0" value="0" step="1"><button id="slowestFrameButton" type="button">${labels.slowestFrame}</button></div>
<section class="aggregate"><h2>${labels.aggregate}</h2><p class="muted">${labels.aggregateNote}</p><div id="aggregateView"></div></section>
<div id="frameView"></div>
<h2>${labels.session}</h2><pre class="muted">${sessionJson}</pre>${initialError}
<script nonce="jellyframe-trace">
const model=${data};
const labels=${safeJson(labels)};
const slider=document.getElementById('frameSlider');
const slowestFrameButton=document.getElementById('slowestFrameButton');
const number=document.getElementById('frameNumber');
const view=document.getElementById('frameView');
const aggregateView=document.getElementById('aggregateView');
const esc=(v)=>String(v??'').replace(/[&<>\"]/g,(c)=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]));
const fmt=(v)=>Number(v||0).toLocaleString();
const aggregateTable=(title,rows)=>'<h3>'+esc(title)+'</h3>'+ (rows.length?'<table><tr><th>'+esc(labels.aggregateCommand)+'</th><th>'+esc(labels.aggregateCount)+'</th><th>'+esc(labels.aggregateTotal)+'</th><th>'+esc(labels.aggregateP95)+'</th></tr>'+rows.slice(0,64).map((row)=>'<tr><td><code>'+esc(row.name)+'</code></td><td>'+fmt(row.count)+'</td><td>'+fmt(row.totalUs)+' us</td><td>'+fmt(row.p95Us)+' us</td></tr>').join('')+'</table>':'<p class="muted">'+esc(labels.none)+'</p>');
function renderAggregate(){
 const aggregate=model.aggregate||{command:[],stage:[],owner:[],missingAttribution:{count:0,totalUs:0,p95Us:0},invalidCommandSamples:0,commandsTruncatedFrames:0,nodesTruncatedFrames:0};
 aggregateView.innerHTML=aggregateTable(labels.aggregateCommand,aggregate.command)+aggregateTable(labels.aggregateStage,aggregate.stage)+aggregateTable(labels.aggregateOwner,aggregate.owner)+
 '<p class="muted"><strong>'+esc(labels.aggregateMissing)+':</strong> '+fmt(aggregate.missingAttribution.count)+' / '+fmt(aggregate.missingAttribution.totalUs)+' us. '+esc(labels.aggregateMissingNote)+'</p>'+
 (aggregate.invalidCommandSamples?'<p class="muted">'+esc(labels.aggregateInvalid)+': '+fmt(aggregate.invalidCommandSamples)+'</p>':'')+
 ((aggregate.commandsTruncatedFrames||aggregate.nodesTruncatedFrames)?'<p class="muted">'+esc(labels.aggregateTruncated)+': commands='+fmt(aggregate.commandsTruncatedFrames)+', owners='+fmt(aggregate.nodesTruncatedFrames)+'</p>':'');
}
const slowestFrameIndex=model.frames.length?model.frames.reduce((best,frame,index)=>frame.totalUs>model.frames[best].totalUs?index:best,0):0;
function render(){
 const frame=model.frames[Number(slider.value)]; if(!frame){view.innerHTML='<p class="muted">'+esc(labels.none)+'</p>';return;}
 number.textContent=fmt(frame.frame)+' / '+fmt(model.frames.length-1);
 const stages=Object.entries(frame.stagesUs||{}); const max=Math.max(1,...stages.map(([,v])=>Number(v)||0));
 const sum=stages.reduce((n,[,v])=>n+(Number(v)||0),0); const fps=frame.totalUs>0?(1000000/frame.totalUs).toFixed(1):labels.none;
 const commands=(Array.isArray(frame.commands)?frame.commands:[]).map((item)=>{if(!item||typeof item!=='object'||typeof item.type!=='string'||!Number.isSafeInteger(item.us)||item.us<0||!Number.isSafeInteger(item.pixels)||item.pixels<0)return null;const samples=Number.isSafeInteger(item.samples)&&item.samples>0?item.samples:1;const owner=typeof item.owner==='string'&&item.owner.trim()?item.owner:(typeof item.nodeId==='string'&&item.nodeId.trim()?item.nodeId:'unattributed');return {...item,owner,samples,attributed:owner!=='unattributed'};}).filter(Boolean).sort((left,right)=>right.us-left.us).slice(0,64); const unattributedCount=commands.filter((item)=>!item.attributed).length; const pipeline=frame.pipeline||{};
 const dirtyPercent=Math.max(0,Math.min(100,Number(frame.dirtyAreaPercent)||0));
 const dirtyRects=Array.isArray(frame.dirtyRects)?frame.dirtyRects.filter((rect)=>rect&&Number.isFinite(Number(rect.x))&&Number.isFinite(Number(rect.y))&&Number.isFinite(Number(rect.width))&&Number.isFinite(Number(rect.height))&&Number(rect.width)>0&&Number(rect.height)>0).slice(0,32):[];
 const viewportWidth=Math.max(1,Number(model.session?.viewport?.width)||1); const viewportHeight=Math.max(1,Number(model.session?.viewport?.height)||1);
 const dirtyRectView=dirtyRects.length?'<h2>'+esc(labels.dirtyRects)+'</h2><div>'+dirtyRects.map((rect)=>{const x=Number(rect.x)||0;const y=Number(rect.y)||0;const width=Math.max(0,Number(rect.width)||0);const height=Math.max(0,Number(rect.height)||0);return '<div class="dirty-rect"><span class="bar"><i style="width:'+Math.min(100,Math.max(1,Math.round(width*100/viewportWidth)))+'%"></i></span><code>'+fmt(x)+','+fmt(y)+' '+fmt(width)+'x'+fmt(height)+'</code></div>';}).join('')+(frame.dirtyRectsTruncated?'<p class="muted">'+esc(labels.dirtyRectsTruncated)+'</p>':'')+'</div>':'';
 const capture=model.frameImages?.[String(frame.frame)];
 const dirtyOverlay=dirtyRects.length?'<p class="muted dirty-overlay-label">'+esc(labels.dirtyOverlay)+'</p><div class="capture-stage">'+dirtyRects.map((rect)=>{const x=Number(rect.x)||0;const y=Number(rect.y)||0;const width=Math.max(0,Number(rect.width)||0);const height=Math.max(0,Number(rect.height)||0);return '<i class="dirty-overlay" style="left:'+Math.max(0,Math.min(100,x*100/viewportWidth))+'%;top:'+Math.max(0,Math.min(100,y*100/viewportHeight))+'%;width:'+Math.max(0,Math.min(100,width*100/viewportWidth))+'%;height:'+Math.max(0,Math.min(100,height*100/viewportHeight))+'%"></i>';}).join('')+'<img src="'+esc(capture||'')+'" alt="'+esc(labels.capture)+'"></div>':'';
 const captureView=capture?'<section class="capture"><strong>'+esc(labels.capture)+'</strong>'+(dirtyRects.length?dirtyOverlay:'<img src="'+esc(capture)+'" alt="'+esc(labels.capture)+'">')+'</section>':'<section class="capture muted">'+esc(labels.noCapture)+'</section>';
 view.innerHTML='<div class="metric-grid">'+
 '<div class="metric"><span>'+esc(labels.total)+'</span><b>'+fmt(frame.totalUs)+' us</b></div>'+
 '<div class="metric"><span>'+esc(labels.fps)+'</span><b>'+esc(fps)+'</b></div>'+
 '<div class="metric"><span>'+esc(labels.action)+'</span><b>'+esc(frame.action||labels.none)+'</b></div>'+
 '<div class="metric"><span>'+esc(labels.dirty)+'</span><b>'+fmt(frame.dirtyRectCount)+' / '+fmt(frame.dirtyAreaPercent)+'%</b></div></div>'+
 captureView+
 '<div class="dirty"><span>'+esc(labels.dirtyCoverage)+'</span><span class="bar"><i style="width:'+dirtyPercent+'%"></i></span><span>'+dirtyPercent.toFixed(1)+'%</span></div>'+
 dirtyRectView+
 '<p><strong>'+esc(labels.reason)+':</strong> '+esc(frame.reason||labels.none)+' <span class="muted">· timingComplete='+esc(frame.timingComplete===true?'true':'false')+'</span></p>'+
 '<h2>'+esc(labels.stages)+'</h2>'+ (stages.length?stages.map(([name,value])=>'<div class="stage"><code>'+esc(name)+'</code><span class="bar"><i style="width:'+Math.min(100,Math.round((Number(value)||0)*100/max))+'%"></i></span><span>'+fmt(value)+' us ('+(sum?((Number(value)||0)*100/sum).toFixed(1):'0.0')+'%)</span></div>').join(''):'<p class="muted">'+esc(labels.none)+'</p>')+
 '<h2>'+esc(labels.pipeline)+'</h2><p class="muted">'+Object.entries(pipeline).map(([key,value])=>'<code>'+esc(key)+'='+esc(value)+'</code>').join(' · ')+'</p>'+ 
 '<h2>'+esc(labels.commands)+'</h2><p class="muted">'+esc(labels.commandTimingNote)+'</p>'+ (commands.length?'<table><tr><th>'+esc(labels.type)+'</th><th>'+esc(labels.owner)+'</th><th>'+esc(labels.time)+'</th><th>'+esc(labels.pixels)+'</th><th>'+esc(labels.samples)+'</th></tr>'+commands.map((item)=>'<tr><td>'+esc(item.type||labels.none)+'</td><td><code>'+esc(item.owner)+'</code></td><td>'+fmt(item.us)+' us</td><td>'+fmt(item.pixels)+'</td><td>'+fmt(item.samples)+'</td></tr>').join('')+'</table>'+(unattributedCount===commands.length?'<p class="muted">'+esc(labels.allUnattributed)+'</p>':unattributedCount>0?'<p class="muted">'+esc(labels.partialAttribution)+'</p>':''):'<p class="muted">'+esc(labels.noAttribution)+'</p>')+(frame.commandsTruncated?'<p class="muted">'+esc(labels.commandsTruncated)+'</p>':'')+(frame.nodesTruncated?'<p class="muted">'+esc(labels.nodesTruncated)+'</p>':'')+(Number.isSafeInteger(frame.commandInvalidSamples)&&frame.commandInvalidSamples>0?'<p class="muted">'+esc(labels.invalidCommandSamples)+': '+fmt(frame.commandInvalidSamples)+'</p>':'');
}
renderAggregate();slider.max=Math.max(0,model.frames.length-1);slider.disabled=model.frames.length<2;slowestFrameButton.disabled=model.frames.length<2;slowestFrameButton.addEventListener('click',()=>{slider.value=String(slowestFrameIndex);render();});slider.addEventListener('input',render);render();
</script></body></html>`;
}

module.exports = { MAX_TRACE_BYTES, MAX_TRACE_LINES, parseRenderTrace, aggregateTrace, renderTraceHtml };
