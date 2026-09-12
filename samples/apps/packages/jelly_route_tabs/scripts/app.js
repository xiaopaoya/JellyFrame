var pages = {
  today: {title: "Today", detail: "UP NEXT", value: "09:30", summary: "Morning reading"},
  focus: {title: "Focus", detail: "SESSION LENGTH", value: "25 min", summary: "One thing at a time"},
  settings: {title: "Settings", detail: "NOTIFICATIONS", value: "Quiet", summary: "Your personal space"}
};
function renderRoute() {
  var route = location.hash ? location.hash.slice(1) : "today";
  if (!pages[route]) { route = "today"; }
  var page = pages[route];
  document.getElementById("title").textContent = page.title;
  document.getElementById("detail").textContent = page.detail;
  document.getElementById("value").textContent = page.value;
  document.getElementById("summary").textContent = page.summary;
  var tabs = document.querySelectorAll("[data-route]");
  for (var i = 0; i < tabs.length; i += 1) { tabs[i].classList.toggle("active", tabs[i].dataset.route == route); }
}
document.getElementById("app").addEventListener("click", function (event) {
  var button = event.target.closest("button");
  if (button && button.dataset.route) { location.hash = button.dataset.route; }
});
document.getElementById("back").addEventListener("click", function () { history.back(); });
window.addEventListener("hashchange", renderRoute);
window.addEventListener("popstate", renderRoute);
renderRoute();
