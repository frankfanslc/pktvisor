/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "DnstapInputModulePlugin.h"
#include "CoreRegistry.h"
#include "InputStreamManager.h"
#include <Corrade/PluginManager/AbstractManager.h>
#include <Corrade/Utility/FormatStl.h>

CORRADE_PLUGIN_REGISTER(VisorInputDnstap, visor::input::dnstap::DnstapInputModulePlugin,
    "visor.module.input/1.0")

namespace visor::input::dnstap {

void DnstapInputModulePlugin::setup_routes([[maybe_unused]] HttpServer *svr)
{
}

std::unique_ptr<InputStream> DnstapInputModulePlugin::instantiate(const std::string name, const Configurable *config)
{
    auto input_stream = std::make_unique<DnstapInputStream>(name);
    input_stream->config_merge(*config);
    return input_stream;
}

}
