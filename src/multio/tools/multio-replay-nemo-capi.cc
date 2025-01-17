#include <fstream>
#include <iomanip>

#include "eckit/config/YAMLConfiguration.h"
#include "eckit/exception/Exceptions.h"
#include "eckit/io/FileHandle.h"
#include "eckit/io/StdFile.h"
#include "eckit/log/JSON.h"
#include "eckit/log/Log.h"
#include "eckit/mpi/Comm.h"
#include "eckit/option/CmdArgs.h"
#include "eckit/option/SimpleOption.h"


#include "multio/LibMultio.h"
// #include "multio/server/MultioNemo.h"
#include "multio/api/multio_c.h"
#include "multio/server/NemoToGrib.h"
#include "multio/tools/MultioTool.h"
#include "multio/util/ConfigurationPath.h"

#include "multio/message/Metadata.h"

#define XSTRM(m) STRM(m)
#define STRM(m) #m


using multio::util::configuration_file_name;

//----------------------------------------------------------------------------------------------------------------
/**
 * \todo Using this simple handler will throuw exceptions before any error codes can be checked. Can
 * we write separate tests which test for specific expected error codes?
 */
void rethrowMaybe(int err) {
    if (err != MULTIO_SUCCESS) {
        throw eckit::Exception{"MULTIO C Exception:" + std::string{multio_error_string(err)}};
    }
}

void multio_throw_failure_handler(void*, int err) {
    rethrowMaybe(err);
}

class MultioReplayNemoCApi final : public multio::MultioTool {
public:
    MultioReplayNemoCApi(int argc, char** argv);

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

    size_t clientCount_ = 1;
    size_t serverCount_ = 0;

    multio_handle_t* multio_handle = nullptr;
};

//----------------------------------------------------------------------------------------------------------------

MultioReplayNemoCApi::MultioReplayNemoCApi(int argc, char** argv) : multio::MultioTool(argc, argv) {
    options_.push_back(
        new eckit::option::SimpleOption<std::string>("transport", "Type of transport layer"));
    options_.push_back(new eckit::option::SimpleOption<std::string>("path", "Path to NEMO data"));
    options_.push_back(new eckit::option::SimpleOption<long>("nbclients", "Number of clients"));
    options_.push_back(new eckit::option::SimpleOption<long>("field", "Name of field to replay"));
    options_.push_back(
        new eckit::option::SimpleOption<long>("step", "Time counter for the field to replay"));
}

void MultioReplayNemoCApi::init(const eckit::option::CmdArgs& args) {
    args.get("transport", transportType_);
    args.get("path", pathToNemoData_);

    args.get("step", step_);

    initClient();
}

void MultioReplayNemoCApi::finish(const eckit::option::CmdArgs&) {
    multio_delete_handle(multio_handle);
}


void MultioReplayNemoCApi::execute(const eckit::option::CmdArgs&) {
    runClient();

    testData();
}

void MultioReplayNemoCApi::runClient() {
    setMetadata();

    multio_open_connections(multio_handle);

    setDomains();

    writeFields();

    multio_close_connections(multio_handle);
}

void MultioReplayNemoCApi::setMetadata() {}

void MultioReplayNemoCApi::setDomains() {
    const std::map<std::string, std::string> grid_type = {
        {"T grid", "grid_T"}, {"U grid", "grid_U"}, {"V grid", "grid_V"}, {"W grid", "grid_W"}};

    multio_metadata_t* md = nullptr;
    multio_new_metadata(&md);

    for (auto const& grid : grid_type) {
        auto buffer = readGrid(grid.second, rank_);
        auto sz = static_cast<int>(buffer.size());
        multio_metadata_set_string_value(md, "name", grid.first.c_str());

        multio_metadata_set_string_value(md, "category", "ocean-domain-map");
        multio_metadata_set_string_value(md, "representation", "structured");
        multio_metadata_set_int_value(md, "globalSize", globalSize_);
        multio_metadata_set_bool_value(md, "toAllServers", true);

        multio_write_domain(multio_handle, md, buffer.data(), sz);
    }
    multio_delete_metadata(md);
}

void MultioReplayNemoCApi::writeFields() {

    for (const auto& param : parameters_) {
        bool is_active = false;
        multio_category_is_fully_active(multio_handle, "ocean-2d", &is_active);
        if (is_active) {
            throw eckit::SeriousBug{"Category should be not fully active: ocean-2d"};
        }
        
        multio_category_is_fully_active(multio_handle, "ocean-2d", &is_active);
        if (is_active) {
            throw eckit::SeriousBug{"Category should be not fully active: ocean-2d"};
        }
        
        multio_field_is_active(multio_handle, param.c_str(), &is_active);
        if (!is_active) {
            throw eckit::SeriousBug{"Field should be active: " + param};
        }
        
        
        auto buffer = readField(param, rank_);

        auto sz = static_cast<int>(buffer.size()) / sizeof(double);
        auto fname = param.c_str();
        
        multio_metadata_t* md = nullptr;
        multio_new_metadata(&md);

        // Set reused fields once at the beginning
        multio_metadata_set_string_value(md, "category", "ocean-2d");
        multio_metadata_set_int_value(md, "globalSize", globalSize_);
        multio_metadata_set_int_value(md, "level", level_);
        multio_metadata_set_int_value(md, "step", step_);

        // TODO: May not need to be a field's metadata
        multio_metadata_set_double_value(md, "missingValue", 0.0);
        multio_metadata_set_bool_value(md, "bitmapPresent", false);
        multio_metadata_set_int_value(md, "bitsPerValue", 16);

        multio_metadata_set_bool_value(md, "toAllServers", false);

        // Overwrite these fields in the existing metadata object
        multio_metadata_set_string_value(md, "name", fname);
        multio_metadata_set_string_value(md, "nemoParam", fname);

        multio_write_field(multio_handle, md, reinterpret_cast<const double*>(buffer.data()), sz);
        
        multio_delete_metadata(md);
    }
}

std::vector<int> MultioReplayNemoCApi::readGrid(const std::string& grid_type, size_t client_id) {
    std::ostringstream oss;
    oss << pathToNemoData_ << grid_type << "_" << std::setfill('0') << std::setw(2) << client_id;

    auto grid = eckit::PathName{oss.str()};

    std::ifstream infile(grid.fullName());

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

eckit::Buffer MultioReplayNemoCApi::readField(const std::string& param, size_t client_id) const {
    std::ostringstream oss;
    oss << pathToNemoData_ << param << "_" << std::setfill('0') << std::setw(2) << step_ << "_"
        << std::setfill('0') << std::setw(2) << client_id;

    auto field = eckit::PathName{oss.str()};

    eckit::Log::info() << " *** Reading path " << field << std::endl;

    eckit::FileHandle infile{field.fullName()};
    size_t bytes = infile.openForRead();

    eckit::Buffer buffer(bytes);
    infile.read(buffer.data(), bytes);

    infile.close();
    return buffer;
}

void MultioReplayNemoCApi::initClient() {
    if (transportType_ != "mpi") {
        throw eckit::SeriousBug("Only MPI transport is supported for this tool");
    }
    //! TODO run metadata will need to be fetched from config. Hence config is read twice - in the
    //! api and here


#if defined(EXPLICIT_MPI) || defined(INIT_BY_MPI)
    eckit::mpi::addComm("multio", eckit::mpi::comm().communicator());
    eckit::mpi::comm("multio").split(777, "multio-clients");
#endif

    
    multio_set_failure_handler(multio_throw_failure_handler, nullptr);
    

#if defined(INIT_BY_FILEPATH)
    multio_configurationcontext_t* multio_cc = nullptr;
    auto configPath = configuration_file_name();
    multio_new_configurationcontext_from_filename(&multio_cc, configPath.asString().c_str());
    multio_new_handle(&multio_handle, multio_cc);
    multio_delete_configurationcontext(multio_cc);
#endif
#if defined(INIT_BY_MPI)
    multio_configurationcontext_t* multio_cc = nullptr;
    int retComm = 0;
    multio_new_configurationcontext(&multio_cc);
    multio_conf_mpi_client_id(multio_cc, "oce");
    multio_conf_mpi_parent_comm(multio_cc, eckit::mpi::comm("multio").communicator());
    multio_conf_mpi_return_client_comm(multio_cc, &retComm);
    multio_new_handle(&multio_handle, multio_cc);
    eckit::Log::info() << " *** multio_new_handle mpi returned comm: " << retComm << std::endl;
    ASSERT(retComm != 0);
    multio_delete_configurationcontext(multio_cc);
#endif
#if defined(INIT_BY_ENV)
    multio_configurationcontext_t* multio_cc = nullptr;
    multio_new_configurationcontext(&multio_cc);
    multio_new_handle(&multio_handle, multio_cc);
    multio_delete_configurationcontext(multio_cc);
#endif
    //! Not required in new transport based api?
    // multio_init_client("oce", eckit::mpi::comm().communicator());
    // Simplate client that knows about nemo communicator and oce
    // eckit::mpi::addComm("nemo", eckit::mpi::comm().communicator());

#if defined(INIT_BY_MPI)
    ASSERT(eckit::mpi::comm("oce").communicator() == retComm);
    ASSERT(eckit::mpi::comm("multio-clients").communicator() == retComm);
#endif

#if defined(SPECIFIC_MPI_GROUP)
    eckit::Log::info() << " *** SPCEFICI_MPI_GROUP: " << XSTRM(SPECIFIC_MPI_GROUP) << std::endl;
    const eckit::mpi::Comm& group = eckit::mpi::comm(XSTRM(SPECIFIC_MPI_GROUP));
    const eckit::mpi::Comm& clients =
        eckit::mpi::comm((std::string(XSTRM(SPECIFIC_MPI_GROUP)) + "-clients").c_str());
#else
    eckit::Log::info() << " *** DEFAULT MPI GROUP: multio " << std::endl;
    const eckit::mpi::Comm& group = eckit::mpi::comm("multio");
    const eckit::mpi::Comm& clients = eckit::mpi::comm("multio-clients");
#endif

    rank_ = group.rank();
    clientCount_ = clients.size();
    serverCount_ = group.size() - clientCount_;

    eckit::Log::info() << " *** initClient - clientcount:  " << clientCount_  
                       << ", serverCount: " << serverCount_ << std::endl;
}

void MultioReplayNemoCApi::testData() {
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
    MultioReplayNemoCApi tool(argc, argv);
    return tool.start();
}
