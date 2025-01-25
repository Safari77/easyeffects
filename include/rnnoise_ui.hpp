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

#pragma once

#include <adwaita.h>
#include <glib-object.h>
#include <glibconfig.h>
#include <gtk/gtkbox.h>
#include <memory>
#include <string>
#include "application.hpp"
#include "rnnoise.hpp"

namespace ui::rnnoise_box {

G_BEGIN_DECLS

#define EE_TYPE_RNNOISE_BOX (rnnoise_box_get_type())

G_DECLARE_FINAL_TYPE(RNNoiseBox, rnnoise_box, EE, RNNOISE_BOX, GtkBox)

G_END_DECLS

auto create() -> RNNoiseBox*;

void setup(RNNoiseBox* self,
           std::shared_ptr<RNNoise> rnnoise,
           const std::string& schema_path,
           app::Application* application);

}  // namespace ui::rnnoise_box
