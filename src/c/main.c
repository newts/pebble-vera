// Vera Thermostat — Pebble watchapp
//
// Reads a Z-Wave thermostat through a Vera controller (via the phone) and
// lets you change the heat setpoint.
//
//   Up      +1 degree
//   Down    -1 degree
//   Select  set to current room temperature + 2
//
// Changes are sent SET_DELAY_MS after the last press, so several presses
// become one request. The screen is white when the heater is idle and red
// when it is heating. Data refreshes every 10 s, and the app closes itself
// after 60 s without a button press.

#include <pebble.h>

#define POLL_INTERVAL_MS   10000
#define SET_DELAY_MS        2000
#define IDLE_TIMEOUT_MS    60000
#define SELECT_OFFSET          2
#define DEFAULT_SETPOINT      70
#define NONE           (-10000)

// Values for MESSAGE_KEY_Cmd
enum { CMD_POLL = 1, CMD_SET = 2 };

static Window *s_window;
static TextLayer *s_room_label;
static TextLayer *s_temp_layer;
static TextLayer *s_set_layer;
static TextLayer *s_status_layer;
static TextLayer *s_hint_up;
static TextLayer *s_hint_select;
static TextLayer *s_hint_down;
static Layer *s_bt_layer;
static bool s_bt_connected = true;

static AppTimer *s_poll_timer;
static AppTimer *s_set_timer;
static AppTimer *s_idle_timer;

static int s_temp = NONE;
static int s_setpoint = NONE;
static int s_pending = NONE;
static bool s_heating = false;
static bool s_sending = false;
static bool s_skipped_poll = false;
static char s_error[48];

static char s_temp_buf[8];
static char s_set_buf[24];

// --------------------------------------------------------------------------
// Display
// --------------------------------------------------------------------------

static void set_text_color(TextLayer *layer, GColor color) {
  text_layer_set_text_color(layer, color);
}

static void update_display(void) {
  GColor bg = s_heating ? PBL_IF_COLOR_ELSE(GColorRed, GColorBlack) : GColorWhite;
  GColor fg = s_heating ? GColorWhite : GColorBlack;
  window_set_background_color(s_window, bg);
  layer_mark_dirty(s_bt_layer);

  TextLayer *layers[] = { s_room_label, s_temp_layer, s_set_layer, s_status_layer,
                          s_hint_up, s_hint_select, s_hint_down };
  for (unsigned i = 0; i < ARRAY_LENGTH(layers); i++) {
    set_text_color(layers[i], fg);
  }

  if (s_temp == NONE) {
    snprintf(s_temp_buf, sizeof(s_temp_buf), "--");
  } else {
    snprintf(s_temp_buf, sizeof(s_temp_buf), "%d", s_temp);
  }
  text_layer_set_text(s_temp_layer, s_temp_buf);

  if (s_pending != NONE) {
    snprintf(s_set_buf, sizeof(s_set_buf), "Set %d° *", s_pending);
  } else if (s_setpoint != NONE) {
    snprintf(s_set_buf, sizeof(s_set_buf), "Set %d°", s_setpoint);
  } else {
    snprintf(s_set_buf, sizeof(s_set_buf), "Set --");
  }
  text_layer_set_text(s_set_layer, s_set_buf);

  if (s_sending) {
    text_layer_set_text(s_status_layer, "SENDING...");
  } else if (s_error[0]) {
    text_layer_set_text(s_status_layer, s_error);
  } else if (s_temp == NONE) {
    text_layer_set_text(s_status_layer, "LOADING...");
  } else {
    text_layer_set_text(s_status_layer, s_heating ? "HEATING" : "IDLE");
  }
}

// Bluetooth rune in the top-left corner. Drawn solid when the phone is
// connected; drawn with an X beside it when the connection is lost.
static void bt_update_proc(Layer *layer, GContext *ctx) {
  GColor fg = s_heating ? GColorWhite : GColorBlack;
  graphics_context_set_stroke_color(ctx, fg);
  graphics_context_set_stroke_width(ctx, 2);

  // Rune fits in a 10x16 box starting at (2, 2).
  const int x = 2, y = 2;
  GPoint top = GPoint(x + 5, y + 0);
  GPoint bot = GPoint(x + 5, y + 16);
  GPoint ur  = GPoint(x + 9, y + 4);
  GPoint lr  = GPoint(x + 9, y + 12);
  GPoint ul  = GPoint(x + 1, y + 4);
  GPoint ll  = GPoint(x + 1, y + 12);

  graphics_draw_line(ctx, top, bot);
  graphics_draw_line(ctx, top, ur);
  graphics_draw_line(ctx, ur, ll);
  graphics_draw_line(ctx, bot, lr);
  graphics_draw_line(ctx, lr, ul);

  if (!s_bt_connected) {
    // X to the right of the rune.
    graphics_draw_line(ctx, GPoint(x + 14, y + 5), GPoint(x + 22, y + 13));
    graphics_draw_line(ctx, GPoint(x + 22, y + 5), GPoint(x + 14, y + 13));
  }
}

static void bt_handler(bool connected) {
  s_bt_connected = connected;
  layer_mark_dirty(s_bt_layer);
}

// --------------------------------------------------------------------------
// Phone communication
// --------------------------------------------------------------------------

static bool send_cmd(int cmd, int value) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return false;
  }
  dict_write_int32(iter, MESSAGE_KEY_Cmd, cmd);
  if (cmd == CMD_SET) {
    dict_write_int32(iter, MESSAGE_KEY_NewSetpoint, value);
  }
  return app_message_outbox_send() == APP_MSG_OK;
}

static void poll_timer_cb(void *data) {
  // Don't poll while the user is mid-adjustment. Skip one cycle while a set
  // is in flight (the phone polls right after setting), but not forever in
  // case the reply never comes.
  if (s_pending == NONE) {
    if (s_sending && !s_skipped_poll) {
      s_skipped_poll = true;
    } else {
      s_sending = false;
      send_cmd(CMD_POLL, 0);
    }
  }
  s_poll_timer = app_timer_register(POLL_INTERVAL_MS, poll_timer_cb, NULL);
}

static void set_timer_cb(void *data) {
  s_set_timer = NULL;
  if (s_pending == NONE) {
    return;
  }
  if (send_cmd(CMD_SET, s_pending)) {
    s_sending = true;
    s_skipped_poll = false;
    s_setpoint = s_pending;  // optimistic; confirmed by the next poll
    s_pending = NONE;
  } else {
    // Outbox busy — try again shortly.
    s_set_timer = app_timer_register(500, set_timer_cb, NULL);
  }
  update_display();
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  bool got_data = false;

  if ((t = dict_find(iter, MESSAGE_KEY_Temp))) {
    s_temp = (int)t->value->int32;
    got_data = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_Setpoint))) {
    s_setpoint = (int)t->value->int32;
    got_data = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_Heating))) {
    s_heating = t->value->int32 != 0;
    got_data = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_Error))) {
    strncpy(s_error, t->value->cstring, sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = '\0';
    s_sending = false;
  } else if (got_data) {
    s_error[0] = '\0';
    s_sending = false;
  }
  update_display();
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Inbox dropped: %d", (int)reason);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox failed: %d", (int)reason);
}

// --------------------------------------------------------------------------
// Buttons & idle timeout
// --------------------------------------------------------------------------

static void idle_timer_cb(void *data) {
  s_idle_timer = NULL;
  window_stack_pop_all(true);
}

static void reset_idle_timer(void) {
  if (s_idle_timer) {
    app_timer_reschedule(s_idle_timer, IDLE_TIMEOUT_MS);
  } else {
    s_idle_timer = app_timer_register(IDLE_TIMEOUT_MS, idle_timer_cb, NULL);
  }
}

static void schedule_set(void) {
  if (s_set_timer) {
    app_timer_reschedule(s_set_timer, SET_DELAY_MS);
  } else {
    s_set_timer = app_timer_register(SET_DELAY_MS, set_timer_cb, NULL);
  }
  update_display();
}

static int base_setpoint(void) {
  if (s_pending != NONE) return s_pending;
  if (s_setpoint != NONE) return s_setpoint;
  return DEFAULT_SETPOINT;
}

static void up_click(ClickRecognizerRef rec, void *ctx) {
  reset_idle_timer();
  s_pending = base_setpoint() + 1;
  schedule_set();
}

static void down_click(ClickRecognizerRef rec, void *ctx) {
  reset_idle_timer();
  s_pending = base_setpoint() - 1;
  schedule_set();
}

static void select_click(ClickRecognizerRef rec, void *ctx) {
  reset_idle_timer();
  if (s_temp == NONE) {
    vibes_short_pulse();  // no room temperature yet
    return;
  }
  s_pending = s_temp + SELECT_OFFSET;
  schedule_set();
}

static void exit_cb(void *data) {
  window_stack_pop_all(true);
}

static void back_click(ClickRecognizerRef rec, void *ctx) {
  // Send any pending change before leaving, and give it a moment to go out.
  if (s_pending != NONE) {
    if (s_set_timer) {
      app_timer_cancel(s_set_timer);
      s_set_timer = NULL;
    }
    send_cmd(CMD_SET, s_pending);
    s_pending = NONE;
    s_sending = true;
    update_display();
    app_timer_register(1000, exit_cb, NULL);
    return;
  }
  window_stack_pop_all(true);
}

static void click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// --------------------------------------------------------------------------
// Window
// --------------------------------------------------------------------------

static TextLayer *make_text(Layer *parent, GRect frame, const char *font_key,
                            GTextAlignment align) {
  TextLayer *tl = text_layer_create(frame);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_font(tl, fonts_get_system_font(font_key));
  text_layer_set_text_alignment(tl, align);
  layer_add_child(parent, text_layer_get_layer(tl));
  return tl;
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  int w = b.size.w;
  int h = b.size.h;
  int hint_w = 22;
  int cw = w - hint_w;  // content width, leaving room for button hints

  int y = h * 12 / 100;
  s_room_label = make_text(root, GRect(0, y, cw, 22), FONT_KEY_GOTHIC_18_BOLD,
                           GTextAlignmentCenter);
  text_layer_set_text(s_room_label, "ROOM");

  s_temp_layer = make_text(root, GRect(0, y + 18, cw, 50), FONT_KEY_BITHAM_42_BOLD,
                           GTextAlignmentCenter);

  s_set_layer = make_text(root, GRect(0, y + 74, cw, 34), FONT_KEY_GOTHIC_28_BOLD,
                          GTextAlignmentCenter);

  s_status_layer = make_text(root, GRect(0, y + 112, cw, 28), FONT_KEY_GOTHIC_24_BOLD,
                             GTextAlignmentCenter);

  // Button hints along the right edge.
  s_hint_up = make_text(root, GRect(cw, h * 15 / 100, hint_w, 20), FONT_KEY_GOTHIC_18_BOLD,
                        GTextAlignmentCenter);
  text_layer_set_text(s_hint_up, "+");
  s_hint_select = make_text(root, GRect(cw, h / 2 - 12, hint_w, 20), FONT_KEY_GOTHIC_18_BOLD,
                            GTextAlignmentCenter);
  text_layer_set_text(s_hint_select, "+2");
  s_hint_down = make_text(root, GRect(cw, h * 80 / 100, hint_w, 20), FONT_KEY_GOTHIC_18_BOLD,
                          GTextAlignmentCenter);
  text_layer_set_text(s_hint_down, "-");

  s_bt_layer = layer_create(GRect(0, 0, 30, 22));
  layer_set_update_proc(s_bt_layer, bt_update_proc);
  layer_add_child(root, s_bt_layer);
  s_bt_connected = connection_service_peek_pebble_app_connection();

  update_display();
}

static void window_unload(Window *window) {
  text_layer_destroy(s_room_label);
  text_layer_destroy(s_temp_layer);
  text_layer_destroy(s_set_layer);
  text_layer_destroy(s_status_layer);
  text_layer_destroy(s_hint_up);
  text_layer_destroy(s_hint_select);
  text_layer_destroy(s_hint_down);
  layer_destroy(s_bt_layer);
}

static void init(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(256, 64);

  window_stack_push(s_window, true);

  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bt_handler,
  });

  // The phone polls on its own as soon as its JS is ready; after that the
  // watch asks every 10 seconds.
  s_poll_timer = app_timer_register(POLL_INTERVAL_MS, poll_timer_cb, NULL);
  reset_idle_timer();
}

static void deinit(void) {
  connection_service_unsubscribe();
  if (s_poll_timer) app_timer_cancel(s_poll_timer);
  if (s_set_timer) app_timer_cancel(s_set_timer);
  if (s_idle_timer) app_timer_cancel(s_idle_timer);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
