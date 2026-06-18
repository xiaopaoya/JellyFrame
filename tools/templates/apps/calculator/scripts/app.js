var display = document.getElementById("display");
var status = document.getElementById("status");
var app = document.getElementById("app");
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

function clearAll() {
  pending = 0;
  operator = "";
  fresh = true;
  setDisplay(0);
  status.textContent = "Ready";
}

app.addEventListener("click", function (event) {
  var button = event.target.closest("button");
  if (!button) {
    return;
  }
  if (button.dataset.key) {
    pressDigit(button.dataset.key);
  } else if (button.dataset.op == "+" || button.dataset.op == "-") {
    applyOperator(button.dataset.op);
  } else if (button.dataset.op == "=") {
    equals();
  } else if (button.dataset.op == "clear") {
    clearAll();
  }
});
