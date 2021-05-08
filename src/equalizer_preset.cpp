/*
 *  Copyright © 2017-2020 Wellington Wallace
 *
 *  This file is part of EasyEffects.
 *
 *  EasyEffects is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  EasyEffects is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with EasyEffects.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "equalizer_preset.hpp"
#include "util.hpp"

EqualizerPreset::EqualizerPreset()
    : input_settings(Gio::Settings::create("com.github.wwmm.easyeffects.equalizer",
                                           "/com/github/wwmm/easyeffects/streaminputs/equalizer/")),
      input_settings_left(Gio::Settings::create("com.github.wwmm.easyeffects.equalizer.channel",
                                                "/com/github/wwmm/easyeffects/streaminputs/equalizer/leftchannel/")),
      input_settings_right(Gio::Settings::create("com.github.wwmm.easyeffects.equalizer.channel",
                                                 "/com/github/wwmm/easyeffects/streaminputs/equalizer/rightchannel/")),
      output_settings(Gio::Settings::create("com.github.wwmm.easyeffects.equalizer",
                                            "/com/github/wwmm/easyeffects/streamoutputs/equalizer/")),
      output_settings_left(Gio::Settings::create("com.github.wwmm.easyeffects.equalizer.channel",
                                                 "/com/github/wwmm/easyeffects/streamoutputs/equalizer/leftchannel/")),
      output_settings_right(
          Gio::Settings::create("com.github.wwmm.easyeffects.equalizer.channel",
                                "/com/github/wwmm/easyeffects/streamoutputs/equalizer/rightchannel/")) {}

void EqualizerPreset::save(boost::property_tree::ptree& root,
                           const std::string& section,
                           const Glib::RefPtr<Gio::Settings>& settings) {
  root.put(section + ".equalizer.mode", settings->get_string("mode"));

  const auto& nbands = settings->get_int("num-bands");

  root.put(section + ".equalizer.num-bands", nbands);

  root.put(section + ".equalizer.input-gain", settings->get_double("input-gain"));

  root.put(section + ".equalizer.output-gain", settings->get_double("output-gain"));

  root.put(section + ".equalizer.split-channels", settings->get_boolean("split-channels"));

  if (section == "input") {
    save_channel(root, "input.equalizer.left", input_settings_left, nbands);
    save_channel(root, "input.equalizer.right", input_settings_right, nbands);
  } else if (section == "output") {
    save_channel(root, "output.equalizer.left", output_settings_left, nbands);
    save_channel(root, "output.equalizer.right", output_settings_right, nbands);
  }
}

void EqualizerPreset::save_channel(boost::property_tree::ptree& root,
                                   const std::string& section,
                                   const Glib::RefPtr<Gio::Settings>& settings,
                                   const int& nbands) {
  for (int n = 0; n < nbands; n++) {
    root.put(section + ".band" + std::to_string(n) + ".type",
             settings->get_string(std::string("band" + std::to_string(n) + "-type")));

    root.put(section + ".band" + std::to_string(n) + ".mode",
             settings->get_string(std::string("band" + std::to_string(n) + "-mode")));

    root.put(section + ".band" + std::to_string(n) + ".slope",
             settings->get_string(std::string("band" + std::to_string(n) + "-slope")));

    root.put(section + ".band" + std::to_string(n) + ".solo",
             settings->get_boolean(std::string("band" + std::to_string(n) + "-solo")));

    root.put(section + ".band" + std::to_string(n) + ".mute",
             settings->get_boolean(std::string("band" + std::to_string(n) + "-mute")));

    root.put(section + ".band" + std::to_string(n) + ".gain",
             settings->get_double(std::string("band" + std::to_string(n) + "-gain")));

    root.put(section + ".band" + std::to_string(n) + ".frequency",
             settings->get_double(std::string("band" + std::to_string(n) + "-frequency")));

    root.put(section + ".band" + std::to_string(n) + ".q",
             settings->get_double(std::string("band" + std::to_string(n) + "-q")));
  }
}

void EqualizerPreset::load(const boost::property_tree::ptree& root,
                           const std::string& section,
                           const Glib::RefPtr<Gio::Settings>& settings) {
  update_string_key(root, settings, "mode", section + ".equalizer.mode");

  update_key<int>(root, settings, "num-bands", section + ".equalizer.num-bands");

  update_key<double>(root, settings, "input-gain", section + ".equalizer.input-gain");

  update_key<double>(root, settings, "output-gain", section + ".equalizer.output-gain");

  const auto& nbands = settings->get_int("num-bands");

  update_key<bool>(root, settings, "split-channels", section + ".equalizer.split-channels");

  if (section == std::string("input")) {
    load_channel(root, "input.equalizer.left", input_settings_left, nbands);
    load_channel(root, "input.equalizer.right", input_settings_right, nbands);
  } else if (section == std::string("output")) {
    load_channel(root, "output.equalizer.left", output_settings_left, nbands);
    load_channel(root, "output.equalizer.right", output_settings_right, nbands);
  }
}

void EqualizerPreset::load_channel(const boost::property_tree::ptree& root,
                                   const std::string& section,
                                   const Glib::RefPtr<Gio::Settings>& settings,
                                   const int& nbands) {
  for (int n = 0; n < nbands; n++) {
    update_string_key(root, settings, std::string("band" + std::to_string(n) + "-type"),
                      section + ".band" + std::to_string(n) + ".type");

    update_string_key(root, settings, std::string("band" + std::to_string(n) + "-mode"),
                      section + ".band" + std::to_string(n) + ".mode");

    update_string_key(root, settings, std::string("band" + std::to_string(n) + "-slope"),
                      section + ".band" + std::to_string(n) + ".slope");

    update_key<bool>(root, settings, std::string("band" + std::to_string(n) + "-solo"),
                     section + ".band" + std::to_string(n) + ".solo");

    update_key<bool>(root, settings, std::string("band" + std::to_string(n) + "-mute"),
                     section + ".band" + std::to_string(n) + ".mute");

    update_key<double>(root, settings, std::string("band" + std::to_string(n) + "-gain"),
                       section + ".band" + std::to_string(n) + ".gain");

    update_key<double>(root, settings, std::string("band" + std::to_string(n) + "-frequency"),
                       section + ".band" + std::to_string(n) + ".frequency");

    update_key<double>(root, settings, std::string("band" + std::to_string(n) + "-q"),
                       section + ".band" + std::to_string(n) + ".q");
  }
}

void EqualizerPreset::write(PresetType preset_type, boost::property_tree::ptree& root) {
  switch (preset_type) {
    case PresetType::output:
      save(root, "output", output_settings);
      break;
    case PresetType::input:
      save(root, "input", input_settings);
      break;
  }
}

void EqualizerPreset::read(PresetType preset_type, const boost::property_tree::ptree& root) {
  switch (preset_type) {
    case PresetType::output:
      load(root, "output", output_settings);
      break;
    case PresetType::input:
      load(root, "input", input_settings);
      break;
  }
}
