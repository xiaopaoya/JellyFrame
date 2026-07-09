import { format_minutes } from "./time.js";

var minutes = 25;
var value = document.getElementById("value");
var status = document.getElementById("status");

document.getElementById("add").addEventListener("click", function () {
  minutes += 5;
  value.textContent = format_minutes(minutes);
  status.textContent = "Updated";
});
