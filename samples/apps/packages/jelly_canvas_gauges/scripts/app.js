(function () {
  var tau = Math.PI * 2;

  function context(id) {
    var canvas = document.getElementById(id);
    return canvas && canvas.getContext ? canvas.getContext("2d") : null;
  }

  function ring(ctx, radius, width, progress, color) {
    ctx.beginPath();
    ctx.strokeStyle = "#173740";
    ctx.lineWidth = width;
    ctx.arc(66, 66, radius, -Math.PI / 2, tau - Math.PI / 2, false);
    ctx.stroke();

    ctx.beginPath();
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.arc(66, 66, radius, -Math.PI / 2, -Math.PI / 2 + tau * progress, false);
    ctx.stroke();
  }

  var rings = context("rings");
  if (rings) {
    rings.clearRect(0, 0, 132, 132);
    rings.globalAlpha = 0.32;
    var glow = rings.createLinearGradient(14, 0, 118, 132);
    glow.addColorStop(0, "#45f3d0");
    glow.addColorStop(1, "#f7d25c");
    rings.fillStyle = glow;
    rings.beginPath();
    rings.moveTo(66, 66);
    rings.arc(66, 66, 52, -Math.PI / 2, 0.95 * tau - Math.PI / 2, false);
    rings.closePath();
    rings.fill();
    rings.globalAlpha = 1;
    ring(rings, 52, 8, 0.82, "#45f3d0");
    ring(rings, 38, 7, 0.64, "#a7f36b");
    ring(rings, 25, 6, 0.48, "#f7d25c");
    rings.font = "bold 18px system-ui";
    rings.fillStyle = "#f4fff7";
    var label = "82";
    var labelWidth = rings.measureText(label).width;
    rings.fillText(label, 66 - labelWidth / 2, 70);
  }

  var heart = context("heart");
  if (heart) {
    heart.clearRect(0, 0, 74, 44);
    heart.strokeStyle = "#ff7aa8";
    heart.lineWidth = 2;
    heart.beginPath();
    heart.moveTo(2, 30);
    heart.lineTo(12, 24);
    heart.lineTo(18, 30);
    heart.lineTo(26, 10);
    heart.lineTo(34, 36);
    heart.lineTo(42, 18);
    heart.lineTo(50, 28);
    heart.lineTo(72, 14);
    heart.stroke();
  }

  var battery = context("battery");
  if (battery) {
    battery.clearRect(0, 0, 74, 44);
    battery.strokeStyle = "#b7ead9";
    battery.lineWidth = 2;
    battery.strokeRect(4, 10, 54, 24);
    var fill = battery.createLinearGradient(8, 0, 42, 0);
    fill.addColorStop(0, "#45f3d0");
    fill.addColorStop(1, "#a7f36b");
    battery.fillStyle = fill;
    battery.fillRect(8, 14, 33, 16);
    battery.globalAlpha = 0.72;
    battery.fillStyle = "#b7ead9";
    battery.fillRect(60, 18, 6, 8);
  }
}());
