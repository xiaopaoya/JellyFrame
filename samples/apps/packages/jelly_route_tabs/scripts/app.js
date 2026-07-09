var pages = {
  today: { title: "Today", summary: "Your next focus block starts at 09:30.", detail: "Morning plan" },
  focus: { title: "Focus", summary: "One block is ready to start.", detail: "25 minute timer" },
  settings: { title: "Settings", summary: "Display and notification choices.", detail: "Quiet mode enabled" }
};

function currentRoute() {
  return location.hash ? location.hash.slice(1) : "today";
}

function renderRoute() {
  var route = currentRoute();
  var page = pages[route] || pages.today;
  document.getElementById("title").textContent = page.title;
  document.getElementById("summary").textContent = page.summary;
  document.getElementById("detail").textContent = page.detail;
  var buttons = document.querySelectorAll("[data-route]");
  for (var index = 0; index < buttons.length; ++index) {
    buttons[index].classList.toggle("active", buttons[index].getAttribute("data-route") === route);
  }
}

var tabs = document.querySelectorAll("[data-route]");
for (var index = 0; index < tabs.length; ++index) {
  tabs[index].addEventListener("click", function () { location.hash = this.getAttribute("data-route"); });
}
window.addEventListener("hashchange", renderRoute);
renderRoute();
