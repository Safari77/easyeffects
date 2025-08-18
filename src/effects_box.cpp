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

#include "effects_box.hpp"
#include <STTypes.h>
#include <adwaita.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/compile.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gobject/gobject.h>
#include <gsl/gsl_interp.h>
#include <gsl/gsl_spline.h>
#include <gtk/gtk.h>
#include <gtk/gtkshortcut.h>
#include <sigc++/connection.h>
#include <algorithm>
#include <cstddef>
#include <vector>
#include "application.hpp"
#include "apps_box.hpp"
#include "blocklist_menu.hpp"
#include "chart.hpp"
#include "effects_base.hpp"
#include "pipeline_type.hpp"
#include "plugins_box.hpp"
#include "tags_app.hpp"
#include "tags_resources.hpp"
#include "tags_schema.hpp"
#include "ui_helpers.hpp"
#include "util.hpp"

namespace {

bool schedule_signal_idle = false;

}

namespace ui::effects_box {

struct Data {
 public:
  ~Data() { util::debug("data struct destroyed"); }

  app::Application* application;

  EffectsBase* effects_base;

  PipelineType pipeline_type;

  uint spectrum_rate, spectrum_n_bands;

  float global_output_level_left, global_output_level_right, pipeline_latency_ms;

  std::vector<double> spectrum_mag, spectrum_x_axis, spectrum_freqs;

  std::vector<sigc::connection> connections;

  std::vector<gulong> gconnections_spectrum;
};

struct _EffectsBox {
  GtkBox parent_instance;

  AdwViewStack* stack;

  AdwViewStackPage *apps_box_page, *plugins_box_page;

  GtkLabel *device_state, *latency_status, *label_global_output_level_left, *label_global_output_level_right;

  GtkToggleButton* toggle_listen_to_mic;

  GtkMenuButton* menubutton_blocklist;

  GtkImage* saturation_icon;

  GtkIconTheme* icon_theme;

  ui::chart::Chart* histogram_chart; // For histogram visualization
  GtkWidget* statistics_label; // For displaying statistics

  ui::chart::Chart* spectrum_chart;

  ui::apps_box::AppsBox* appsBox;

  ui::plugins_box::PluginsBox* pluginsBox;

  ui::blocklist_menu::BlocklistMenu* blocklist_menu;

  GSettings *settings_spectrum, *app_settings;

  Data* data;
};

// NOLINTNEXTLINE
G_DEFINE_TYPE(EffectsBox, effects_box, GTK_TYPE_BOX)

void init_spectrum_frequency_axis(EffectsBox* self) {
  uint bands = self->data->spectrum_n_bands;
  g_message("init_spectrum_frequency_axis %u", bands);
  self->data->spectrum_freqs.resize(bands);

  for (uint n = 0U; n < bands; n++) {
    self->data->spectrum_freqs[n] = 0.5F * static_cast<float>(self->data->spectrum_rate) * static_cast<float>(n) /
                                    static_cast<float>(bands);
  }

  if (!self->data->spectrum_freqs.empty()) {
    const auto min_freq = static_cast<float>(g_settings_get_int(self->settings_spectrum, "minimum-frequency"));
    const auto max_freq = static_cast<float>(g_settings_get_int(self->settings_spectrum, "maximum-frequency"));

    if (min_freq > (max_freq - 100.0F)) {
      return;
    }

    auto log_x_axis = util::logspace(min_freq, max_freq, g_settings_get_int(self->settings_spectrum, "n-points"));

    self->data->spectrum_x_axis.resize(log_x_axis.size());
    self->data->spectrum_mag.resize(log_x_axis.size());

    std::copy(log_x_axis.begin(), log_x_axis.end(), self->data->spectrum_x_axis.begin());

    ui::chart::set_x_data(self->spectrum_chart, self->data->spectrum_x_axis);
  }
}

static void update_histogram_visibility(EffectsBox* self) {
    if (self->histogram_chart && ui::chart::EE_IS_CHART(self->histogram_chart)) {
        gtk_widget_set_visible(GTK_WIDGET(self->histogram_chart),
            g_settings_get_boolean(self->settings_spectrum, "show-histogram"));
    }
    if (self->statistics_label) {
        gtk_widget_set_visible(self->statistics_label,
            g_settings_get_boolean(self->settings_spectrum, "show-statistics"));
    }
}

void setup_spectrum(EffectsBox* self) {
  self->data->spectrum_rate = 0U;
  self->data->spectrum_n_bands = 0U;

  // Get the box_spectrum container from the template
  GtkBox* box_spectrum = GTK_BOX(gtk_widget_get_first_child(GTK_WIDGET(self)));

  // Clear existing children from box_spectrum
  GtkWidget* child;
  while ((child = gtk_widget_get_first_child(GTK_WIDGET(box_spectrum))) != nullptr) {
      gtk_box_remove(box_spectrum, child);
  }

  // Create charts container
  GtkWidget* charts_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(charts_box, TRUE);
  gtk_box_set_homogeneous(GTK_BOX(charts_box), FALSE);

  // Get color settings once
  auto chart_color = util::gsettings_get_color(self->settings_spectrum, "color");
  auto axis_labels_color = util::gsettings_get_color(self->settings_spectrum, "color-axis-labels");
  auto fill_bars = g_settings_get_boolean(self->settings_spectrum, "fill") != 0;
  auto line_width = static_cast<float>(g_settings_get_double(self->settings_spectrum, "line-width"));
  auto chart_type = util::gsettings_get_string(self->settings_spectrum, "type");

  // Set up spectrum chart
  ui::chart::set_color(self->spectrum_chart, chart_color);
  ui::chart::set_axis_labels_color(self->spectrum_chart, axis_labels_color);
  ui::chart::set_fill_bars(self->spectrum_chart, fill_bars);
  ui::chart::set_line_width(self->spectrum_chart, line_width);
  ui::chart::set_chart_scale(self->spectrum_chart, ui::chart::ChartScale::logarithmic);
  ui::chart::set_x_unit(self->spectrum_chart, "Hz");
  ui::chart::set_y_unit(self->spectrum_chart, "dB");
  if (chart_type == "Bars") {
    ui::chart::set_chart_type(self->spectrum_chart, chart::ChartType::bar);
  } else if (chart_type == "Lines") {
    ui::chart::set_chart_type(self->spectrum_chart, chart::ChartType::line);
  } else if (chart_type == "Dots") {
    ui::chart::set_chart_type(self->spectrum_chart, chart::ChartType::dots);
  }

  // Add spectrum chart to charts box
  gtk_box_append(GTK_BOX(charts_box), GTK_WIDGET(self->spectrum_chart));
  gtk_widget_set_vexpand(GTK_WIDGET(self->spectrum_chart), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(self->spectrum_chart), -1,
                              g_settings_get_int(self->settings_spectrum, "height"));

  // Visual settings - use same color settings as spectrum
  ui::chart::set_color(self->histogram_chart, chart_color);
  ui::chart::set_axis_labels_color(self->histogram_chart, axis_labels_color);
  ui::chart::set_fill_bars(self->histogram_chart, fill_bars);
  ui::chart::set_line_width(self->histogram_chart, line_width);
  ui::chart::set_chart_scale(self->histogram_chart, ui::chart::ChartScale::linear);
  ui::chart::set_x_unit(self->histogram_chart, "dB");
  ui::chart::set_y_unit(self->histogram_chart, "%");

  // Get number of bins from settings
  const size_t n_bins = g_settings_get_uint(self->settings_spectrum, "histogram-bins");
  // Since values are very small except bin[0], use logarithmic scale
  // and adjust ranges to better show the distribution
  ui::chart::set_histogram_ranges(ui::chart::EE_CHART(self->histogram_chart),
                                util::minimum_db_d_level, 0.0, n_bins);
  // Container setup
  gtk_box_append(GTK_BOX(charts_box), GTK_WIDGET(self->histogram_chart));
  gtk_widget_set_vexpand(GTK_WIDGET(self->histogram_chart), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(self->histogram_chart), -1,
                            std::min(g_settings_get_int(self->settings_spectrum, "height")/3, 100));


  // Create an outer horizontal box for the statistics row.
  GtkWidget *stats_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(stats_row, TRUE);

  // Create a left spacer that expands.
  GtkWidget *left_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(left_spacer, TRUE);

  // Create a middle box (nonexpanding) for the statistics label and Reset button.
  GtkWidget *middle_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_halign(middle_box, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(middle_box, FALSE);

  // Create the statistics label.
  self->statistics_label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(self->statistics_label), 0.5);
  gtk_widget_set_halign(self->statistics_label, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(self->statistics_label, FALSE);

  // Create the Reset button.
  GtkWidget *reset_button = gtk_button_new_with_label("⌫");
  gtk_widget_set_halign(reset_button, GTK_ALIGN_START);
  gtk_widget_set_hexpand(reset_button, FALSE);

  // Pack the label and the reset button together in the middle box.
  gtk_box_append(GTK_BOX(middle_box), self->statistics_label);
  gtk_box_append(GTK_BOX(middle_box), reset_button);

  // Create a right spacer that expands.
  GtkWidget *right_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(right_spacer, TRUE);

  // Append left spacer, middle box, and right spacer into the stats_row.
  gtk_box_append(GTK_BOX(stats_row), left_spacer);
  gtk_box_append(GTK_BOX(stats_row), middle_box);
  gtk_box_append(GTK_BOX(stats_row), right_spacer);

  // Connect the Reset button to call reset_statistics().
  g_signal_connect(reset_button, "clicked", G_CALLBACK(+[](GtkButton* /*button*/, gpointer user_data) {
      EffectsBox* self = static_cast<EffectsBox*>(user_data);
      if (self->data->effects_base && self->data->effects_base->spectrum) {
          self->data->effects_base->spectrum->reset_statistics();
          g_message("Statistics reset!");
      }
  }), self);
  gtk_box_append(GTK_BOX(charts_box), stats_row);

  update_histogram_visibility(self);

  // Add charts box to box_spectrum
  gtk_box_append(box_spectrum, charts_box);
  g_settings_bind(self->settings_spectrum, "show", self->spectrum_chart, "visible", G_SETTINGS_BIND_GET);

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::color", G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        ui::chart::set_color(self->spectrum_chart, util::gsettings_get_color(self->settings_spectrum, key));
      }),
      self));

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::color-axis-labels",
      G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        ui::chart::set_axis_labels_color(self->spectrum_chart, util::gsettings_get_color(self->settings_spectrum, key));
      }),
      self));

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::fill", G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        ui::chart::set_fill_bars(self->spectrum_chart, g_settings_get_boolean(self->settings_spectrum, key) != 0);
      }),
      self));

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::dynamic-y-scale",
      G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        ui::chart::set_dynamic_y_scale(self->spectrum_chart, g_settings_get_boolean(self->settings_spectrum, key) != 0);
      }),
      self));

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::rounded-corners",
      G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        ui::chart::set_rounded_corners(self->spectrum_chart, g_settings_get_boolean(self->settings_spectrum, key) != 0);
      }),
      self));

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::show-bar-border",
      G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        ui::chart::set_draw_bar_border(self->spectrum_chart, g_settings_get_boolean(self->settings_spectrum, key) != 0);
      }),
      self));

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::line-width", G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        ui::chart::set_line_width(self->spectrum_chart,
                                  static_cast<float>(g_settings_get_double(self->settings_spectrum, key)));
      }),
      self));

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::height", G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        int set_height = g_settings_get_int(self->settings_spectrum, key);
        gtk_widget_set_size_request(GTK_WIDGET(self->spectrum_chart), -1, set_height);
        gtk_widget_set_size_request(GTK_WIDGET(self->histogram_chart), -1, std::min(set_height/3, 100));
      }),
      self));

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::type", G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        auto chart_type = util::gsettings_get_string(self->settings_spectrum, key);

        if (chart_type == "Bars") {
          ui::chart::set_chart_type(self->spectrum_chart, chart::ChartType::bar);
        } else if (chart_type == "Lines") {
          ui::chart::set_chart_type(self->spectrum_chart, chart::ChartType::line);
        } else if (chart_type == "Dots") {
          ui::chart::set_chart_type(self->spectrum_chart, chart::ChartType::dots);
        }
      }),
      self));

  self->data->gconnections_spectrum.push_back(g_signal_connect(
      self->settings_spectrum, "changed::n-points", G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        g_object_ref(self);

        util::idle_add([=]() { init_spectrum_frequency_axis(self); }, [=]() { g_object_unref(self); });
      }),
      self));

  self->data->gconnections_spectrum.push_back(
      g_signal_connect(self->settings_spectrum, "changed::minimum-frequency",
                       G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
                         g_object_ref(self);

                         util::idle_add([=]() { init_spectrum_frequency_axis(self); }, [=]() { g_object_unref(self); });
                       }),
                       self));

  self->data->gconnections_spectrum.push_back(
      g_signal_connect(self->settings_spectrum, "changed::maximum-frequency",
                       G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
                         g_object_ref(self);

                         util::idle_add([=]() { init_spectrum_frequency_axis(self); }, [=]() { g_object_unref(self); });
                       }),
                       self));
}

void stack_visible_child_changed(EffectsBox* self, GParamSpec* pspec, GtkWidget* stack) {
  const auto* name = adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(stack));

  gtk_widget_set_visible(GTK_WIDGET(self->menubutton_blocklist), (g_strcmp0(name, "apps") == 0) ? 1 : 0);

  if (self->data->pipeline_type == PipelineType::input) {
    gtk_widget_set_visible(GTK_WIDGET(self->toggle_listen_to_mic), (g_strcmp0(name, "plugins") == 0) ? 1 : 0);
  }
}

void on_listen_to_mic_toggled(EffectsBox* self, GtkToggleButton* button) {
    if (gtk_toggle_button_get_active(button) != 0) {
        g_settings_set_boolean(self->app_settings, "listen-to-mic", 1);
    } else {
        g_settings_set_boolean(self->app_settings, "listen-to-mic", 0);
    }
}

static gboolean histogram_data_update(GtkWidget* widget, GdkFrameClock* frame_clock, EffectsBox* self) {
  if (g_settings_get_boolean(self->settings_spectrum, "show-statistics") &&
      gtk_widget_get_visible(GTK_WIDGET(self->statistics_label))) {
    const auto& stats = self->data->effects_base->spectrum->get_statistics();
    auto stats_text = fmt::format("<span font_desc=\"monospace bold\">Mean: {:4.2f} dB   Kurtosis: {:8.2f}</span>",
                                  stats.get_mean(), stats.get_kurtosis());
    gtk_label_set_markup(GTK_LABEL(self->statistics_label), stats_text.c_str());
  }

  if (self->histogram_chart && ui::chart::get_is_visible(self->histogram_chart)) {
    const auto& stats = self->data->effects_base->spectrum->get_statistics();
    const auto& hist_data = stats.get_histogram_data();

    if (!hist_data.empty()) {
        ui::chart::set_histogram_data(self->histogram_chart, hist_data);
        ui::chart::set_histogram_x_data(EE_CHART(self->histogram_chart));
        gtk_widget_queue_draw(GTK_WIDGET(ui::chart::EE_CHART(self->histogram_chart)));
    }
  }

  gtk_widget_queue_draw(GTK_WIDGET(ui::chart::EE_CHART(self->histogram_chart)));
  return G_SOURCE_CONTINUE;
}

static gboolean spectrum_data_update(GtkWidget* widget, GdkFrameClock* frame_clock, EffectsBox* self) {
  if (!ui::chart::get_is_visible(self->spectrum_chart)) {
    return G_SOURCE_CONTINUE;
  }

  if (!schedule_signal_idle) {
    return G_SOURCE_CONTINUE;
  }

  auto [rate, n_bands, magnitudes] = self->data->effects_base->spectrum->compute_magnitudes();

  // No new data available, no redraw required.
  if (rate == 0 || n_bands == 0) {
    return G_SOURCE_CONTINUE;
  }

  if (self->data->spectrum_rate != rate || self->data->spectrum_n_bands != n_bands) {
    self->data->spectrum_rate = rate;
    self->data->spectrum_n_bands = n_bands;

    init_spectrum_frequency_axis(self);
  }

  auto* acc = gsl_interp_accel_alloc();
  auto* spline = gsl_spline_alloc(gsl_interp_steffen, n_bands);

  gsl_spline_init(spline, self->data->spectrum_freqs.data(), magnitudes, n_bands);

  for (size_t n = 0; n < self->data->spectrum_x_axis.size(); n++) {
    self->data->spectrum_mag[n] = static_cast<float>(gsl_spline_eval(spline, self->data->spectrum_x_axis[n], acc));
  }

  gsl_spline_free(spline);
  gsl_interp_accel_free(acc);

  std::ranges::for_each(self->data->spectrum_mag, [](auto& v) {
    v = 10.0F * std::log10(v);

    if (!std::isinf(v)) {
      v = (v > util::minimum_db_level) ? v : util::minimum_db_level;
    } else {
      v = util::minimum_db_level;
    }
  });

  ui::chart::set_y_data(self->spectrum_chart, self->data->spectrum_mag);

  return G_SOURCE_CONTINUE;
}

void setup(EffectsBox* self, app::Application* application, PipelineType pipeline_type, GtkIconTheme* icon_theme) {
  self->data->application = application;
  self->data->pipeline_type = pipeline_type;
  self->icon_theme = icon_theme;

  switch (pipeline_type) {
    case PipelineType::input: {
      self->data->effects_base = static_cast<EffectsBase*>(self->data->application->sie);

      self->apps_box_page = adw_view_stack_add_titled(self->stack, GTK_WIDGET(self->appsBox), "apps", _("Recorders"));

      adw_view_stack_page_set_icon_name(self->apps_box_page, "media-record-symbolic");

      auto set_device_state_label = [=]() {
        auto source_rate = static_cast<float>(application->pm->ee_source_node.rate) * 0.001F;

        gtk_label_set_text(self->device_state, fmt::format(ui::get_user_locale(), "{0:.1Lf} kHz", source_rate).c_str());
      };

      set_device_state_label();

      self->data->connections.push_back(application->pm->source_changed.connect([=](const auto nd_info) {
        if (nd_info.id == application->pm->ee_source_node.id) {
          set_device_state_label();
        }
      }));

      break;
    }
    case PipelineType::output: {
      self->data->effects_base = static_cast<EffectsBase*>(self->data->application->soe);

      self->apps_box_page = adw_view_stack_add_titled(self->stack, GTK_WIDGET(self->appsBox), "apps", _("Players"));

      adw_view_stack_page_set_icon_name(self->apps_box_page, "multimedia-player-symbolic");

      auto set_device_state_label = [=]() {
        auto sink_rate = static_cast<float>(application->pm->ee_sink_node.rate) * 0.001F;

        gtk_label_set_text(self->device_state, fmt::format(ui::get_user_locale(), "{0:.1Lf} kHz", sink_rate).c_str());
      };

      set_device_state_label();

      self->data->connections.push_back(application->pm->sink_changed.connect([=](const auto nd_info) {
        if (nd_info.id == application->pm->ee_sink_node.id) {
          set_device_state_label();
        }
      }));

      break;
    }
  }

  self->plugins_box_page =
      adw_view_stack_add_titled(self->stack, GTK_WIDGET(self->pluginsBox), "plugins", _("Effects"));

  adw_view_stack_page_set_icon_name(self->plugins_box_page, "folder-music-symbolic");

  if (self->data->effects_base->spectrum) {
    self->data->effects_base->spectrum->signal_histogram_bins_changed.connect(
        [eb = self](size_t new_bins) {
            ui::chart::set_histogram_ranges(EE_CHART(eb->histogram_chart), util::minimum_db_d_level, 0.0, new_bins);
            gtk_widget_queue_draw(GTK_WIDGET(EE_CHART(eb->histogram_chart)));
        }
    );
  }

  // Connect settings changes
  self->data->gconnections_spectrum.push_back(
    g_signal_connect(self->settings_spectrum, "changed::show-statistics",
      G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        gtk_widget_set_visible(self->statistics_label, g_settings_get_boolean(settings, key));
      }), self));

  self->data->gconnections_spectrum.push_back(
    g_signal_connect(self->settings_spectrum, "changed::show-histogram",
      G_CALLBACK(+[](GSettings* settings, char* key, EffectsBox* self) {
        update_histogram_visibility(self);
      }), self));

  // Initialize visibility for statistics and histogram
  update_histogram_visibility(self);

  // setting up the boxes we added to the stack

  ui::apps_box::setup(self->appsBox, application, pipeline_type, icon_theme);
  ui::plugins_box::setup(self->pluginsBox, application, pipeline_type);
  ui::blocklist_menu::setup(self->blocklist_menu, application, pipeline_type);

  // output level

  self->data->connections.push_back(
      self->data->effects_base->output_level->output_level.connect([=](const float left, const float right) {
        self->data->global_output_level_left = left;
        self->data->global_output_level_right = right;

        if (!schedule_signal_idle) {
          return;
        }

        g_idle_add((GSourceFunc) +
                       [](EffectsBox* self) {
                         if (!schedule_signal_idle) {
                           return G_SOURCE_REMOVE;
                         }

                         gtk_label_set_text(self->label_global_output_level_left,
                                            fmt::format("{0:.0f}", self->data->global_output_level_left).c_str());

                         gtk_label_set_text(self->label_global_output_level_right,
                                            fmt::format("{0:.0f}", self->data->global_output_level_right).c_str());

                         gtk_widget_set_opacity(
                             GTK_WIDGET(self->saturation_icon),
                             (self->data->global_output_level_left > 0.0 || self->data->global_output_level_right > 0.0)
                                 ? 1.0
                                 : 0.0);

                         return G_SOURCE_REMOVE;
                       },
                   self);
      }));

  // spectrum array

  gtk_widget_add_tick_callback(GTK_WIDGET(self->spectrum_chart), (GtkTickCallback)spectrum_data_update, self, NULL);
  gtk_widget_add_tick_callback(GTK_WIDGET(self->histogram_chart), (GtkTickCallback)histogram_data_update, self, NULL);

  // As we are showing the window we want the filters to send notifications about level meters, etc

  self->data->effects_base->spectrum->bypass = g_settings_get_boolean(self->settings_spectrum, "show") == 0;

  self->data->effects_base->output_level->set_post_messages(true);

  // pipeline latency

  gtk_label_set_text(
      self->latency_status,
      fmt::format(ui::get_user_locale(), "     {0:.1Lf} ms", self->data->effects_base->get_pipeline_latency()).c_str());

  self->data->connections.push_back(self->data->effects_base->pipeline_latency.connect([=](const float& v) {
    self->data->pipeline_latency_ms = v;

    if (!schedule_signal_idle) {
      return;
    }

    g_idle_add(
        (GSourceFunc) +
            [](EffectsBox* self) {
              if (!schedule_signal_idle) {
                return G_SOURCE_REMOVE;
              }

              gtk_label_set_text(
                  self->latency_status,
                  fmt::format(ui::get_user_locale(), "     {0:.1Lf} ms", self->data->pipeline_latency_ms).c_str());

              return G_SOURCE_REMOVE;
            },
        self);
  }));
}

void realize(GtkWidget* widget) {
  schedule_signal_idle = true;

  GTK_WIDGET_CLASS(effects_box_parent_class)->realize(widget);
}

void unroot(GtkWidget* widget) {
  schedule_signal_idle = false;

  GTK_WIDGET_CLASS(effects_box_parent_class)->unroot(widget);
}

void dispose(GObject* object) {
  auto* self = EE_EFFECTS_BOX(object);

  schedule_signal_idle = false;

  self->data->effects_base->spectrum->bypass = true;

  for (auto& c : self->data->connections) {
    c.disconnect();
  }

  for (auto& handler_id : self->data->gconnections_spectrum) {
    g_signal_handler_disconnect(self->settings_spectrum, handler_id);
  }

  self->data->connections.clear();
  self->data->gconnections_spectrum.clear();

  g_object_unref(self->app_settings);
  g_object_unref(self->settings_spectrum);

  util::debug("disposed");

  G_OBJECT_CLASS(effects_box_parent_class)->dispose(object);
}

void finalize(GObject* object) {
  auto* self = EE_EFFECTS_BOX(object);

  delete self->data;

  util::debug("finalized");

  G_OBJECT_CLASS(effects_box_parent_class)->finalize(object);
}

void effects_box_class_init(EffectsBoxClass* klass) {
  auto* object_class = G_OBJECT_CLASS(klass);
  auto* widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = dispose;
  object_class->finalize = finalize;

  widget_class->realize = realize;
  widget_class->unroot = unroot;

  gtk_widget_class_set_template_from_resource(widget_class, tags::resources::effects_box_ui);

  gtk_widget_class_bind_template_child(widget_class, EffectsBox, stack);
  gtk_widget_class_bind_template_child(widget_class, EffectsBox, device_state);
  gtk_widget_class_bind_template_child(widget_class, EffectsBox, latency_status);
  gtk_widget_class_bind_template_child(widget_class, EffectsBox, label_global_output_level_left);
  gtk_widget_class_bind_template_child(widget_class, EffectsBox, label_global_output_level_right);
  gtk_widget_class_bind_template_child(widget_class, EffectsBox, toggle_listen_to_mic);
  gtk_widget_class_bind_template_child(widget_class, EffectsBox, menubutton_blocklist);
  gtk_widget_class_bind_template_child(widget_class, EffectsBox, saturation_icon);

  gtk_widget_class_bind_template_callback(widget_class, stack_visible_child_changed);
  gtk_widget_class_bind_template_callback(widget_class, on_listen_to_mic_toggled);
}

void effects_box_init(EffectsBox* self) {
  gtk_widget_init_template(GTK_WIDGET(self));

  self->data = new Data();

  schedule_signal_idle = false;

  self->app_settings = g_settings_new(tags::app::id);
  self->settings_spectrum = g_settings_new(tags::schema::spectrum::id);

  self->spectrum_chart = ui::chart::create();
  self->histogram_chart = ui::chart::create();

  self->appsBox = ui::apps_box::create();
  self->pluginsBox = ui::plugins_box::create();
  self->blocklist_menu = ui::blocklist_menu::create();

  g_settings_bind(self->app_settings, "listen-to-mic",
                  self->toggle_listen_to_mic, "active",
                  G_SETTINGS_BIND_DEFAULT);

  gtk_menu_button_set_popover(self->menubutton_blocklist, GTK_WIDGET(self->blocklist_menu));

  setup_spectrum(self);

  g_signal_connect(GTK_WIDGET(self->spectrum_chart), "show", G_CALLBACK(+[](GtkWidget* widget, EffectsBox* self) {
                     self->data->effects_base->spectrum->bypass = false;
                   }),
                   self);

  g_signal_connect(GTK_WIDGET(self->spectrum_chart), "hide", G_CALLBACK(+[](GtkWidget* widget, EffectsBox* self) {
                     self->data->effects_base->spectrum->bypass = true;
                   }),
                   self);
}

auto create() -> EffectsBox* {
  return static_cast<EffectsBox*>(g_object_new(EE_TYPE_EFFECTS_BOX, nullptr));
}

}  // namespace ui::effects_box
