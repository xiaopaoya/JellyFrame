var time = document.getElementById("time");
var state = document.getElementById("state");
var tick = document.getElementById("tick");
var toggle = document.getElementById("toggle");
var reset = document.getElementById("reset");
var seconds = 0;
var running = false;

function twoDigits(value) {
  return value < 10 ? "0" + String(value) : String(value);
}

function renderTimer() {
  var minutes = Math.floor(seconds / 60);
  var rest = seconds - minutes * 60;
  time.textContent = twoDigits(minutes) + ":" + twoDigits(rest);
  state.textContent = running ? "Running" : "Stopped";
  toggle.textContent = running ? "Stop" : "Start";
}

tick.addEventListener("click", function () {
  if (running) {
    seconds += 1;
  }
  renderTimer();
});

toggle.addEventListener("click", function () {
  running = !running;
  renderTimer();
});

reset.addEventListener("click", function () {
  seconds = 0;
  running = false;
  renderTimer();
});

renderTimer();
"timer ready";
