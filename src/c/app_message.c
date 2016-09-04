#include <pebble.h>

static Window *s_window;
static TextLayer *s_price_layer, *s_time_layer, *s_title_layer;

#define COLOR_BG GColorFromRGB(31, 37, 44)
#define COLOR_SELL GColorFromRGB(247, 105, 77)
#define COLOR_BUY GColorFromRGB(214, 161, 36)

// Write message to buffer & send
static void refresh(void){
	DictionaryIterator *iter;

	app_message_outbox_begin(&iter);
	dict_write_cstring(iter, MESSAGE_KEY_action, "refresh");

	dict_write_end(iter);
  app_message_outbox_send();
}

// Called when a message is received from PebbleKitJS
static void in_received_handler(DictionaryIterator *received, void *context) {
	Tuple *ltp_tuple = dict_find(received, MESSAGE_KEY_ltp);
	if(ltp_tuple) {
		char *price = ltp_tuple->value->cstring;
		// Use a static buffer to store the string for display
    static char s_buffer[30];
    snprintf(s_buffer, sizeof(s_buffer), "%s", price);
		text_layer_set_text(s_price_layer, s_buffer);
  }

	Tuple *status_tuple = dict_find(received, MESSAGE_KEY_status);
	if(status_tuple) {
    char *status = status_tuple->value->cstring;
		// Use a static buffer to store the string for display
    static char s_buffer[30];
    snprintf(s_buffer, sizeof(s_buffer), "%s", status);
		text_layer_set_text(s_title_layer, s_buffer);
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
  s_price_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 25, bounds.size.w, 50));
  text_layer_set_background_color(s_price_layer, COLOR_BG);
  text_layer_set_text_color(s_price_layer, GColorWhite);
  text_layer_set_text(s_price_layer, "-");
  text_layer_set_font(s_price_layer, fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS));
  text_layer_set_text_alignment(s_price_layer, GTextAlignmentCenter);

	// time
  s_time_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 50, bounds.size.w, 20));
  text_layer_set_background_color(s_time_layer, COLOR_BG);
  text_layer_set_text_color(s_time_layer, COLOR_BUY);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

	// detail text layer
  s_title_layer = text_layer_create(GRect(0, bounds.size.h / 2 + 25, bounds.size.w, 20));
  text_layer_set_background_color(s_title_layer, COLOR_BG);
  text_layer_set_text_color(s_title_layer, COLOR_FALLBACK(COLOR_SELL, GColorWhite));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_text(s_title_layer, "connecting");
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, text_layer_get_layer(s_price_layer));
	layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void main_window_unload(Window *window) {
  // Destroy TextLayer
  text_layer_destroy(s_price_layer);
	text_layer_destroy(s_time_layer);
}

// ==============================================

static void init(void) {
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

  // Initialize AppMessage inbox and outbox buffers with a suitable size
  const int inbox_size = 128;
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
