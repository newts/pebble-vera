var Clay = require('pebble-clay');
var clayConfig = require('./config');
var vera = require('./vera');

var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var SETTINGS_KEY = 'vera-settings';
var CMD_POLL = 1;
var CMD_SET = 2;
// Vera often takes a moment to reflect a new setpoint / heat state.
var POST_SET_POLL_DELAY_MS = 3000;

var busy = false;

function loadSettings() {
  var s = {};
  try {
    s = JSON.parse(localStorage.getItem(SETTINGS_KEY)) || {};
  } catch (e) { s = {}; }
  return {
    ip: String(s.VeraIP || '').trim(),
    device: parseInt(s.DeviceNum, 10) || 36
  };
}

function sendToWatch(msg) {
  Pebble.sendAppMessage(msg, function () {}, function (e) {
    console.log('sendAppMessage failed: ' + JSON.stringify(e));
  });
}

function sendError(text) {
  sendToWatch({ Error: String(text).substring(0, 40) });
}

function poll() {
  var cfg = loadSettings();
  if (!cfg.ip) {
    sendError('Set Vera IP in app settings');
    return;
  }
  if (busy) return;
  busy = true;
  vera.readState(cfg.ip, cfg.device, function (err, state) {
    busy = false;
    if (err) {
      console.log('Poll failed: ' + err.message);
      sendError(err.message);
      return;
    }
    console.log('Vera: ' + JSON.stringify(state));
    sendToWatch({
      Temp: state.temp,
      Setpoint: state.setpoint,
      Heating: state.heating ? 1 : 0
    });
  });
}

function setSetpoint(value) {
  var cfg = loadSettings();
  if (!cfg.ip) {
    sendError('Set Vera IP in app settings');
    return;
  }
  console.log('Sending setpoint ' + value);
  vera.setSetpoint(cfg.ip, cfg.device, value, function (err) {
    if (err) {
      sendError('Set failed: ' + err.message);
      return;
    }
    setTimeout(poll, POST_SET_POLL_DELAY_MS);
  });
}

Pebble.addEventListener('ready', function () {
  poll();
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  if (p.Cmd === CMD_SET && typeof p.NewSetpoint === 'number') {
    setSetpoint(p.NewSetpoint);
  } else if (p.Cmd === CMD_POLL) {
    poll();
  }
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var settings = clay.getSettings(e.response, false);
  var saved = {
    VeraIP: settings.VeraIP && settings.VeraIP.value !== undefined ? settings.VeraIP.value : settings.VeraIP,
    DeviceNum: settings.DeviceNum && settings.DeviceNum.value !== undefined ? settings.DeviceNum.value : settings.DeviceNum
  };
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(saved));
  poll();
});
