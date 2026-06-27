(function () {
  var hourHand = document.getElementById("hourHand");
  var minuteHand = document.getElementById("minuteHand");
  var secondHand = document.getElementById("secondHand");
  var digitalTime = document.getElementById("digitalTime");
  var dateLabel = document.getElementById("dateLabel");

  function two(value) {
    return value < 10 ? "0" + value : String(value);
  }

  function update() {
    var now = new Date();
    var hours = now.getHours();
    var minutes = now.getMinutes();
    var seconds = now.getSeconds();
    var hourAngle = ((hours % 12) + minutes / 60) * 30;
    var minuteAngle = (minutes + seconds / 60) * 6;
    var secondAngle = seconds * 6;

    hourHand.style.transform = "rotate(" + hourAngle + "deg)";
    minuteHand.style.transform = "rotate(" + minuteAngle + "deg)";
    secondHand.style.transform = "rotate(" + secondAngle + "deg)";
    digitalTime.textContent = two(hours) + ":" + two(minutes);
    dateLabel.textContent = "WED " + two(now.getMonth() + 1) + "/" + two(now.getDate());
  }

  update();
  setInterval(update, 1000);
}());
