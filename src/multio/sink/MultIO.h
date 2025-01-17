/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

/// @author Tiago Quintino
/// @date Dec 2015


#ifndef multio_MultIO_H
#define multio_MultIO_H

#include <iosfwd>
#include <mutex>
#include <string>
#include <vector>

#include "eckit/io/Length.h"
#include "eckit/log/Timer.h"
#include "eckit/types/Types.h"
#include "eckit/message/Message.h"

#include "multio/sink/DataSink.h"
#include "multio/sink/IOStats.h"
#include "multio/sink/Trigger.h"
#include <multio/util/ConfigurationContext.h>

namespace multio {

using util::ConfigurationContext;

//----------------------------------------------------------------------------------------------------------------------

class MultIO final : public DataSink {
public:
    explicit MultIO(const ConfigurationContext& config);

    ~MultIO() override = default;

    bool ready() const override;

    void write(eckit::message::Message message) override;

    void flush() override;

    void trigger(const eckit::StringDict& metadata) const;

    void report(std::ostream&);

protected:  // methods

    void print(std::ostream&) const override;

protected:  // members

    IOStats stats_;

    std::vector<std::shared_ptr<DataSink>> sinks_;

    Trigger trigger_;

    mutable std::mutex mutex_;

    eckit::Timer timer_;

private:  // methods
    friend std::ostream& operator<<(std::ostream& s, const MultIO& p) {
        p.print(s);
        return s;
    }
};

//----------------------------------------------------------------------------------------------------------------------

}  // namespace multio

#endif
