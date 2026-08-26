(function () {
  var mover = document.getElementById("ancestorMover");
  var selfClip = document.getElementById("selfClip");
  var scaleTarget = document.getElementById("scaleTarget");
  var state = document.getElementById("state");
  var tick = 0;

  function update() {
    tick += 1;
    mover.style.transform = "rotate(" + ((tick % 8) * 45) + "deg)";
    selfClip.style.transform = "rotate(" + (((tick + 2) % 8) * 45) + "deg)";
    scaleTarget.style.transform = tick % 4 === 0 ? "scale(0)" : "scale(1)";
    state.textContent = "Tick " + tick;
  }

  update();
  setInterval(update, 500);
}());
