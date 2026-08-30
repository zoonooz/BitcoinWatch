// apis
var apis = {}
apis["binance"] = {
  "url": "https://api.binance.com/api/v3/ticker/price?symbol={base}{quote}",
  "responsePath": "price"
}
apis["coinbase"] = {
  "url": "https://api.coinbase.com/v2/prices/{base}-{quote}/spot",
  "responsePath": "data.amount"
}

// Require the keys' numeric values.
var keys = require('message_keys');

// Clay
var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var currentService = "binance";
var currentBase = "BTC";
var currentQuote = "USD";

// ========================================================

// Create the request
var request = new XMLHttpRequest();

// Specify the callback for when the request is completed
request.onload = function() {
  console.log('Got response: ' + this.responseText);

  try {
    var json = JSON.parse(this.responseText);
    var api = apis[currentService];
    var price = parseFloat(getByPath(json, api.responsePath));

    var dict = {};
    dict[keys.price] = Math.round(price).toString();
    dict[keys.status] = currentBase + "/" + currentQuote;

    Pebble.sendAppMessage(dict, function() {
      console.log('Message sent successfully: ' + JSON.stringify(dict));
    }, function(e) {
      console.log('Message failed: ' + JSON.stringify(e));
    });
  } catch(err) {
    console.log('Error parsing JSON response: ' + err);
  }
};

// Get nested property from object using dot notation path
function getByPath(obj, path) {
  return path.split('.').reduce(function(o, key) {
    return o && o[key];
  }, obj);
}

function getLatestPrice() {
  var api = apis[currentService];
  var url = api.url
    .replace("{base}", currentBase)
    .replace("{quote}", currentQuote);

  console.log('Fetching: ' + url);
  request.open("GET", url);
  request.send();
}

// ===================================================

// Called when JS is ready
Pebble.addEventListener("ready", function(e) {
  console.log("JS is ready!");
  var storedService = localStorage.getItem('service');
  currentService = (storedService && apis[storedService]) ? storedService : "binance";
  currentBase = localStorage.getItem('base') || "BTC";
  currentQuote = localStorage.getItem('quote') || "USD";
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
  console.log("Received settings: " + e.response);
  // Get the keys and values from each config item
  var dict = clay.getSettings(e.response);

  var newService = dict[keys.service];
  currentService = (newService && apis[newService]) ? newService : "binance";
  currentBase = dict[keys.currencyBase].toUpperCase() || "BTC";
  currentQuote = dict[keys.currencyQuote].toUpperCase() || "USD";

  localStorage.setItem('service', currentService);
  localStorage.setItem('base', currentBase);
  localStorage.setItem('quote', currentQuote);

  getLatestPrice();
});
