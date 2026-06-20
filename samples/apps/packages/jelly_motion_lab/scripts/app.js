var frameLabel = document.getElementById("frame");
var pulse = document.getElementById("pulse");
var tick = 0;

function step() {
  tick = tick + 1;
  if (frameLabel) {
    var label = tick < 10 ? "0" + String(tick) : String(tick);
    frameLabel.textContent = label;
  }
  if (pulse) {
    pulse.className = tick % 20 < 10 ? "pill active" : "pill";
  }
  requestAnimationFrame(step);
}

requestAnimationFrame(step);
