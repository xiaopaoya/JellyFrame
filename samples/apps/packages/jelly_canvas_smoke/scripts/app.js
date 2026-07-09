(function () {
  var canvas = document.getElementById("trend");
  var source = document.getElementById("trend-source");
  var ctx = canvas && canvas.getContext ? canvas.getContext("2d") : null;
  var sourceCtx = source && source.getContext ? source.getContext("2d") : null;
  if (!ctx || !sourceCtx) {
    return;
  }

  var bars = [28, 44, 38, 62, 55, 70, 48, 76, 68, 84, 66, 90];
  sourceCtx.clearRect(0, 0, 110, 48);
  sourceCtx.fillStyle = "#123040";
  sourceCtx.fillRect(0, 0, 110, 48);

  sourceCtx.fillStyle = "#2dd4bf";
  for (var i = 0; i < bars.length; i += 1) {
    var h = bars[i];
    sourceCtx.fillRect(6 + i * 8, 44 - Math.floor(h / 2), 4, Math.floor(h / 2));
  }

  sourceCtx.strokeStyle = "#f7fff6";
  sourceCtx.lineWidth = 1;
  sourceCtx.beginPath();
  sourceCtx.moveTo(6, 40);
  for (var j = 0; j < bars.length; j += 1) {
    sourceCtx.lineTo(8 + j * 8, 44 - Math.floor(bars[j] / 2));
  }
  sourceCtx.stroke();

  ctx.clearRect(0, 0, 220, 96);
  ctx.drawImage(source, 0, 0, 110, 48, 0, 0, 220, 96);
}());
