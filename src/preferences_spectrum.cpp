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

#include "preferences_spectrum.hpp"
#include <adwaita.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <gobject/gobject.h>
#include <gtk/gtk.h>
#include <gtk/gtkdropdown.h>
#include <vector>
#include "tags_resources.hpp"
#include "tags_schema.hpp"
#include "ui_helpers.hpp"
#include "util.hpp"

namespace ui::preferences::spectrum {

struct Data {
 public:
  ~Data() { util::debug("data struct destroyed"); }

  std::vector<gulong> gconnections;
};

struct _PreferencesSpectrum {
  AdwPreferencesPage parent_instance;

  GtkSwitch *show, *fill, *show_bar_border, *rounded_corners, *dynamic_y_scale,
            *show_histogram, *show_statistics;

  GtkColorDialogButton *color_button, *axis_color_button;

  GtkDropDown* type;

  GtkSpinButton *n_points, *height, *line_width, *minimum_frequency, *maximum_frequency, *avsync_delay,
                *window_size, *histogram_bins;

  GSettings* settings;

  Data* data;
};

// NOLINTNEXTLINE
G_DEFINE_TYPE(PreferencesSpectrum, preferences_spectrum, ADW_TYPE_PREFERENCES_PAGE)

void on_spectrum_color_set(GtkColorDialogButton* button, GParamSpec* pspec, PreferencesSpectrum* self) {
  auto* rgba = gtk_color_dialog_button_get_rgba(button);

  g_settings_set(self->settings, "color", "(dddd)", rgba->red, rgba->green, rgba->blue, rgba->alpha);
}

void on_spectrum_axis_color_set(GtkColorDialogButton* button, GParamSpec* pspec, PreferencesSpectrum* self) {
  auto* rgba = gtk_color_dialog_button_get_rgba(button);

  g_settings_set(self->settings, "color-axis-labels", "(dddd)", rgba->red, rgba->green, rgba->blue, rgba->alpha);
}

void dispose(GObject* object) {
  auto* self = EE_PREFERENCES_SPECTRUM(object);

  for (auto& handler_id : self->data->gconnections) {
    g_signal_handler_disconnect(self->settings, handler_id);
  }

  self->data->gconnections.clear();

  g_object_unref(self->settings);

  util::debug("disposed");

  G_OBJECT_CLASS(preferences_spectrum_parent_class)->dispose(object);
}

void finalize(GObject* object) {
  auto* self = EE_PREFERENCES_SPECTRUM(object);

  delete self->data;

  util::debug("finalized");

  G_OBJECT_CLASS(preferences_spectrum_parent_class)->finalize(object);
}

void preferences_spectrum_class_init(PreferencesSpectrumClass* klass) {
  auto* object_class = G_OBJECT_CLASS(klass);
  auto* widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = dispose;
  object_class->finalize = finalize;

  gtk_widget_class_set_template_from_resource(widget_class, tags::resources::preferences_spectrum_ui);

  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, show);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, type);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, fill);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, n_points);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, line_width);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, height);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, show_bar_border);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, rounded_corners);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, dynamic_y_scale);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, color_button);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, axis_color_button);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, minimum_frequency);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, maximum_frequency);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, avsync_delay);

  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, show_histogram);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, show_statistics);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, window_size);
  gtk_widget_class_bind_template_child(widget_class, PreferencesSpectrum, histogram_bins);

  gtk_widget_class_bind_template_callback(widget_class, on_spectrum_color_set);
  gtk_widget_class_bind_template_callback(widget_class, on_spectrum_axis_color_set);
}

void setup_statistics_grid(PreferencesSpectrum* self) {
  auto* statistics_grid = gtk_grid_new();

  gtk_grid_set_column_spacing(GTK_GRID(statistics_grid), 6);
  gtk_grid_set_row_spacing(GTK_GRID(statistics_grid), 6);
  gtk_widget_set_margin_start(statistics_grid, 6);
  gtk_widget_set_margin_end(statistics_grid, 6);
  gtk_widget_set_margin_top(statistics_grid, 6);
  gtk_widget_set_margin_bottom(statistics_grid, 6);

  auto* statistics_frame = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(statistics_frame), "Statistics");
  gtk_box_append(GTK_BOX(self), GTK_WIDGET(statistics_frame));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(statistics_frame), statistics_grid);

  // Show Histogram switch
  auto* show_histogram_label = gtk_label_new("Show Histogram");
  gtk_widget_set_halign(show_histogram_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(statistics_grid), show_histogram_label, 0, 0, 1, 1);

  self->show_histogram = GTK_SWITCH(gtk_switch_new());
  gtk_widget_set_halign(GTK_WIDGET(self->show_histogram), GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(statistics_grid), GTK_WIDGET(self->show_histogram), 1, 0, 1, 1);

  // Show Statistics switch
  auto* show_statistics_label = gtk_label_new("Show Statistics");
  gtk_widget_set_halign(show_statistics_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(statistics_grid), show_statistics_label, 0, 1, 1, 1);

  self->show_statistics = GTK_SWITCH(gtk_switch_new());
  gtk_widget_set_halign(GTK_WIDGET(self->show_statistics), GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(statistics_grid), GTK_WIDGET(self->show_statistics), 1, 1, 1, 1);

  // Window Size spinner
  auto* window_size_label = gtk_label_new("Window Size");
  gtk_widget_set_halign(window_size_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(statistics_grid), window_size_label, 0, 2, 1, 1);

  self->window_size = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(128, 8192, 128));
  gtk_widget_set_halign(GTK_WIDGET(self->window_size), GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(statistics_grid), GTK_WIDGET(self->window_size), 1, 2, 1, 1);

  // Histogram Bins spinner
  auto* histogram_bins_label = gtk_label_new("Histogram Bins");
  gtk_widget_set_halign(histogram_bins_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(statistics_grid), histogram_bins_label, 0, 3, 1, 1);

  self->histogram_bins = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(10, 128, 10));
  gtk_widget_set_halign(GTK_WIDGET(self->histogram_bins), GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(statistics_grid), GTK_WIDGET(self->histogram_bins), 1, 3, 1, 1);
}

void preferences_spectrum_init(PreferencesSpectrum* self) {
  gtk_widget_init_template(GTK_WIDGET(self));

  self->data = new Data();

  self->settings = g_settings_new(tags::schema::spectrum::id);

  // initializing some widgets

  auto color = util::gsettings_get_color(self->settings, "color");

  gtk_color_dialog_button_set_rgba(self->color_button, &color);

  color = util::gsettings_get_color(self->settings, "color-axis-labels");

  gtk_color_dialog_button_set_rgba(self->axis_color_button, &color);

  // connecting some widgets signals

  prepare_spinbuttons<"px">(self->height, self->line_width);

  g_signal_connect(self->minimum_frequency, "output", G_CALLBACK(+[](GtkSpinButton* button, gpointer user_data) {
                     return parse_spinbutton_output(button, "Hz");
                   }),
                   nullptr);

  g_signal_connect(self->minimum_frequency, "input",
                   G_CALLBACK(+[](GtkSpinButton* button, gdouble* new_value, PreferencesSpectrum* self) {
                     const auto parse_result = parse_spinbutton_input(button, new_value);

                     if (parse_result != GTK_INPUT_ERROR) {
                       const auto max_freq =
                           static_cast<double>(g_settings_get_int(self->settings, "maximum-frequency"));

                       if (const auto valid_min_freq = max_freq - 100.0; *new_value > valid_min_freq) {
                         *new_value = valid_min_freq;
                       }
                     }

                     return parse_result;
                   }),
                   self);

  g_signal_connect(self->maximum_frequency, "output", G_CALLBACK(+[](GtkSpinButton* button, gpointer user_data) {
                     return parse_spinbutton_output(button, "Hz");
                   }),
                   nullptr);

  g_signal_connect(self->maximum_frequency, "input",
                   G_CALLBACK(+[](GtkSpinButton* button, gdouble* new_value, PreferencesSpectrum* self) {
                     const auto parse_result = parse_spinbutton_input(button, new_value);

                     if (parse_result != GTK_INPUT_ERROR) {
                       const auto min_freq =
                           static_cast<double>(g_settings_get_int(self->settings, "minimum-frequency"));

                       if (const auto valid_max_freq = min_freq + 100.0; *new_value < valid_max_freq) {
                         *new_value = valid_max_freq;
                       }
                     }

                     return parse_result;
                   }),
                   self);

  // spectrum section gsettings bindings

  gsettings_bind_widgets<"show", "fill", "rounded-corners", "show-bar-border", "dynamic-y-scale", "n-points", "height",
                         "line-width", "minimum-frequency", "maximum-frequency", "avsync-delay",
                         "show-histogram", "show-statistics", "window-size", "histogram-bins">(
      self->settings, self->show, self->fill, self->rounded_corners, self->show_bar_border, self->dynamic_y_scale,
      self->n_points, self->height, self->line_width, self->minimum_frequency, self->maximum_frequency,
      self->avsync_delay,
      self->show_histogram, self->show_statistics, self->window_size, self->histogram_bins);

  ui::gsettings_bind_enum_to_combo_widget(self->settings, "type", self->type);

  // Spectrum gsettings signals connections

  self->data->gconnections.push_back(g_signal_connect(
      self->settings, "changed::color", G_CALLBACK(+[](GSettings* settings, char* key, PreferencesSpectrum* self) {
        auto color = util::gsettings_get_color(settings, key);

        gtk_color_dialog_button_set_rgba(self->color_button, &color);
      }),
      self));

  self->data->gconnections.push_back(
      g_signal_connect(self->settings, "changed::color-axis-labels",
                       G_CALLBACK(+[](GSettings* settings, char* key, PreferencesSpectrum* self) {
                         auto color = util::gsettings_get_color(settings, key);

                         gtk_color_dialog_button_set_rgba(self->axis_color_button, &color);
                       }),
                       self));

  setup_statistics_grid(self);
  // Bind the new settings
  g_settings_bind(self->settings, "show-histogram",
                 self->show_histogram, "active",
                 G_SETTINGS_BIND_DEFAULT);

  g_settings_bind(self->settings, "show-statistics",
                 self->show_statistics, "active",
                 G_SETTINGS_BIND_DEFAULT);

  g_settings_bind(self->settings, "window-size",
                 self->window_size, "value",
                 G_SETTINGS_BIND_DEFAULT);

  g_settings_bind(self->settings, "histogram-bins",
                 self->histogram_bins, "value",
                 G_SETTINGS_BIND_DEFAULT);
}

auto create() -> PreferencesSpectrum* {
  return static_cast<PreferencesSpectrum*>(g_object_new(EE_TYPE_PREFERENCES_SPECTRUM, nullptr));
}

}  // namespace ui::preferences::spectrum
