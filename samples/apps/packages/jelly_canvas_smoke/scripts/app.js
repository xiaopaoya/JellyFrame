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
    var nextX = 8 + j * 8;
    var nextY = 44 - Math.floor(bars[j] / 2);
    if (j === 0) {
      sourceCtx.lineTo(nextX, nextY);
    } else if (j % 3 === 0) {
      sourceCtx.bezierCurveTo(nextX - 6, 44 - Math.floor(bars[j - 1] / 2), nextX - 2, nextY, nextX, nextY);
    } else {
      sourceCtx.quadraticCurveTo(nextX - 4, 44 - Math.floor(bars[j - 1] / 2), nextX, nextY);
    }
  }
  sourceCtx.stroke();

  sourceCtx.translate(3, 2);
  sourceCtx.fillStyle = "#f7fff6";
  sourceCtx.fillRect(0, 0, 4, 4);
  sourceCtx.resetTransform();

  ctx.clearRect(0, 0, 220, 96);
  var glow = ctx.createRadialGradient(110, 48, 6, 110, 48, 118);
  if (glow) {
    glow.addColorStop(0, "#1d6f82");
    glow.addColorStop(1, "#0b1f2a");
    ctx.fillStyle = glow;
    ctx.fillRect(0, 0, 220, 96);
  }
  ctx.drawImage(source, 0, 0, 110, 48, 0, 0, 220, 96);
}());
