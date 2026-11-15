var choice = document.getElementById("choice");
var result = document.getElementById("result");

choice.addEventListener("change", function () {
  result.textContent = "SELECTED: " + choice.value.toUpperCase();
});
