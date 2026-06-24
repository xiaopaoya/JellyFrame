(function () {
  var state = document.getElementById("state");
  var line = document.getElementById("line");
  var net = document.getElementById("net");
  var audio = document.getElementById("audio");
  var location = document.getElementById("location");
  var data = document.getElementById("data");
  var store = document.getElementById("store");

  function hasStorage() {
    return typeof localStorage != "undefined";
  }

  function refresh() {
    var hidden = !!document.hidden;
    var online = navigator.onLine !== false;
    state.textContent = hidden ? "Hidden" : "Active";
    line.textContent = hidden ? "UI is paused; approved services may continue." : "Foreground services are live.";
    net.textContent = online ? "Online" : "Offline";
  }

  function rememberStatus(value) {
    if (!hasStorage()) {
      store.textContent = "No shadow";
      return;
    }
    localStorage.setItem("serviceStatus", value);
    store.textContent = localStorage.getItem("serviceStatus") || "None";
  }

  function applyServicePayload(payload) {
    if (!payload) {
      return;
    }
    data.textContent = payload.data || "Ready";
    audio.textContent = payload.audio || audio.textContent;
    rememberStatus(data.textContent);
  }

  function refreshLocation() {
    if (!navigator.geolocation || !navigator.geolocation.getCurrentPosition) {
      location.textContent = "No host";
      return;
    }
    navigator.geolocation.getCurrentPosition(function (position) {
      var coords = position.coords;
      location.textContent = String(coords.latitude).slice(0, 5) + "," + String(coords.longitude).slice(0, 6);
    }, function (error) {
      location.textContent = "E" + String(error.code);
    });
  }

  function fetchStatus() {
    if (typeof XMLHttpRequest == "undefined") {
      data.textContent = "Local";
      rememberStatus("Local");
      return;
    }
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/data/service-status.json", true);
    xhr.onload = function () {
      try {
        applyServicePayload(JSON.parse(xhr.responseText));
      } catch (error) {
        data.textContent = "Bad data";
        rememberStatus("Bad data");
      }
    };
    xhr.onerror = function () {
      data.textContent = "Error";
      rememberStatus("Error");
    };
    xhr.send();
  }

  document.addEventListener("visibilitychange", refresh);
  window.addEventListener("online", refresh);
  window.addEventListener("offline", refresh);
  refresh();
  if (hasStorage() && localStorage.getItem("serviceStatus")) {
    store.textContent = localStorage.getItem("serviceStatus");
  }
  fetchStatus();
  refreshLocation();
}());
