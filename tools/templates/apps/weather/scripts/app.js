var modes = {
  hourly: {temp: "27", condition: "Rain soon", summary: "Next hour / 35% rain", wind: "9", rain: "35", icon: "rain"},
  daily: {temp: "26", condition: "Cloudy", summary: "High 29 / Low 22", wind: "8", rain: "20", icon: "cloudy"},
  air: {temp: "42", condition: "Good air", summary: "Air quality index", wind: "5", rain: "10", icon: "haze"}
};
var currentMode = "daily";
function renderWeather(mode) {
  if (!modes[mode]) { mode = "daily"; }
  currentMode = mode;
  var data = modes[mode];
  document.getElementById("temp").textContent = String(data.temp);
  document.getElementById("unit").textContent = mode == "air" ? "AQI" : "C";
  document.getElementById("condition").textContent = data.condition;
  document.getElementById("summary").textContent = data.summary;
  document.getElementById("wind").textContent = String(data.wind) + " km/h";
  document.getElementById("rain").textContent = String(data.rain) + "%";
  var icon = data.icon;
  if (icon != "sunny" && icon != "rain" && icon != "haze") { icon = "cloudy"; }
  document.getElementById("icon").setAttribute("src", "assets/" + icon + ".bmp");
  var tabs = document.querySelectorAll("[data-mode]");
  for (var i = 0; i < tabs.length; i += 1) {
    tabs[i].classList.toggle("selected", tabs[i].dataset.mode == mode);
  }
}
document.getElementById("app").addEventListener("click", function (event) {
  var button = event.target.closest("button");
  if (button && button.dataset.mode) {
    renderWeather(button.dataset.mode);
    saveMode();
  }
});
function saveMode() {}
renderWeather(currentMode);
