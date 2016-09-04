#include <pebble.h>

static Window *s_window;
static TextLayer *s_price_layer;

#define COLOR_BG GColorFromRGB(31, 37, 44)
#define COLOR_SELL GColorFromRGB(247, 105, 77)

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
    // This value was stored as JS Number, which is stored here as int32_t
    int ltp = ltp_tuple->value->int32;
    
    static char price[] = "00000000000";
    snprintf(price, sizeof(price), "%d", ltp);
    text_layer_set_text(s_price_layer, price);
  }
}

// Called when an incoming message from PebbleKitJS is dropped
static void in_dropped_handler(AppMessageResult reason, void *context) {	
}

// Called when PebbleKitJS does not acknowledge receipt of a message
static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
}

// ==========================================

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  refresh();
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
  
  TextLayer *s_title_layer = text_layer_create(GRect(0, bounds.size.h / 2 + 25, bounds.size.w, 20));
  text_layer_set_background_color(s_title_layer, COLOR_BG);
  text_layer_set_text_color(s_title_layer, COLOR_SELL);
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_text(s_title_layer, "BTC/JPY");
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, text_layer_get_layer(s_price_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));
  
  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void main_window_unload(Window *window) {
  // Destroy TextLayer
  text_layer_destroy(s_price_layer);
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