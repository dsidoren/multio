/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#include "MultioServerTool.h"

#include "eckit/option/CmdArgs.h"
#include "eckit/option/SimpleOption.h"

#include "multio/LibMultio.h"

namespace multio {
namespace server {

MultioServerTool::MultioServerTool(int argc, char** argv) : eckit::Tool(argc, argv, "MULTIO_HOME") {
    options_.push_back(new eckit::option::SimpleOption<size_t>("nbservers", "Number of servers"));
}

void MultioServerTool::init(const eckit::option::CmdArgs& args) {
    args.get("nbservers", nbServers_);
}

void MultioServerTool::finish(const eckit::option::CmdArgs&) {}

void MultioServerTool::run() {
    std::function<void(const std::string&)> usage = [this](const std::string& name) {
        this->usage(name);
    };

    eckit::option::CmdArgs args(usage,
                                options_,
                                numberOfPositionalArguments(),
                                minimumPositionalArguments());

    init(args);
    execute(args);
    finish(args);
}

}  // namespace server
}  // namespace multio