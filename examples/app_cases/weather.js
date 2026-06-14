var data = [
  ["Shanghai", 24, "Cloudy", "68%", "12 km/h"],
  ["Shenzhen", 29, "Light rain", "74%", "9 km/h"],
  ["Beijing", 21, "Clear", "38%", "16 km/h"]
];

var useF = false;
var picker = document.getElementById("cityPicker");
var unit = document.getElementById("unit");
var city = document.getElementById("city");
var temperature = document.getElementById("temperature");
var condition = document.getElementById("condition");
var humidity = document.getElementById("humidity");
var wind = document.getElementById("wind");

function render() {
  var item = data[picker.selectedIndex];
  var temp = item[1];
  city.textContent = item[0];
  temperature.textContent = useF ? String(Math.round(temp * 9 / 5 + 32)) + " F" : String(temp) + " C";
  condition.textContent = item[2];
  humidity.textContent = item[3];
  wind.textContent = item[4];
  unit.textContent = useF ? "Use C" : "Use F";
}

picker.addEventListener("change", render);
unit.addEventListener("click", function () {
  useF = !useF;
  render();
});

render();
"weather ready";
