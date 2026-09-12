var mode = "breathe";
var playing = true;
var elapsed = 0;
var last = Date.now();
var raf = 0;
var orb = document.getElementById("orb");
function paint() {
  var wave = (1 - Math.cos(elapsed * Math.PI / 1600)) / 2;
  if (mode == "breathe") {
    orb.style.transform = "scale(" + String(.76 + wave * .24) + ")";
    orb.style.opacity = String(.6 + wave * .4);
  } else {
    orb.style.transform = "translate(" + String(Math.round(-28 + wave * 56)) + "px, 0px)";
    orb.style.opacity = "1";
  }
}
function step() {
  raf = 0;
  if (!playing) { return; }
  var now = Date.now(); elapsed += Math.max(0, Math.min(100, now - last)); last = now;
  paint(); raf = requestAnimationFrame(step);
}
document.getElementById("app").addEventListener("click", function (event) {
  var button = event.target.closest("button");
  if (!button || !button.dataset.mode) { return; }
  mode = button.dataset.mode; elapsed = 0;
  document.getElementById("motion-label").textContent = mode == "breathe" ? "Breathe" : "Slide";
  document.getElementById("breathe").classList.toggle("active", mode == "breathe");
  document.getElementById("slide").classList.toggle("active", mode == "slide");
  paint();
});
document.getElementById("play").addEventListener("click", function () {
  playing = !playing; this.textContent = playing ? "Pause" : "Play";
  if (playing) { last = Date.now(); raf = requestAnimationFrame(step); }
  else if (raf) { cancelAnimationFrame(raf); raf = 0; }
});
document.getElementById("breathe").classList.add("active");
paint(); raf = requestAnimationFrame(step);
