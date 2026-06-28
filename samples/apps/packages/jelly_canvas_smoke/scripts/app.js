(function () {
  var canvas = document.getElementById("trend");
  var ctx = canvas && canvas.getContext ? canvas.getContext("2d") : null;
  if (!ctx) {
    return;
  }

  var bars = [28, 44, 38, 62, 55, 70, 48, 76, 68, 84, 66, 90];
  ctx.clearRect(0, 0, 220, 96);
  ctx.fillStyle = "#123040";
  ctx.fillRect(0, 0, 220, 96);

  ctx.fillStyle = "#2dd4bf";
  for (var i = 0; i < bars.length; i += 1) {
    var h = bars[i];
    ctx.fillRect(12 + i * 16, 88 - h, 8, h);
  }

  ctx.strokeStyle = "#f7fff6";
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(12, 80);
  for (var j = 0; j < bars.length; j += 1) {
    ctx.lineTo(16 + j * 16, 88 - bars[j]);
  }
  ctx.stroke();
}());
