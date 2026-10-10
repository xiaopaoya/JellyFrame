var messages = [
  { id: "message-1", from: "Maya", title: "Coffee?", body: "Are you free after the walk?", time: "08:10", read: false },
  { id: "message-2", from: "Owen", title: "Route changed", body: "Meet at the north gate instead.", time: "07:42", read: false },
  { id: "message-3", from: "Sam", title: "All set", body: "The package is waiting at home.", time: "Yesterday", read: true }
];
var startX = 0;
var startY = 0;
var touchRow = null;
function id(name) { return document.getElementById(name); }
function show(page) { id("list-page").classList.toggle("hidden", page !== "list"); id("detail-page").classList.toggle("hidden", page !== "detail"); }
function orderedMessages() { return messages.slice().sort(function (a, b) { return Number(a.read) - Number(b.read); }); }
function findMessage(messageId) { for (var i = 0; i < messages.length; i += 1) if (messages[i].id === messageId) return messages[i]; return null; }
function renderMessages() {
  var list = id("message-list"); list.textContent = ""; var ordered = orderedMessages(); var unread = 0;
  ordered.forEach(function (message) {
    if (!message.read) unread += 1;
    var row = document.createElement("article"); row.className = "message-row" + (message.read ? " is-read" : ""); row.dataset.id = message.id;
    var avatar = document.createElement("span"); avatar.className = "avatar"; avatar.textContent = message.from.slice(0, 1);
    var copy = document.createElement("button"); copy.className = "message-copy"; copy.type = "button"; copy.id = message.id + "-main";
    var head = document.createElement("span"); head.className = "message-head";
    var name = document.createElement("strong"); name.className = "message-name"; name.textContent = message.from;
    var time = document.createElement("span"); time.className = "message-time"; time.textContent = message.time; head.append(name, time);
    var title = document.createElement("span"); title.className = "message-title"; title.textContent = message.title;
    var preview = document.createElement("span"); preview.className = "message-preview"; preview.textContent = message.body; copy.append(head, title, preview);
    var actions = document.createElement("span"); actions.className = "message-actions";
    var remove = document.createElement("button"); remove.className = "delete-button"; remove.type = "button"; remove.id = message.id + "-delete"; remove.textContent = "Delete"; actions.append(remove);
    row.append(avatar, copy, actions);
    if (!message.read) { var dot = document.createElement("span"); dot.className = "unread-dot"; row.append(dot); }
    list.append(row);
  });
  id("unread-count").textContent = String(unread);
}
function openMessage(message) {
  message.read = true;
  id("detail-from").textContent = message.from; id("detail-title").textContent = message.title; id("detail-body").textContent = message.body; id("detail-time").textContent = message.time;
  renderMessages(); show("detail");
}
id("message-list").addEventListener("click", function (event) {
  var row = event.target.closest(".message-row"); if (!row) return;
  var message = findMessage(row.dataset.id);
  if (event.target.closest(".delete-button")) { var keep = []; messages.forEach(function (item) { if (item.id !== row.dataset.id) keep.push(item); }); messages = keep; renderMessages(); return; }
  if (event.target.closest(".message-copy")) openMessage(message);
});
id("message-list").addEventListener("touchstart", function (event) { touchRow = event.target.closest(".message-row"); startX = event.touches[0].clientX; startY = event.touches[0].clientY; });
id("message-list").addEventListener("touchend", function (event) {
  if (!touchRow) return;
  var dx = event.changedTouches[0].clientX - startX; var dy = event.changedTouches[0].clientY - startY;
  if (dx < -30) touchRow.classList.add("is-revealed"); if (dx > 30) touchRow.classList.remove("is-revealed");
  if (dy > 30 && Math.abs(dy) > Math.abs(dx)) id("quick-reply").classList.remove("hidden"); touchRow = null;
});
id("detail-back").addEventListener("click", function () { show("list"); });
id("quick-reply").addEventListener("click", function (event) { var button = event.target.closest("button[data-reply]"); if (!button) return; id("reply-status").textContent = "Reply queued: " + button.dataset.reply; id("quick-reply").classList.add("hidden"); });
renderMessages();
