var alarms = [
  { id: "alarm-1", time: "07:00", repeat: "Weekdays", sound: "Chime", enabled: true },
  { id: "alarm-2", time: "08:30", repeat: "Weekend", sound: "Rise", enabled: false },
  { id: "alarm-3", time: "09:15", repeat: "Every day", sound: "Pulse", enabled: true }
];
var editIndex = -1;
var repeats = ["Weekdays", "Weekend", "Every day", "Once"];
var sounds = ["Chime", "Rise", "Pulse"];
var touchStartX = 0;
var touchRow = null;
function byId(id) { return document.getElementById(id); }
function showPage(name) {
  byId("home-page").classList.toggle("hidden", name !== "home");
  byId("edit-page").classList.toggle("hidden", name !== "edit");
}
function renderAlarms() {
  var list = byId("alarm-list");
  list.textContent = "";
  alarms.forEach(function (alarm, index) {
    var row = document.createElement("article");
    row.className = "alarm-row";
    row.dataset.index = String(index);
    row.id = alarm.id;
    var main = document.createElement("button");
    main.className = "alarm-main";
    main.id = alarm.id + "-main";
    main.type = "button";
    var time = document.createElement("span");
    time.className = "alarm-time";
    time.textContent = alarm.time;
    var meta = document.createElement("span");
    meta.className = "alarm-meta";
    meta.textContent = alarm.repeat + " / " + alarm.sound;
    main.append(time, meta);
    var toggle = document.createElement("button");
    toggle.className = "toggle" + (alarm.enabled ? " is-on" : "");
    toggle.id = alarm.id + "-toggle";
    toggle.type = "button";
    toggle.setAttribute("aria-label", "Toggle alarm");
    toggle.setAttribute("aria-pressed", alarm.enabled ? "true" : "false");
    var actions = document.createElement("span");
    actions.className = "alarm-actions";
    var remove = document.createElement("button");
    remove.className = "delete-button";
    remove.id = alarm.id + "-delete";
    remove.type = "button";
    remove.textContent = "Delete";
    actions.append(remove);
    row.append(main, toggle, actions);
    list.append(row);
  });
}
function openEditor(index) {
  editIndex = index;
  var alarm = index < 0 ? { time: "07:00", repeat: "Weekdays", sound: "Chime" } : alarms[index];
  byId("edit-title").textContent = index < 0 ? "New alarm" : "Edit alarm";
  byId("alarm-time").value = alarm.time;
  byId("repeat-value").textContent = alarm.repeat;
  byId("sound-value").textContent = alarm.sound;
  showPage("edit");
}
function cycleValue(id, values) {
  var current = byId(id).textContent;
  byId(id).textContent = values[(values.indexOf(current) + 1) % values.length];
}
byId("add-alarm").addEventListener("click", function () { openEditor(-1); });
byId("edit-back").addEventListener("click", function () { showPage("home"); });
byId("save-alarm").addEventListener("click", function () {
  var item = {
    id: editIndex < 0 ? "alarm-new" : alarms[editIndex].id,
    time: byId("alarm-time").value || "07:00",
    repeat: byId("repeat-value").textContent,
    sound: byId("sound-value").textContent,
    enabled: editIndex < 0 ? true : alarms[editIndex].enabled
  };
  if (editIndex < 0) alarms.push(item); else alarms[editIndex] = item;
  renderAlarms();
  showPage("home");
});
byId("repeat-button").addEventListener("click", function () { cycleValue("repeat-value", repeats); });
byId("sound-button").addEventListener("click", function () { cycleValue("sound-value", sounds); });
byId("alarm-list").addEventListener("click", function (event) {
  var row = event.target.closest(".alarm-row");
  if (!row) return;
  var index = Number(row.dataset.index);
  if (event.target.closest(".delete-button")) { alarms.splice(index, 1); renderAlarms(); return; }
  if (event.target.closest(".toggle")) { alarms[index].enabled = !alarms[index].enabled; renderAlarms(); return; }
  if (event.target.closest(".alarm-main")) openEditor(index);
});
byId("alarm-list").addEventListener("touchstart", function (event) {
  touchStartX = event.touches[0].clientX;
  touchRow = event.target.closest(".alarm-row");
});
byId("alarm-list").addEventListener("touchend", function (event) {
  if (!touchRow) return;
  var delta = event.changedTouches[0].clientX - touchStartX;
  if (delta < -30) touchRow.classList.add("is-revealed");
  if (delta > 30) touchRow.classList.remove("is-revealed");
  touchRow = null;
});
renderAlarms();
