var title = document.getElementById("title");
var summary = document.getElementById("summary");
var app = document.getElementById("app");

title.textContent = "DOM updated by JS";
summary.textContent = "JellyFrame M3 can now expose a tiny document object and mutate the native DOM tree.";

var note = document.createElement("p");
note.setAttribute("class", "note");
note.appendChild(document.createTextNode("Created with document.createElement and appendChild."));
app.appendChild(note);

title.textContent;
