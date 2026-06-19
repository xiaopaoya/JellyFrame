var count = 0;
var button = document.getElementById("counter");
var status = document.getElementById("status");

button.addEventListener("click", function (event) {
  count += 1;
  button.textContent = String(count);
  status.textContent = "Last click at " + event.clientX + "," + event.clientY;
  event.preventDefault();
});

"event listener ready";
