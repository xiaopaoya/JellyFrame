var tone = new Audio("/audio/tone.wav");
var button = document.getElementById("play");
var statusText = document.getElementById("status");

tone.onended = function () {
  statusText.textContent = "Ended";
};

tone.onerror = function () {
  statusText.textContent = "Unavailable";
};

button.addEventListener("click", function () {
  try {
    tone.volume = 1;
    tone.play();
    statusText.textContent = "Playing";
  } catch (error) {
    statusText.textContent = "Unavailable";
  }
});
