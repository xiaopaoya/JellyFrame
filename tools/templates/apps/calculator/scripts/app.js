var display = document.getElementById("display");
var status = document.getElementById("status");
var pending = 0;
var operator = "";
var fresh = true;
function clearAll() { pending = 0; operator = ""; fresh = true; display.value = "0"; status.textContent = "QUICK MATH"; }
function calculate() {
  if (!operator || fresh) { return true; }
  var right = parseInt(display.value, 10);
  var value = operator == "+" ? pending + right : pending - right;
  if (value > 99999999 || value < -9999999) { clearAll(); status.textContent = "LIMIT / 8 DIGITS"; return false; }
  display.value = String(value); pending = value; return true;
}
document.getElementById("app").addEventListener("click", function (event) {
  var key = event.target.closest("button");
  if (!key) { return; }
  var value = key.dataset.key;
  if (value == "C") { clearAll(); return; }
  if (value == "+" || value == "-") {
    if (!calculate()) { return; }
    pending = parseInt(display.value, 10); operator = value; fresh = true;
    status.textContent = String(pending) + " " + operator; return;
  }
  if (value == "=") {
    if (!operator || fresh) { return; }
    if (calculate()) { operator = ""; fresh = true; status.textContent = "RESULT"; } return;
  }
  if (fresh || display.value == "0") { display.value = value; fresh = false; }
  else if (display.value.length < 8) { display.value = display.value + value; }
});
