/*
 *  Copyright © 2017-2025 Wellington Wallace
 *
 *  This file is part of Easy Effects.
 *
 *  Easy Effects is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Easy Effects is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Easy Effects. If not, see <https://www.gnu.org/licenses/>.
 */

#include "chart.hpp"
#include <cairo.h>
#include <fmt/format.h>
#include <fmt/compile.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <gobject/gobject.h>
#include <graphene.h>
#include <gsk/gsk.h>
#include <gtk/gtk.h>
#include <gtk/gtkshortcut.h>
#include <pango/pango-font.h>
#include <pango/pango-layout.h>
#include <sys/types.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>
#include "tags_resources.hpp"
#include "ui_helpers.hpp"
#include "util.hpp"

namespace ui::chart {

struct Data {
 public:
  ~Data() { util::debug("data struct destroyed"); }

  bool draw_bar_border, fill_bars, is_visible, rounded_corners, dynamic_y_scale = false;

  int x_axis_height, n_x_decimals, n_y_decimals;

  double mouse_y, mouse_x, margin, line_width;

  double x_min, x_max, y_min, y_max;

  double x_min_log, x_max_log;

  double global_min_y = 0.0, global_max_y = 0.0;

  ChartType chart_type;

  ChartScale chart_scale;

  GdkRGBA background_color, color, color_axis_labels, gradient_color;

  std::string x_unit, y_unit;

  std::vector<double> y_axis, raw_y_axis, x_axis, x_axis_log, objects_x;
};

struct _Chart {
  GtkBox parent_instance;

  GtkEventController* controller_motion;

  Data* data;
};

// NOLINTNEXTLINE
G_DEFINE_TYPE(Chart, chart, GTK_TYPE_WIDGET)

const char* const HISTOGRAM_DATA_KEY = "histogram-data";
const char* const HISTOGRAM_RANGES_KEY = "histogram-ranges";

void set_chart_type(Chart* self, const ChartType& value) {
  if (self->data == nullptr) {
    return;
  }

  self->data->chart_type = value;
}

void set_chart_scale(Chart* self, const ChartScale& value) {
  if (self->data == nullptr) {
    return;
  }

  self->data->chart_scale = value;
}

void set_background_color(Chart* self, GdkRGBA color) {
  if (self->data == nullptr) {
    return;
  }

  self->data->background_color = color;
}

void set_color(Chart* self, GdkRGBA color) {
  if (self->data == nullptr) {
    return;
  }

  self->data->color = color;
}

void set_axis_labels_color(Chart* self, GdkRGBA color) {
  if (self->data == nullptr) {
    return;
  }

  self->data->color_axis_labels = color;
}

void set_line_width(Chart* self, const float& value) {
  if (self->data == nullptr) {
    return;
  }

  self->data->line_width = value;
}

void set_draw_bar_border(Chart* self, const bool& v) {
  if (self->data == nullptr) {
    return;
  }

  self->data->draw_bar_border = v;
}

void set_rounded_corners(Chart* self, const bool& v) {
  if (self->data == nullptr) {
    return;
  }

  self->data->rounded_corners = v;
}

void set_fill_bars(Chart* self, const bool& v) {
  if (self->data == nullptr) {
    return;
  }

  self->data->fill_bars = v;
}

void set_n_x_decimals(Chart* self, const int& v) {
  if (self->data == nullptr) {
    return;
  }

  self->data->n_x_decimals = v;
}

void set_n_y_decimals(Chart* self, const int& v) {
  if (self->data == nullptr) {
    return;
  }

  self->data->n_y_decimals = v;
}

void set_x_unit(Chart* self, const std::string& value) {
  if (self->data == nullptr) {
    return;
  }

  self->data->x_unit = value;
}

void set_y_unit(Chart* self, const std::string& value) {
  if (self->data == nullptr) {
    return;
  }

  self->data->y_unit = value;
}

void set_margin(Chart* self, const float& v) {
  if (self->data == nullptr) {
    return;
  }

  self->data->margin = v;
}

auto get_is_visible(Chart* self) -> bool {
  if (!GTK_IS_WIDGET(self)) {
    return false;
  }

  if (self->data == nullptr) {
    return false;
  }

  return self->data->is_visible;
}

void set_dynamic_y_scale(Chart* self, const bool& v) {
  if (self->data == nullptr) {
    return;
  }

  self->data->dynamic_y_scale = v;
  self->data->global_min_y = 0.0;
  self->data->global_max_y = 0.0;
}

void set_histogram_data(Chart* self, const std::vector<double>& data) {
    // Get or create HistogramData
    auto* hist_data = static_cast<HistogramData*>(
        g_object_get_data(G_OBJECT(self), HISTOGRAM_DATA_KEY));
    if (hist_data == nullptr) {
        hist_data = new HistogramData();
        g_object_set_data_full(G_OBJECT(self), HISTOGRAM_DATA_KEY, hist_data,
            [](void* data) { delete static_cast<HistogramData*>(data); });
    }
    hist_data->data = data;
    // Also update the chart's y_axis so that snapshot() uses the histogram values
    if (self->data != nullptr) {
        self->data->y_axis = data;
    }
}

void set_histogram_ranges(Chart* self, double min, double max, size_t n_bins) {
    auto* hist_data = static_cast<HistogramData*>(
        g_object_get_data(G_OBJECT(self), HISTOGRAM_DATA_KEY));
    if (hist_data == nullptr) {
        hist_data = new HistogramData();
        g_object_set_data_full(G_OBJECT(self), HISTOGRAM_DATA_KEY, hist_data,
            [](void* data) { delete static_cast<HistogramData*>(data); });
    }

    g_message("set_histogram_ranges min=%f max=%f n_bins=%zu", min, max, n_bins);
    hist_data->min_value = min;
    hist_data->max_value = max;
    hist_data->n_bins = n_bins;
    hist_data->prev_n_bins = 0;
}

void set_x_data(Chart* self, const std::vector<double>& x) {
  if (!GTK_IS_WIDGET(self) || x.empty()) {
    return;
  }

  if (self->data == nullptr) {
    return;
  }

  self->data->x_axis = x;

  self->data->x_min = std::ranges::min(x);
  self->data->x_max = std::ranges::max(x);

  self->data->objects_x.resize(x.size());

  self->data->x_min_log = std::log10(self->data->x_min);
  self->data->x_max_log = std::log10(self->data->x_max);

  self->data->x_axis_log.resize(x.size());

  for (size_t n = 0U; n < self->data->x_axis_log.size(); n++) {
    self->data->x_axis_log[n] = std::log10(self->data->x_axis[n]);
  }

  // making each x value a number between 0 and 1

  std::ranges::for_each(self->data->x_axis,
                        [&](auto& v) { v = (v - self->data->x_min) / (self->data->x_max - self->data->x_min); });

  std::ranges::for_each(self->data->x_axis_log, [&](auto& v) {
    v = (v - self->data->x_min_log) / (self->data->x_max_log - self->data->x_min_log);
  });
}

void set_y_data(Chart* self, const std::vector<double>& y) {
  if (!GTK_IS_WIDGET(self) || y.empty()) {
    return;
  }

  // Save raw values to be used later in the snapshot.
  self->data->raw_y_axis = y;
  self->data->y_axis = y;

  auto min_y = std::ranges::min(y);
  auto max_y = std::ranges::max(y);

  if (self->data->dynamic_y_scale) {
    self->data->y_min = min_y;
    self->data->y_max = max_y;
  } else {
    self->data->global_min_y = (min_y < self->data->global_min_y) ? min_y : self->data->global_min_y;
    self->data->global_max_y = (max_y > self->data->global_max_y) ? max_y : self->data->global_max_y;

    self->data->y_min = self->data->global_min_y;
    self->data->y_max = self->data->global_max_y;
  }

  if (std::fabs(self->data->y_max - self->data->y_min) < 0.00001) {
    std::ranges::fill(self->data->y_axis, 0.0);
  } else {
    // making each y value a number between 0 and 1

    std::ranges::for_each(self->data->y_axis,
                          [&](auto& v) { v = (v - self->data->y_min) / (self->data->y_max - self->data->y_min); });
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void set_histogram_x_data(Chart* self) {
  auto* hist_data = static_cast<HistogramData*>(g_object_get_data(G_OBJECT(self), HISTOGRAM_DATA_KEY));
  if (hist_data == nullptr || hist_data->n_bins == 0)
    return;
  // return if already up to date
  if (hist_data->n_bins == hist_data->prev_n_bins) return;

  hist_data->prev_n_bins = hist_data->n_bins;
  std::vector<double> bin_centers(hist_data->n_bins);
  std::vector<double> bin_norm(hist_data->n_bins);
  double bin_width = (hist_data->max_value - hist_data->min_value) / static_cast<double>(hist_data->n_bins);
  for (size_t i = 0; i < hist_data->n_bins; i++) {
      double center = hist_data->min_value + (i + 0.5) * bin_width;
      bin_centers[i] = center;
      bin_norm[i] = (center - hist_data->min_value) / (hist_data->max_value - hist_data->min_value);
  }
  hist_data->x_axis_raw = bin_centers;
  hist_data->x_axis_norm = bin_norm;

  // Update the chart's x_axis and related properties.
  self->data->x_axis = hist_data->x_axis_norm;
  self->data->x_min = 0.0;
  self->data->x_max = 1.0;
  self->data->objects_x.resize(self->data->x_axis.size());
}

void on_pointer_motion(GtkEventControllerMotion* controller, double xpos, double ypos, Chart* self) {
  // Static cast trying to fix codeql issue
  const auto x = xpos;
  const auto y = ypos;

  const auto width = static_cast<double>(gtk_widget_get_width(GTK_WIDGET(self)));
  const auto height = static_cast<double>(gtk_widget_get_height(GTK_WIDGET(self)));

  const auto usable_height = height - self->data->margin * height - self->data->x_axis_height;

  if (y < height - self->data->x_axis_height && y > self->data->margin * height && x > self->data->margin * width &&
      x < width - self->data->margin * width) {
    // At least for now the y axis is always linear

    self->data->mouse_y =
        (usable_height - y) / usable_height * (self->data->y_max - self->data->y_min) + self->data->y_min;

    switch (self->data->chart_scale) {
      case ChartScale::logarithmic: {
        const double mouse_x_log = (x - self->data->margin * width) / (width - 2 * self->data->margin * width) *
                                       (self->data->x_max_log - self->data->x_min_log) +
                                   self->data->x_min_log;

        self->data->mouse_x = std::pow(10.0, mouse_x_log);  // exp10 does not exist on FreeBSD

        break;
      }
      case ChartScale::linear: {
        self->data->mouse_x = (x - self->data->margin * width) / (width - 2 * self->data->margin * width) *
                                  (self->data->x_max - self->data->x_min) +
                              self->data->x_min;

        break;
      }
    }

    gtk_widget_queue_draw(GTK_WIDGET(self));
  }
}

auto draw_unit(Chart* self, GtkSnapshot* snapshot, const int& width, const int& height, const std::string& unit) {
  auto* layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), unit.c_str());

  auto* description = pango_font_description_from_string("monospace bold");

  pango_layout_set_font_description(layout, description);
  pango_font_description_free(description);

  int text_width = 0;
  int text_height = 0;

  pango_layout_get_pixel_size(layout, &text_width, &text_height);

  gtk_snapshot_save(snapshot);

  auto point = GRAPHENE_POINT_INIT(static_cast<float>(width - text_width), static_cast<float>(height - text_height));

  gtk_snapshot_translate(snapshot, &point);

  gtk_snapshot_append_layout(snapshot, layout, &self->data->color_axis_labels);

  gtk_snapshot_restore(snapshot);

  g_object_unref(layout);
}

auto draw_x_labels(Chart* self, GtkSnapshot* snapshot, const int& width, const int& height) -> int {
  double labels_offset = 0.1 * width;

  int n_x_labels = static_cast<int>(std::ceil((width - 2 * self->data->margin * width) / labels_offset)) + 1;

  if (n_x_labels < 2) {
    return 0;
  }

  /*
    Correcting the offset based on the final n_x_labels value
  */

  labels_offset = (width - 2.0 * self->data->margin * width) / static_cast<float>(n_x_labels - 1);

  std::vector<double> labels;

  // Determine the label range.
  double x_min, x_max;
  auto* hist_data = static_cast<HistogramData*>(g_object_get_data(G_OBJECT(self), HISTOGRAM_DATA_KEY));
  if (hist_data != nullptr) {
    // For histogram, use the raw dB range.
    x_min = hist_data->min_value;
    x_max = hist_data->max_value;
  } else {
    // Otherwise, use the chart's current range.
    x_min = self->data->x_min;
    x_max = self->data->x_max;
  }

  switch (self->data->chart_scale) {
    case ChartScale::logarithmic: {
      labels = util::logspace(x_min, x_max, n_x_labels);
      break;
    }
    case ChartScale::linear: {
      labels = util::linspace(x_min, x_max, n_x_labels);
      break;
    }
  }

  draw_unit(self, snapshot, width, height, " " + self->data->x_unit + " ");

  /*
    There is no space left in the window to show the last label. So we skip it.
    All labels are enclosed by whitespaces to not stick the first and the final
    at window borders.
  */

  for (size_t n = 0U; n < labels.size() - 1U; n++) {
    const auto msg = fmt::format(ui::get_user_locale(), " {0:.{1}Lf} ", labels[n], self->data->n_x_decimals);

    auto* layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), msg.c_str());

    auto* description = pango_font_description_from_string("monospace bold");

    pango_layout_set_font_description(layout, description);
    pango_font_description_free(description);

    int text_width = 0;
    int text_height = 0;

    pango_layout_get_pixel_size(layout, &text_width, &text_height);

    gtk_snapshot_save(snapshot);

    auto point = GRAPHENE_POINT_INIT(static_cast<float>(self->data->margin * width + n * labels_offset),
                                     static_cast<float>(height - text_height));

    gtk_snapshot_translate(snapshot, &point);

    gtk_snapshot_append_layout(snapshot, layout, &self->data->color_axis_labels);

    gtk_snapshot_restore(snapshot);

    g_object_unref(layout);

    if (n == labels.size() - 2U) {
      return text_height;
    }
  }

  return 0;
}

void snapshot(GtkWidget* widget, GtkSnapshot* snapshot) {
  auto* self = EE_CHART(widget);

  switch (self->data->chart_scale) {
    case ChartScale::logarithmic: {
      if (self->data->y_axis.size() != self->data->x_axis_log.size()) {
        return;
      }

      break;
    }
    case ChartScale::linear: {
      if (self->data->y_axis.size() != self->data->x_axis.size()) {
        return;
      }

      break;
    }
  }

  auto width = gtk_widget_get_width(widget);
  auto height = gtk_widget_get_height(widget);

  auto widget_rectangle = GRAPHENE_RECT_INIT(0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height));

  gtk_snapshot_append_color(snapshot, &self->data->background_color, &widget_rectangle);

  if (const auto n_points = self->data->y_axis.size(); n_points > 0) {
    double usable_width = width - 2.0 * (self->data->line_width + self->data->margin * width);

    auto usable_height = (height - self->data->margin * height) - self->data->x_axis_height;

    switch (self->data->chart_scale) {
      case ChartScale::logarithmic: {
        for (size_t n = 0U; n < n_points; n++) {
          self->data->objects_x[n] =
              usable_width * self->data->x_axis_log[n] + self->data->line_width + self->data->margin * width;
        }

        break;
      }
      case ChartScale::linear: {
        for (size_t n = 0U; n < n_points; n++) {
          self->data->objects_x[n] =
              usable_width * self->data->x_axis[n] + self->data->line_width + self->data->margin * width;
        }

        break;
      }
    }

    self->data->x_axis_height = draw_x_labels(self, snapshot, width, height);

    auto border_color = std::to_array({self->data->color, self->data->color, self->data->color, self->data->color});

    std::array<float, 4> border_width;
    border_width.fill(static_cast<float>(self->data->line_width));

    float radius = (self->data->rounded_corners) ? 5.0F : 0.0F;

    switch (self->data->chart_type) {
      case ChartType::bar: {
        double dw = width / static_cast<double>(n_points);

        for (uint n = 0U; n < n_points; n++) {
          double bar_height = usable_height * self->data->y_axis[n];

          double rect_x = self->data->objects_x[n];
          double rect_y = self->data->margin * height + usable_height - bar_height;
          double rect_height = bar_height;
          double rect_width = dw;

          if (self->data->draw_bar_border) {
            rect_width -= self->data->line_width;
          }

          auto bar_rectangle = GRAPHENE_RECT_INIT(static_cast<float>(rect_x), static_cast<float>(rect_y),
                                                  static_cast<float>(rect_width), static_cast<float>(rect_height));

          GskRoundedRect outline;

          gsk_rounded_rect_init_from_rect(&outline, &bar_rectangle, radius);

          if (self->data->fill_bars) {
            gtk_snapshot_push_rounded_clip(snapshot, &outline);

            gtk_snapshot_append_color(snapshot, &self->data->color, &outline.bounds);

            gtk_snapshot_pop(snapshot);
          } else {
            gtk_snapshot_append_border(snapshot, &outline, border_width.data(), border_color.data());
          }
        }

        break;
      }
      case ChartType::dots: {
        double dw = width / static_cast<double>(n_points);

        usable_height -= radius;  // this avoids the dots being drawn over the axis label

        for (uint n = 0U; n < n_points; n++) {
          double dot_y = usable_height * self->data->y_axis[n];

          double rect_x = self->data->objects_x[n];
          double rect_y = self->data->margin * height + radius + usable_height - dot_y;
          double rect_width = dw;

          if (self->data->draw_bar_border) {
            rect_width -= self->data->line_width;
          }

          auto rectangle = GRAPHENE_RECT_INIT(static_cast<float>(rect_x - radius), static_cast<float>(rect_y - radius),
                                              static_cast<float>(rect_width), static_cast<float>(rect_width));

          GskRoundedRect outline;

          gsk_rounded_rect_init_from_rect(&outline, &rectangle, radius);

          if (self->data->fill_bars) {
            gtk_snapshot_push_rounded_clip(snapshot, &outline);

            gtk_snapshot_append_color(snapshot, &self->data->color, &outline.bounds);

            gtk_snapshot_pop(snapshot);
          } else {
            gtk_snapshot_append_border(snapshot, &outline, border_width.data(), border_color.data());
          }
        }

        break;
      }
      case ChartType::line: {
        auto* ctx = gtk_snapshot_append_cairo(snapshot, &widget_rectangle);

        cairo_set_source_rgba(ctx, static_cast<double>(self->data->color.red),
                              static_cast<double>(self->data->color.green), static_cast<double>(self->data->color.blue),
                              static_cast<double>(self->data->color.alpha));

        if (self->data->fill_bars) {
          cairo_move_to(ctx, self->data->margin * width, self->data->margin * height + usable_height);
        } else {
          const auto point_height = self->data->y_axis.front() * usable_height;

          cairo_move_to(ctx, self->data->objects_x.front(), self->data->margin * height + usable_height - point_height);
        }

        for (uint n = 0U; n < n_points - 1U; n++) {
          const auto next_point_height = self->data->y_axis[n + 1U] * usable_height;

          cairo_line_to(ctx, self->data->objects_x[n + 1U],
                        self->data->margin * height + usable_height - next_point_height);
        }

        if (self->data->fill_bars) {
          cairo_line_to(ctx, self->data->objects_x.back(), self->data->margin * height + usable_height);

          cairo_move_to(ctx, self->data->objects_x.back(), self->data->margin * height + usable_height);

          cairo_close_path(ctx);
        }

        cairo_set_line_width(ctx, self->data->line_width);

        if (self->data->fill_bars) {
          cairo_fill(ctx);
        } else {
          cairo_stroke(ctx);
        }

        cairo_destroy(ctx);

        break;
      }
    }

    if (gtk_event_controller_motion_contains_pointer(GTK_EVENT_CONTROLLER_MOTION(self->controller_motion)) != 0) {
      // If histogram data is available, show bin info on mouse over.
      auto* hist_data = static_cast<HistogramData*>(g_object_get_data(G_OBJECT(self), HISTOGRAM_DATA_KEY));
      if (hist_data && !hist_data->data.empty()) {
        // Assume self->data->mouse_x is already normalized [0,1] (set in on_pointer_motion).
        size_t bin_index = static_cast<size_t>(self->data->mouse_x * hist_data->data.size());
        if (bin_index >= hist_data->data.size())
          bin_index = hist_data->data.size() - 1;
        double bin_center_db = hist_data->x_axis_raw[bin_index]; // use raw dB value
        double bin_value = hist_data->data[bin_index] * 100.0; // percentage
        auto msg = fmt::format(FMT_COMPILE("   Bin Center: {:8.2f} dB   Value: {:6.2f}%   "),
                               bin_center_db, bin_value);
        // Create layout and draw at a corner (for example, top-right)
        auto* layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), msg.c_str());
        auto* desc = pango_font_description_from_string("monospace bold");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);

        int text_width = 0, text_height = 0;
        pango_layout_get_pixel_size(layout, &text_width, &text_height);

        double widget_width = gtk_widget_get_width(GTK_WIDGET(self));
        gtk_snapshot_save(snapshot);
        auto point = GRAPHENE_POINT_INIT(static_cast<float>(widget_width - text_width), 0.0F);
        gtk_snapshot_translate(snapshot, &point);
        gtk_snapshot_append_layout(snapshot, layout, &self->data->color_axis_labels);
        gtk_snapshot_restore(snapshot);
        g_object_unref(layout);
      } else {
        double peak_dB = -100.0;
        // If FFT data is available, compute the normalized frequency for mouse_x.
        if (!self->data->x_axis.empty() && !self->data->raw_y_axis.empty()) {
            // Convert mouse_x (in Hz) to its normalized value based on actual frequency bounds.
            double normalized_mouse = (self->data->mouse_x - self->data->x_min) / (self->data->x_max - self->data->x_min);

            // Use binary search (std::lower_bound) to find the closest position.
            auto it = std::lower_bound(self->data->x_axis.begin(), self->data->x_axis.end(), normalized_mouse);
            size_t closest_index = 0;
            if (it == self->data->x_axis.end()) {
                closest_index = self->data->x_axis.size() - 1;
            } else if (it == self->data->x_axis.begin()) {
                closest_index = 0;
            } else {
                size_t index = it - self->data->x_axis.begin();
                double lower_val = *(it - 1);
                double upper_val = *it;
                closest_index = (normalized_mouse - lower_val < upper_val - normalized_mouse) ? index - 1 : index;
            }

            // Retrieve the actual dB value (from raw data) at the FFT bin closest to the mouse frequency.
            peak_dB = self->data->raw_y_axis[closest_index];
            g_message("peak_dB=%f normalized_mouse=%f mouse_x=%f closest_index=%zu",
                      peak_dB, normalized_mouse, self->data->mouse_x, closest_index);
        }

        // We leave a whitespace at the end to not stick the string at the window border.
        const auto msg = fmt::format(FMT_COMPILE("   x = {:7.{}Lf} {} y = {:6.{}Lf} {} peak = {:6.{}Lf} dB   "),
                                     self->data->mouse_x, self->data->n_x_decimals,
                                     self->data->x_unit, self->data->mouse_y,
                                     self->data->n_y_decimals, self->data->y_unit,
                                     peak_dB, self->data->n_y_decimals);
        auto* layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), msg.c_str());
        auto* description = pango_font_description_from_string("monospace bold");
        pango_layout_set_font_description(layout, description);
        pango_font_description_free(description);
        int text_width = 0;
        int text_height = 0;
        pango_layout_get_pixel_size(layout, &text_width, &text_height);
        gtk_snapshot_save(snapshot);
        auto point = GRAPHENE_POINT_INIT(width - static_cast<float>(text_width), 0.0F);
        gtk_snapshot_translate(snapshot, &point);
        gtk_snapshot_append_layout(snapshot, layout, &self->data->color_axis_labels);
        gtk_snapshot_restore(snapshot);
        g_object_unref(layout);
      }
    }
  }
}

void unroot(GtkWidget* widget) {
  auto* self = EE_CHART(widget);

  self->data->is_visible = false;

  GTK_WIDGET_CLASS(chart_parent_class)->unmap(widget);
}

void finalize(GObject* object) {
  auto* self = EE_CHART(object);

  delete self->data;

  self->data = nullptr;

  util::debug("finalized");

  G_OBJECT_CLASS(chart_parent_class)->finalize(object);
}

void chart_class_init(ChartClass* klass) {
  auto* object_class = G_OBJECT_CLASS(klass);
  auto* widget_class = GTK_WIDGET_CLASS(klass);

  object_class->finalize = finalize;

  widget_class->snapshot = snapshot;
  widget_class->unroot = unroot;

  gtk_widget_class_set_template_from_resource(widget_class, tags::resources::chart_ui);
}

void chart_init(Chart* self) {
  gtk_widget_init_template(GTK_WIDGET(self));

  self->data = new Data();

  self->data->draw_bar_border = true;
  self->data->fill_bars = true;
  self->data->is_visible = true;
  self->data->x_axis_height = 0;
  self->data->n_x_decimals = 1;
  self->data->n_y_decimals = 1;
  self->data->line_width = 2.0;
  self->data->margin = 0.02;

  self->data->x_min = 0.0;
  self->data->y_min = 0.0;
  self->data->x_max = 1.0;
  self->data->y_max = 1.0;

  self->data->background_color = GdkRGBA{0.0F, 0.0F, 0.0F, 1.0F};
  self->data->color = GdkRGBA{1.0F, 1.0F, 1.0F, 1.0F};
  self->data->color_axis_labels = GdkRGBA{1.0F, 1.0F, 1.0F, 1.0F};
  self->data->gradient_color = GdkRGBA{1.0F, 1.0F, 1.0F, 1.0F};

  self->data->chart_type = ChartType::bar;
  self->data->chart_scale = ChartScale::logarithmic;

  self->controller_motion = gtk_event_controller_motion_new();

  g_signal_connect(self->controller_motion, "motion", G_CALLBACK(on_pointer_motion), self);

  g_signal_connect(GTK_WIDGET(self), "hide",
                   G_CALLBACK(+[](GtkWidget* widget, Chart* self) { self->data->is_visible = false; }), self);

  g_signal_connect(GTK_WIDGET(self), "show",
                   G_CALLBACK(+[](GtkWidget* widget, Chart* self) { self->data->is_visible = true; }), self);

  gtk_widget_add_controller(GTK_WIDGET(self), self->controller_motion);
}

auto create() -> Chart* {
  return static_cast<Chart*>(g_object_new(EE_TYPE_CHART, nullptr));
}

}  // namespace ui::chart
