
#include <fstream>
#include <iomanip>

#include "eckit/exception/Exceptions.h"
#include "eckit/io/FileHandle.h"
#include "eckit/io/StdFile.h"
#include "eckit/log/JSON.h"
#include "eckit/log/Log.h"
#include "eckit/mpi/Comm.h"
#include "eckit/option/CmdArgs.h"
#include "eckit/option/SimpleOption.h"

#include "multio/LibMultio.h"
#include "multio/server/MultioNemo.h"
#include "multio/server/NemoToGrib.h"
#include "multio/tools/MultioTool.h"

//----------------------------------------------------------------------------------------------------------------

class MultioReplay final : public multio::MultioTool {
public:
    MultioReplay(int argc, char** argv);

private:
    void usage(const std::string& tool) const override {
        eckit::Log::info() << std::endl << "Usage: " << tool << " [options]" << std::endl;
    }

    void init(const eckit::option::CmdArgs& args) override;

    void finish(const eckit::option::CmdArgs& args) override;

    void execute(const eckit::option::CmdArgs& args) override;

    void runClient();

    void setMetadata();
    void setDomains();
    void writeFields();

    std::vector<int> readGrid(const std::string& grid_type, size_t client_id);
    eckit::Buffer readField(const std::string& param, size_t client_id) const;

    size_t commSize() const;
    void initClient();
    void testData();

    const NemoToGrib paramMap_;
    const std::vector<std::string> parameters_{"sst", "ssu", "ssv", "ssw"};

    std::string transportType_ = "mpi";
    std::string pathToNemoData_ = "";

    int globalSize_ = 105704;
    int level_ = 1;
    int step_ = 24;
    size_t rank_ = 0;
};

//----------------------------------------------------------------------------------------------------------------

MultioReplay::MultioReplay(int argc, char** argv) : multio::MultioTool(argc, argv) {
    options_.push_back(
        new eckit::option::SimpleOption<std::string>("transport", "Type of transport layer"));
    options_.push_back(new eckit::option::SimpleOption<std::string>("path", "Path to NEMO data"));
    options_.push_back(new eckit::option::SimpleOption<long>("nbclients", "Number of clients"));
    options_.push_back(new eckit::option::SimpleOption<long>("field", "Name of field to replay"));
    options_.push_back(
        new eckit::option::SimpleOption<long>("step", "Time counter for the field to replay"));
}

void MultioReplay::init(const eckit::option::CmdArgs& args) {
    args.get("transport", transportType_);
    args.get("path", pathToNemoData_);

    args.get("step", step_);

    initClient();
}

void MultioReplay::finish(const eckit::option::CmdArgs&) {}

void MultioReplay::execute(const eckit::option::CmdArgs&) {
    runClient();

    testData();
}

void MultioReplay::runClient() {
    setMetadata();

    multio_open_connections();

    setDomains();

    writeFields();

    multio_close_connections();
}

void MultioReplay::setMetadata() {
    multio_metadata_set_string_value("category", "ocean-2d");
    multio_metadata_set_int_value("globalSize", globalSize_);
    multio_metadata_set_int_value("level", level_);
    multio_metadata_set_int_value("step", step_);
}

void MultioReplay::setDomains() {
    const std::map<std::string, std::string> grid_type = {
        {"T grid", "grid_T"}, {"U grid", "grid_U"}, {"V grid", "grid_V"}, {"W grid", "grid_W"}};

    for (auto const& grid : grid_type) {
        auto buffer = readGrid(grid.second, rank_);
        auto sz = static_cast<int>(buffer.size());

        multio_set_domain(grid.first.c_str(), buffer.data(), sz);
    }
}

void MultioReplay::writeFields() {
    for (const auto& param : parameters_) {
        auto buffer = readField(param, rank_);

        auto sz = static_cast<int>(buffer.size()) / sizeof(double);
        multio_write_field(param.c_str(), reinterpret_cast<const double*>(buffer.data()), sz,
                           false);
    }
}

std::vector<int> MultioReplay::readGrid(const std::string& grid_type, size_t client_id) {
    std::ostringstream oss;
    oss << grid_type << "_" << std::setfill('0') << std::setw(2) << client_id;

    auto grid = eckit::PathName{pathToNemoData_ + oss.str()};

    std::ifstream infile(std::string(grid.fullName()).c_str());

    std::string gtype;
    infile >> gtype;
    if (gtype != grid_type) {
        throw eckit::SeriousBug{"Wrong grid is being read: " + grid.fullName()};
    }

    std::vector<int> domain_dims;
    for (int next; infile >> next;) {
        domain_dims.push_back(next);
    }

    ASSERT(globalSize_ == (domain_dims[0] * domain_dims[1]));

    return domain_dims;
}

eckit::Buffer MultioReplay::readField(const std::string& param, size_t client_id) const {
    std::ostringstream oss;
    oss << pathToNemoData_ << param << "_" << std::setfill('0') << std::setw(2) << step_ << "_"
        << std::setfill('0') << std::setw(2) << client_id;

    auto field = eckit::PathName{oss.str()};

    eckit::Log::info() << " *** Reading path " << field << std::endl;

    eckit::FileHandle infile{field.fullName()};
    size_t bytes = infile.openForRead();

    eckit::Buffer buffer(bytes);
    infile.read(buffer.data(), bytes);

    return buffer;
}

void MultioReplay::initClient() {
    if (transportType_ != "mpi") {
        throw eckit::SeriousBug("Only MPI transport is supported for this tool");
    }

    rank_ = eckit::mpi::comm("world").rank();

    multio_init_client("oce", eckit::mpi::comm().communicator());
}

void MultioReplay::testData() {
    eckit::mpi::comm().barrier();
    if (eckit::mpi::comm().rank() != 0) {
        return;
    }

    for (const auto& param : parameters_) {
        std::ostringstream oss;
        oss << level_ << "::" << paramMap_.get(param).param << "::" << step_;

        std::string actual_file_path{oss.str()};
        std::ifstream infile_actual{actual_file_path};
        std::string actual{std::istreambuf_iterator<char>(infile_actual),
                           std::istreambuf_iterator<char>()};
        infile_actual.close();

        oss.str("");
        oss.clear();
        oss << pathToNemoData_ << param << "_" << step_ << "_reference";
        auto path = eckit::PathName{oss.str()};

        std::ifstream infile_expected{path.fullName()};
        std::string expected{std::istreambuf_iterator<char>(infile_expected),
                             std::istreambuf_iterator<char>()};

        eckit::Log::info() << " *** testData - ActualFilePath: " << actual_file_path
                           << ", expected path: " << path << std::endl;

        infile_actual.close();
        infile_expected.close();
        ASSERT(actual == expected);
        std::remove(actual_file_path.c_str());
    }
}


//----------------------------------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    MultioReplay tool(argc, argv);
    return tool.start();
}
