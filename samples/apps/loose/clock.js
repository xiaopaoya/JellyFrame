var time = document.getElementById("time");
var date = document.getElementById("date");
var refresh = document.getElementById("refresh");
var note = document.getElementById("note");
var samples = [
  ["07:30", "Morning routine"],
  ["12:05", "Lunch window"],
  ["18:40", "Evening walk"],
  ["22:15", "Rest mode"]
];
var index = 0;

function renderClock() {
  var item = samples[index];
  time.textContent = item[0];
  date.textContent = item[1];
  note.textContent = "Timer tick " + String(index + 1);
  index = (index + 1) % samples.length;
}

refresh.addEventListener("click", renderClock);
renderClock();
setInterval(renderClock, 1000);
"clock ready";
