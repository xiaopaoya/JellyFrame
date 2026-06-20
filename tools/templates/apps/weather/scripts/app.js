var app = document.getElementById("app");
var temp = document.getElementById("temp");
var summary = document.getElementById("summary");
var wind = document.getElementById("wind");
var rain = document.getElementById("rain");
var updated = document.getElementById("updated");
var modes = {
  now: ["24", "Bright breaks", "7", "12%", "Now"],
  hourly: ["26", "Clear next hour", "8", "8%", "14:00"],
  daily: ["25", "Soft evening", "6", "20%", "Today"],
  air: ["38", "Air is clean", "5", "4%", "AQI"]
};

app.addEventListener("click", function (event) {
  var button = event.target.closest("button");
  if (!button || !button.dataset.mode) {
    return;
  }
  var data = modes[button.dataset.mode];
  temp.textContent = data[0];
  summary.textContent = data[1];
  wind.textContent = data[2];
  rain.textContent = data[3];
  updated.textContent = data[4];
});
