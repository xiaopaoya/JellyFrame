var value = 64;
var enabled = true;
var fill = document.getElementById("fill");
var pct = document.getElementById("pct");
var more = document.getElementById("more");
var less = document.getElementById("less");
var toggle = document.getElementById("switch");
var toast = document.getElementById("toast");

function render() {
  if (value < 0) {
    value = 0;
  }
  if (value > 100) {
    value = 100;
  }
  fill.style.width = String(value) + "%";
  pct.textContent = String(value);
  toggle.className = enabled ? "switch is-on" : "switch";
  toggle.setAttribute("aria-checked", enabled ? "true" : "false");
  toast.textContent = enabled ? "Motion uses paint-safe CSS only." : "Low power mode keeps UI readable.";
}

more.addEventListener("click", function () {
  value += 8;
  render();
});

less.addEventListener("click", function () {
  value -= 8;
  render();
});

toggle.addEventListener("click", function () {
  enabled = !enabled;
  render();
});

render();
"jelly controls ready";
