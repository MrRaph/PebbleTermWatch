/*
 * Pebble Term Watch
 *
 * pebble watchface for sdk 2
 *
 * Special thanks to the following based watchfaces:
 *  CMD Time Typed: https://github.com/C-D-Lewis/cmd-time-typed
 *  91 Dub v2.0: https://github.com/orviwan/91-Dub-v2.0
 */
#include <pebble.h>

#define TYPE_DELTA 200
#define PROMPT_DELTA 1000
//XXX: Starts with 0?
#define SETTINGS_KEY 262

static AppSync sync;
static uint8_t sync_buffer[64];

// Layers
static Window *window;
static Layer *window_layer;

static TextLayer *time_label, *time_layer,
                 *date_label, *date_layer,
                 *hour_label, *hour_layer,
                 *prompt_label;

static InverterLayer *prompt_layer;

static AppTimer *timer;

typedef struct persist {
  uint8_t BluetoothVibe;
  uint8_t TypingAnimation;
  int16_t TimezoneOffset;
} __attribute__((__packed__)) persist;

persist settings = {
  .BluetoothVibe = 1,
  .TypingAnimation = 1,
  .TimezoneOffset = 0
};

enum {
  BLUETOOTH_VIBE_KEY,
  TYPING_ANIMATION_KEY,
  TIMEZONE_OFFSET_KEY
};

static bool appStarted = false;

#define INITTIME_PROMPT_LIMIT 10
static bool firstRun = true;
static int initTime = 0;
static int secondsSync = 0;

// bluetooth
static GBitmap *bluetooth_image;
static BitmapLayer *bluetooth_layer;

// battery 
static uint8_t batteryPercent;
static GBitmap *battery_image;
static BitmapLayer *battery_image_layer;
static BitmapLayer *battery_layer;

static GBitmap *background_image;
static BitmapLayer *background_layer;

static GBitmap *branding_mask_image;
static BitmapLayer *branding_mask_layer;

// battery percent(XX% - XXX%)
#define TOTAL_BATTERY_PERCENT_DIGITS 4
static GBitmap *battery_percent_image[TOTAL_BATTERY_PERCENT_DIGITS];
static BitmapLayer *battery_percent_layers[TOTAL_BATTERY_PERCENT_DIGITS];

const int TINY_IMAGE_RESOURCE_IDS[] = {
  RESOURCE_ID_IMAGE_TINY_0,
  RESOURCE_ID_IMAGE_TINY_1,
  RESOURCE_ID_IMAGE_TINY_2,
  RESOURCE_ID_IMAGE_TINY_3,
  RESOURCE_ID_IMAGE_TINY_4,
  RESOURCE_ID_IMAGE_TINY_5,
  RESOURCE_ID_IMAGE_TINY_6,
  RESOURCE_ID_IMAGE_TINY_7,
  RESOURCE_ID_IMAGE_TINY_8,
  RESOURCE_ID_IMAGE_TINY_9,
  RESOURCE_ID_IMAGE_TINY_PERCENT
};

// Buffers
static char date_buffer[] = "XXXX-XX-XX",
            hour_buffer[] = "XX:XX:XX",
            //TODO: Display day ("Sun", "Mon" ...)
            //day_buffer[] = "XXX",
            // unixtime ("0" - "2147483647"?)
            time_buffer[] = "XXXXXXXXXXXXXXX";

// State
static int state = 0;
static bool prompt_visible = false;

// Prototypes
static TextLayer* cl_init_text_layer(GRect location,
                                     GColor colour,
                                     GColor background,
                                     ResHandle handle,
                                     GTextAlignment alignment);

static void set_container_image(GBitmap **bmp_image,
                                BitmapLayer *bmp_layer,
                                const int resource_id,
                                GPoint origin) {

  GBitmap *old_image = *bmp_image;
  *bmp_image = gbitmap_create_with_resource(resource_id);

  GRect frame = (GRect) {
    .origin = origin,
    .size = (*bmp_image)->bounds.size
  };
  bitmap_layer_set_bitmap(bmp_layer, *bmp_image);
  layer_set_frame(bitmap_layer_get_layer(bmp_layer), frame);

  if (old_image != NULL) {
    gbitmap_destroy(old_image);
    old_image = NULL;
  }
}

void change_background() {
  gbitmap_destroy(background_image);
  gbitmap_destroy(branding_mask_image);

  //XXX: settings.Invert
  background_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
  branding_mask_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BRANDING_MASK);

  bitmap_layer_set_bitmap(branding_mask_layer, branding_mask_image);
  layer_mark_dirty(bitmap_layer_get_layer(branding_mask_layer));

  bitmap_layer_set_bitmap(background_layer, background_image);
  layer_mark_dirty(bitmap_layer_get_layer(background_layer));
}


void change_battery_icon(bool charging) {
  gbitmap_destroy(battery_image);

  if (charging) {
    battery_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY_CHARGE);
  } else {
    battery_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY);
  }  
  bitmap_layer_set_bitmap(battery_image_layer, battery_image);
  layer_mark_dirty(bitmap_layer_get_layer(battery_image_layer));
}

// battery
static void update_battery(BatteryChargeState charge_state) {
  batteryPercent = charge_state.charge_percent;

  if (batteryPercent == 100) {
    change_battery_icon(false);
    layer_set_hidden(bitmap_layer_get_layer(battery_layer), false);

    for (int i = 0; i < TOTAL_BATTERY_PERCENT_DIGITS; ++i) {
      layer_set_hidden(bitmap_layer_get_layer(battery_percent_layers[i]), false);
    }

    set_container_image(&battery_percent_image[0],
                        battery_percent_layers[0],
                        TINY_IMAGE_RESOURCE_IDS[1],
                        GPoint(93, 6));

    set_container_image(&battery_percent_image[1],
                        battery_percent_layers[1],
                        TINY_IMAGE_RESOURCE_IDS[0],
                        GPoint(99, 6));

    set_container_image(&battery_percent_image[2],
                        battery_percent_layers[2],
                        TINY_IMAGE_RESOURCE_IDS[0],
                        GPoint(105, 6));

    set_container_image(&battery_percent_image[3],
                        battery_percent_layers[3],
                        TINY_IMAGE_RESOURCE_IDS[10],
                        GPoint(111, 7));
    return;
  }

  layer_set_hidden(bitmap_layer_get_layer(battery_layer), charge_state.is_charging);
  change_battery_icon(charge_state.is_charging);

  layer_set_hidden(bitmap_layer_get_layer(battery_percent_layers[0]), true);
  for (int i = 1; i < TOTAL_BATTERY_PERCENT_DIGITS; ++i) {
    layer_set_hidden(bitmap_layer_get_layer(battery_percent_layers[i]), false);
  }

  set_container_image(&battery_percent_image[1],
                      battery_percent_layers[1],
                      TINY_IMAGE_RESOURCE_IDS[charge_state.charge_percent / 10],
                      GPoint(99, 6));

  set_container_image(&battery_percent_image[2],
                      battery_percent_layers[2],
                      TINY_IMAGE_RESOURCE_IDS[charge_state.charge_percent % 10],
                      GPoint(105, 6));

  set_container_image(&battery_percent_image[3],
                      battery_percent_layers[3],
                      TINY_IMAGE_RESOURCE_IDS[10],
                      GPoint(111, 7));
}

void battery_layer_update_callback(Layer *me, GContext* ctx) {
  // draw the remaining battery percentage
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(2, 2, ((batteryPercent / 100.0) * 11.0), 5), 0, GCornerNone);
}

// bluetooth
static void toggle_bluetooth_icon(bool connected) {
  if (appStarted && !connected && settings.BluetoothVibe) {
    // handle bluetooth disconnect
    vibes_long_pulse();
  }
  layer_set_hidden(bitmap_layer_get_layer(bluetooth_layer), !connected);
}

void bluetooth_connection_callback(bool connected) {
  toggle_bluetooth_icon(connected);
}

// Callback for settings
static void sync_tuple_changed_callback(const uint32_t key,
                                        const Tuple* new_tuple,
                                        const Tuple* old_tuple,
                                        void* context) {
  switch (key) {
    case BLUETOOTH_VIBE_KEY:
      settings.BluetoothVibe = new_tuple->value->uint8;
      break;
    case TYPING_ANIMATION_KEY:
      settings.TypingAnimation = new_tuple->value->uint8;
      break;
    case TIMEZONE_OFFSET_KEY:
      settings.TimezoneOffset = new_tuple->value->int16;
      break;
  }
}


// Time Lifecycle

static void set_time(struct tm *t) {
  // date
  if (clock_is_24h_style()) {
    strftime(hour_buffer, sizeof("XX:XX:XX"),"%H:%M:%S", t);
  } else {
    strftime(hour_buffer, sizeof("XX:XX:XX"),"%I:%M:%S", t);
  }
  text_layer_set_text(hour_layer, hour_buffer);

  strftime(date_buffer, sizeof("XXXX-XX-XX"), "%Y-%m-%d", t);
  text_layer_set_text(date_layer, date_buffer);

  // unixtime
  // Pebble SDK 2 can't get timezone offset?
  snprintf(time_buffer, sizeof("XXXXXXXXXXXXXXX"), "%u", (unsigned)time(NULL) + settings.TimezoneOffset);
  text_layer_set_text(time_layer, time_buffer);
}

static void set_time_anim() {
  // Time structures -- Cannot be branch declared
  time_t temp;
  struct tm *t;
  bool timed = false;

  // frame animation
  switch (state) {
    case 0:
      temp = time(NULL);
      t = localtime(&temp);
      set_time(t);
      timed = true;
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 1:
      text_layer_set_text(date_label, "pebble>d");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 2:
      text_layer_set_text(date_label, "pebble>da");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 3:
      text_layer_set_text(date_label, "pebble>dat");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 4:
      text_layer_set_text(date_label, "pebble>date");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 5:
      text_layer_set_text(date_label, "pebble>date +");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 6:
      text_layer_set_text(date_label, "pebble>date +%");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 7:
      text_layer_set_text(date_label, "pebble>date +%F");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 8:
      layer_add_child(window_get_root_layer(window), text_layer_get_layer(date_layer));
      text_layer_set_text(hour_label, "pebble>");
      timer = app_timer_register(10 * TYPE_DELTA, set_time_anim, 0);
      break;
    case 9:
      text_layer_set_text(hour_label, "pebble>d");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 10:
      text_layer_set_text(hour_label, "pebble>da");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 11:
      text_layer_set_text(hour_label, "pebble>dat");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 12:
      text_layer_set_text(hour_label, "pebble>date");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 13:
      text_layer_set_text(hour_label, "pebble>date +");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 14:
      text_layer_set_text(hour_label, "pebble>date +%");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 15:
      text_layer_set_text(hour_label, "pebble>date +%T");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 16:
      layer_add_child(window_get_root_layer(window), text_layer_get_layer(hour_layer));
      text_layer_set_text(time_label, "pebble>");

      if (firstRun && secondsSync == 0 && !settings.TypingAnimation) {
        secondsSync = 10;
        timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      } else {
        timer = app_timer_register(10 * TYPE_DELTA, set_time_anim, 0);
      }
      break;
    case 17:
      text_layer_set_text(time_label, "pebble>d");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 18:
      text_layer_set_text(time_label, "pebble>da");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 19:
      text_layer_set_text(time_label, "pebble>date");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 20:
      text_layer_set_text(time_label, "pebble>date +");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 21:
      text_layer_set_text(time_label, "pebble>date +%");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 22:
      text_layer_set_text(time_label, "pebble>date +%s");
      timer = app_timer_register(TYPE_DELTA, set_time_anim, 0);
      break;
    case 23:
      layer_add_child(window_get_root_layer(window), text_layer_get_layer(time_layer));
      text_layer_set_text(prompt_label, "pebble>");
      prompt_visible = true;
      timer = app_timer_register(PROMPT_DELTA, set_time_anim, 0);
      break;
    default:
      // Rest of the minute
      if (prompt_visible) {
        prompt_visible = false;
        layer_remove_from_parent(inverter_layer_get_layer(prompt_layer));
      } else {
        prompt_visible = true;
        layer_add_child(window_get_root_layer(window), inverter_layer_get_layer(prompt_layer));
      }

      if (firstRun && ++initTime > INITTIME_PROMPT_LIMIT) {
        firstRun = false;
        initTime = 0;
      }
      timer = app_timer_register(PROMPT_DELTA, set_time_anim, 0);
      break;
  }

  // disabled animation
  if (!settings.TypingAnimation && state > 16) {
    if (!timed) {
      temp = time(NULL);
      t = localtime(&temp);
      set_time(t);
    }
    layer_remove_from_parent(text_layer_get_layer(date_layer));
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(date_layer));
    if (state > 16) {
      layer_remove_from_parent(text_layer_get_layer(hour_layer));
      layer_add_child(window_get_root_layer(window), text_layer_get_layer(hour_layer));
      if (state > 23) {
        layer_remove_from_parent(text_layer_get_layer(time_layer));
        layer_add_child(window_get_root_layer(window), text_layer_get_layer(time_layer));
      }
    }
    if (secondsSync > 0) {
      secondsSync--;
      return;
    }
  }
  state++;
}

static void tick_handler(struct tm *t, TimeUnits units_changed) {
  if (timer != NULL) {
    app_timer_cancel(timer);

    //XXX: delay for typing animation
    if (firstRun && state < 26) {
      timer = app_timer_register(PROMPT_DELTA, set_time_anim, 0);
      return;
    }
  }

  //TODO: display seconds
  timer = app_timer_register(PROMPT_DELTA, set_time_anim, 0);
  if (!firstRun && !settings.TypingAnimation) {
    if (state > 25) {
      state = 25;

      layer_remove_from_parent(text_layer_get_layer(date_layer));
      layer_add_child(window_get_root_layer(window), text_layer_get_layer(date_layer));
      layer_remove_from_parent(text_layer_get_layer(hour_layer));
      layer_add_child(window_get_root_layer(window), text_layer_get_layer(hour_layer));
      layer_remove_from_parent(text_layer_get_layer(time_layer));
      layer_add_child(window_get_root_layer(window), text_layer_get_layer(time_layer));

      prompt_visible = false;
    }
  } else {
    // Start anim cycle
    state = 0;

    // Blank before time change
    text_layer_set_text(date_label, "pebble>");
    layer_remove_from_parent(text_layer_get_layer(date_layer));
    text_layer_set_text(hour_label, "");
    layer_remove_from_parent(text_layer_get_layer(hour_layer));
    text_layer_set_text(time_label, "");
    layer_remove_from_parent(text_layer_get_layer(time_layer));
    text_layer_set_text(prompt_label, "");

    layer_remove_from_parent(inverter_layer_get_layer(prompt_layer));
    prompt_visible = false;
  }

  // Change time display
  set_time(t);
}

// Window Lifecycle

static void window_load(Window *window) {
  // font
  ResHandle font_handle = resource_get_handle(RESOURCE_ID_FONT_LUCIDA_13);

  // date
  date_label = cl_init_text_layer(GRect(5, 24, 144, 30),
                                  GColorWhite,
                                  GColorClear,
                                  font_handle,
                                  GTextAlignmentLeft);
  text_layer_set_text(date_label, "");
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(date_label));

  date_layer = cl_init_text_layer(GRect(5, 40, 144, 30),
                                  GColorWhite,
                                  GColorClear,
                                  font_handle,
                                  GTextAlignmentLeft);
  text_layer_set_text(date_layer, "");
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(date_layer));

  // hour
  hour_label = cl_init_text_layer(GRect(5, 55, 144, 30),
                                  GColorWhite,
                                  GColorClear,
                                  font_handle,
                                  GTextAlignmentLeft);
  text_layer_set_text(hour_label, "");
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(hour_label));

  hour_layer = cl_init_text_layer(GRect(5, 71, 144, 30),
                                  GColorWhite,
                                  GColorClear,
                                  font_handle,
                                  GTextAlignmentLeft);
  text_layer_set_text(hour_layer, "");
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(hour_layer));

  // Time
  time_label = cl_init_text_layer(GRect(5, 87, 144, 30),
                                  GColorWhite,
                                  GColorClear,
                                  font_handle,
                                  GTextAlignmentLeft);
  text_layer_set_text(time_label, "");
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(time_label));

  time_layer = cl_init_text_layer(GRect(5, 103, 144, 30),
                                  GColorWhite,
                                  GColorClear,
                                  font_handle,
                                  GTextAlignmentLeft);
  text_layer_set_text(time_layer, "");
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(time_layer));

  // Prompt
  prompt_label = cl_init_text_layer(GRect(5, 119, 144, 30),
                                    GColorWhite,
                                    GColorClear,
                                    font_handle,
                                    GTextAlignmentLeft);
  text_layer_set_text(prompt_label, "");
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(prompt_label));

  prompt_layer = inverter_layer_create(GRect(61, 132, 8, 2));
}

static void window_unload(Window *window) {
  // date
  text_layer_destroy(date_label);
  text_layer_destroy(date_layer);

  // time
  text_layer_destroy(time_label);
  text_layer_destroy(time_layer);

  // hour
  text_layer_destroy(hour_label);
  text_layer_destroy(hour_layer);

  // Prompt
  text_layer_destroy(prompt_label);
  inverter_layer_destroy(prompt_layer);
}

// App Lifecycle

static void init(void) {
  memset(&battery_percent_layers, 0, sizeof(battery_percent_layers));
  memset(&battery_percent_image, 0, sizeof(battery_percent_image));

  window = window_create();
  if (window == NULL) {
    return;
  }
  window_layer = window_get_root_layer(window);

  const int inbound_size = 64;
  const int outbound_size = 64;
  app_message_open(inbound_size, outbound_size);
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));

  background_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
  background_layer = bitmap_layer_create(layer_get_frame(window_layer));

  bitmap_layer_set_bitmap(background_layer, background_image);
  layer_add_child(window_layer, bitmap_layer_get_layer(background_layer));

  WindowHandlers handlers = {
    .load = window_load,
    .unload = window_unload
  };

  window_set_window_handlers(window, handlers);
  window_set_background_color(window, GColorBlack);

  // Get tick events
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  // bluetooth
  bluetooth_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BLUETOOTH);
  GRect frame3 = (GRect) {
    .origin = { .x = 80, .y = 5 },
    .size = bluetooth_image->bounds.size
  };
  bluetooth_layer = bitmap_layer_create(frame3);
  bitmap_layer_set_bitmap(bluetooth_layer, bluetooth_image);

  // battery
  battery_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY);
  GRect frame4 = (GRect) {
    .origin = { .x = 121, .y = 6 },
    .size = battery_image->bounds.size
  };
  battery_layer = bitmap_layer_create(frame4);
  battery_image_layer = bitmap_layer_create(frame4);
  bitmap_layer_set_bitmap(battery_image_layer, battery_image);
  layer_set_update_proc(bitmap_layer_get_layer(battery_layer), battery_layer_update_callback);

  // mask the pebble branding
  GRect framemask = (GRect) {
    .origin = { .x = 0, .y = 0 },
    .size = { .w = 144, .h = 19 }
  };
  branding_mask_layer = bitmap_layer_create(framemask);
  layer_add_child(window_layer, bitmap_layer_get_layer(branding_mask_layer));
  branding_mask_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BRANDING_MASK);
  bitmap_layer_set_bitmap(branding_mask_layer, branding_mask_image);
  //XXX: mask
  layer_set_hidden(bitmap_layer_get_layer(branding_mask_layer), true);

  layer_add_child(window_layer, bitmap_layer_get_layer(bluetooth_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(battery_image_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(battery_layer));

  // Create time and date layers
  GRect dummy_frame = { {0, 0}, {0, 0} };

  for (int i = 0; i < TOTAL_BATTERY_PERCENT_DIGITS; ++i) {
    battery_percent_layers[i] = bitmap_layer_create(dummy_frame);
    layer_add_child(window_layer, bitmap_layer_get_layer(battery_percent_layers[i]));
  }

  toggle_bluetooth_icon(bluetooth_connection_service_peek());
  update_battery(battery_state_service_peek());

  Tuplet initial_values[] = {
    TupletInteger(BLUETOOTH_VIBE_KEY, settings.BluetoothVibe),
    TupletInteger(TYPING_ANIMATION_KEY, settings.TypingAnimation),
    TupletInteger(TIMEZONE_OFFSET_KEY, settings.TimezoneOffset)
  };

  app_sync_init(&sync, sync_buffer, sizeof(sync_buffer),
                initial_values, ARRAY_LENGTH(initial_values),
                sync_tuple_changed_callback, NULL, NULL);

  appStarted = true;

  bluetooth_connection_service_subscribe(bluetooth_connection_callback);
  battery_state_service_subscribe(&update_battery);

  window_stack_push(window, true);
}

static void deinit(void) {
  app_sync_deinit(&sync);

  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();

  layer_remove_from_parent(bitmap_layer_get_layer(background_layer));
  bitmap_layer_destroy(background_layer);
  gbitmap_destroy(background_image);
  background_image = NULL;

  layer_remove_from_parent(bitmap_layer_get_layer(bluetooth_layer));
  bitmap_layer_destroy(bluetooth_layer);
  gbitmap_destroy(bluetooth_image);
  bluetooth_image = NULL;

  layer_remove_from_parent(bitmap_layer_get_layer(battery_layer));
  bitmap_layer_destroy(battery_layer);
  gbitmap_destroy(battery_image);
  battery_image = NULL;

  background_image = NULL;

  layer_remove_from_parent(bitmap_layer_get_layer(battery_image_layer));
  bitmap_layer_destroy(battery_image_layer);

  for (int i = 0; i < TOTAL_BATTERY_PERCENT_DIGITS; i++) {
    layer_remove_from_parent(bitmap_layer_get_layer(battery_percent_layers[i]));
    gbitmap_destroy(battery_percent_image[i]);
    battery_percent_image[i] = NULL;
    bitmap_layer_destroy(battery_percent_layers[i]); 
	  battery_percent_layers[i] = NULL;
  }

  layer_remove_from_parent(window_layer);
  layer_destroy(window_layer);

  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

// Other functions

static TextLayer* cl_init_text_layer(GRect location,
                                     GColor colour,
                                     GColor background,
                                     ResHandle handle,
                                     GTextAlignment alignment) {

  TextLayer *layer = text_layer_create(location);
  text_layer_set_text_color(layer, colour);
  text_layer_set_background_color(layer, background);
  text_layer_set_font(layer, fonts_load_custom_font(handle));
  text_layer_set_text_alignment(layer, alignment);

  return layer;
}