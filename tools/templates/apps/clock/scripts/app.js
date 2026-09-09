var offset = 0;
var twelveHour = false;
function two(value) { return value < 10 ? "0" + String(value) : String(value); }
function renderClock() {
  var total = Math.floor(Date.now() / 1000) + offset * 3600;
  var day = ((total % 86400) + 86400) % 86400;
  var hour = Math.floor(day / 3600);
  var minute = Math.floor(day / 60) % 60;
  var second = day % 60;
  var shown = twelveHour ? (hour % 12 || 12) : hour;
  document.getElementById("time").textContent = two(shown) + ":" + two(minute);
  document.getElementById("seconds").textContent = two(second) + " seconds" + (twelveHour ? (hour < 12 ? " / AM" : " / PM") : "");
  document.getElementById("phase").textContent = hour < 6 ? "Night" : hour < 12 ? "Morning" : hour < 18 ? "Afternoon" : "Evening";
  document.getElementById("dayfill").style.width = String(Math.floor(day * 100 / 86400)) + "%";
}
document.getElementById("zoneButton").addEventListener("click", function () {
  offset = offset == 0 ? 8 : 0;
  this.textContent = offset == 0 ? "UTC" : "UTC+8";
  renderClock();
});
document.getElementById("formatButton").addEventListener("click", function () {
  twelveHour = !twelveHour;
  this.textContent = twelveHour ? "12 hour" : "24 hour";
  renderClock();
});
renderClock();
setInterval(renderClock, 1000);
