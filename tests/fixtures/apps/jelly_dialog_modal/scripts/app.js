var dialog = document.getElementById("confirm");
var state = document.getElementById("state");

document.getElementById("open").addEventListener("click", function () {
  dialog.showModal();
  state.textContent = "open";
});

dialog.addEventListener("cancel", function () {
  state.textContent = "cancel";
});

dialog.addEventListener("close", function () {
  state.textContent = "closed";
});
