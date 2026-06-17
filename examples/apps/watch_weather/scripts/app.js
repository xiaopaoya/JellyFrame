var temp = document.getElementById("temp");
var summary = document.getElementById("summary");
var modes = {
  hourly: ["27", "Next: light rain"],
  daily: ["26", "High 29 · Low 22"],
  air: ["42", "AQI good"],
  settings: ["C", "Metric units"]
};

document.body.addEventListener("click", function (event) {
  var button = event.target.closest("button");
  if (!button || !button.dataset.mode) {
    return;
  }
  var data = modes[button.dataset.mode];
  temp.textContent = data[0];
  summary.textContent = data[1];
});
