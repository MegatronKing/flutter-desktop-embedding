// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "include/window_size/window_size_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include <cstring>

// See window_size_channel.dart for documentation.
const char kChannelName[] = "flutter/windowsize";
const char kBadArgumentsError[] = "Bad Arguments";
const char kNoScreenError[] = "No Screen";
const char kGetScreenListMethod[] = "getScreenList";
const char kGetWindowInfoMethod[] = "getWindowInfo";
const char kSetWindowFrameMethod[] = "setWindowFrame";
const char kSetWindowMinimumSizeMethod[] = "setWindowMinimumSize";
const char kSetWindowMaximumSizeMethod[] = "setWindowMaximumSize";
const char kSetWindowTitleMethod[] = "setWindowTitle";
const char ksetWindowVisibilityMethod[] = "setWindowVisibility";
const char kGetWindowMinimumSizeMethod[] = "getWindowMinimumSize";
const char kGetWindowMaximumSizeMethod[] = "getWindowMaximumSize";
const char kCloseWindowMethod[] = "closeWindow";
const char kMinimumWindowMethod[] = "minimumWindow";
const char kDragWindowMethod[] = "dragWindow";
const char kDragTopMethod[] = "dragTop";
const char kDragLeftMethod[] = "dragLeft";
const char kDragRightMethod[] = "dragRight";
const char kDragBottomMethod[] = "dragBottom";
const char kDragTopLeftMethod[] = "dragTopLeft";
const char kDragTopRightMethod[] = "dragTopRight";
const char kDragBottomLeftMethod[] = "dragBottomLeft";
const char kDragBottomRightMethod[] = "dragBottomRight";
const char kToggleFullscreenMethod[] = "toggleFullscreen";
const char kIsFullscreenMethod[] = "isFullscreen";
const char kFrameKey[] = "frame";
const char kVisibleFrameKey[] = "visibleFrame";
const char kScaleFactorKey[] = "scaleFactor";
const char kScreenKey[] = "screen";

struct _FlWindowSizePlugin {
  GObject parent_instance;

  FlPluginRegistrar* registrar;

  // Connection to Flutter engine.
  FlMethodChannel* channel;

  // Requested window geometry.
  GdkGeometry window_geometry;

  GtkWidget* event_box;
  bool is_dragging;
  bool is_drag_pending;
  gdouble drag_x_ratio;
  gint drag_y_offset;
  guint32 drag_timestamp;
};

G_DEFINE_TYPE(FlWindowSizePlugin, fl_window_size_plugin, g_object_get_type())

// Gets the window being controlled.
GtkWindow* get_window(FlWindowSizePlugin* self) {
  FlView* view = fl_plugin_registrar_get_view(self->registrar);
  if (view == nullptr) return nullptr;

  return GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view)));
}

// Gets the display connection.
GdkDisplay* get_display(FlWindowSizePlugin* self) {
  FlView* view = fl_plugin_registrar_get_view(self->registrar);
  if (view == nullptr) return nullptr;

  return gtk_widget_get_display(GTK_WIDGET(view));
}

// Converts frame dimensions into the Flutter representation.
FlValue* make_frame_value(gint x, gint y, gint width, gint height) {
  g_autoptr(FlValue) value = fl_value_new_list();

  fl_value_append_take(value, fl_value_new_float(x));
  fl_value_append_take(value, fl_value_new_float(y));
  fl_value_append_take(value, fl_value_new_float(width));
  fl_value_append_take(value, fl_value_new_float(height));

  return fl_value_ref(value);
}

// Converts monitor information into the Flutter representation.
FlValue* make_monitor_value(GdkMonitor* monitor) {
  g_autoptr(FlValue) value = fl_value_new_map();

  GdkRectangle frame;
  gdk_monitor_get_geometry(monitor, &frame);
  fl_value_set_string_take(
      value, kFrameKey,
      make_frame_value(frame.x, frame.y, frame.width, frame.height));

  gdk_monitor_get_workarea(monitor, &frame);
  fl_value_set_string_take(
      value, kVisibleFrameKey,
      make_frame_value(frame.x, frame.y, frame.width, frame.height));

  gint scale_factor = gdk_monitor_get_scale_factor(monitor);
  fl_value_set_string_take(value, kScaleFactorKey,
                           fl_value_new_float(scale_factor));

  return fl_value_ref(value);
}

// Gets the list of current screens.
static FlMethodResponse* get_screen_list(FlWindowSizePlugin* self) {
  g_autoptr(FlValue) screens = fl_value_new_list();

  GdkDisplay* display = get_display(self);
  if (display == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }

  gint n_monitors = gdk_display_get_n_monitors(display);
  for (gint i = 0; i < n_monitors; i++) {
    GdkMonitor* monitor = gdk_display_get_monitor(display, i);
    fl_value_append_take(screens, make_monitor_value(monitor));
  }

  return FL_METHOD_RESPONSE(fl_method_success_response_new(screens));
}

// Gets information about the Flutter window.
static FlMethodResponse* get_window_info(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }

  g_autoptr(FlValue) window_info = fl_value_new_map();

  gint x, y, width, height;
  gtk_window_get_position(window, &x, &y);
  gtk_window_get_size(window, &width, &height);
  fl_value_set_string_take(window_info, kFrameKey,
                           make_frame_value(x, y, width, height));

  // Get the monitor this window is inside, or the primary monitor if doesn't
  // appear to be in any.
  GdkDisplay* display = get_display(self);
  GdkMonitor* monitor_with_window = gdk_display_get_primary_monitor(display);
  int n_monitors = gdk_display_get_n_monitors(display);
  for (int i = 0; i < n_monitors; i++) {
    GdkMonitor* monitor = gdk_display_get_monitor(display, i);

    GdkRectangle frame;
    gdk_monitor_get_geometry(monitor, &frame);
    if ((x >= frame.x && x <= frame.x + frame.width) &&
        (y >= frame.y && y <= frame.y + frame.width)) {
      monitor_with_window = monitor;
      break;
    }
  }
  fl_value_set_string_take(window_info, kScreenKey,
                           make_monitor_value(monitor_with_window));

  gint scale_factor = gtk_widget_get_scale_factor(GTK_WIDGET(window));
  fl_value_set_string_take(window_info, kScaleFactorKey,
                           fl_value_new_float(scale_factor));

  return FL_METHOD_RESPONSE(fl_method_success_response_new(window_info));
}

// Sets the window position and dimensions.
static FlMethodResponse* set_window_frame(FlWindowSizePlugin* self,
                                          FlValue* args) {
  if (fl_value_get_type(args) != FL_VALUE_TYPE_LIST ||
      fl_value_get_length(args) != 4) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kBadArgumentsError, "Expected 4-element list", nullptr));
  }
  double x = fl_value_get_float(fl_value_get_list_value(args, 0));
  double y = fl_value_get_float(fl_value_get_list_value(args, 1));
  double width = fl_value_get_float(fl_value_get_list_value(args, 2));
  double height = fl_value_get_float(fl_value_get_list_value(args, 3));

  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }

  gtk_window_move(window, static_cast<gint>(x), static_cast<gint>(y));
  gtk_window_resize(window, static_cast<gint>(width),
                    static_cast<gint>(height));

  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Send updated window geometry to GTK.
static void update_window_geometry(FlWindowSizePlugin* self) {
  gtk_window_set_geometry_hints(
      get_window(self), nullptr, &self->window_geometry,
      static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE));
}

// Sets the window minimum size.
static FlMethodResponse* set_window_minimum_size(FlWindowSizePlugin* self,
                                                 FlValue* args) {
  if (fl_value_get_type(args) != FL_VALUE_TYPE_LIST ||
      fl_value_get_length(args) != 2) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kBadArgumentsError, "Expected 2-element list", nullptr));
  }
  double width = fl_value_get_float(fl_value_get_list_value(args, 0));
  double height = fl_value_get_float(fl_value_get_list_value(args, 1));

  if (get_window(self) == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }

  if (width >= 0 && height >= 0) {
    self->window_geometry.min_width = static_cast<gint>(width);
    self->window_geometry.min_height = static_cast<gint>(height);
  }

  update_window_geometry(self);

  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Sets the window maximum size.
static FlMethodResponse* set_window_maximum_size(FlWindowSizePlugin* self,
                                                 FlValue* args) {
  if (fl_value_get_type(args) != FL_VALUE_TYPE_LIST ||
      fl_value_get_length(args) != 2) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kBadArgumentsError, "Expected 2-element list", nullptr));
  }
  double width = fl_value_get_float(fl_value_get_list_value(args, 0));
  double height = fl_value_get_float(fl_value_get_list_value(args, 1));

  if (get_window(self) == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }

  self->window_geometry.max_width = static_cast<gint>(width);
  self->window_geometry.max_height = static_cast<gint>(height);

  // Flutter uses -1 as unconstrained, GTK doesn't have an unconstrained value.
  if (self->window_geometry.max_width < 0) {
    self->window_geometry.max_width = G_MAXINT;
  }
  if (self->window_geometry.max_height < 0) {
    self->window_geometry.max_height = G_MAXINT;
  }

  update_window_geometry(self);

  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Sets the window title.
static FlMethodResponse* set_window_title(FlWindowSizePlugin* self,
                                          FlValue* args) {
  if (fl_value_get_type(args) != FL_VALUE_TYPE_STRING) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kBadArgumentsError, "Expected string", nullptr));
  }

  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  gtk_window_set_title(window, fl_value_get_string(args));

  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Sets the window visibility.
static FlMethodResponse* set_window_visible(FlWindowSizePlugin* self,
                                          FlValue* args) {
  if (fl_value_get_type(args) != FL_VALUE_TYPE_BOOL) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        kBadArgumentsError, "Expected bool", nullptr));
  }

  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  if (fl_value_get_bool(args)) {
    gtk_widget_show(GTK_WIDGET(window));
  } else {
    gtk_widget_hide(GTK_WIDGET(window));
  }

  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Gets the window minimum size.
static FlMethodResponse* get_window_minimum_size(FlWindowSizePlugin* self) {
  g_autoptr(FlValue) size = fl_value_new_list();

  gint min_width = self->window_geometry.min_width;
  gint min_height = self->window_geometry.min_height;

  // GTK uses -1 for the requisition size (the size GTK has calculated).
  // Report this as zero (smallest possible) so this doesn't look like Size(-1, -1).
  if (min_width < 0) {
    min_width = 0;
  }
  if (min_height < 0) {
    min_height = 0;
  }

  fl_value_append_take(size, fl_value_new_float(min_width));
  fl_value_append_take(size, fl_value_new_float(min_height));

  return FL_METHOD_RESPONSE(fl_method_success_response_new(size));
}

// Gets the window maximum size.
static FlMethodResponse* get_window_maximum_size(FlWindowSizePlugin* self) {
  g_autoptr(FlValue) size = fl_value_new_list();

  gint max_width = self->window_geometry.max_width;
  gint max_height = self->window_geometry.max_height;

  // Flutter uses -1 as unconstrained, GTK doesn't have an unconstrained value.
  if (max_width == G_MAXINT) {
    max_width = -1;
  }
  if (max_height == G_MAXINT) {
    max_height = -1;
  }

  fl_value_append_take(size, fl_value_new_float(max_width));
  fl_value_append_take(size, fl_value_new_float(max_height));

  return FL_METHOD_RESPONSE(fl_method_success_response_new(size));
}

// Closes the window.
static FlMethodResponse* close_window(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  gtk_window_close(window);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Minimizes (iconifies) the window.
static FlMethodResponse* minimum_window(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  gtk_window_iconify(window);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

static GtkWidget* find_event_box(GtkWidget* widget) {
  if (GTK_IS_EVENT_BOX(widget)) {
    return widget;
  }
  if (!GTK_IS_CONTAINER(widget)) {
    return nullptr;
  }

  GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
  GtkWidget* event_box = nullptr;
  for (GList* child = children; child != nullptr && event_box == nullptr;
       child = child->next) {
    event_box = find_event_box(GTK_WIDGET(child->data));
  }
  g_list_free(children);
  return event_box;
}

static void emit_button_release(FlWindowSizePlugin* self, guint32 timestamp,
                                GdkModifierType state) {
  if (self->event_box == nullptr) {
    return;
  }

  GdkWindow* event_window = gtk_widget_get_window(self->event_box);
  GdkDisplay* display = gtk_widget_get_display(self->event_box);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint root_x, root_y;
  gint origin_x, origin_y;
  gdk_device_get_position(device, nullptr, &root_x, &root_y);
  gdk_window_get_origin(event_window, &origin_x, &origin_y);

  GdkEvent* event = gdk_event_new(GDK_BUTTON_RELEASE);
  event->button.window = GDK_WINDOW(g_object_ref(event_window));
  event->button.send_event = TRUE;
  event->button.time = timestamp;
  event->button.x = root_x - origin_x;
  event->button.y = root_y - origin_y;
  event->button.x_root = root_x;
  event->button.y_root = root_y;
  event->button.state = state;
  event->button.button = 1;
  gdk_event_set_device(event, device);
  gboolean handled = FALSE;
  g_signal_emit_by_name(self->event_box, "button-release-event", event,
                        &handled);
  gdk_event_free(event);
}

static void on_window_event_after(GtkWidget*, GdkEvent* event,
                                  gpointer user_data) {
  FlWindowSizePlugin* self = FL_WINDOW_SIZE_PLUGIN(user_data);
  if (event->type == GDK_ENTER_NOTIFY && self->is_dragging) {
    self->is_dragging = false;
    emit_button_release(
        self, event->crossing.time,
        static_cast<GdkModifierType>(event->crossing.state));
  }
}

static bool is_window_fullscreen(GtkWindow* window);

static void begin_window_drag(FlWindowSizePlugin* self, GtkWindow* window,
                              gint root_x, gint root_y, guint32 timestamp) {
  self->is_drag_pending = false;
  self->is_dragging = true;
  gtk_window_begin_move_drag(window, 1, root_x, root_y, timestamp);
}

static gboolean resume_pending_window_drag(gpointer user_data) {
  FlWindowSizePlugin* self = FL_WINDOW_SIZE_PLUGIN(user_data);
  if (!self->is_drag_pending || self->registrar == nullptr) {
    return G_SOURCE_REMOVE;
  }

  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    self->is_drag_pending = false;
    return G_SOURCE_REMOVE;
  }

  GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(window));
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  GdkModifierType state;
  gdk_window_get_device_position(gtk_widget_get_window(GTK_WIDGET(window)),
                                 device, nullptr, nullptr, &state);
  if ((state & GDK_BUTTON1_MASK) == 0) {
    self->is_drag_pending = false;
    return G_SOURCE_REMOVE;
  }

  gint root_x, root_y;
  gint width, height;
  gdk_device_get_position(device, nullptr, &root_x, &root_y);
  gtk_window_get_size(window, &width, &height);
  gint drag_y_offset = MIN(self->drag_y_offset, MAX(height - 1, 0));
  gtk_window_move(
      window, root_x - static_cast<gint>(width * self->drag_x_ratio),
      root_y - drag_y_offset);
  begin_window_drag(self, window, root_x, root_y, self->drag_timestamp);
  return G_SOURCE_REMOVE;
}

static gboolean on_window_state_event(GtkWidget*,
                                      GdkEventWindowState* event,
                                      gpointer user_data) {
  FlWindowSizePlugin* self = FL_WINDOW_SIZE_PLUGIN(user_data);
  if (!self->is_drag_pending ||
      (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) == 0 ||
      (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0) {
    return FALSE;
  }
  if (!self->is_drag_pending ||
      (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) == 0 ||
      (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0) {
    return FALSE;
  }

  g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, resume_pending_window_drag,
                  g_object_ref(self), g_object_unref);
  return FALSE;
}

// Begins a window move drag operation.
static FlMethodResponse* drag_window(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  // Use the current mouse position and begin a window drag.
  GdkDisplay* display = get_display(self);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint x, y;
  gdk_device_get_position(device, nullptr, &x, &y);
  guint32 timestamp = gtk_get_current_event_time();
  if (is_window_fullscreen(window)) {
    gint window_x, window_y, width, height;
    gtk_window_get_position(window, &window_x, &window_y);
    gtk_window_get_size(window, &width, &height);
    self->is_drag_pending = true;
    self->drag_x_ratio =
        width > 0 ? CLAMP(static_cast<gdouble>(x - window_x) / width, 0.0, 1.0)
                  : 0.5;
    self->drag_y_offset = MAX(y - window_y, 0);
    self->drag_timestamp = timestamp;
    gtk_window_unmaximize(window);
  } else {
    begin_window_drag(self, window, x, y, timestamp);
  }
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Begins a window resize drag from the top edge.
static FlMethodResponse* drag_top(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  GdkDisplay* display = get_display(self);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint x, y;
  gdk_device_get_position(device, nullptr, &x, &y);
  self->is_dragging = true;
  gtk_window_begin_resize_drag(window, GDK_WINDOW_EDGE_NORTH, 1, x, y,
                               gtk_get_current_event_time());
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Begins a window resize drag from the left edge.
static FlMethodResponse* drag_left(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  GdkDisplay* display = get_display(self);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint x, y;
  gdk_device_get_position(device, nullptr, &x, &y);
  self->is_dragging = true;
  gtk_window_begin_resize_drag(window, GDK_WINDOW_EDGE_WEST, 1, x, y,
                               gtk_get_current_event_time());
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Begins a window resize drag from the right edge.
static FlMethodResponse* drag_right(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  GdkDisplay* display = get_display(self);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint x, y;
  gdk_device_get_position(device, nullptr, &x, &y);
  self->is_dragging = true;
  gtk_window_begin_resize_drag(window, GDK_WINDOW_EDGE_EAST, 1, x, y,
                               gtk_get_current_event_time());
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Begins a window resize drag from the bottom edge.
static FlMethodResponse* drag_bottom(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  GdkDisplay* display = get_display(self);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint x, y;
  gdk_device_get_position(device, nullptr, &x, &y);
  self->is_dragging = true;
  gtk_window_begin_resize_drag(window, GDK_WINDOW_EDGE_SOUTH, 1, x, y,
                               gtk_get_current_event_time());
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Begins a window resize drag from the top-left corner.
static FlMethodResponse* drag_top_left(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  GdkDisplay* display = get_display(self);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint x, y;
  gdk_device_get_position(device, nullptr, &x, &y);
  self->is_dragging = true;
  gtk_window_begin_resize_drag(window, GDK_WINDOW_EDGE_NORTH_WEST, 1, x, y,
                               gtk_get_current_event_time());
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Begins a window resize drag from the top-right corner.
static FlMethodResponse* drag_top_right(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  GdkDisplay* display = get_display(self);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint x, y;
  gdk_device_get_position(device, nullptr, &x, &y);
  self->is_dragging = true;
  gtk_window_begin_resize_drag(window, GDK_WINDOW_EDGE_NORTH_EAST, 1, x, y,
                               gtk_get_current_event_time());
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Begins a window resize drag from the bottom-left corner.
static FlMethodResponse* drag_bottom_left(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  GdkDisplay* display = get_display(self);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint x, y;
  gdk_device_get_position(device, nullptr, &x, &y);
  self->is_dragging = true;
  gtk_window_begin_resize_drag(window, GDK_WINDOW_EDGE_SOUTH_WEST, 1, x, y,
                               gtk_get_current_event_time());
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Begins a window resize drag from the bottom-right corner.
static FlMethodResponse* drag_bottom_right(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  GdkDisplay* display = get_display(self);
  GdkSeat* seat = gdk_display_get_default_seat(display);
  GdkDevice* device = gdk_seat_get_pointer(seat);
  gint x, y;
  gdk_device_get_position(device, nullptr, &x, &y);
  self->is_dragging = true;
  gtk_window_begin_resize_drag(window, GDK_WINDOW_EDGE_SOUTH_EAST, 1, x, y,
                               gtk_get_current_event_time());
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

static bool is_window_fullscreen(GtkWindow* window) {
  GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
  return gdk_window != nullptr &&
         (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_MAXIMIZED) != 0;
}

// Toggles the fullscreen state of the window.
static FlMethodResponse* toggle_fullscreen(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  if (is_window_fullscreen(window)) {
    gtk_window_unmaximize(window);
  } else {
    gtk_window_maximize(window);
  }
  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

// Checks whether the window is fullscreen.
static FlMethodResponse* is_fullscreen(FlWindowSizePlugin* self) {
  GtkWindow* window = get_window(self);
  if (window == nullptr) {
    return FL_METHOD_RESPONSE(
        fl_method_error_response_new(kNoScreenError, nullptr, nullptr));
  }
  bool fullscreen = is_window_fullscreen(window);
  return FL_METHOD_RESPONSE(
      fl_method_success_response_new(fl_value_new_bool(fullscreen)));
}

// Called when a method call is received from Flutter.
static void method_call_cb(FlMethodChannel* channel, FlMethodCall* method_call,
                           gpointer user_data) {
  FlWindowSizePlugin* self = FL_WINDOW_SIZE_PLUGIN(user_data);

  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* args = fl_method_call_get_args(method_call);

  g_autoptr(FlMethodResponse) response = nullptr;
  if (strcmp(method, kGetScreenListMethod) == 0) {
    response = get_screen_list(self);
  } else if (strcmp(method, kGetWindowInfoMethod) == 0) {
    response = get_window_info(self);
  } else if (strcmp(method, kSetWindowFrameMethod) == 0) {
    response = set_window_frame(self, args);
  } else if (strcmp(method, kSetWindowMinimumSizeMethod) == 0) {
    response = set_window_minimum_size(self, args);
  } else if (strcmp(method, kSetWindowMaximumSizeMethod) == 0) {
    response = set_window_maximum_size(self, args);
  } else if (strcmp(method, kSetWindowTitleMethod) == 0) {
    response = set_window_title(self, args);
  } else if (strcmp(method, ksetWindowVisibilityMethod) == 0) {
    response = set_window_visible(self, args);
  } else if (strcmp(method, kGetWindowMinimumSizeMethod) == 0) {
    response = get_window_minimum_size(self);
  } else if (strcmp(method, kGetWindowMaximumSizeMethod) == 0) {
    response = get_window_maximum_size(self);
  } else if (strcmp(method, kCloseWindowMethod) == 0) {
    response = close_window(self);
  } else if (strcmp(method, kMinimumWindowMethod) == 0) {
    response = minimum_window(self);
  } else if (strcmp(method, kDragWindowMethod) == 0) {
    response = drag_window(self);
  } else if (strcmp(method, kDragTopMethod) == 0) {
    response = drag_top(self);
  } else if (strcmp(method, kDragLeftMethod) == 0) {
    response = drag_left(self);
  } else if (strcmp(method, kDragRightMethod) == 0) {
    response = drag_right(self);
  } else if (strcmp(method, kDragBottomMethod) == 0) {
    response = drag_bottom(self);
  } else if (strcmp(method, kDragTopLeftMethod) == 0) {
    response = drag_top_left(self);
  } else if (strcmp(method, kDragTopRightMethod) == 0) {
    response = drag_top_right(self);
  } else if (strcmp(method, kDragBottomLeftMethod) == 0) {
    response = drag_bottom_left(self);
  } else if (strcmp(method, kDragBottomRightMethod) == 0) {
    response = drag_bottom_right(self);
  } else if (strcmp(method, kToggleFullscreenMethod) == 0) {
    response = toggle_fullscreen(self);
  } else if (strcmp(method, kIsFullscreenMethod) == 0) {
    response = is_fullscreen(self);
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  g_autoptr(GError) error = nullptr;
  if (!fl_method_call_respond(method_call, response, &error))
    g_warning("Failed to send method call response: %s", error->message);
}

static void fl_window_size_plugin_dispose(GObject* object) {
  FlWindowSizePlugin* self = FL_WINDOW_SIZE_PLUGIN(object);

  if (self->registrar != nullptr) {
    GtkWindow* window = get_window(self);
    if (window != nullptr) {
      g_signal_handlers_disconnect_by_data(window, self);
    }
  }
  g_clear_object(&self->registrar);
  g_clear_object(&self->channel);

  G_OBJECT_CLASS(fl_window_size_plugin_parent_class)->dispose(object);
}

static void fl_window_size_plugin_class_init(FlWindowSizePluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_window_size_plugin_dispose;
}

static void fl_window_size_plugin_init(FlWindowSizePlugin* self) {
  self->window_geometry.min_width = -1;
  self->window_geometry.min_height = -1;
  self->window_geometry.max_width = G_MAXINT;
  self->window_geometry.max_height = G_MAXINT;
  self->event_box = nullptr;
  self->is_dragging = false;
  self->is_drag_pending = false;
  self->drag_x_ratio = 0.5;
  self->drag_y_offset = 0;
  self->drag_timestamp = GDK_CURRENT_TIME;
}

FlWindowSizePlugin* fl_window_size_plugin_new(FlPluginRegistrar* registrar) {
  FlWindowSizePlugin* self = FL_WINDOW_SIZE_PLUGIN(
      g_object_new(fl_window_size_plugin_get_type(), nullptr));

  self->registrar = FL_PLUGIN_REGISTRAR(g_object_ref(registrar));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  self->channel =
      fl_method_channel_new(fl_plugin_registrar_get_messenger(registrar),
                            kChannelName, FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(self->channel, method_call_cb,
                                            g_object_ref(self), g_object_unref);

  GtkWindow* window = get_window(self);
  self->event_box =
      find_event_box(GTK_WIDGET(fl_plugin_registrar_get_view(registrar)));
  g_signal_connect(window, "event-after", G_CALLBACK(on_window_event_after),
                   self);
  g_signal_connect(window, "window-state-event",
                   G_CALLBACK(on_window_state_event), self);

  return self;
}

void window_size_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  FlWindowSizePlugin* plugin = fl_window_size_plugin_new(registrar);
  g_object_unref(plugin);
}
