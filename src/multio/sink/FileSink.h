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
/// @author Simon Smart
/// @date Dec 2015


#ifndef multio_FileSink_H
#define multio_FileSink_H

#include <iosfwd>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

#include "eckit/filesystem/PathName.h"

#include "multio/sink/DataSink.h"
#include "multio/util/ConfigurationContext.h"

//----------------------------------------------------------------------------------------------------------------------

namespace eckit {
class FileHandle;
}

namespace multio {

class FileSink final : public DataSink {
public:
    explicit FileSink(const util::ConfigurationContext& confCtx);

    ~FileSink() override;

private:  // methods
    void write(eckit::message::Message msg) override;

    void flush() override;

    void print(std::ostream&) const override;

private:  // members
    eckit::PathName path_;
    std::unique_ptr<eckit::DataHandle> handle_;
    std::mutex mutex_;
};

//----------------------------------------------------------------------------------------------------------------------

}  // namespace multio

#endif  // multio_FileSink_H
