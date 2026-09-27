// Brings the shared pluginkit UI into Simplr's namespace.
#pragma once

#include "pluginkit/ui/Theme.h"
#include "pluginkit/ui/Widgets.h"

namespace simplr {
namespace theme = pk::theme;
using pk::ActionButton;
using pk::Choice;
using pk::Group;
using pk::HSlider;
using pk::Knob;
using pk::Label;
using pk::Panel;
using pk::ParamHost;
using pk::ParamView;
using pk::Segmented;
using pk::Toggle;
} // namespace simplr
