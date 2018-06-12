#ifndef PITCH_UI_HPP
#define PITCH_UI_HPP

#include <gtkmm/grid.h>
#include <gtkmm/togglebutton.h>
#include "plugin_ui_base.hpp"

class PitchUi : public Gtk::Grid, public PluginUiBase {
   public:
    PitchUi(BaseObjectType* cobject,
            const Glib::RefPtr<Gtk::Builder>& builder,
            const std::string& settings_name);
    virtual ~PitchUi();

    void reset();

   private:
    Glib::RefPtr<Gtk::Adjustment> cents, crispness, semitones, octaves;
    Gtk::ToggleButton *faster, *formant_preserving;
};

#endif
