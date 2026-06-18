var time = document.getElementById("time");
var state = document.getElementById("state");
var toggle = document.getElementById("toggle");
var round = document.getElementById("round");
var app = document.getElementById("app");
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
  round.textContent = String(Math.floor(seconds / 60) + 1) + "/4";
}

function stopTimer() {
  if (intervalId != 0) {
    clearInterval(intervalId);
    intervalId = 0;
  }
  running = false;
}

app.addEventListener("click", function (event) {
  var button = event.target.closest("button");
  if (!button || !button.dataset.action) {
    return;
  }
  if (button.dataset.action == "tick") {
    seconds += 1;
  } else if (button.dataset.action == "toggle") {
    if (running) {
      stopTimer();
    } else {
      running = true;
      intervalId = setInterval(function () {
        seconds += 1;
        renderTimer();
      }, 1000);
    }
  } else if (button.dataset.action == "reset") {
    stopTimer();
    seconds = 0;
  }
  renderTimer();
});

renderTimer();
