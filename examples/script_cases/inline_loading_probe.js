var count = 0;
document.getElementById("status").textContent = "loaded:0";
window.bump = function () {
  count += 1;
  document.getElementById("count").textContent = String(count);
  document.getElementById("status").textContent = "loaded:" + String(count);
};
