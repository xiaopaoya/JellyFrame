(function () {
  var state = document.getElementById("state");

  function request(index) {
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/debug/budget-" + index + ".txt", true);
    xhr.onerror = function () {};
    xhr.onload = function () {};
    xhr.send();
  }

  for (var i = 0; i < 96; ++i) {
    try {
      request(i);
    } catch (error) {
      break;
    }
  }
  state.textContent = "Requests submitted";
}());
