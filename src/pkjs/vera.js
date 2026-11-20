// Vera (Luup) HTTP helpers. Mirrors the requests the CircuitPython version
// makes against port 3480.

var SVC_TEMP = 'urn:upnp-org:serviceId:TemperatureSensor1';
var SVC_HVAC_STATE = 'urn:micasaverde-com:serviceId:HVAC_OperatingState1';
var SVC_SETPOINT_HEAT = 'urn:upnp-org:serviceId:TemperatureSetpoint1_Heat';

var TIMEOUT_MS = 8000;

function base(ip) {
  return 'http://' + ip + ':3480/data_request?';
}

function variableUrl(ip, device, service, variable) {
  return base(ip) +
    'id=variableget&DeviceNum=' + device +
    '&serviceId=' + service +
    '&Variable=' + variable;
}

function setpointUrl(ip, device, value) {
  return base(ip) +
    'id=action&output_format=xml&DeviceNum=' + device +
    '&serviceId=' + SVC_SETPOINT_HEAT +
    '&action=SetCurrentSetpoint&NewCurrentSetpoint=' + value;
}

function httpGet(url, cb) {
  var xhr = new XMLHttpRequest();
  var done = false;
  function finish(err, text) {
    if (done) return;
    done = true;
    cb(err, text);
  }
  xhr.onload = function () {
    if (xhr.status >= 200 && xhr.status < 300) {
      finish(null, String(xhr.responseText || '').trim());
    } else {
      finish(new Error('HTTP ' + xhr.status));
    }
  };
  xhr.onerror = function () { finish(new Error('No connection')); };
  xhr.ontimeout = function () { finish(new Error('Timed out')); };
  xhr.open('GET', url, true);
  try { xhr.timeout = TIMEOUT_MS; } catch (e) { /* not supported everywhere */ }
  xhr.send();
}

function parseNumber(text, what) {
  var n = parseFloat(text);
  if (isNaN(n)) {
    throw new Error('Bad ' + what);
  }
  return Math.round(n);
}

// cb(err, { temp, setpoint, heating })
function readState(ip, device, cb) {
  httpGet(variableUrl(ip, device, SVC_TEMP, 'CurrentTemperature'), function (err, tempText) {
    if (err) return cb(err);
    httpGet(variableUrl(ip, device, SVC_SETPOINT_HEAT, 'CurrentSetpoint'), function (err2, spText) {
      if (err2) return cb(err2);
      httpGet(variableUrl(ip, device, SVC_HVAC_STATE, 'ModeState'), function (err3, modeText) {
        if (err3) return cb(err3);
        try {
          cb(null, {
            temp: parseNumber(tempText, 'temp'),
            setpoint: parseNumber(spText, 'setpoint'),
            heating: modeText.toUpperCase().indexOf('HEAT') === 0
          });
        } catch (e) {
          cb(e);
        }
      });
    });
  });
}

function setSetpoint(ip, device, value, cb) {
  httpGet(setpointUrl(ip, device, value), function (err) { cb(err || null); });
}

module.exports = {
  readState: readState,
  setSetpoint: setSetpoint,
  variableUrl: variableUrl,
  setpointUrl: setpointUrl
};
