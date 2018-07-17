/*
 * (C) Copyright 1996-2015 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#include <stdlib.h>
#include <fstream>

#include "eckit/log/Log.h"
#include "eckit/config/YAMLConfiguration.h"
#include "eckit/testing/Test.h"
#include "eckit/parser/JSON.h"
#include "eckit/io/DataBlob.h"
#include "eckit/io/DataHandle.h"
#include "eckit/parser/JSONDataBlob.h"
#include "eckit/utils/Translator.h"

#include "multio/DataSink.h"
#include "multio/FileSink.h"
#include "multio/MultIO.h"

using namespace eckit;
using namespace eckit::testing;

namespace multio {
namespace test {

namespace {
std::string create_test_configuration(const eckit::PathName& file1, const eckit::PathName& file2,
                                      const eckit::PathName& file3, int jobId, int port) {
    file1.unlink();
    file2.unlink();
    file3.unlink();

    std::stringstream ss;
    ss << "{ \"triggers\" : [ "
             "{ \"type\" : \"MetadataChange\", \"host\" : \"localhost\", \"port\" : "
          << port
          << ", "
             "\"retries\" "
             ": 0, \"timeout\" : 1, \"key\" : \"step\", \"values\" : [\"0\", \"3\", \"6\", \"9\", "
             "\"12\", \"24\"],"
             " \"info\" : { \"job\" : \""
          << jobId << "\", \"job_name\" : \"epsnemo\" } },"
          << "{ \"type\" : \"MetadataChange\", \"file\" : \"" << file1.baseName()
          << "\", \"key\" : \"step\", "
             "\"values\" : "
             "[\"0\", \"3\", \"6\", \"9\", \"12\", \"24\"],"
             " \"info\" : { \"job\" : \""
          << jobId << "\", \"job_name\" : \"epsnemo\" } },"
          << "{ \"type\" : \"MetadataChange\", \"file\" : \"" << file2.baseName()
          << "\", \"key\" : \"step\", "
             "\"values\" : "
             "[\"1\", \"4\", \"5\", \"6\", \"10\"],"
             " \"info\" : { \"job\" : \""
          << jobId
          << "\", \"job_name\" : \"epsnemo\" } }"
             "] }";

    return ss.str();
}

std::string expected_file_content(const std::vector<int> steps_to_issue, const int jobId) {
    std::ostringstream oss;
    for (std::vector<int>::const_iterator it = steps_to_issue.begin(); it != steps_to_issue.end();
         ++it) {
        oss << "{\"type\":\"MetadataChange\",\"info\":{\"app\":\"multio\",\"job\":\"" << jobId
            << "\",\"job_name\":\"epsnemo\"},\"metadata\":{\"step\":\"" << *it << "\"}}"
            << std::endl;
    }
    return oss.str();
}

bool trigger_executed_correctly(const eckit::PathName& file, const std::vector<int>& steps_to_issue,
                                const int jobId) {
    EXPECT(file.exists());

    std::string expected = expected_file_content(steps_to_issue, jobId);

    std::fstream ifs(std::string(file.baseName()).c_str());
    std::string actual((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));

    return (expected == actual);
}

}  // namespace

//-----------------------------------------------------------------------------

class XX {

    std::string stream_;
    std::string step_;
    std::string level_;

public: // methods

    XX(const std::string& stream) : stream_(stream), step_(), level_() {}

    void step(long step) { step_ = Translator<long,std::string>()(step); }
    void level(long level) { level_ = Translator<long,std::string>()(level); }

    void json(JSON& s) const {
        s.startObject();
        s << "stream" << stream_;
        s << "step"   << step_;
        s << "level"  << level_;
        s.endObject();
    }
};


CASE("test_multio_with_event_trigger") {

    eckit::PathName file1("tmp.1");
    eckit::PathName file2("tmp.2");
    eckit::PathName file3("tmp.3");

    // Set up
    int port = 10000;
    int jobId = 234;

    std::string tconf = create_test_configuration(file1, file2, file3, jobId, port);
    ::setenv("MULTIO_CONFIG_TRIGGERS", tconf.c_str(), 1);

    std::string sinks("{ \"sinks\" : [ {\"type\" : \"file\", \"path\" : \"" + file3.baseName() +
                      "\"} ] }");
    eckit::YAMLConfiguration config(sinks);
    {
        eckit::ScopedPtr<MultIO> mio(new MultIO(config));

        XX x("oper");

        for (int step = 0; step <= 24; step++) {
            for (int level = 1; level <= 5; level++) {
                std::ostringstream os;
                JSON msg(os);

                x.step(step);
                x.level(level);
                x.json(msg);

                DataBlobPtr blob(new JSONDataBlob(os.str().c_str(), os.str().size()));

                mio->write(blob);
            }
        }
    } // mio is destroyed

    // First list of triggers
    {
        const int arr_sz = 6;
        int arr[arr_sz] = {0, 3, 6, 9, 12, 24};
        std::vector<int> steps_to_issue(&arr[0], &arr[arr_sz]);
        EXPECT(trigger_executed_correctly(file1, steps_to_issue, jobId));
    }

    // Second list of triggers
    {
        const int arr_sz = 5;
        int arr[arr_sz] = {1, 4, 5, 6, 10};
        std::vector<int> steps_to_issue(&arr[0], &arr[arr_sz]);
        EXPECT(trigger_executed_correctly(file2, steps_to_issue, jobId));
    }
}

//-----------------------------------------------------------------------------

}  // namespace test
}  // namespace multio

int main(int argc, char** argv) {
    return run_tests(argc, argv);
}