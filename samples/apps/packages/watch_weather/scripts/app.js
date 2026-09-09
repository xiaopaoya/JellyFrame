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

function saveMode() {
  if (typeof localStorage != "undefined") {
    try { localStorage.setItem("weatherMode", currentMode); } catch (error) { /* The view remains usable without storage. */ }
  }
}
if (typeof localStorage != "undefined") {
  try { currentMode = localStorage.getItem("weatherMode") || "daily"; } catch (error) {}
}
renderWeather(currentMode);
function useSample() { document.getElementById("source").textContent = "Sample"; }
// This data route is supplied by the host; it never loads remote page resources.
if (typeof XMLHttpRequest != "undefined") {
  var request = new XMLHttpRequest();
  request.open("GET", "/data/weather.json", true);
  request.onload = function () {
    if (request.status < 200 || request.status >= 300) { useSample(); return; }
    try {
      var payload = JSON.parse(request.responseText);
      var data = payload.modes ? payload.modes[currentMode] : payload;
      if (!data || data.temp == null || !data.condition || !data.summary || data.wind == null || data.rain == null) { useSample(); return; }
      modes[currentMode] = data;
      renderWeather(currentMode);
      // Desktop host fixtures are demonstration data, not a live weather service.
      document.getElementById("source").textContent = "Demo";
    } catch (error) { useSample(); }
  };
  request.onerror = useSample;
  request.send();
}
