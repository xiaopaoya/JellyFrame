var display = document.getElementById("display");
var status = document.getElementById("status");
var pending = 0;
var operator = "";
var fresh = true;

function numberValue() {
  return parseInt(display.value, 10) || 0;
}

function setDisplay(value) {
  display.value = String(value);
}

function pressDigit(digit) {
  if (fresh || display.value == "0") {
    setDisplay(digit);
    fresh = false;
    return;
  }
  display.value = display.value + digit;
}

function applyOperator(nextOperator) {
  pending = numberValue();
  operator = nextOperator;
  fresh = true;
  status.textContent = String(pending) + " " + operator;
}

function equals() {
  var right = numberValue();
  var result = pending;
  if (operator == "+") {
    result = pending + right;
  } else if (operator == "-") {
    result = pending - right;
  }
  setDisplay(result);
  status.textContent = "Result";
  operator = "";
  fresh = true;
}

function bindDigit(id, digit) {
  document.getElementById(id).addEventListener("click", function () {
    pressDigit(digit);
  });
}

bindDigit("n0", "0");
bindDigit("n1", "1");
bindDigit("n2", "2");
bindDigit("n3", "3");
bindDigit("n4", "4");
bindDigit("n5", "5");
bindDigit("n6", "6");
bindDigit("n7", "7");
bindDigit("n8", "8");
bindDigit("n9", "9");

document.getElementById("add").addEventListener("click", function () { applyOperator("+"); });
document.getElementById("sub").addEventListener("click", function () { applyOperator("-"); });
document.getElementById("eq").addEventListener("click", equals);
document.getElementById("clear").addEventListener("click", function () {
  pending = 0;
  operator = "";
  fresh = true;
  setDisplay(0);
  status.textContent = "Ready";
});

"calculator ready";
