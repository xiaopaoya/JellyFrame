var activities = [
  { name: "Run", icon: "RUN", goal: "20 min", copy: "A steady effort with a light finish.", metrics: ["00:12", "5:42", "128"] },
  { name: "Cycle", icon: "RIDE", goal: "30 min", copy: "Keep the cadence smooth and controlled.", metrics: ["08:24", "18.4", "134"] },
  { name: "Swim", icon: "SWIM", goal: "12 min", copy: "Short intervals with relaxed recovery.", metrics: ["04:10", "1:48", "121"] }
];
var chosen = 0;
var page = 0;
var startX = 0;
function id(name) { return document.getElementById(name); }
function show(name) { ["home-page", "prepare-page", "session-page"].forEach(function (key) { id(key).classList.toggle("hidden", key !== name); }); }
function renderActivities() {
  var root = id("workout-list");
  root.textContent = "";
  activities.forEach(function (activity, index) {
    var button = document.createElement("button");
    button.className = "activity-button"; button.type = "button"; button.dataset.index = String(index); button.id = "workout-" + String(index);
    var icon = document.createElement("span"); icon.className = "activity-icon"; icon.textContent = activity.icon;
    var copy = document.createElement("span"); copy.className = "activity-copy";
    var title = document.createElement("strong"); title.textContent = activity.name;
    var detail = document.createElement("span"); detail.textContent = activity.goal + " target"; copy.append(title, detail);
    var arrow = document.createElement("span"); arrow.className = "chevron"; arrow.textContent = ">";
    button.append(icon, copy, arrow); root.append(button);
  });
}
function renderPrepare() {
  var activity = activities[chosen];
  id("prepare-title").textContent = activity.name; id("prepare-icon").textContent = activity.icon;
  id("prepare-goal").textContent = activity.goal; id("prepare-copy").textContent = activity.copy;
  id("target-range").value = parseInt(activity.goal, 10); id("target-value").textContent = activity.goal;
}
function renderSession() {
  var activity = activities[chosen];
  id("session-title").textContent = activity.name;
  ["elapsed", "pace", "heart"].forEach(function (key, index) { id(key).textContent = activity.metrics[index]; });
  document.querySelectorAll(".metric-page").forEach(function (element, index) { element.classList.toggle("is-active", index === page); });
  id("page-label").textContent = String(page + 1) + " / 3";
  id("page-dots").textContent = page === 0 ? "o o o" : page === 1 ? "o o o" : "o o o";
}
id("workout-list").addEventListener("click", function (event) {
  var button = event.target.closest(".activity-button"); if (!button) return;
  chosen = Number(button.dataset.index); renderPrepare(); show("prepare-page");
});
id("prepare-back").addEventListener("click", function () { show("home-page"); });
id("start-workout").addEventListener("click", function () { page = 0; renderSession(); show("session-page"); });
id("session-back").addEventListener("click", function () { show("home-page"); });
id("target-range").addEventListener("input", function () { id("target-value").textContent = this.value + " min"; id("prepare-goal").textContent = this.value + " min"; });
function movePage(delta) { page = (page + delta + 3) % 3; renderSession(); }
id("prev-page").addEventListener("click", function () { movePage(-1); });
id("next-page").addEventListener("click", function () { movePage(1); });
id("metric-pages").addEventListener("touchstart", function (event) { startX = event.touches[0].clientX; });
id("metric-pages").addEventListener("touchend", function (event) { var delta = event.changedTouches[0].clientX - startX; if (Math.abs(delta) > 30) movePage(delta < 0 ? 1 : -1); });
renderActivities();
