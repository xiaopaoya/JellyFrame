var temp = document.getElementById("temp");
var summary = document.getElementById("summary");
var condition = document.getElementById("condition");
var icon = document.getElementById("icon");
var wind = document.getElementById("wind");
var rain = document.getElementById("rain");
var updated = document.getElementById("updated");
var net = document.getElementById("net");
var visible = document.getElementById("visible");
var app = document.getElementById("app");
var modes = {
  hourly: { temp: "27", condition: "Rain soon", summary: "Next hour 35%", wind: "9", rain: "35", updated: "1h", icon: "rain" },
  daily: { temp: "26", condition: "Cloudy", summary: "High 29 Low 22", wind: "8", rain: "20", updated: "Today", icon: "cloudy" },
  air: { temp: "42", condition: "Air", summary: "AQI good", wind: "5", rain: "10", updated: "AQI", icon: "haze" }
};
var currentMode = "daily";

function iconPath(name) {
  if (name == "sunny") {
    return "assets/sunny.bmp";
  }
  if (name == "rain") {
    return "assets/rain.bmp";
  }
  if (name == "haze") {
    return "assets/haze.bmp";
  }
  return "assets/cloudy.bmp";
}

function hasStorage() {
  return typeof localStorage != "undefined";
}

function applyWeather(data, mode) {
  if (!data) {
    return;
  }
  currentMode = mode || currentMode;
  temp.textContent = data.temp || temp.textContent;
  condition.textContent = data.condition || condition.textContent;
  summary.textContent = data.summary || summary.textContent;
  wind.textContent = data.wind || wind.textContent;
  rain.textContent = data.rain || rain.textContent;
  updated.textContent = data.updated || updated.textContent;
  icon.setAttribute("src", iconPath(data.icon));
  if (hasStorage()) {
    localStorage.setItem("weatherMode", currentMode);
  }
}

function parseWeatherPayload(text) {
  try {
    return JSON.parse(text);
  } catch (error) {
    return null;
  }
}

function refreshSystemState() {
  net.textContent = navigator.onLine ? "Online" : "Offline";
  visible.textContent = document.visibilityState;
}

function fetchWeather() {
  if (typeof XMLHttpRequest == "undefined") {
    updated.textContent = "Local";
    return;
  }
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "app://weather", true);
  xhr.onload = function () {
    var payload = parseWeatherPayload(xhr.responseText);
    if (payload && payload.modes) {
      modes = payload.modes;
      applyWeather(modes[currentMode] || modes.daily, currentMode);
    } else if (payload) {
      applyWeather(payload, currentMode);
    }
  };
  xhr.onerror = function () {
    updated.textContent = "Local";
  };
  xhr.send();
}

app.addEventListener("click", function (event) {
  var button = event.target.closest("button");
  if (!button || !button.dataset.mode) {
    return;
  }
  applyWeather(modes[button.dataset.mode], button.dataset.mode);
});

document.addEventListener("visibilitychange", refreshSystemState);

if (hasStorage() && localStorage.getItem("weatherMode")) {
  currentMode = localStorage.getItem("weatherMode");
}
applyWeather(modes[currentMode] || modes.daily, currentMode);
refreshSystemState();
fetchWeather();
