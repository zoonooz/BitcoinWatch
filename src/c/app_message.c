#include <pebble.h>

static Window *s_window;
static TextLayer *s_price_layer, *s_time_layer, *s_title_layer;
static Layer *s_chart_layer;
static GRect s_price_bounds;
static int16_t s_price_center_y;  // Vertical center for price text

#define COLOR_BG GColorFromRGB(31, 37, 44)
// Accent colors, configurable from the phone (0xRRGGBB).
// Primary: time and rising chart. Secondary: pair label and falling chart.
#define DEFAULT_COLOR_PRIMARY 0xFFAA00
#define DEFAULT_COLOR_SECONDARY 0xFF5555
static GColor s_color_primary;
static GColor s_color_secondary;

// Persistent storage keys
#define PERSIST_CHART_ENABLED 1
#define PERSIST_CHART_DATA 2
#define PERSIST_CHART_STYLE 3
#define PERSIST_COLOR_PRIMARY 4
#define PERSIST_COLOR_SECONDARY 5

// Chart state: values are scaled 0..255 by the phone.
// Line style: one close price per byte.
// Candle style: 4 bytes per candle (open, high, low, close).
#define CHART_STYLE_LINE 0
#define CHART_STYLE_CANDLES 1
#define MAX_CHART_POINTS 128
static uint8_t s_chart_points[MAX_CHART_POINTS];
static int s_chart_count = 0;
static int s_chart_style = CHART_STYLE_LINE;
static bool s_chart_enabled = true;

static char s_price_text[30] = "-";

// Font options for price display (largest to smallest)

static const char* s_price_fonts[] = {
  FONT_KEY_LECO_42_NUMBERS,
  FONT_KEY_LECO_38_BOLD_NUMBERS,
  FONT_KEY_LECO_36_BOLD_NUMBERS,
  FONT_KEY_LECO_32_BOLD_NUMBERS,
  FONT_KEY_LECO_20_BOLD_NUMBERS
};
#define NUM_PRICE_FONTS 5

// Set price text with auto-sizing font and vertical centering
static void set_price_text(const char *text) {
  GFont font = NULL;
  GSize text_size;
  GRect measure_bound = GRect(0, 0, 10000, 10000);

  // Try each font from largest to smallest until text fits
  for (int i = 0; i < NUM_PRICE_FONTS; i++) {
    font = fonts_get_system_font(s_price_fonts[i]);
    text_size = graphics_text_layout_get_content_size(
      text, font, measure_bound, GTextOverflowModeFill, GTextAlignmentCenter);

    if (text_size.w <= s_price_bounds.size.w) {
      break;  // This font fits
    }
  }

  // Vertically center the text by adjusting the layer position
  int16_t new_y = s_price_center_y - (text_size.h / 2);
  GRect new_frame = GRect(s_price_bounds.origin.x, new_y, s_price_bounds.size.w, text_size.h);
  layer_set_frame(text_layer_get_layer(s_price_layer), new_frame);

  text_layer_set_font(s_price_layer, font);
  text_layer_set_text(s_price_layer, text);
}

// ------------------- chart ----------------------

static void draw_line_chart(GContext *ctx, GRect bounds) {
  if (s_chart_count < 2) {
    return;
  }

  int16_t w = bounds.size.w - 1;
  int16_t h = bounds.size.h - 2;  // leave room for the 2px stroke

#ifdef PBL_COLOR
  // Primary when the price rose over the range, secondary when it fell
  bool rising = s_chart_points[s_chart_count - 1] >= s_chart_points[0];
  graphics_context_set_stroke_color(ctx, rising ? s_color_primary : s_color_secondary);
#else
  graphics_context_set_stroke_color(ctx, GColorWhite);
#endif
  graphics_context_set_stroke_width(ctx, PBL_IF_COLOR_ELSE(2, 1));
  graphics_context_set_antialiased(ctx, true);

  GPoint prev = GPointZero;
  for (int i = 0; i < s_chart_count; i++) {
    GPoint p = GPoint(
      (int16_t)(i * w / (s_chart_count - 1)),
      (int16_t)(1 + h - (s_chart_points[i] * h / 255)));
    if (i > 0) {
      graphics_draw_line(ctx, prev, p);
    }
    prev = p;
  }
}

// Retro pixel candles: crisp 1px wicks and solid bodies, no anti-aliasing.
static void draw_candle_chart(GContext *ctx, GRect bounds) {
  int n = s_chart_count / 4;
  if (n < 1) {
    return;
  }

  int16_t h = bounds.size.h - 1;
  int16_t w = bounds.size.w;
  int16_t slot = w / n;
  int16_t body_w = slot - 1;  // at least 1px gap between candles
  if (body_w > 7) body_w = 7;
  if (body_w < 1) body_w = 1;

  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, 1);

  for (int i = 0; i < n; i++) {
    const uint8_t *c = &s_chart_points[i * 4];
    uint8_t open = c[0], high = c[1], low = c[2], close = c[3];
    bool rising = close >= open;

    // Spread candles over the full width so no pixels are left over
    int16_t x = i * w / n + (slot - body_w) / 2;
    int16_t wick_x = x + (body_w - 1) / 2;
    int16_t y_high = h - high * h / 255;
    int16_t y_low = h - low * h / 255;
    int16_t y_top = h - (rising ? close : open) * h / 255;
    int16_t y_bottom = h - (rising ? open : close) * h / 255;
    GRect body = GRect(x, y_top, body_w, y_bottom - y_top + 1);

#ifdef PBL_COLOR
    GColor color = rising ? s_color_primary : s_color_secondary;
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_fill_color(ctx, color);
    graphics_draw_line(ctx, GPoint(wick_x, y_high), GPoint(wick_x, y_low));
    graphics_fill_rect(ctx, body, 0, GCornerNone);
#else
    // Black & white: rising candles hollow, falling candles filled
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_draw_line(ctx, GPoint(wick_x, y_high), GPoint(wick_x, y_low));
    if (rising && body_w >= 3 && body.size.h >= 3) {
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx, body, 0, GCornerNone);
      graphics_draw_rect(ctx, body);
    } else {
      graphics_fill_rect(ctx, body, 0, GCornerNone);
    }
#endif
  }
}

static void chart_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (s_chart_style == CHART_STYLE_CANDLES) {
    draw_candle_chart(ctx, bounds);
  } else {
    draw_line_chart(ctx, bounds);
  }
}

// Position price and chart. With the chart off this is the original layout.
static void apply_layout(void) {
  GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
  int16_t title_y = bounds.size.h * 0.7;

  if (s_chart_enabled) {
    int16_t chart_h = bounds.size.h / 9;
    int16_t gap = bounds.size.h / 28;  // space around the chart (6px on 168px screens)
    int16_t inset = PBL_IF_ROUND_ELSE(bounds.size.w / 5, bounds.size.w / 10);
    // Fonts have built-in top padding, so a small offset from the label is enough
    GRect chart_frame = GRect(inset, title_y - chart_h - 2,
                              bounds.size.w - 2 * inset, chart_h);
    layer_set_frame(s_chart_layer, chart_frame);
    s_price_center_y = bounds.size.h / 2 - chart_h / 2 - gap;
  } else {
    s_price_center_y = bounds.size.h / 2;
  }

  layer_set_hidden(s_chart_layer, !s_chart_enabled);
  set_price_text(s_price_text);
}

// Apply accent colors to the labels and redraw the chart.
static void apply_colors(void) {
  text_layer_set_text_color(s_time_layer, COLOR_FALLBACK(s_color_primary, GColorWhite));
  text_layer_set_text_color(s_title_layer, COLOR_FALLBACK(s_color_secondary, GColorWhite));
  layer_mark_dirty(s_chart_layer);
}

// ------------------- messaging ----------------------

// Write message to buffer & send
static void refresh(void){
  DictionaryIterator *iter;

  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return;
  }
  dict_write_cstring(iter, MESSAGE_KEY_action, "refresh");

  dict_write_end(iter);
  app_message_outbox_send();
}

// Called when a message is received from PebbleKitJS
static void in_received_handler(DictionaryIterator *received, void *context) {
  Tuple *ltp_tuple = dict_find(received, MESSAGE_KEY_price);
  if(ltp_tuple) {
    snprintf(s_price_text, sizeof(s_price_text), "%s", ltp_tuple->value->cstring);
    set_price_text(s_price_text);
  }

  Tuple *status_tuple = dict_find(received, MESSAGE_KEY_status);
  if(status_tuple) {
    char *status = status_tuple->value->cstring;
    // Use a static buffer to store the string for display
    static char s_buffer[30];
    snprintf(s_buffer, sizeof(s_buffer), "%s", status);
    text_layer_set_text(s_title_layer, s_buffer);
  }

  Tuple *enabled_tuple = dict_find(received, MESSAGE_KEY_chartEnabled);
  if (enabled_tuple) {
    bool enabled = enabled_tuple->value->int32 != 0;
    if (enabled != s_chart_enabled) {
      s_chart_enabled = enabled;
      persist_write_bool(PERSIST_CHART_ENABLED, enabled);
      apply_layout();
    }
  }

  Tuple *primary_tuple = dict_find(received, MESSAGE_KEY_colorPrimary);
  Tuple *secondary_tuple = dict_find(received, MESSAGE_KEY_colorSecondary);
  if (primary_tuple) {
    s_color_primary = GColorFromHEX(primary_tuple->value->int32);
    persist_write_int(PERSIST_COLOR_PRIMARY, primary_tuple->value->int32);
  }
  if (secondary_tuple) {
    s_color_secondary = GColorFromHEX(secondary_tuple->value->int32);
    persist_write_int(PERSIST_COLOR_SECONDARY, secondary_tuple->value->int32);
  }
  if (primary_tuple || secondary_tuple) {
    apply_colors();
  }

  // Style arrives together with the chart data it describes
  Tuple *style_tuple = dict_find(received, MESSAGE_KEY_chartStyle);
  if (style_tuple) {
    s_chart_style = style_tuple->value->int32;
    persist_write_int(PERSIST_CHART_STYLE, s_chart_style);
  }

  Tuple *chart_tuple = dict_find(received, MESSAGE_KEY_chart);
  if (chart_tuple && chart_tuple->type == TUPLE_BYTE_ARRAY) {
    int count = chart_tuple->length;
    if (count > MAX_CHART_POINTS) {
      count = MAX_CHART_POINTS;
    }
    memcpy(s_chart_points, chart_tuple->value->data, count);
    s_chart_count = count;
    persist_write_data(PERSIST_CHART_DATA, s_chart_points, count);
    layer_mark_dirty(s_chart_layer);
  }
}

// Called when an incoming message from PebbleKitJS is dropped
static void in_dropped_handler(AppMessageResult reason, void *context) {
}

// Called when PebbleKitJS does not acknowledge receipt of a message
static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
}

// ==========================================

static void update_time() {
  // Get a tm structure
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  // Write the current hours and minutes into a buffer
  static char s_buffer[8];
  strftime(s_buffer, sizeof(s_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);

  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, s_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  refresh();
  update_time();
}

// ------------------- window event ----------------------

static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Background
  window_set_background_color(window, COLOR_BG);

  // Create the TextLayer with specific bounds
  s_price_center_y = bounds.size.h / 2;  // Store vertical center
  s_price_bounds = GRect(0, bounds.size.h / 2 - 25, bounds.size.w, 50);
  s_price_layer = text_layer_create(s_price_bounds);
  text_layer_set_background_color(s_price_layer, GColorClear);
  text_layer_set_text_color(s_price_layer, GColorWhite);
  text_layer_set_text_alignment(s_price_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_price_layer, GTextOverflowModeFill);

  // time
  char *time_font = bounds.size.w > 180 ? FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM : FONT_KEY_LECO_20_BOLD_NUMBERS;
  s_time_layer = text_layer_create(GRect(0, bounds.size.h * 0.2, bounds.size.w, 30));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_font(s_time_layer, fonts_get_system_font(time_font));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  // detail text layer
  char *detail_font = bounds.size.w > 180 ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD;
  s_title_layer = text_layer_create(GRect(0, bounds.size.h * 0.7, bounds.size.w, 30));
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_text(s_title_layer, "connecting");
  text_layer_set_font(s_title_layer, fonts_get_system_font(detail_font));

  // chart
  s_chart_layer = layer_create(GRectZero);
  layer_set_update_proc(s_chart_layer, chart_update_proc);

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, text_layer_get_layer(s_price_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));
  layer_add_child(window_layer, s_chart_layer);

  apply_colors();
  apply_layout();

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void main_window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  layer_destroy(s_chart_layer);
  text_layer_destroy(s_price_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_title_layer);
}

// ==============================================

static void init(void) {
  s_color_primary = GColorFromHEX(persist_exists(PERSIST_COLOR_PRIMARY)
    ? persist_read_int(PERSIST_COLOR_PRIMARY) : DEFAULT_COLOR_PRIMARY);
  s_color_secondary = GColorFromHEX(persist_exists(PERSIST_COLOR_SECONDARY)
    ? persist_read_int(PERSIST_COLOR_SECONDARY) : DEFAULT_COLOR_SECONDARY);

  // Restore chart state so it shows immediately on launch
  if (persist_exists(PERSIST_CHART_ENABLED)) {
    s_chart_enabled = persist_read_bool(PERSIST_CHART_ENABLED);
  }
  if (persist_exists(PERSIST_CHART_STYLE)) {
    s_chart_style = persist_read_int(PERSIST_CHART_STYLE);
  }
  if (persist_exists(PERSIST_CHART_DATA)) {
    int read = persist_read_data(PERSIST_CHART_DATA, s_chart_points, sizeof(s_chart_points));
    s_chart_count = read > 0 ? read : 0;
  }

  s_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  window_stack_push(s_window, true);

  // Register AppMessage handlers
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_register_outbox_failed(out_failed_handler);

  // Inbox fits price + status + up to 64 chart points
  const int inbox_size = 256;
  const int outbox_size = 128;
  app_message_open(inbox_size, outbox_size);

  // Set the current time
  update_time();
}

static void deinit(void) {
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main( void ) {
  init();
  app_event_loop();
  deinit();
}
