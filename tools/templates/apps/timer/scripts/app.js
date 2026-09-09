var remaining = 60000;
var deadline = 0;
var running = false;
var ticker = 0;
var state = "Ready";
function two(value) { return value < 10 ? "0" + String(value) : String(value); }
function renderTimer() {
  if (running) {
    remaining = Math.max(0, deadline - Date.now());
    if (remaining == 0) { stop(); state = "Complete"; }
  }
  var seconds = Math.ceil(remaining / 1000);
  document.getElementById("time").textContent = two(Math.floor(seconds / 60)) + ":" + two(seconds % 60);
  document.getElementById("state").textContent = state;
  document.getElementById("toggle").textContent = running ? "Pause" : remaining == 0 ? "Again" : "Start";
  var pct = Math.ceil(remaining / 600);
  document.getElementById("ring").style.background = "conic-gradient(#67DDED 0% " + pct + "%, #243747 " + pct + "% 100%)";
}
function stop() { running = false; if (ticker) { clearInterval(ticker); ticker = 0; } }
document.getElementById("toggle").addEventListener("click", function () {
  if (running) { remaining = Math.max(0, deadline - Date.now()); stop(); state = remaining ? "Paused" : "Complete"; }
  else { if (!remaining) { remaining = 60000; } deadline = Date.now() + remaining; running = true; state = "Focusing"; ticker = setInterval(renderTimer, 250); }
  renderTimer();
});
document.getElementById("reset").addEventListener("click", function () { stop(); remaining = 60000; state = "Ready"; renderTimer(); });
renderTimer();
