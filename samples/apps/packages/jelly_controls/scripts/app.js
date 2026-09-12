var level = document.getElementById("brightness");
var quiet = document.getElementById("quiet");
var goal = document.getElementById("goal");
var toast = document.getElementById("toast");
function edit() { document.getElementById("pct").textContent = level.value + "%"; toast.textContent = "Changes not saved yet."; }
level.addEventListener("input", edit);
quiet.addEventListener("change", edit);
goal.addEventListener("input", edit);
document.getElementById("save").addEventListener("click", function () {
  toast.textContent = goal.value.length ? (quiet.checked ? "Saved / Quiet mode on" : "Saved / Quiet mode off") : "Enter a session name.";
});
// State stays in this app session; this example makes no hardware or storage claim.
