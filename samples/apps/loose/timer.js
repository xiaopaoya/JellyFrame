var time = document.getElementById("time");
var state = document.getElementById("state");
var tick = document.getElementById("tick");
var toggle = document.getElementById("toggle");
var reset = document.getElementById("reset");
var note = document.getElementById("note");
var seconds = 0;
var running = false;
var intervalId = 0;

function twoDigits(value) {
  return value < 10 ? "0" + String(value) : String(value);
}

function renderTimer() {
  var minutes = Math.floor(seconds / 60);
  var rest = seconds - minutes * 60;
  time.textContent = twoDigits(minutes) + ":" + twoDigits(rest);
  state.textContent = running ? "Running" : "Stopped";
  toggle.textContent = running ? "Stop" : "Start";
  note.textContent = running ? "setInterval active" : "Timer idle";
}

tick.addEventListener("click", function () {
  seconds += 1;
  renderTimer();
});

toggle.addEventListener("click", function () {
  if (running) {
    clearInterval(intervalId);
    intervalId = 0;
    running = false;
  } else {
    running = true;
    intervalId = setInterval(function () {
      seconds += 1;
      renderTimer();
    }, 1000);
  }
  renderTimer();
});

reset.addEventListener("click", function () {
  if (intervalId != 0) {
    clearInterval(intervalId);
    intervalId = 0;
  }
  seconds = 0;
  running = false;
  renderTimer();
});

renderTimer();
"timer ready";
