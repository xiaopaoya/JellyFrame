var time = document.getElementById("time");
var note = document.getElementById("note");
var refresh = document.getElementById("refresh");
var steps = document.getElementById("steps");
var heart = document.getElementById("heart");
var samples = [
  ["07:30", "Morning routine", "6.4k", "72"],
  ["12:05", "Lunch window", "8.1k", "76"],
  ["18:40", "Evening walk", "11k", "88"],
  ["22:15", "Rest mode", "12k", "64"]
];
var index = 0;

function renderClock() {
  var item = samples[index];
  time.textContent = item[0];
  note.textContent = item[1];
  steps.textContent = item[2];
  heart.textContent = item[3];
  index = (index + 1) % samples.length;
}

refresh.addEventListener("click", renderClock);
renderClock();
setInterval(renderClock, 1000);
