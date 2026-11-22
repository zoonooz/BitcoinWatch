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

// Price history (candles) for the trend chart. Each parser returns candles
// oldest first as {o, h, l, c}.
// Binance klines: oldest first, [openTime, open, high, low, close, ...].
// Coinbase Exchange candles: newest first, [time, low, high, open, close, volume].
// Coinbase has no limit parameter and returns ~300 candles by default, so we
// ask for just the time window we need with start/end.
var chartApis = {
  "binance": {
    "url": "https://api.binance.com/api/v3/klines?symbol={base}{quote}&interval={interval}&limit={limit}",
    "parse": function(json) {
      return json.map(function(c) {
        return { o: parseFloat(c[1]), h: parseFloat(c[2]), l: parseFloat(c[3]), c: parseFloat(c[4]) };
      });
    }
  },
  "coinbase": {
    "url": "https://api.exchange.coinbase.com/products/{base}-{quote}/candles?granularity={granularity}&start={start}&end={end}",
    "parse": function(json) {
      return json.map(function(c) {
        return { o: c[3], h: c[2], l: c[1], c: c[4] };
      }).reverse();
    }
  }
};

// Chart styles; the number is what the watch receives.
var CHART_STYLE_LINE = 0;
var CHART_STYLE_CANDLES = 1;
var chartStyles = { "line": CHART_STYLE_LINE, "candles": CHART_STYLE_CANDLES };
var DEFAULT_CHART_STYLE = "line";
// Candle mode shows at least this many candles; longer histories are merged
// down towards it (e.g. 60 x 1m becomes 30 x 2m).
var MIN_CANDLES = 24;

// For each range: candle size per service, number of candles, and how often
// (in minutes) the chart is re-fetched.
var chartRanges = {
  "1h":  { interval: "1m",  granularity: 60,    count: 60, refreshMinutes: 1 },
  "6h":  { interval: "15m", granularity: 900,   count: 24, refreshMinutes: 5 },
  "24h": { interval: "1h",  granularity: 3600,  count: 24, refreshMinutes: 15 },
  "7d":  { interval: "6h",  granularity: 21600, count: 28, refreshMinutes: 60 }
};

// Require the keys' numeric values.
var keys = require('message_keys');

// Clay
var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var DEFAULT_SERVICE = "coinbase";
var DEFAULT_BASE = "BTC";
var DEFAULT_QUOTE = "USD";
var REQUEST_TIMEOUT_MS = 15000;

var currentService = DEFAULT_SERVICE;
var currentBase = DEFAULT_BASE;
var currentQuote = DEFAULT_QUOTE;

var DEFAULT_CHART_RANGE = "24h";
var chartEnabled = true;
var chartRange = DEFAULT_CHART_RANGE;
var chartStyle = DEFAULT_CHART_STYLE;
var lastChartFetch = 0;  // ms timestamp of the last successful chart fetch

// ========================================================

// Send messages one at a time; the watch can only receive one at once.
var outbox = [];
var sending = false;

function sendToWatch(dict) {
  outbox.push(dict);
  pumpOutbox();
}

function pumpOutbox() {
  if (sending || outbox.length === 0) {
    return;
  }
  sending = true;
  var dict = outbox.shift();
  Pebble.sendAppMessage(dict, function() {
    console.log('Message sent: ' + JSON.stringify(dict));
    sending = false;
    pumpOutbox();
  }, function(e) {
    console.log('Message failed: ' + JSON.stringify(e));
    sending = false;
    pumpOutbox();
  });
}

function sendStatus(status) {
  var dict = {};
  dict[keys.status] = status;
  sendToWatch(dict);
}

function pairLabel() {
  return currentBase + "/" + currentQuote;
}

// Pair plus chart range (e.g. "BTC/USD · 24H") when the chart is shown.
function statusLabel() {
  if (!chartEnabled) {
    return pairLabel();
  }
  return pairLabel() + " · " + chartRange.toUpperCase();
}

function handleResponse(responseText) {
  var json = JSON.parse(responseText);
  var api = apis[currentService];
  var rawPrice = String(getByPath(json, api.responsePath));

  if (!isFinite(parseFloat(rawPrice))) {
    throw new Error('No price in response');
  }

  var dict = {};
  dict[keys.price] = formatPrice(rawPrice);
  dict[keys.status] = statusLabel();
  sendToWatch(dict);
}

// Get nested property from object using dot notation path
function getByPath(obj, path) {
  return path.split('.').reduce(function(o, key) {
    return o && o[key];
  }, obj);
}

// Format a raw price string for display.
// - If the integer part has more than 3 digits (price >= 1000), the
//   fraction is hidden and the price is rounded to the nearest integer.
// - Otherwise, the fraction is shown starting from its first non-zero
//   digit and continuing until (but not including) the next zero digit.
//   e.g. "0.000055101" -> "0.0000551", "0.0101" -> "0.01"
function formatPrice(rawPrice) {
  var parts = rawPrice.split(".");
  var intPart = parts[0] || "0";
  var fracPart = parts[1] || "";

  if (intPart.length > 3) {
    return Math.round(parseFloat(rawPrice)).toString();
  }

  var firstNonZero = -1;
  for (var i = 0; i < fracPart.length; i++) {
    if (fracPart.charAt(i) !== "0") {
      firstNonZero = i;
      break;
    }
  }

  if (firstNonZero === -1) {
    return intPart;
  }

  var end = firstNonZero;
  while (end < fracPart.length && fracPart.charAt(end) !== "0") {
    end++;
  }

  return intPart + "." + fracPart.substring(0, end);
}

function getLatestPrice() {
  var api = apis[currentService];
  var url = api.url
    .replace("{base}", encodeURIComponent(currentBase))
    .replace("{quote}", encodeURIComponent(currentQuote));

  console.log('Fetching: ' + url);

  // Fresh request each time so a slow request is never silently reused.
  var request = new XMLHttpRequest();
  request.timeout = REQUEST_TIMEOUT_MS;

  request.onload = function() {
    if (this.status < 200 || this.status >= 300) {
      console.log('HTTP ' + this.status + ': ' + this.responseText);
      sendStatus(pairLabel() + " error");
      return;
    }
    try {
      handleResponse(this.responseText);
    } catch (err) {
      console.log('Bad response: ' + err + ' ' + this.responseText);
      sendStatus(pairLabel() + " error");
    }
  };
  request.onerror = function() {
    console.log('Network error');
    sendStatus("network error");
  };
  request.ontimeout = function() {
    console.log('Request timed out');
    sendStatus("timeout");
  };

  request.open("GET", url);
  request.send();
}

// Scale prices to 0..255 so the chart fits in a small byte array.
// min/max default to the prices' own range.
function scaleToBytes(prices, min, max) {
  if (min === undefined) {
    min = Math.min.apply(null, prices);
    max = Math.max.apply(null, prices);
  }
  var span = max - min;
  return prices.map(function(p) {
    return span > 0 ? Math.round((p - min) / span * 255) : 128;
  });
}

// Merge neighbouring candles while keeping at least `min` of them.
function mergeCandles(candles, min) {
  var size = Math.floor(candles.length / min);
  if (size <= 1) {
    return candles;
  }
  var merged = [];
  for (var i = 0; i < candles.length; i += size) {
    var group = candles.slice(i, i + size);
    merged.push({
      o: group[0].o,
      h: Math.max.apply(null, group.map(function(c) { return c.h; })),
      l: Math.min.apply(null, group.map(function(c) { return c.l; })),
      c: group[group.length - 1].c
    });
  }
  return merged;
}

// Build the chart payload: closes for the line, or o,h,l,c bytes per candle.
function chartBytes(candles) {
  if (chartStyle !== "candles") {
    return scaleToBytes(candles.map(function(c) { return c.c; }));
  }
  candles = mergeCandles(candles, MIN_CANDLES);
  var min = Math.min.apply(null, candles.map(function(c) { return c.l; }));
  var max = Math.max.apply(null, candles.map(function(c) { return c.h; }));
  var flat = [];
  candles.forEach(function(c) {
    flat.push(c.o, c.h, c.l, c.c);
  });
  return scaleToBytes(flat, min, max);
}

function sendChartEnabled() {
  var dict = {};
  dict[keys.chartEnabled] = chartEnabled ? 1 : 0;
  sendToWatch(dict);
}

// Clay returns colors as 0xRRGGBB ints; the watch converts with GColorFromHEX.
// Color items are hidden on black & white watches, so they may be missing.
function normalizeColor(value) {
  var color = typeof value === "string" ? parseInt(value.replace(/^(#|0x)/i, ""), 16) : value;
  return (typeof color === "number" && isFinite(color)) ? color & 0xFFFFFF : null;
}

function sendColors(settings) {
  var primary = normalizeColor(settings[keys.colorPrimary]);
  var secondary = normalizeColor(settings[keys.colorSecondary]);
  if (primary === null && secondary === null) {
    return;
  }
  var dict = {};
  if (primary !== null) dict[keys.colorPrimary] = primary;
  if (secondary !== null) dict[keys.colorSecondary] = secondary;
  sendToWatch(dict);
}

function getChart() {
  var range = chartRanges[chartRange];
  var api = chartApis[currentService];
  // One extra candle of margin; the parsed result is trimmed to range.count.
  var end = new Date();
  var start = new Date(end.getTime() - (range.count + 1) * range.granularity * 1000);
  var url = api.url
    .replace("{base}", encodeURIComponent(currentBase))
    .replace("{quote}", encodeURIComponent(currentQuote))
    .replace("{interval}", range.interval)
    .replace("{limit}", range.count)
    .replace("{granularity}", range.granularity)
    .replace("{start}", encodeURIComponent(start.toISOString()))
    .replace("{end}", encodeURIComponent(end.toISOString()));

  console.log('Fetching chart: ' + url);

  var request = new XMLHttpRequest();
  request.timeout = REQUEST_TIMEOUT_MS;
  request.onload = function() {
    if (this.status < 200 || this.status >= 300) {
      console.log('Chart HTTP ' + this.status + ': ' + this.responseText);
      return;
    }
    try {
      var candles = api.parse(JSON.parse(this.responseText))
        .slice(-range.count)
        .filter(function(c) {
          return isFinite(c.o) && isFinite(c.h) && isFinite(c.l) && isFinite(c.c);
        });
      if (candles.length < 2) {
        throw new Error('Not enough chart data');
      }
      lastChartFetch = Date.now();
      // Style travels with the data so the watch always knows how to read it.
      var dict = {};
      dict[keys.chartStyle] = chartStyles[chartStyle];
      dict[keys.chart] = chartBytes(candles);
      sendToWatch(dict);
    } catch (err) {
      console.log('Bad chart response: ' + err);
    }
  };
  request.onerror = function() { console.log('Chart network error'); };
  request.ontimeout = function() { console.log('Chart request timed out'); };
  request.open("GET", url);
  request.send();
}

// Fetch the chart when it is enabled and its refresh interval has passed.
function maybeGetChart(force) {
  if (!chartEnabled) {
    return;
  }
  var refreshMs = chartRanges[chartRange].refreshMinutes * 60 * 1000;
  // Small margin so a 1-minute refresh isn't skipped by timer jitter.
  if (force || Date.now() - lastChartFetch >= refreshMs - 5000) {
    getChart();
  }
}

function refreshAll(force) {
  getLatestPrice();
  maybeGetChart(force);
}

function normalizeChartEnabled(value) {
  if (value === null || value === undefined) {
    return true;
  }
  return value === true || value === 1 || value === "1" || value === "true";
}

function normalizeChartRange(value) {
  return (value && chartRanges[value]) ? value : DEFAULT_CHART_RANGE;
}

function normalizeChartStyle(value) {
  return (value && chartStyles[value] !== undefined) ? value : DEFAULT_CHART_STYLE;
}

function normalizeSymbol(value, fallback) {
  var symbol = String(value || "").replace(/\s+/g, "").toUpperCase();
  return symbol || fallback;
}

function normalizeService(value) {
  return (value && apis[value]) ? value : DEFAULT_SERVICE;
}

// ===================================================

// Called when JS is ready
Pebble.addEventListener("ready", function(e) {
  console.log("JS is ready!");
  currentService = normalizeService(localStorage.getItem('service'));
  currentBase = normalizeSymbol(localStorage.getItem('base'), DEFAULT_BASE);
  currentQuote = normalizeSymbol(localStorage.getItem('quote'), DEFAULT_QUOTE);
  chartEnabled = normalizeChartEnabled(localStorage.getItem('chartEnabled'));
  chartRange = normalizeChartRange(localStorage.getItem('chartRange'));
  chartStyle = normalizeChartStyle(localStorage.getItem('chartStyle'));
  sendChartEnabled();
  refreshAll(true);
});

// Called when incoming message from the Pebble is received
// We are currently only checking the "message" appKey defined in appinfo.json/Settings
Pebble.addEventListener("appmessage", function(e) {
  console.log("Received Message: " + e.payload.action);
  // Get the dictionary from the message
  var dict = e.payload;
  if (dict.action == "refresh") {
    refreshAll(false);
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

  currentService = normalizeService(dict[keys.service]);
  currentBase = normalizeSymbol(dict[keys.currencyBase], DEFAULT_BASE);
  currentQuote = normalizeSymbol(dict[keys.currencyQuote], DEFAULT_QUOTE);

  chartEnabled = normalizeChartEnabled(dict[keys.chartEnabled]);
  chartRange = normalizeChartRange(dict[keys.chartRange]);
  chartStyle = normalizeChartStyle(dict[keys.chartStyle]);

  localStorage.setItem('service', currentService);
  localStorage.setItem('base', currentBase);
  localStorage.setItem('quote', currentQuote);
  localStorage.setItem('chartEnabled', chartEnabled ? "1" : "0");
  localStorage.setItem('chartRange', chartRange);
  localStorage.setItem('chartStyle', chartStyle);

  sendColors(dict);
  sendChartEnabled();
  refreshAll(true);
});
