var temp = document.getElementById("temp");
var summary = document.getElementById("summary");
var wind = document.getElementById("wind");
var rain = document.getElementById("rain");
var updated = document.getElementById("updated");
var app = document.getElementById("app");
var modes = {
  hourly: ["27", "Next: light rain", "9", "35", "1h"],
  daily: ["26", "High 29 Low 22", "8", "20", "Today"],
  air: ["42", "AQI good", "5", "10", "AQI"]
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
