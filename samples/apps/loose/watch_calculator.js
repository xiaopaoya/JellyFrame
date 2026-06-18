var display = document.getElementById("display");

var currentText = "0";
var storedValue = 0;
var pendingOperator = "";
var waitingForOperand = false;
var lastOperator = "";
var lastOperand = 0;
var tipMode = false;
var tipPercent = 20;

function numberFromText(text) {
  var value = parseFloat(text);
  if (isNaN(value)) {
    return 0;
  }
  return value;
}

function formatNumber(value) {
  if (value > 999999999 || value < -99999999) {
    return "Error";
  }

  var text = String(Math.round(value * 1000000) / 1000000);
  if (text.length > 9 && text.indexOf(".") >= 0) {
    text = String(Math.round(value * 1000) / 1000);
  }
  if (text.length > 9) {
    text = text.substr(0, 9);
  }
  return text;
}

function setDisplay(text) {
  currentText = text;
  display.textContent = text;
}

function clearActiveOperator() {
  document.getElementById("divide").setAttribute("class", "key operator");
  document.getElementById("multiply").setAttribute("class", "key operator");
  document.getElementById("subtract").setAttribute("class", "key operator");
  document.getElementById("add").setAttribute("class", "key operator");
}

function markActiveOperator(id) {
  clearActiveOperator();
  document.getElementById(id).setAttribute("class", "key operator active");
}

function calculate(left, right, op) {
  if (op == "+") {
    return left + right;
  }
  if (op == "-") {
    return left - right;
  }
  if (op == "*") {
    return left * right;
  }
  if (op == "/") {
    if (right == 0) {
      return 9999999999;
    }
    return left / right;
  }
  return right;
}

function pressDigit(digit) {
  if (currentText == "Error") {
    clearAll();
  }
  if (waitingForOperand) {
    setDisplay(digit);
    waitingForOperand = false;
    return;
  }
  if (currentText == "0") {
    setDisplay(digit);
    return;
  }
  if (currentText.length < 9) {
    setDisplay(currentText + digit);
  }
}

function pressDecimal() {
  if (waitingForOperand) {
    setDisplay("0.");
    waitingForOperand = false;
    return;
  }
  if (currentText.indexOf(".") < 0 && currentText.length < 9) {
    setDisplay(currentText + ".");
  }
}

function pressOperator(op, id) {
  var value = numberFromText(currentText);
  if (pendingOperator != "" && !waitingForOperand) {
    value = calculate(storedValue, value, pendingOperator);
    setDisplay(formatNumber(value));
  }
  storedValue = value;
  pendingOperator = op;
  waitingForOperand = true;
  lastOperator = "";
  markActiveOperator(id);
}

function pressEquals() {
  var right = numberFromText(currentText);
  var op = pendingOperator;
  if (op == "") {
    op = lastOperator;
    right = lastOperand;
  } else {
    lastOperator = op;
    lastOperand = right;
  }
  if (op != "") {
    var result = calculate(storedValue, right, op);
    setDisplay(formatNumber(result));
    storedValue = result;
  }
  pendingOperator = "";
  waitingForOperand = true;
  clearActiveOperator();
}

function clearAll() {
  setDisplay("0");
  storedValue = 0;
  pendingOperator = "";
  waitingForOperand = false;
  lastOperator = "";
  lastOperand = 0;
  tipMode = false;
  clearActiveOperator();
}

function toggleSign() {
  if (currentText == "0" || currentText == "Error") {
    return;
  }
  if (currentText.charAt(0) == "-") {
    setDisplay(currentText.substr(1));
  } else if (currentText.length < 9) {
    setDisplay("-" + currentText);
  }
}

function percent() {
  var value = numberFromText(currentText) / 100;
  setDisplay(formatNumber(value));
}

function deleteDigit() {
  if (waitingForOperand || currentText == "Error" || currentText.length <= 1) {
    setDisplay("0");
    waitingForOperand = false;
    return;
  }
  if (currentText.length == 2 && currentText.charAt(0) == "-") {
    setDisplay("0");
    return;
  }
  setDisplay(currentText.substr(0, currentText.length - 1));
}

function pressTip() {
  var bill = numberFromText(currentText);
  if (!tipMode) {
    tipMode = true;
    tipPercent = 20;
  } else {
    tipPercent = tipPercent + 5;
    if (tipPercent > 30) {
      tipPercent = 10;
    }
  }
  setDisplay(formatNumber(bill + bill * tipPercent / 100));
  waitingForOperand = true;
  clearActiveOperator();
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

document.getElementById("decimal").addEventListener("click", pressDecimal);
document.getElementById("clear").addEventListener("click", clearAll);
document.getElementById("sign").addEventListener("click", toggleSign);
document.getElementById("tip").addEventListener("click", pressTip);
document.getElementById("delete").addEventListener("click", deleteDigit);
document.getElementById("divide").addEventListener("click", function () { pressOperator("/", "divide"); });
document.getElementById("multiply").addEventListener("click", function () { pressOperator("*", "multiply"); });
document.getElementById("subtract").addEventListener("click", function () { pressOperator("-", "subtract"); });
document.getElementById("add").addEventListener("click", function () { pressOperator("+", "add"); });
document.getElementById("equals").addEventListener("click", pressEquals);

"watch calculator ready";
