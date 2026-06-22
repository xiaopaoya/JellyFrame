var tone = new Audio("/audio/tone.wav");
var button = document.getElementById("play");
var statusText = document.getElementById("status");

button.addEventListener("click", function () {
  try {
    tone.volume = 1;
    tone.play();
    statusText.textContent = "Playing";
  } catch (error) {
    statusText.textContent = "Unavailable";
  }
});
