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
    if (record.stageSpans !== undefined) {
      if (!Array.isArray(record.stageSpans)) {
        errors.push(`line ${index + 1}: stageSpans must be an array`);
        continue;
      }
      const invalidSpan = record.stageSpans.some((span) =>
        !span || typeof span !== "object" || typeof span.name !== "string" || !span.name.trim() ||
        !Number.isSafeInteger(span.startUs) || span.startUs < 0 ||
        !Number.isSafeInteger(span.durationUs) || span.durationUs < 0 ||
        span.startUs + span.durationUs > Number.MAX_SAFE_INTEGER
      );
      if (invalidSpan) {
        errors.push(`line ${index + 1}: stage spans must have non-negative startUs and durationUs`);
        continue;
      }
    }
    if (record.commandSpans !== undefined) {
      if (!Array.isArray(record.commandSpans)) {
        errors.push(`line ${index + 1}: commandSpans must be an array`);
        continue;
      }
      const invalidCommandSpan = record.commandSpans.some((span) =>
        !span || typeof span !== "object" || typeof span.type !== "string" || !span.type.trim() ||
        typeof span.owner !== "string" || !span.owner.trim() ||
        !Number.isSafeInteger(span.startUs) || span.startUs < 0 ||
        !Number.isSafeInteger(span.durationUs) || span.durationUs < 0 ||
        !Number.isSafeInteger(span.pixels) || span.pixels < 0 ||
        span.startUs + span.durationUs > Number.MAX_SAFE_INTEGER ||
        (span.rect !== undefined && (!span.rect || typeof span.rect !== "object" ||
          !Number.isSafeInteger(span.rect.x) || !Number.isSafeInteger(span.rect.y) ||
          !Number.isSafeInteger(span.rect.width) || span.rect.width < 0 ||
          !Number.isSafeInteger(span.rect.height) || span.rect.height < 0))
      );
      if (invalidCommandSpan) {
        errors.push(`line ${index + 1}: command spans must have stable non-negative fields`);
        continue;
      }
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

function normalizeCommandSpan(item) {
  if (!item || typeof item !== "object" || typeof item.type !== "string" || !item.type.trim() ||
      typeof item.owner !== "string" || !item.owner.trim() ||
      !Number.isSafeInteger(item.startUs) || item.startUs < 0 ||
      !Number.isSafeInteger(item.durationUs) || item.durationUs < 0 ||
      !Number.isSafeInteger(item.pixels) || item.pixels < 0) {
    return null;
  }
  const rect = item.rect;
  if (rect !== undefined && (!rect || typeof rect !== "object" ||
      !Number.isSafeInteger(rect.x) || !Number.isSafeInteger(rect.y) ||
      !Number.isSafeInteger(rect.width) || rect.width < 0 ||
      !Number.isSafeInteger(rect.height) || rect.height < 0)) {
    return null;
  }
  return {
    type: item.type.trim(),
    owner: item.owner.trim(),
    startUs: item.startUs,
    durationUs: item.durationUs,
    pixels: item.pixels,
    ...(rect !== undefined ? { rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height } } : {})
  };
}

function rectIntersectionArea(left, right) {
  if (!left || !right) return 0;
  const x = Math.max(left.x, right.x);
  const y = Math.max(left.y, right.y);
  const rightEdge = Math.min(left.x + left.width, right.x + right.width);
  const bottomEdge = Math.min(left.y + left.height, right.y + right.height);
  return rightEdge > x && bottomEdge > y ? (rightEdge - x) * (bottomEdge - y) : 0;
}

function normalizeDirtyRect(rect) {
  if (!rect || typeof rect !== "object" ||
      !Number.isSafeInteger(rect.x) || !Number.isSafeInteger(rect.y) ||
      !Number.isSafeInteger(rect.width) || rect.width <= 0 ||
      !Number.isSafeInteger(rect.height) || rect.height <= 0) {
    return null;
  }
  return { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
}

function frameDirtyRepaintEvidence(frame) {
  if (!Array.isArray(frame?.dirtyRects) || !Array.isArray(frame?.commandSpans)) {
    return { available: false, entries: [] };
  }
  const dirtyRects = frame.dirtyRects.map(normalizeDirtyRect).filter(Boolean).slice(0, 32);
  const entries = [];
  for (const rawSpan of frame.commandSpans) {
    const span = normalizeCommandSpan(rawSpan);
    if (!span?.rect) continue;
    const overlaps = [];
    for (let index = 0; index < dirtyRects.length; index += 1) {
      const area = rectIntersectionArea(span.rect, dirtyRects[index]);
      if (area > 0) overlaps.push({ index, area });
    }
    if (overlaps.length) {
      entries.push({
        name: span.type + " · " + span.owner,
        type: span.type,
        owner: span.owner,
        durationUs: span.durationUs,
        pixels: span.pixels,
        dirtyRectIndexes: overlaps.map((item) => item.index),
        overlapPixels: overlaps.reduce((sum, item) => sum + item.area, 0)
      });
    }
  }
  return { available: true, entries };
}

function spanEndUs(startUs, durationUs) {
  return startUs + durationUs;
}

function commandSpanStage(commandSpan, stageSpans) {
  const commandStartUs = commandSpan.startUs;
  const commandEndUs = spanEndUs(commandStartUs, commandSpan.durationUs);
  let best = null;
  for (const stage of stageSpans) {
    if (!stage || typeof stage.name !== "string" || !stage.name.trim() ||
        !Number.isSafeInteger(stage.startUs) || stage.startUs < 0 ||
        !Number.isSafeInteger(stage.durationUs) || stage.durationUs < 0) {
      continue;
    }
    const overlapUs = Math.max(0, Math.min(commandEndUs, spanEndUs(stage.startUs, stage.durationUs)) -
      Math.max(commandStartUs, stage.startUs));
    if (overlapUs > 0 && (!best || overlapUs > best.overlapUs)) {
      best = { name: stage.name.trim(), overlapUs };
    }
  }
  return best;
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

function addCommandSpanAggregate(map, span, stageName) {
  const name = span.type + " · " + span.owner;
  const key = name + "\u0000" + (stageName || "");
  let entry = map.get(key);
  if (!entry) {
    entry = {
      name,
      type: span.type,
      owner: span.owner,
      stage: stageName || "unattributed",
      count: 0,
      totalUs: 0,
      totalPixels: 0,
      distribution: []
    };
    map.set(key, entry);
  }
  entry.count = Math.min(Number.MAX_SAFE_INTEGER, entry.count + 1);
  entry.totalUs = Math.min(Number.MAX_SAFE_INTEGER, entry.totalUs + span.durationUs);
  entry.totalPixels = Math.min(Number.MAX_SAFE_INTEGER, entry.totalPixels + span.pixels);
  entry.distribution.push({ count: 1, us: span.durationUs });
}

function addDirtyEvidenceAggregate(map, entry) {
  const key = entry.type + "\u0000" + entry.owner;
  let aggregate = map.get(key);
  if (!aggregate) {
    aggregate = {
      name: entry.name,
      type: entry.type,
      owner: entry.owner,
      count: 0,
      totalUs: 0,
      totalOverlapPixels: 0,
      dirtyRectHits: 0,
      distribution: []
    };
    map.set(key, aggregate);
  }
  aggregate.count = Math.min(Number.MAX_SAFE_INTEGER, aggregate.count + 1);
  aggregate.totalUs = Math.min(Number.MAX_SAFE_INTEGER, aggregate.totalUs + entry.durationUs);
  aggregate.totalOverlapPixels = Math.min(Number.MAX_SAFE_INTEGER, aggregate.totalOverlapPixels + entry.overlapPixels);
  aggregate.dirtyRectHits = Math.min(Number.MAX_SAFE_INTEGER, aggregate.dirtyRectHits + entry.dirtyRectIndexes.length);
  aggregate.distribution.push({ count: 1, us: entry.durationUs });
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

function finalizeCommandSpanAggregates(map) {
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
      type: entry.type,
      owner: entry.owner,
      stage: entry.stage,
      count: entry.count,
      totalUs: entry.totalUs,
      p95Us: Math.round(p95Us),
      totalPixels: entry.totalPixels
    };
  }).sort((left, right) => right.totalUs - left.totalUs || right.count - left.count || left.name.localeCompare(right.name));
}

function finalizeDirtyEvidenceAggregates(map) {
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
      type: entry.type,
      owner: entry.owner,
      count: entry.count,
      totalUs: entry.totalUs,
      p95Us: Math.round(p95Us),
      totalOverlapPixels: entry.totalOverlapPixels,
      dirtyRectHits: entry.dirtyRectHits
    };
  }).sort((left, right) => right.totalUs - left.totalUs || right.count - left.count || left.name.localeCompare(right.name));
}

function aggregateTrace(parsed) {
  const command = new Map();
  const commandSpan = new Map();
  const dirtyEvidence = new Map();
  const stage = new Map();
  const owner = new Map();
  let invalidCommandSamples = 0;
  let commandsTruncatedFrames = 0;
  let nodesTruncatedFrames = 0;
  let commandSpansTruncatedFrames = 0;
  let dirtyRectsTruncatedFrames = 0;
  for (const frame of parsed?.frames || []) {
    if (frame.commandsTruncated === true) commandsTruncatedFrames += 1;
    if (frame.nodesTruncated === true) nodesTruncatedFrames += 1;
    if (frame.commandSpansTruncated === true) commandSpansTruncatedFrames += 1;
    if (frame.dirtyRectsTruncated === true) dirtyRectsTruncatedFrames += 1;
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
    const stageSpans = Array.isArray(frame.stageSpans) ? frame.stageSpans : [];
    for (const item of Array.isArray(frame.commandSpans) ? frame.commandSpans : []) {
      const span = normalizeCommandSpan(item);
      if (!span) continue;
      const stage = commandSpanStage(span, stageSpans);
      addCommandSpanAggregate(commandSpan, span, stage?.name);
    }
    for (const entry of frameDirtyRepaintEvidence(frame).entries) {
      addDirtyEvidenceAggregate(dirtyEvidence, entry);
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
    commandSpan: finalizeCommandSpanAggregates(commandSpan),
    dirtyEvidence: finalizeDirtyEvidenceAggregates(dirtyEvidence),
    stage: finalizeAggregates(stage),
    owner: owners,
    missingAttribution: unattributed,
    invalidCommandSamples,
    commandsTruncatedFrames,
    nodesTruncatedFrames,
    commandSpansTruncatedFrames,
    dirtyRectsTruncatedFrames
  };
}

function frameTimingBreakdown(frame) {
  const totalUs = Number.isSafeInteger(frame?.totalUs) && frame.totalUs >= 0
    ? frame.totalUs : 0;
  let recordedStageUs = 0;
  for (const value of Object.values(frame?.stagesUs || {})) {
    if (!Number.isSafeInteger(value) || value < 0) continue;
    recordedStageUs = Math.min(Number.MAX_SAFE_INTEGER, recordedStageUs + value);
  }
  return {
    totalUs,
    recordedStageUs,
    unaccountedUs: Math.max(0, totalUs - recordedStageUs),
    timingComplete: frame?.timingComplete === true
  };
}

function frameStageComposition(frame) {
  const timing = frameTimingBreakdown(frame);
  const stages = Object.entries(frame?.stagesUs || {})
    .filter(([, value]) => Number.isSafeInteger(value) && value > 0)
    .map(([name, us]) => ({ name, us, kind: "stage" }));
  const denominatorUs = Math.max(1, timing.totalUs, timing.recordedStageUs);
  const segments = stages.map((stage) => ({
    ...stage,
    widthPercent: stage.us * 100 / denominatorUs,
    frameSharePercent: timing.totalUs > 0 ? stage.us * 100 / timing.totalUs : 0
  }));
  if (timing.unaccountedUs > 0) {
    segments.push({
      name: "unaccounted",
      us: timing.unaccountedUs,
      kind: "unaccounted",
      widthPercent: timing.unaccountedUs * 100 / denominatorUs,
      frameSharePercent: timing.totalUs > 0 ? timing.unaccountedUs * 100 / timing.totalUs : 0
    });
  }
  return {
    ...timing,
    denominatorUs,
    overrunUs: Math.max(0, timing.recordedStageUs - timing.totalUs),
    segments
  };
}

function frameStageTimeline(frame) {
  if (!Array.isArray(frame?.stageSpans)) {
    return { available: false, totalUs: frameTimingBreakdown(frame).totalUs, segments: [] };
  }
  const timing = frameTimingBreakdown(frame);
  const spans = frame.stageSpans.filter((span) =>
    span && typeof span.name === "string" && span.name.trim() &&
    Number.isSafeInteger(span.startUs) && span.startUs >= 0 &&
    Number.isSafeInteger(span.durationUs) && span.durationUs >= 0
  );
  const maxEndUs = spans.reduce((maxEnd, span) => Math.max(maxEnd, spanEndUs(span.startUs, span.durationUs)), timing.totalUs);
  const denominatorUs = Math.max(1, maxEndUs);
  const segments = [];
  let cursorUs = 0;
  for (const span of spans) {
    const endUs = spanEndUs(span.startUs, span.durationUs);
    if (span.startUs > cursorUs) {
      segments.push({
        name: "unaccounted",
        kind: "gap",
        startUs: cursorUs,
        durationUs: span.startUs - cursorUs,
        leftPercent: cursorUs * 100 / denominatorUs,
        widthPercent: (span.startUs - cursorUs) * 100 / denominatorUs
      });
    }
    segments.push({
      name: span.name.trim(),
      kind: "stage",
      startUs: span.startUs,
      durationUs: span.durationUs,
      leftPercent: span.startUs * 100 / denominatorUs,
      widthPercent: span.durationUs * 100 / denominatorUs
    });
    cursorUs = Math.max(cursorUs, endUs);
  }
  if (cursorUs < timing.totalUs) {
    segments.push({
      name: "unaccounted",
      kind: "gap",
      startUs: cursorUs,
      durationUs: timing.totalUs - cursorUs,
      leftPercent: cursorUs * 100 / denominatorUs,
      widthPercent: (timing.totalUs - cursorUs) * 100 / denominatorUs
    });
  }
  return {
    available: true,
    totalUs: timing.totalUs,
    denominatorUs,
    overrunUs: Math.max(0, maxEndUs - timing.totalUs),
    segments
  };
}

function frameCommandTimeline(frame) {
  if (!Array.isArray(frame?.commandSpans)) {
    return { available: false, totalUs: frameTimingBreakdown(frame).totalUs, segments: [] };
  }
  const timing = frameTimingBreakdown(frame);
  const spans = frame.commandSpans.map(normalizeCommandSpan).filter(Boolean);
  const maxEndUs = spans.reduce((maxEnd, span) => Math.max(maxEnd, spanEndUs(span.startUs, span.durationUs)), timing.totalUs);
  const stageSpans = Array.isArray(frame.stageSpans) ? frame.stageSpans : [];
  const denominatorUs = Math.max(1, maxEndUs);
  const segments = [];
  let cursorUs = 0;
  for (const span of spans) {
    if (span.startUs > cursorUs) {
      segments.push({
        name: "unaccounted",
        kind: "gap",
        startUs: cursorUs,
        durationUs: span.startUs - cursorUs,
        leftPercent: cursorUs * 100 / denominatorUs,
        widthPercent: (span.startUs - cursorUs) * 100 / denominatorUs
      });
    }
    const stage = commandSpanStage(span, stageSpans);
    segments.push({
      name: span.type + " · " + span.owner,
      type: span.type,
      owner: span.owner,
      pixels: span.pixels,
      ...(stage ? { stageName: stage.name, stageOverlapUs: stage.overlapUs } : {}),
      kind: "command",
      startUs: span.startUs,
      durationUs: span.durationUs,
      leftPercent: span.startUs * 100 / denominatorUs,
      widthPercent: span.durationUs * 100 / denominatorUs
    });
    cursorUs = Math.max(cursorUs, spanEndUs(span.startUs, span.durationUs));
  }
  if (cursorUs < timing.totalUs) {
    segments.push({
      name: "unaccounted",
      kind: "gap",
      startUs: cursorUs,
      durationUs: timing.totalUs - cursorUs,
      leftPercent: cursorUs * 100 / denominatorUs,
      widthPercent: (timing.totalUs - cursorUs) * 100 / denominatorUs
    });
  }
  return {
    available: true,
    totalUs: timing.totalUs,
    denominatorUs,
    overrunUs: Math.max(0, maxEndUs - timing.totalUs),
    segments
  };
}

function frameHotspotSummary(frame) {
  const stages = Object.entries(frame?.stagesUs || {})
    .filter(([, value]) => Number.isSafeInteger(value) && value >= 0)
    .map(([name, us]) => ({ name, us }));
  const stage = stages.reduce((best, item) => !best || item.us > best.us ? item : best, null);
  const commandTotals = new Map();
  const ownerTotals = new Map();
  for (const item of Array.isArray(frame?.commands) ? frame.commands : []) {
    const sample = normalizeCommandSample(item);
    if (!sample) continue;
    const command = commandTotals.get(sample.command) || { name: sample.command, us: 0, pixels: 0, samples: 0 };
    command.us = Math.min(Number.MAX_SAFE_INTEGER, command.us + sample.us);
    command.pixels = Math.min(Number.MAX_SAFE_INTEGER, command.pixels + sample.pixels);
    command.samples = Math.min(Number.MAX_SAFE_INTEGER, command.samples + sample.samples);
    commandTotals.set(sample.command, command);
    const owner = ownerTotals.get(sample.owner) || { name: sample.owner, us: 0, samples: 0 };
    owner.us = Math.min(Number.MAX_SAFE_INTEGER, owner.us + sample.us);
    owner.samples = Math.min(Number.MAX_SAFE_INTEGER, owner.samples + sample.samples);
    ownerTotals.set(sample.owner, owner);
  }
  const largest = (map) => Array.from(map.values()).reduce(
    (best, item) => !best || item.us > best.us ? item : best, null
  );
  return {
    stage,
    command: largest(commandTotals),
    owner: largest(ownerTotals)
  };
}

function frameCommandCostMap(frame) {
  const map = new Map();
  const spans = Array.isArray(frame?.commandSpans) ? frame.commandSpans.map(normalizeCommandSpan).filter(Boolean) : null;
  if (spans !== null) {
    for (const span of spans) {
      const key = span.type + "\u0000" + span.owner;
      const entry = map.get(key) || { name: span.type + " · " + span.owner, type: span.type, owner: span.owner, us: 0, pixels: 0, count: 0 };
      entry.us = Math.min(Number.MAX_SAFE_INTEGER, entry.us + span.durationUs);
      entry.pixels = Math.min(Number.MAX_SAFE_INTEGER, entry.pixels + span.pixels);
      entry.count = Math.min(Number.MAX_SAFE_INTEGER, entry.count + 1);
      map.set(key, entry);
    }
    return { source: "spans", map, truncated: frame?.commandSpansTruncated === true };
  }
  for (const item of Array.isArray(frame?.commands) ? frame.commands : []) {
    const sample = normalizeCommandSample(item);
    if (!sample) continue;
    const key = sample.command + "\u0000" + sample.owner;
    const entry = map.get(key) || { name: sample.command + " · " + sample.owner, type: sample.command, owner: sample.owner, us: 0, pixels: 0, count: 0 };
    entry.us = Math.min(Number.MAX_SAFE_INTEGER, entry.us + sample.us);
    entry.pixels = Math.min(Number.MAX_SAFE_INTEGER, entry.pixels + sample.pixels);
    entry.count = Math.min(Number.MAX_SAFE_INTEGER, entry.count + sample.samples);
    map.set(key, entry);
  }
  return { source: "aggregate", map, truncated: frame?.commandsTruncated === true };
}

function frameDeltaSummary(frame, previousFrame) {
  if (!frame || !previousFrame) return { available: false };
  const currentTiming = frameTimingBreakdown(frame);
  const previousTiming = frameTimingBreakdown(previousFrame);
  const stageNames = new Set([
    ...Object.keys(frame.stagesUs || {}),
    ...Object.keys(previousFrame.stagesUs || {})
  ]);
  const stages = Array.from(stageNames).map((name) => ({
    name,
    deltaUs: (Number.isSafeInteger(frame.stagesUs?.[name]) ? frame.stagesUs[name] : 0) -
      (Number.isSafeInteger(previousFrame.stagesUs?.[name]) ? previousFrame.stagesUs[name] : 0)
  })).filter((item) => item.deltaUs !== 0).sort((left, right) => right.deltaUs - left.deltaUs || left.name.localeCompare(right.name));
  const currentCommands = frameCommandCostMap(frame);
  const previousCommands = frameCommandCostMap(previousFrame);
  const commandComparable = currentCommands.source === previousCommands.source;
  const commands = [];
  if (commandComparable) {
    const commandKeys = new Set([...currentCommands.map.keys(), ...previousCommands.map.keys()]);
    for (const key of commandKeys) {
      const current = currentCommands.map.get(key);
      const previous = previousCommands.map.get(key);
      const deltaUs = (current?.us || 0) - (previous?.us || 0);
      if (deltaUs !== 0) {
        commands.push({
          name: current?.name || previous.name,
          type: current?.type || previous.type,
          owner: current?.owner || previous.owner,
          deltaUs,
          currentUs: current?.us || 0,
          previousUs: previous?.us || 0
        });
      }
    }
    commands.sort((left, right) => right.deltaUs - left.deltaUs || left.name.localeCompare(right.name));
  }
  return {
    available: true,
    frame: frame.frame,
    previousFrame: previousFrame.frame,
    totalDeltaUs: currentTiming.totalUs - previousTiming.totalUs,
    dirtyRectCountDelta: (Number.isSafeInteger(frame.dirtyRectCount) ? frame.dirtyRectCount : 0) -
      (Number.isSafeInteger(previousFrame.dirtyRectCount) ? previousFrame.dirtyRectCount : 0),
    dirtyAreaPercentDelta: (Number.isFinite(Number(frame.dirtyAreaPercent)) ? Number(frame.dirtyAreaPercent) : 0) -
      (Number.isFinite(Number(previousFrame.dirtyAreaPercent)) ? Number(previousFrame.dirtyAreaPercent) : 0),
    stages,
    commandComparable,
    commandSource: currentCommands.source,
    commandDataTruncated: currentCommands.truncated || previousCommands.truncated,
    commands: commands.slice(0, 16)
  };
}

function frameAnomalySummary(parsed) {
  const frames = parsed?.frames || [];
  const percentile = (values, fraction) => {
    const sorted = values.slice().sort((left, right) => left - right);
    return sorted.length ? sorted[Math.min(sorted.length - 1, Math.max(0, Math.ceil(sorted.length * fraction) - 1))] : 0;
  };
  const totalValues = frames.map((frame) => frameTimingBreakdown(frame).totalUs);
  const paintValues = frames.map((frame) => Number.isSafeInteger(frame.stagesUs?.paint) ? frame.stagesUs.paint : 0);
  const dirtyValues = frames.map((frame) => Number.isFinite(Number(frame.dirtyAreaPercent)) ? Number(frame.dirtyAreaPercent) : 0);
  const thresholds = {
    totalUsP95: percentile(totalValues, 0.95),
    paintUsP95: percentile(paintValues, 0.95),
    dirtyAreaPercentP95: percentile(dirtyValues, 0.95)
  };
  const trend = frames.map((frame, index) => {
    const totalUs = totalValues[index];
    const paintUs = paintValues[index];
    const dirtyAreaPercent = dirtyValues[index];
    const reasons = [];
    if (totalUs > thresholds.totalUsP95 && thresholds.totalUsP95 > 0) reasons.push("total");
    if (paintUs > thresholds.paintUsP95 && thresholds.paintUsP95 > 0) reasons.push("paint");
    if (dirtyAreaPercent > thresholds.dirtyAreaPercentP95 && thresholds.dirtyAreaPercentP95 > 0) reasons.push("dirty");
    return { index, frame: frame.frame, totalUs, paintUs, dirtyAreaPercent, anomaly: reasons.length > 0, reasons };
  });
  return {
    thresholds,
    frames: trend,
    anomalies: trend.filter((item) => item.anomaly).map((item) => item.index)
  };
}

function frameAnomalyAttribution(frame, previousFrame, thresholds = {}) {
  if (!frame) return { available: false, anomalous: false, reasons: [], limitations: ["missing-frame"] };
  const timing = frameTimingBreakdown(frame);
  const totalUsP95 = Number.isFinite(Number(thresholds.totalUsP95)) ? Number(thresholds.totalUsP95) : 0;
  const paintUsP95 = Number.isFinite(Number(thresholds.paintUsP95)) ? Number(thresholds.paintUsP95) : 0;
  const dirtyAreaPercentP95 = Number.isFinite(Number(thresholds.dirtyAreaPercentP95)) ? Number(thresholds.dirtyAreaPercentP95) : 0;
  const paintUs = Number.isSafeInteger(frame.stagesUs?.paint) ? frame.stagesUs.paint : 0;
  const dirtyAreaPercent = Number.isFinite(Number(frame.dirtyAreaPercent)) ? Number(frame.dirtyAreaPercent) : 0;
  const reasons = [];
  if (timing.totalUs > totalUsP95 && totalUsP95 > 0) reasons.push("total");
  if (paintUs > paintUsP95 && paintUsP95 > 0) reasons.push("paint");
  if (dirtyAreaPercent > dirtyAreaPercentP95 && dirtyAreaPercentP95 > 0) reasons.push("dirty");

  const commandCosts = frameCommandCostMap(frame);
  const commands = Array.from(commandCosts.map.values()).sort((left, right) =>
    right.us - left.us || left.name.localeCompare(right.name)
  );
  const hottestCommand = commands[0] || null;
  const stages = Object.entries(frame.stagesUs || {})
    .filter(([, value]) => Number.isSafeInteger(value) && value >= 0)
    .map(([name, us]) => ({ name, us }))
    .sort((left, right) => right.us - left.us || left.name.localeCompare(right.name));

  const dirtyEvidence = frameDirtyRepaintEvidence(frame);
  const dirtyOwners = new Map();
  for (const entry of dirtyEvidence.entries) {
    const current = dirtyOwners.get(entry.owner) || { name: entry.owner, us: 0, overlapPixels: 0, hits: 0 };
    current.us = Math.min(Number.MAX_SAFE_INTEGER, current.us + entry.durationUs);
    current.overlapPixels = Math.min(Number.MAX_SAFE_INTEGER, current.overlapPixels + entry.overlapPixels);
    current.hits += entry.dirtyRectIndexes.length;
    dirtyOwners.set(entry.owner, current);
  }
  const dirtyOwner = Array.from(dirtyOwners.values()).sort((left, right) =>
    right.us - left.us || left.name.localeCompare(right.name)
  )[0] || null;

  const delta = previousFrame ? frameDeltaSummary(frame, previousFrame) : { available: false };
  const largestCommandIncrease = delta.commandComparable
    ? (delta.commands || []).filter((item) => item.deltaUs > 0)[0] || null
    : null;
  const limitations = [];
  if (!commandCosts.map.size) limitations.push("no-command-timing");
  if (commandCosts.truncated) limitations.push("command-data-truncated");
  if (!dirtyEvidence.available) limitations.push("dirty-evidence-unavailable");
  else if (frame.dirtyRectsTruncated || frame.commandSpansTruncated) limitations.push("dirty-evidence-truncated");
  if (!delta.available) limitations.push("no-previous-frame");
  else if (!delta.commandComparable) limitations.push("command-delta-unavailable");
  if (frame.timingComplete !== true) limitations.push("partial-frame-timing");
  return {
    available: true,
    anomalous: reasons.length > 0,
    frame: frame.frame,
    reasons,
    thresholds: { totalUsP95, paintUsP95, dirtyAreaPercentP95 },
    values: { totalUs: timing.totalUs, paintUs, dirtyAreaPercent },
    hottestStage: stages[0] || null,
    hottestCommand,
    dirtyOwner,
    largestCommandIncrease,
    commandSource: commandCosts.source,
    limitations
  };
}

function frameTimingSummary(parsed) {
  const values = (parsed?.frames || [])
    .map((frame) => frameTimingBreakdown(frame).totalUs)
    .filter((value) => Number.isSafeInteger(value) && value >= 0)
    .sort((left, right) => left - right);
  const percentile = (fraction) => values.length
    ? values[Math.min(values.length - 1, Math.max(0, Math.ceil(values.length * fraction) - 1))]
    : 0;
  return {
    count: values.length,
    p50Us: percentile(0.50),
    p95Us: percentile(0.95),
    maxUs: values.length ? values[values.length - 1] : 0
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
  const frameTiming = Object.fromEntries((parsed?.frames || []).map((frame) => [
    String(frame.frame), frameTimingBreakdown(frame)
  ]));
  const frameComposition = Object.fromEntries((parsed?.frames || []).map((frame) => [
    String(frame.frame), frameStageComposition(frame)
  ]));
  const frameTimelines = Object.fromEntries((parsed?.frames || []).map((frame) => [
    String(frame.frame), frameStageTimeline(frame)
  ]));
  const frameCommandTimelines = Object.fromEntries((parsed?.frames || []).map((frame) => [
    String(frame.frame), frameCommandTimeline(frame)
  ]));
  const frameDirtyEvidence = Object.fromEntries((parsed?.frames || []).map((frame) => [
    String(frame.frame), frameDirtyRepaintEvidence(frame)
  ]));
  const frameDeltas = Object.fromEntries((parsed?.frames || []).map((frame, index) => [
    String(frame.frame), frameDeltaSummary(frame, index > 0 ? parsed.frames[index - 1] : null)
  ]));
  const frameHotspots = Object.fromEntries((parsed?.frames || []).map((frame) => [
    String(frame.frame), frameHotspotSummary(frame)
  ]));
  const frameSummary = frameTimingSummary(parsed);
  const frameAnomalies = frameAnomalySummary(parsed);
  const frameAnomalyAttributions = Object.fromEntries((parsed?.frames || []).map((frame, index) => [
    String(frame.frame), frameAnomalyAttribution(frame, index > 0 ? parsed.frames[index - 1] : null, frameAnomalies.thresholds)
  ]));
  const data = safeJson({ ...parsed, frameImages, frameTiming, frameComposition, frameTimelines, frameCommandTimelines, frameDirtyEvidence, frameDeltas, frameHotspots, frameSummary, frameAnomalies, frameAnomalyAttributions, aggregate: aggregateTrace(parsed) });
  const labels = chinese ? {
    title: "Render Trace",
    frame: "帧",
    total: "总耗时",
    fps: "等效 FPS",
    action: "更新动作",
    reason: "原因",
    dirty: "脏区",
    stages: "阶段耗时",
    stageComposition: "单帧阶段构成",
    stageCompositionNote: "各段按 producer 输出顺序展示累计耗时占比，不代表真实开始/结束时刻；未归因时间可能出现在帧内任意位置。",
    stageOverrun: "已记录阶段之和超过 frame wall time，构成条已按阶段总和归一化；请检查重叠计时或时钟来源。",
    stageDetail: "阶段详情",
    stageSource: "数据来源",
    sourceUnspecified: "trace producer（未声明 runtime）",
    stageTimeline: "单帧实际时间线",
    stageTimelineNote: "仅由 trace 提供的 stageSpans 绘制；空白段表示已采集 span 之间的未归因间隙。",
    stageTimelineUnavailable: "此 trace 没有实际 span 数据，已显示累计阶段构成。",
    timelineGap: "未归因间隙",
    commandTimeline: "绘制命令实际时间线",
    commandTimelineNote: "仅覆盖带有效时钟的 raster invocation；rounded composite、transform 和宿主绘制仍可能未归因。",
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
     dirtyEvidence: "脏区内的实际重绘证据",
     dirtyEvidenceNote: "仅表示带有最终 raster 矩形的命令与已记录 dirty rect 发生空间重叠，不是 DOM 变更根因；未命中的命令或截断数据不会显示。",
     dirtyRectIndexes: "脏区编号",
     overlapPixels: "重叠像素",
    aggregate: "跨帧聚合",
    aggregateNote: "按有效 trace 样本累计；p95 是单次调用耗时的第 95 百分位。被截断的帧只代表已记录的下界。",
    aggregateCommand: "命令",
     aggregateCommandSpan: "实际命令 · 元素",
     aggregatePixels: "候选像素",
     aggregateDirtyEvidence: "脏区重绘证据 · 元素",
     aggregateDirtyHits: "命中脏区",
     aggregateOverlapPixels: "重叠像素",
    aggregateStage: "阶段",
    aggregateOwner: "归因对象",
    aggregateCount: "调用数",
    aggregateTotal: "总耗时",
    aggregateP95: "p95",
    aggregateMissing: "缺失归因",
    aggregateMissingNote: "有效命令没有 owner/nodeId，已计入 unattributed；无效计时样本未纳入聚合。",
    aggregateInvalid: "无效命令样本",
     aggregateTruncated: "截断帧",
     frameDelta: "相邻帧变化",
     frameDeltaNote: "与上一条有效 frame 记录对比；命令变化仅在两帧使用相同数据源时显示。正值表示增加，负值表示减少。",
     noPreviousFrame: "没有上一条有效 frame 记录",
     commandDeltaUnavailable: "相邻帧命令数据源不同，无法进行命令耗时对比。",
     delta: "变化",
     currentValue: "当前",
     previousValue: "上一帧",
    unaccounted: "未归因时间",
    hotspots: "当前帧热点",
    hottestStage: "最耗时阶段",
    hottestCommand: "最耗时命令",
    hottestOwner: "最高耗时归因",
    noHotspot: "无有效样本",
    frameSummary: "跨帧总耗时",
    frameCount: "有效帧",
    p50: "p50",
    p95: "p95",
     maximum: "最大值",
     anomalyOnly: "仅显示异常帧",
     trend: "帧耗时趋势",
     trendNote: "异常按当前 trace 的 p95 判定；点击条目跳转到对应帧。",
     anomaly: "异常",
     anomalyReasons: "异常原因",
     anomalyCount: "异常帧",
     reasonTotal: "总帧耗时",
     reasonPaint: "paint 耗时",
     reasonDirty: "dirty 面积",
     anomalyAttribution: "异常帧归因",
     anomalyAttributionNote: "以下是性能相关性和空间重叠证据，不是 DOM 变更根因。缺失、截断或部分计时会在限制中标出。",
     triggeredBy: "触发条件",
     threshold: "阈值",
     observed: "当前值",
     hottestStage: "最耗时阶段",
     hottestCommandDetail: "最耗时命令",
     dirtyOwnerDetail: "脏区证据中耗时最高的对象",
     largestCommandIncrease: "相对上一帧增长最大的命令",
     noEvidence: "没有可用证据",
     limitation: "限制",
     attributionUnavailable: "当前帧不是异常帧，或没有足够数据生成归因详情",
     reasonValue: "总帧耗时 / paint / dirty 面积"
  } : {
    title: "Render Trace",
    frame: "Frame",
    total: "Total",
    fps: "Effective FPS",
    action: "Action",
    reason: "Reason",
    dirty: "Dirty region",
    stages: "Stage timing",
    stageComposition: "Frame stage composition",
    stageCompositionNote: "Segments show accumulated duration shares in producer order, not true start/end spans. Unaccounted time may occur anywhere in the frame.",
    stageOverrun: "Recorded stages exceed frame wall time. The composition is normalized to the stage total; inspect overlapping timers or clock sources.",
    stageDetail: "Stage detail",
    stageSource: "Source",
    sourceUnspecified: "trace producer (runtime unspecified)",
    stageTimeline: "Frame span timeline",
    stageTimelineNote: "Rendered only from stageSpans supplied by the trace; blank segments are unaccounted gaps between recorded spans.",
    stageTimelineUnavailable: "This trace has no span data; accumulated stage composition is shown instead.",
    timelineGap: "Unaccounted gap",
    commandTimeline: "Paint command span timeline",
    commandTimelineNote: "Covers only raster invocations with valid clocks; rounded composites, transforms and host painting may remain unattributed.",
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
     dirtyEvidence: "Observed repaint inside dirty regions",
     dirtyEvidenceNote: "Shows only spatial overlap between commands with final raster rectangles and recorded dirty rectangles; it is not the DOM mutation cause. Unmatched or truncated work is omitted.",
     dirtyRectIndexes: "Dirty rects",
     overlapPixels: "Overlap pixels",
    aggregate: "Cross-frame aggregation",
    aggregateNote: "Totals include valid trace samples; p95 is the 95th percentile of per-call time. Truncated frames are lower bounds.",
    aggregateCommand: "Command",
     aggregateCommandSpan: "Raster command · owner",
     aggregatePixels: "Candidate pixels",
     aggregateDirtyEvidence: "Dirty repaint evidence · owner",
     aggregateDirtyHits: "Dirty hits",
     aggregateOverlapPixels: "Overlap pixels",
    aggregateStage: "Stage",
    aggregateOwner: "Owner",
    aggregateCount: "Count",
    aggregateTotal: "Total",
    aggregateP95: "p95",
    aggregateMissing: "Missing attribution",
    aggregateMissingNote: "Valid commands without owner/nodeId are grouped as unattributed; invalid timing samples are excluded.",
    aggregateInvalid: "Invalid command samples",
     aggregateTruncated: "Truncated frames",
     frameDelta: "Adjacent-frame change",
     frameDeltaNote: "Compared with the previous valid frame record; command deltas are shown only when both frames use the same data source. Positive values increased, negative values decreased.",
     noPreviousFrame: "No previous valid frame record",
     commandDeltaUnavailable: "Adjacent frames use different command data sources; command timing cannot be compared.",
     delta: "Delta",
     currentValue: "Current",
     previousValue: "Previous",
    unaccounted: "Unaccounted time",
    hotspots: "Frame hotspots",
    hottestStage: "Hottest stage",
    hottestCommand: "Hottest command",
    hottestOwner: "Top attributed owner",
    noHotspot: "No valid samples",
    frameSummary: "Cross-frame total timing",
    frameCount: "Valid frames",
    p50: "p50",
    p95: "p95",
     maximum: "Max",
     anomalyOnly: "Show anomalous frames only",
     trend: "Frame timing trend",
     trendNote: "Anomalies use p95 thresholds from this trace; click an entry to jump to its frame.",
     anomaly: "Anomaly",
     anomalyReasons: "Reasons",
     anomalyCount: "Anomalous frames",
     reasonTotal: "total frame time",
     reasonPaint: "paint time",
     reasonDirty: "dirty area",
     anomalyAttribution: "Anomaly attribution",
     anomalyAttributionNote: "These are performance-correlation and spatial-overlap signals, not the DOM mutation cause. Missing, truncated, or partial timing is listed as a limitation.",
     triggeredBy: "Triggered by",
     threshold: "Threshold",
     observed: "Observed",
     hottestStage: "Hottest stage",
     hottestCommandDetail: "Hottest command",
     dirtyOwnerDetail: "Highest-cost owner in dirty evidence",
     largestCommandIncrease: "Largest command increase from previous frame",
     noEvidence: "No usable evidence",
     limitation: "Limitations",
     attributionUnavailable: "This frame is not anomalous or lacks enough data for attribution details",
     reasonValue: "total frame time / paint / dirty area"
  };
  const sessionJson = escapeHtml(JSON.stringify(parsed.session || {}));
  const titleText = escapeHtml(title || labels.title);
  const initialError = parsed.errors.length ? `<section class="notice error"><strong>${labels.errors}</strong><ul>${parsed.errors.map((item) => `<li>${escapeHtml(item)}</li>`).join("")}</ul></section>` : "";
  return `<!doctype html>
<html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src ${options.cspSource || "data:"} data:; style-src 'unsafe-inline'; script-src 'nonce-jellyframe-trace';">
<style>
body{font-family:var(--vscode-font-family);color:var(--vscode-foreground);padding:16px;line-height:1.4}h1{font-size:18px;margin:0 0 8px}h2{font-size:14px;margin:20px 0 8px}.muted{color:var(--vscode-descriptionForeground)}.notice{border:1px solid var(--vscode-panel-border);padding:8px;margin:10px 0}.error{color:var(--vscode-errorForeground)}.controls{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.controls input{flex:1;min-width:180px}.metric-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin-top:12px}.metric{border:1px solid var(--vscode-panel-border);padding:8px}.metric b{display:block;font-size:16px}.stage-composition{display:flex;width:100%;height:24px;border:1px solid var(--vscode-panel-border);background:var(--vscode-editorWidget-background);overflow:hidden;box-sizing:border-box}.stage-segment{min-width:2px;height:100%;padding:0;border:0;border-right:1px solid var(--vscode-editor-background);background:var(--vscode-charts-blue);cursor:pointer}.stage-segment:nth-child(4n+2){background:var(--vscode-charts-green)}.stage-segment:nth-child(4n+3){background:var(--vscode-charts-orange)}.stage-segment:nth-child(4n+4){background:var(--vscode-charts-purple)}.stage-segment.unaccounted{background:var(--vscode-descriptionForeground)}.stage-segment:focus{outline:2px solid var(--vscode-focusBorder);outline-offset:-2px}.stage-detail{border-left:3px solid var(--vscode-charts-blue);padding:6px 8px;margin:8px 0;background:var(--vscode-editorWidget-background)}.stage{display:grid;grid-template-columns:minmax(90px,1fr) 3fr 80px;gap:8px;align-items:center;margin:5px 0}.bar{height:8px;background:var(--vscode-editorWidget-background);border-radius:2px;overflow:hidden}.bar i{display:block;height:100%;background:var(--vscode-charts-blue)}.capture{border:1px solid var(--vscode-panel-border);padding:8px;margin-top:12px}.capture-stage{position:relative;display:inline-block;max-width:100%;margin-top:8px}.capture-stage img{display:block;max-width:100%;max-height:420px;object-fit:contain;background:var(--vscode-editor-background)}.dirty-overlay{position:absolute;border:1px solid var(--vscode-charts-orange);background:var(--vscode-editorWarning-foreground);opacity:.25;box-sizing:border-box;pointer-events:none}.dirty-overlay-label{font-size:11px;margin:8px 0 0}.dirty{display:grid;grid-template-columns:minmax(110px,auto) 1fr auto;gap:8px;align-items:center;margin:8px 0}.dirty .bar i{background:var(--vscode-charts-orange)}.dirty-rect{display:grid;grid-template-columns:minmax(80px,1fr) auto;gap:8px;align-items:center;margin:4px 0}.dirty-rect .bar i{background:var(--vscode-charts-orange)}.aggregate{border:1px solid var(--vscode-panel-border);padding:8px;margin-top:16px}.aggregate table{margin-top:8px}table{border-collapse:collapse;width:100%;font-size:12px}th,td{text-align:left;border-bottom:1px solid var(--vscode-panel-border);padding:5px}code{color:var(--vscode-textPreformat-foreground)}ul{margin:5px 0;padding-left:20px}.hidden{display:none}.pill{border:1px solid var(--vscode-panel-border);padding:1px 5px}
</style><style>.hotspots{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:8px}.hotspot{border-left:3px solid var(--vscode-charts-orange);padding:6px 8px;background:var(--vscode-editorWidget-background)}.hotspot b{display:block}.stage-timeline{position:relative;width:100%;height:28px;border:1px solid var(--vscode-panel-border);background:var(--vscode-editorWidget-background);overflow:hidden;box-sizing:border-box}.timeline-segment{position:absolute;top:0;height:100%;padding:0;border:0;border-right:1px solid var(--vscode-editor-background);background:var(--vscode-charts-blue);cursor:pointer}.timeline-segment:nth-child(4n+2){background:var(--vscode-charts-green)}.timeline-segment:nth-child(4n+3){background:var(--vscode-charts-orange)}.timeline-segment:nth-child(4n+4){background:var(--vscode-charts-purple)}.timeline-segment.gap{background:var(--vscode-descriptionForeground);cursor:pointer}.timeline-segment:focus{outline:2px solid var(--vscode-focusBorder);outline-offset:-2px}</style></head><body><h1>${titleText}</h1>
<style>.trend{margin:8px 0}.trend-row{display:grid;grid-template-columns:50px 1fr 90px;gap:8px;align-items:center;margin:4px 0}.trend-row button{display:block;width:100%;height:10px;padding:0;border:0;background:var(--vscode-editorWidget-background);cursor:pointer}.trend-row button i{display:block;height:100%;background:var(--vscode-charts-blue)}.trend-row.anomaly button i{background:var(--vscode-charts-orange)}.trend-row button:focus{outline:2px solid var(--vscode-focusBorder);outline-offset:1px}</style>
<p class="muted">${labels.timingNote}<br>${labels.sourceNote}</p>
<div class="controls"><label>${labels.frame} <output id="frameNumber"></output></label><input id="frameSlider" type="range" min="0" max="0" value="0" step="1"><button id="slowestFrameButton" type="button">${labels.slowestFrame}</button><label><input id="anomalyOnly" type="checkbox"> ${labels.anomalyOnly}</label></div>
<section class="aggregate"><h2>${labels.aggregate}</h2><p class="muted">${labels.aggregateNote}</p><h3>${labels.frameSummary}</h3><div id="frameSummaryView" class="metric-grid"></div><div id="frameTrendView"></div><div id="aggregateView"></div></section>
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
const frameSummaryView=document.getElementById('frameSummaryView');
const frameTrendView=document.getElementById('frameTrendView');
const anomalyOnly=document.getElementById('anomalyOnly');
const esc=(v)=>String(v??'').replace(/[&<>\"]/g,(c)=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]));
const fmt=(v)=>Number(v||0).toLocaleString();
const aggregateTable=(title,rows)=>'<h3>'+esc(title)+'</h3>'+ (rows.length?'<table><tr><th>'+esc(labels.aggregateCommand)+'</th><th>'+esc(labels.aggregateCount)+'</th><th>'+esc(labels.aggregateTotal)+'</th><th>'+esc(labels.aggregateP95)+'</th></tr>'+rows.slice(0,64).map((row)=>'<tr><td><code>'+esc(row.name)+'</code></td><td>'+fmt(row.count)+'</td><td>'+fmt(row.totalUs)+' us</td><td>'+fmt(row.p95Us)+' us</td></tr>').join('')+'</table>':'<p class="muted">'+esc(labels.none)+'</p>');
const aggregateCommandSpanTable=(rows)=>'<h3>'+esc(labels.aggregateCommandSpan)+'</h3>'+ (rows.length?'<table><tr><th>'+esc(labels.aggregateCommandSpan)+'</th><th>'+esc(labels.aggregateStage)+'</th><th>'+esc(labels.aggregateCount)+'</th><th>'+esc(labels.aggregateTotal)+'</th><th>'+esc(labels.aggregateP95)+'</th><th>'+esc(labels.aggregatePixels)+'</th></tr>'+rows.slice(0,64).map((row)=>'<tr><td><code>'+esc(row.name)+'</code></td><td>'+esc(row.stage)+'</td><td>'+fmt(row.count)+'</td><td>'+fmt(row.totalUs)+' us</td><td>'+fmt(row.p95Us)+' us</td><td>'+fmt(row.totalPixels)+'</td></tr>').join('')+'</table>':'<p class="muted">'+esc(labels.none)+'</p>');
const aggregateDirtyEvidenceTable=(rows)=>'<h3>'+esc(labels.aggregateDirtyEvidence)+'</h3>'+ (rows.length?'<table><tr><th>'+esc(labels.aggregateDirtyEvidence)+'</th><th>'+esc(labels.aggregateCount)+'</th><th>'+esc(labels.aggregateTotal)+'</th><th>'+esc(labels.aggregateP95)+'</th><th>'+esc(labels.aggregateDirtyHits)+'</th><th>'+esc(labels.aggregateOverlapPixels)+'</th></tr>'+rows.slice(0,64).map((row)=>'<tr><td><code>'+esc(row.name)+'</code></td><td>'+fmt(row.count)+'</td><td>'+fmt(row.totalUs)+' us</td><td>'+fmt(row.p95Us)+' us</td><td>'+fmt(row.dirtyRectHits)+'</td><td>'+fmt(row.totalOverlapPixels)+'</td></tr>').join('')+'</table>':'<p class="muted">'+esc(labels.none)+'</p>');
function renderAggregate(){
 const aggregate=model.aggregate||{command:[],commandSpan:[],dirtyEvidence:[],stage:[],owner:[],missingAttribution:{count:0,totalUs:0,p95Us:0},invalidCommandSamples:0,commandsTruncatedFrames:0,nodesTruncatedFrames:0,commandSpansTruncatedFrames:0,dirtyRectsTruncatedFrames:0};
 aggregateView.innerHTML=aggregateTable(labels.aggregateCommand,aggregate.command)+aggregateCommandSpanTable(aggregate.commandSpan||[])+aggregateDirtyEvidenceTable(aggregate.dirtyEvidence||[])+aggregateTable(labels.aggregateStage,aggregate.stage)+aggregateTable(labels.aggregateOwner,aggregate.owner)+
 '<p class="muted"><strong>'+esc(labels.aggregateMissing)+':</strong> '+fmt(aggregate.missingAttribution.count)+' / '+fmt(aggregate.missingAttribution.totalUs)+' us. '+esc(labels.aggregateMissingNote)+'</p>'+
 (aggregate.invalidCommandSamples?'<p class="muted">'+esc(labels.aggregateInvalid)+': '+fmt(aggregate.invalidCommandSamples)+'</p>':'')+
 ((aggregate.commandsTruncatedFrames||aggregate.nodesTruncatedFrames||aggregate.commandSpansTruncatedFrames||aggregate.dirtyRectsTruncatedFrames)?'<p class="muted">'+esc(labels.aggregateTruncated)+': commands='+fmt(aggregate.commandsTruncatedFrames)+', spans='+fmt(aggregate.commandSpansTruncatedFrames)+', dirtyRects='+fmt(aggregate.dirtyRectsTruncatedFrames)+', owners='+fmt(aggregate.nodesTruncatedFrames)+'</p>':'');
}
function renderFrameSummary(){
  const summary=model.frameSummary||{count:0,p50Us:0,p95Us:0,maxUs:0};
 const anomalies=model.frameAnomalies?.anomalies||[];
  frameSummaryView.innerHTML='<div class="metric"><span>'+esc(labels.frameCount)+'</span><b>'+fmt(summary.count)+'</b></div>'+
  '<div class="metric"><span>'+esc(labels.p50)+'</span><b>'+fmt(summary.p50Us)+' us</b></div>'+
  '<div class="metric"><span>'+esc(labels.p95)+'</span><b>'+fmt(summary.p95Us)+' us</b></div>'+
  '<div class="metric"><span>'+esc(labels.maximum)+'</span><b>'+fmt(summary.maxUs)+' us</b></div>'+
  '<div class="metric"><span>'+esc(labels.anomalyCount)+'</span><b>'+fmt(anomalies.length)+'</b></div>';
}
function renderFrameTrend(){
 const summary=model.frameAnomalies||{frames:[],thresholds:{}};
 const rows=summary.frames.slice(-200);
 const max=Math.max(1,...summary.frames.map((item)=>item.totalUs));
 const reasonName=(reason)=>reason==='total'?labels.reasonTotal:reason==='paint'?labels.reasonPaint:labels.reasonDirty;
 frameTrendView.innerHTML='<h3>'+esc(labels.trend)+'</h3><p class="muted">'+esc(labels.trendNote)+'</p><div class="trend">'+rows.map((item)=>'<div class="trend-row '+(item.anomaly?'anomaly':'')+'"><code>#'+fmt(item.frame)+'</code><button type="button" data-trend-index="'+item.index+'" title="'+esc((item.anomaly?labels.anomaly+': ':'')+item.totalUs+' us')+'" aria-label="'+esc('#'+item.frame+' '+item.totalUs+' us')+'"><i style="width:'+Math.max(1,item.totalUs*100/max)+'%"></i></button><span>'+fmt(item.totalUs)+' us'+(item.anomaly?' · '+esc(item.reasons.map(reasonName).join(', ')):'')+'</span></div>').join('')+'</div>'+(summary.frames.length>rows.length?'<p class="muted">'+esc(labels.commandsTruncated)+'</p>':'');
 frameTrendView.querySelectorAll('[data-trend-index]').forEach((button)=>button.addEventListener('click',()=>{selectedFrameIndex=Number(button.dataset.trendIndex);if(!visibleFrameIndexes().includes(selectedFrameIndex))anomalyOnly.checked=false;updateFrameSelector();render();}));
}
const slowestFrameIndex=model.frames.length?model.frames.reduce((best,frame,index)=>frame.totalUs>model.frames[best].totalUs?index:best,0):0;
let selectedFrameIndex=0;
function visibleFrameIndexes(){
 const anomalies=new Set(model.frameAnomalies?.anomalies||[]);
 return anomalyOnly.checked?model.frames.map((_,index)=>index).filter((index)=>anomalies.has(index)):model.frames.map((_,index)=>index);
}
function updateFrameSelector(){
 const indexes=visibleFrameIndexes();
 if(!indexes.includes(selectedFrameIndex))selectedFrameIndex=indexes[0]??0;
 const position=indexes.indexOf(selectedFrameIndex);
 slider.max=Math.max(0,indexes.length-1);slider.disabled=indexes.length<2;slider.value=String(Math.max(0,position));
}
function render(){
 const indexes=visibleFrameIndexes();
 const frameIndex=indexes[Number(slider.value)];
 const frame=model.frames[frameIndex]; if(!frame){view.innerHTML='<p class="muted">'+esc(labels.none)+'</p>';return;}
 selectedFrameIndex=frameIndex;
 number.textContent=fmt(frame.frame)+' / '+fmt(model.frames.length-1);
 const stages=Object.entries(frame.stagesUs||{}); const timing=model.frameTiming?.[String(frame.frame)]||{totalUs:0,recordedStageUs:0,unaccountedUs:0}; const composition=model.frameComposition?.[String(frame.frame)]||{segments:[],overrunUs:0}; const timeline=model.frameTimelines?.[String(frame.frame)]||{available:false,segments:[],overrunUs:0}; const commandTimeline=model.frameCommandTimelines?.[String(frame.frame)]||{available:false,segments:[],overrunUs:0}; const hotspots=model.frameHotspots?.[String(frame.frame)]||{}; const delta=model.frameDeltas?.[String(frame.frame)]||{available:false,stages:[],commands:[]}; const total=timing.totalUs; const unaccountedUs=timing.unaccountedUs; const max=Math.max(1,total,...stages.map(([,v])=>Number(v)||0));
 const fps=total>0?(1000000/total).toFixed(1):labels.none;
 const commands=(Array.isArray(frame.commands)?frame.commands:[]).map((item)=>{if(!item||typeof item!=='object'||typeof item.type!=='string'||!Number.isSafeInteger(item.us)||item.us<0||!Number.isSafeInteger(item.pixels)||item.pixels<0)return null;const samples=Number.isSafeInteger(item.samples)&&item.samples>0?item.samples:1;const owner=typeof item.owner==='string'&&item.owner.trim()?item.owner:(typeof item.nodeId==='string'&&item.nodeId.trim()?item.nodeId:'unattributed');return {...item,owner,samples,attributed:owner!=='unattributed'};}).filter(Boolean).sort((left,right)=>right.us-left.us).slice(0,64); const unattributedCount=commands.filter((item)=>!item.attributed).length; const pipeline=frame.pipeline||{};
 const dirtyPercent=Math.max(0,Math.min(100,Number(frame.dirtyAreaPercent)||0));
 const dirtyEvidence=model.frameDirtyEvidence?.[String(frame.frame)]||{available:false,entries:[]};
 const dirtyRects=Array.isArray(frame.dirtyRects)?frame.dirtyRects.filter((rect)=>rect&&Number.isFinite(Number(rect.x))&&Number.isFinite(Number(rect.y))&&Number.isFinite(Number(rect.width))&&Number.isFinite(Number(rect.height))&&Number(rect.width)>0&&Number(rect.height)>0).slice(0,32):[];
 const viewportWidth=Math.max(1,Number(model.session?.viewport?.width)||1); const viewportHeight=Math.max(1,Number(model.session?.viewport?.height)||1);
 const dirtyRectView=dirtyRects.length?'<h2>'+esc(labels.dirtyRects)+'</h2><div>'+dirtyRects.map((rect,index)=>{const x=Number(rect.x)||0;const y=Number(rect.y)||0;const width=Math.max(0,Number(rect.width)||0);const height=Math.max(0,Number(rect.height)||0);return '<div class="dirty-rect"><span class="bar"><i style="width:'+Math.min(100,Math.max(1,Math.round(width*100/viewportWidth)))+'%"></i></span><code>#'+fmt(index)+' '+fmt(x)+','+fmt(y)+' '+fmt(width)+'x'+fmt(height)+'</code></div>';}).join('')+(frame.dirtyRectsTruncated?'<p class="muted">'+esc(labels.dirtyRectsTruncated)+'</p>':'')+'</div>':'';
 const dirtyEvidenceView=dirtyEvidence.available?'<h2>'+esc(labels.dirtyEvidence)+'</h2><p class="muted">'+esc(labels.dirtyEvidenceNote)+'</p>'+(dirtyEvidence.entries.length?'<table><tr><th>'+esc(labels.type)+'</th><th>'+esc(labels.owner)+'</th><th>'+esc(labels.time)+'</th><th>'+esc(labels.dirtyRectIndexes)+'</th><th>'+esc(labels.overlapPixels)+'</th></tr>'+dirtyEvidence.entries.map((entry)=>'<tr><td>'+esc(entry.type)+'</td><td><code>'+esc(entry.owner)+'</code></td><td>'+fmt(entry.durationUs)+' us</td><td>'+entry.dirtyRectIndexes.map((index)=>'#'+fmt(index)).join(', ')+'</td><td>'+fmt(entry.overlapPixels)+'</td></tr>').join('')+'</table>':'<p class="muted">'+esc(labels.none)+'</p>'):'';
 const capture=model.frameImages?.[String(frame.frame)];
 const dirtyOverlay=dirtyRects.length?'<p class="muted dirty-overlay-label">'+esc(labels.dirtyOverlay)+'</p><div class="capture-stage">'+dirtyRects.map((rect)=>{const x=Number(rect.x)||0;const y=Number(rect.y)||0;const width=Math.max(0,Number(rect.width)||0);const height=Math.max(0,Number(rect.height)||0);return '<i class="dirty-overlay" style="left:'+Math.max(0,Math.min(100,x*100/viewportWidth))+'%;top:'+Math.max(0,Math.min(100,y*100/viewportHeight))+'%;width:'+Math.max(0,Math.min(100,width*100/viewportWidth))+'%;height:'+Math.max(0,Math.min(100,height*100/viewportHeight))+'%"></i>';}).join('')+'<img src="'+esc(capture||'')+'" alt="'+esc(labels.capture)+'"></div>':'';
 const captureView=capture?'<section class="capture"><strong>'+esc(labels.capture)+'</strong>'+(dirtyRects.length?dirtyOverlay:'<img src="'+esc(capture)+'" alt="'+esc(labels.capture)+'">')+'</section>':'<section class="capture muted">'+esc(labels.noCapture)+'</section>';
 const hotspotValue=(item)=>item?'<b>'+esc(item.name)+'</b><span>'+fmt(item.us)+' us</span>':'<span class="muted">'+esc(labels.noHotspot)+'</span>';
 const hotspotView='<h2>'+esc(labels.hotspots)+'</h2><div class="hotspots">'+
 '<div class="hotspot"><span>'+esc(labels.hottestStage)+'</span>'+hotspotValue(hotspots.stage)+'</div>'+
 '<div class="hotspot"><span>'+esc(labels.hottestCommand)+'</span>'+hotspotValue(hotspots.command)+'</div>'+
 '<div class="hotspot"><span>'+esc(labels.hottestOwner)+'</span>'+hotspotValue(hotspots.owner)+'</div></div>';
 const attribution=model.frameAnomalyAttributions?.[String(frame.frame)]||{available:false,anomalous:false,reasons:[],limitations:[]};
 const reasonName=(reason)=>reason==='total'?labels.reasonTotal:reason==='paint'?labels.reasonPaint:labels.reasonDirty;
 const attributionValue=(item, value=item?.us)=>item?'<b>'+esc(item.name)+'</b><span>'+fmt(value)+' us</span>':'<span class="muted">'+esc(labels.noEvidence)+'</span>';
 const attributionView=attribution.anomalous?'<section class="anomaly-attribution"><h2>'+esc(labels.anomalyAttribution)+'</h2><p class="muted">'+esc(labels.anomalyAttributionNote)+'</p>'+
  '<p><strong>'+esc(labels.triggeredBy)+':</strong> '+attribution.reasons.map(reasonName).map(esc).join(', ')+'</p>'+
  '<table><tr><th>'+esc(labels.reason)+'</th><th>'+esc(labels.threshold)+'</th><th>'+esc(labels.observed)+'</th></tr>'+attribution.reasons.map((reason)=>{const threshold=reason==='total'?attribution.thresholds.totalUsP95:reason==='paint'?attribution.thresholds.paintUsP95:attribution.thresholds.dirtyAreaPercentP95;const observed=reason==='total'?attribution.values.totalUs:reason==='paint'?attribution.values.paintUs:attribution.values.dirtyAreaPercent;return '<tr><td>'+esc(reasonName(reason))+'</td><td>'+fmt(threshold)+(reason==='dirty'?'%':' us')+'</td><td>'+fmt(observed)+(reason==='dirty'?'%':' us')+'</td></tr>';}).join('')+'</table>'+
  '<div class="hotspots attribution-hotspots">'+
  '<div class="hotspot"><span>'+esc(labels.hottestStage)+'</span>'+attributionValue(attribution.hottestStage)+'</div>'+
  '<div class="hotspot"><span>'+esc(labels.hottestCommandDetail)+'</span>'+attributionValue(attribution.hottestCommand)+'</div>'+
  '<div class="hotspot"><span>'+esc(labels.dirtyOwnerDetail)+'</span>'+attributionValue(attribution.dirtyOwner)+'</div>'+
  '<div class="hotspot"><span>'+esc(labels.largestCommandIncrease)+'</span>'+attributionValue(attribution.largestCommandIncrease, attribution.largestCommandIncrease?.deltaUs)+'</div></div>'+
  (attribution.limitations.length?'<p class="muted"><strong>'+esc(labels.limitation)+':</strong> '+attribution.limitations.map(esc).join(', ')+'</p>':'')+'</section>':'';
 const deltaNumber=(value)=>{const number=Number(value)||0;return (number>0?'+':'')+fmt(number);};
 const deltaStageRows=delta.stages?.length?'<table><tr><th>'+esc(labels.aggregateStage)+'</th><th>'+esc(labels.delta)+'</th></tr>'+delta.stages.map((item)=>'<tr><td><code>'+esc(item.name)+'</code></td><td>'+deltaNumber(item.deltaUs)+' us</td></tr>').join('')+'</table>':'<p class="muted">'+esc(labels.none)+'</p>';
 const deltaCommandRows=delta.commandComparable?(delta.commands?.length?'<table><tr><th>'+esc(labels.type)+'</th><th>'+esc(labels.owner)+'</th><th>'+esc(labels.previousValue)+'</th><th>'+esc(labels.currentValue)+'</th><th>'+esc(labels.delta)+'</th></tr>'+delta.commands.map((item)=>'<tr><td>'+esc(item.type)+'</td><td><code>'+esc(item.owner)+'</code></td><td>'+fmt(item.previousUs)+' us</td><td>'+fmt(item.currentUs)+' us</td><td>'+deltaNumber(item.deltaUs)+' us</td></tr>').join('')+'</table>':'<p class="muted">'+esc(labels.none)+'</p>'): '<p class="muted">'+esc(labels.commandDeltaUnavailable)+'</p>';
 const frameDeltaView=delta.available?'<h2>'+esc(labels.frameDelta)+'</h2><p class="muted">'+esc(labels.frameDeltaNote)+' ('+esc(labels.previousValue)+' #'+fmt(delta.previousFrame)+')</p><div class="metric-grid"><div class="metric"><span>'+esc(labels.total)+'</span><b>'+deltaNumber(delta.totalDeltaUs)+' us</b></div><div class="metric"><span>'+esc(labels.dirtyRects)+'</span><b>'+deltaNumber(delta.dirtyRectCountDelta)+'</b></div><div class="metric"><span>'+esc(labels.dirtyCoverage)+'</span><b>'+deltaNumber(delta.dirtyAreaPercentDelta)+'%</b></div></div><h3>'+esc(labels.stages)+'</h3>'+deltaStageRows+'<h3>'+esc(labels.commands)+'</h3>'+deltaCommandRows+(delta.commandDataTruncated?'<p class="muted">'+esc(labels.commandsTruncated)+'</p>':''):'<p class="muted">'+esc(labels.noPreviousFrame)+'</p>';
 const runtimeSource=typeof model.session?.runtime==='string'&&model.session.runtime.trim()?model.session.runtime.trim():labels.sourceUnspecified;
 const stageTimelineView=timeline.available?'<h2>'+esc(labels.stageTimeline)+'</h2><p class="muted">'+esc(labels.stageTimelineNote)+'</p><div class="stage-timeline">'+timeline.segments.map((segment,index)=>{const name=segment.kind==='gap'?labels.timelineGap:segment.name;const detail=name+': '+fmt(segment.durationUs)+' us (start '+fmt(segment.startUs)+' us)';return '<button type="button" class="timeline-segment '+(segment.kind==='gap'?'gap':'')+'" data-timeline-index="'+index+'" style="left:'+Math.max(0,Number(segment.leftPercent)||0)+'%;width:'+Math.max(0,Number(segment.widthPercent)||0)+'%" title="'+esc(detail)+'" aria-label="'+esc(detail)+'"></button>';}).join('')+'</div><div id="timelineDetail" class="stage-detail muted">'+esc(labels.stageDetail)+': '+esc(labels.none)+'</div>'+(timeline.overrunUs>0?'<p class="notice error">'+esc(labels.stageOverrun)+' ('+fmt(timeline.overrunUs)+' us)</p>':''):'<p class="muted">'+esc(labels.stageTimelineUnavailable)+'</p>';
 const commandTimelineView=commandTimeline.available?'<h2>'+esc(labels.commandTimeline)+'</h2><p class="muted">'+esc(labels.commandTimelineNote)+'</p><div class="stage-timeline">'+commandTimeline.segments.map((segment,index)=>{const name=segment.kind==='gap'?labels.timelineGap:segment.name;const detail=name+': '+fmt(segment.durationUs)+' us (start '+fmt(segment.startUs)+' us)';return '<button type="button" class="timeline-segment '+(segment.kind==='gap'?'gap':'')+'" data-command-timeline-index="'+index+'" style="left:'+Math.max(0,Number(segment.leftPercent)||0)+'%;width:'+Math.max(0,Number(segment.widthPercent)||0)+'%" title="'+esc(detail)+'" aria-label="'+esc(detail)+'"></button>';}).join('')+'</div><div id="commandTimelineDetail" class="stage-detail muted">'+esc(labels.stageDetail)+': '+esc(labels.none)+'</div>'+(commandTimeline.overrunUs>0?'<p class="notice error">'+esc(labels.stageOverrun)+' ('+fmt(commandTimeline.overrunUs)+' us)</p>':''):'';
 const stageCompositionView='<h2>'+esc(labels.stageComposition)+'</h2><p class="muted">'+esc(labels.stageCompositionNote)+'</p>'+
 (composition.segments.length?'<div class="stage-composition">'+composition.segments.map((segment,index)=>{const name=segment.kind==='unaccounted'?labels.unaccounted:segment.name;const detail=name+': '+fmt(segment.us)+' us ('+Number(segment.frameSharePercent||0).toFixed(1)+'%)';return '<button type="button" class="stage-segment '+(segment.kind==='unaccounted'?'unaccounted':'')+'" data-stage-index="'+index+'" style="width:'+Math.max(0,Number(segment.widthPercent)||0)+'%" title="'+esc(detail)+'" aria-label="'+esc(detail)+'"></button>';}).join('')+'</div><div id="stageDetail" class="stage-detail muted">'+esc(labels.stageDetail)+': '+esc(labels.none)+'</div>':'<p class="muted">'+esc(labels.none)+'</p>')+
 (composition.overrunUs>0?'<p class="notice error">'+esc(labels.stageOverrun)+' ('+fmt(composition.overrunUs)+' us)</p>':'');
 view.innerHTML='<div class="metric-grid">'+
 '<div class="metric"><span>'+esc(labels.total)+'</span><b>'+fmt(frame.totalUs)+' us</b></div>'+
 '<div class="metric"><span>'+esc(labels.fps)+'</span><b>'+esc(fps)+'</b></div>'+
 '<div class="metric"><span>'+esc(labels.action)+'</span><b>'+esc(frame.action||labels.none)+'</b></div>'+
 '<div class="metric"><span>'+esc(labels.dirty)+'</span><b>'+fmt(frame.dirtyRectCount)+' / '+fmt(frame.dirtyAreaPercent)+'%</b></div></div>'+
 captureView+
 hotspotView+
 attributionView+
 frameDeltaView+
 stageTimelineView+
 commandTimelineView+
 stageCompositionView+
 '<div class="dirty"><span>'+esc(labels.dirtyCoverage)+'</span><span class="bar"><i style="width:'+dirtyPercent+'%"></i></span><span>'+dirtyPercent.toFixed(1)+'%</span></div>'+
 dirtyRectView+
 dirtyEvidenceView+
 '<p><strong>'+esc(labels.reason)+':</strong> '+esc(frame.reason||labels.none)+' <span class="muted">· timingComplete='+esc(frame.timingComplete===true?'true':'false')+'</span></p>'+
 '<h2>'+esc(labels.stages)+'</h2>'+ (stages.length?stages.map(([name,value])=>'<div class="stage"><code>'+esc(name)+'</code><span class="bar"><i style="width:'+Math.min(100,Math.round((Number(value)||0)*100/max))+'%"></i></span><span>'+fmt(value)+' us ('+(total?((Number(value)||0)*100/total).toFixed(1):'0.0')+'%)</span></div>').join(''):'<p class="muted">'+esc(labels.none)+'</p>')+(unaccountedUs>0?'<div class="stage"><code>'+esc(labels.unaccounted)+'</code><span class="bar"><i style="width:'+Math.min(100,Math.round(unaccountedUs*100/max))+'%"></i></span><span>'+fmt(unaccountedUs)+' us ('+(total?(unaccountedUs*100/total).toFixed(1):'0.0')+'%)</span></div>':'')+
 '<h2>'+esc(labels.pipeline)+'</h2><p class="muted">'+Object.entries(pipeline).map(([key,value])=>'<code>'+esc(key)+'='+esc(value)+'</code>').join(' · ')+'</p>'+ 
 '<h2>'+esc(labels.commands)+'</h2><p class="muted">'+esc(labels.commandTimingNote)+'</p>'+ (commands.length?'<table><tr><th>'+esc(labels.type)+'</th><th>'+esc(labels.owner)+'</th><th>'+esc(labels.time)+'</th><th>'+esc(labels.pixels)+'</th><th>'+esc(labels.samples)+'</th></tr>'+commands.map((item)=>'<tr><td>'+esc(item.type||labels.none)+'</td><td><code>'+esc(item.owner)+'</code></td><td>'+fmt(item.us)+' us</td><td>'+fmt(item.pixels)+'</td><td>'+fmt(item.samples)+'</td></tr>').join('')+'</table>'+(unattributedCount===commands.length?'<p class="muted">'+esc(labels.allUnattributed)+'</p>':unattributedCount>0?'<p class="muted">'+esc(labels.partialAttribution)+'</p>':''):'<p class="muted">'+esc(labels.noAttribution)+'</p>')+(frame.commandsTruncated?'<p class="muted">'+esc(labels.commandsTruncated)+'</p>':'')+(frame.nodesTruncated?'<p class="muted">'+esc(labels.nodesTruncated)+'</p>':'')+(Number.isSafeInteger(frame.commandInvalidSamples)&&frame.commandInvalidSamples>0?'<p class="muted">'+esc(labels.invalidCommandSamples)+': '+fmt(frame.commandInvalidSamples)+'</p>':'');
 const stageDetail=document.getElementById('stageDetail');
 if(stageDetail){view.querySelectorAll('[data-stage-index]').forEach((button)=>button.addEventListener('click',()=>{const segment=composition.segments[Number(button.dataset.stageIndex)];if(!segment)return;const name=segment.kind==='unaccounted'?labels.unaccounted:segment.name;stageDetail.innerHTML='<strong>'+esc(name)+'</strong>: '+fmt(segment.us)+' us ('+Number(segment.frameSharePercent||0).toFixed(1)+'%) · '+esc(labels.stageSource)+': '+esc(runtimeSource);}));}
 const timelineDetail=document.getElementById('timelineDetail');
 if(timelineDetail){view.querySelectorAll('[data-timeline-index]').forEach((button)=>button.addEventListener('click',()=>{const segment=timeline.segments[Number(button.dataset.timelineIndex)];if(!segment)return;const name=segment.kind==='gap'?labels.timelineGap:segment.name;timelineDetail.innerHTML='<strong>'+esc(name)+'</strong>: '+fmt(segment.durationUs)+' us (start '+fmt(segment.startUs)+' us) · '+esc(labels.stageSource)+': '+esc(runtimeSource);}));}
 const commandTimelineDetail=document.getElementById('commandTimelineDetail');
 if(commandTimelineDetail){view.querySelectorAll('[data-command-timeline-index]').forEach((button)=>button.addEventListener('click',()=>{const segment=commandTimeline.segments[Number(button.dataset.commandTimelineIndex)];if(!segment)return;const name=segment.kind==='gap'?labels.timelineGap:segment.name;const pixels=segment.kind==='gap'?'':' · '+fmt(segment.pixels)+' '+esc(labels.pixels);const stage=segment.stageName?' · '+esc(labels.aggregateStage)+': '+esc(segment.stageName):'';commandTimelineDetail.innerHTML='<strong>'+esc(name)+'</strong>: '+fmt(segment.durationUs)+' us (start '+fmt(segment.startUs)+' us)'+pixels+stage+' · '+esc(labels.stageSource)+': '+esc(runtimeSource);}));}
}
 renderFrameSummary();renderFrameTrend();renderAggregate();updateFrameSelector();slowestFrameButton.disabled=model.frames.length<1;slowestFrameButton.addEventListener('click',()=>{selectedFrameIndex=slowestFrameIndex;if(!visibleFrameIndexes().includes(selectedFrameIndex))anomalyOnly.checked=false;updateFrameSelector();render();});anomalyOnly.addEventListener('change',()=>{updateFrameSelector();render();});slider.addEventListener('input',render);render();
</script></body></html>`;
}

module.exports = { MAX_TRACE_BYTES, MAX_TRACE_LINES, parseRenderTrace, aggregateTrace, frameTimingBreakdown, frameStageComposition, frameStageTimeline, frameCommandTimeline, frameDirtyRepaintEvidence, frameHotspotSummary, frameCommandCostMap, frameDeltaSummary, frameAnomalySummary, frameAnomalyAttribution, frameTimingSummary, renderTraceHtml };
