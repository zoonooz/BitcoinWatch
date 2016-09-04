// bit flyer api
var url = "https://api.bitflyer.jp/v1/ticker";

// Require the keys' numeric values.
var keys = require('message_keys');

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
    dict[keys.ltp] = json.ltp;
    
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
  request.open("GET", url);
  request.send();
}

// ===================================================

// Called when JS is ready
Pebble.addEventListener("ready", function(e) {
  console.log("JS is ready!");
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