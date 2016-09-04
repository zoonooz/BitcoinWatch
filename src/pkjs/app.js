// bit flyer api
var URL_BITFLYER = "https://api.bitflyer.jp/v1/ticker";
var URL_COINBASE = "https://api.coinbase.com/v2/prices/spot?currency=USD";

// Require the keys' numeric values.
var keys = require('message_keys');

// Clay
var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var currentService = "coinbase";

// ========================================================

// Create the request
var request = new XMLHttpRequest();

// Specify the callback for when the request is completed
request.onload = function() {
  // The request was successfully completed!
  console.log('Got response: ' + this.responseText);

  try {
    // Transform in to JSON
    var json = JSON.parse(this.responseText);

    // Read data
    var dict = {};
    if (currentService == "bitflyer") {
      dict[keys.ltp] = json.ltp.toString();
      dict[keys.status] = "BTC/JPY";
    } else {
      dict[keys.ltp] = json.data.amount;
      dict[keys.status] = "BTC/USD";
    }

    // Send the object
    Pebble.sendAppMessage(dict, function() {
      console.log('Message sent successfully: ' + JSON.stringify(dict));
    }, function(e) {
      console.log('Message failed: ' + JSON.stringify(e));
    });
  } catch(err) {
    console.log('Error parsing JSON response!');
  }
};

function getLatestPrice() {
  // Send the request
  var url = currentService == "bitflyer" ? URL_BITFLYER : URL_COINBASE;
  request.open("GET", url);
  request.send();
}

// ===================================================

// Called when JS is ready
Pebble.addEventListener("ready", function(e) {
  console.log("JS is ready!");
  currentService = localStorage.getItem('service');
  getLatestPrice();
});

// Called when incoming message from the Pebble is received
// We are currently only checking the "message" appKey defined in appinfo.json/Settings
Pebble.addEventListener("appmessage", function(e) {
  console.log("Received Message: " + e.payload.action);
  // Get the dictionary from the message
  var dict = e.payload;
  if (dict.action == "refresh") {
    getLatestPrice();
  }
});

// config
Pebble.addEventListener('showConfiguration', function(e) {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) {
    return;
  }
  // Get the keys and values from each config item
  var dict = clay.getSettings(e.response);
  currentService = dict[keys.service];
  localStorage.setItem('service', currentService);
  getLatestPrice();
});
