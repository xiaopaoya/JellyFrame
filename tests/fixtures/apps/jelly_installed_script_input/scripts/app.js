var count = 0;
var status = document.getElementById("status");
var action = document.getElementById("action");

action.addEventListener("click", function () {
  count += 1;
  status.textContent = "Tapped: " + String(count);
  action.className = "changed";
});
