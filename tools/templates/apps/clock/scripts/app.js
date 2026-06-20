var time = document.getElementById("time");
var note = document.getElementById("note");
var phase = document.getElementById("phase");
var refresh = document.getElementById("refresh");
var zoneButton = document.getElementById("zoneButton");
var zone = document.getElementById("zone");
var steps = document.getElementById("steps");
var heart = document.getElementById("heart");
var samples = [
  ["07:30", "Morning", "Start steady", "6.4k", "72"],
  ["12:05", "Midday", "Refuel soon", "8.1k", "76"],
  ["18:40", "Evening", "Walk window", "11k", "88"],
  ["22:15", "Night", "Wind down", "12k", "64"]
];
var index = 0;
var zones = ["08", "UTC+8", "Trip"];
var zoneIndex = 0;

function renderClock() {
  var item = samples[index];
  time.textContent = item[0];
  phase.textContent = item[1];
  note.textContent = item[2];
  steps.textContent = item[3];
  heart.textContent = item[4];
  index = (index + 1) % samples.length;
}

refresh.addEventListener("click", renderClock);
zoneButton.addEventListener("click", function () {
  zoneIndex = (zoneIndex + 1) % zones.length;
  zone.textContent = zones[zoneIndex];
});
renderClock();
setInterval(renderClock, 1000);
