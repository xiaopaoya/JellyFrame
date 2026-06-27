(function () {
  var state = document.getElementById("state");
  var netOk = document.getElementById("netOk");
  var netFail = document.getElementById("netFail");
  var audio = document.getElementById("audio");
  var geo = document.getElementById("geo");
  var errors = document.getElementById("errors");
  var counts = { netOk: 0, netFail: 0, audio: 0, geo: 0, errors: 0 };

  function render() {
    netOk.textContent = String(counts.netOk);
    netFail.textContent = String(counts.netFail);
    audio.textContent = String(counts.audio);
    geo.textContent = String(counts.geo);
    errors.textContent = String(counts.errors) + " errors";
    state.textContent = "Requests bounded";
  }

  function noteError() {
    counts.errors += 1;
    render();
  }

  function requestNetwork(index) {
    try {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", index % 2 === 0 ? "/debug/ping.txt" : "/debug/missing-" + index + ".txt", true);
      xhr.onload = function () {
        counts.netOk += 1;
        render();
      };
      xhr.onerror = function () {
        counts.netFail += 1;
        render();
      };
      xhr.send();
    } catch (error) {
      noteError();
    }
  }

  function requestAudio(index) {
    try {
      var tone = new Audio(index % 2 === 0 ? "/audio/tone.wav" : "/audio/missing-" + index + ".wav");
      tone.onended = function () {
        counts.audio += 1;
        render();
      };
      tone.onerror = noteError;
      tone.play();
    } catch (error) {
      noteError();
    }
  }

  function requestLocation() {
    if (!navigator.geolocation || !navigator.geolocation.getCurrentPosition) {
      noteError();
      return;
    }
    try {
      navigator.geolocation.getCurrentPosition(function () {
        counts.geo += 1;
        render();
      }, noteError);
    } catch (error) {
      noteError();
    }
  }

  for (var i = 0; i < 24; ++i) {
    requestNetwork(i);
  }
  for (var j = 0; j < 12; ++j) {
    requestAudio(j);
  }
  for (var k = 0; k < 10; ++k) {
    requestLocation();
  }
  render();
}());
