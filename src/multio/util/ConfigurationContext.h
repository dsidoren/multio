/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

/// @author Philipp Geier

/// @date Aug 2022

#ifndef multio_util_ConfigurationContext_H
#define multio_util_ConfigurationContext_H

#include "eckit/config/LocalConfiguration.h"
#include "eckit/utils/Optional.h"

#include "multio/util/ConfigurationPath.h"
#include "multio/util/IteratorMapper.h"
#include "multio/util/ParameterMappings.h"
#include "multio/util/Translate.h"

#include <functional>
#include <tuple>
#include <unordered_map>


namespace multio {
namespace util {


enum class ComponentTag : unsigned
{
    Unrelated = 0,
    Client = 1,
    Server = 2,
    Plan,
    Action,
    Transport,
    Receiver,
    Dispatcher,
};


enum class LocalPeerTag : unsigned
{
    Client = 1,
    Server = 2,
};
}  // namespace util
}  // namespace multio


namespace eckit {
template <>
struct Translator<multio::util::ComponentTag, std::string> {
    std::string operator()(multio::util::ComponentTag);
};


template <>
struct Translator<multio::util::LocalPeerTag, std::string> {
    std::string operator()(multio::util::LocalPeerTag);
};
}  // namespace eckit



namespace multio {
namespace util {

class SubContextIteratorMapper;

// Explicitly create specializations for compile time checks
class ServerConfigurationContext;
class ClientConfigurationContext;


class GlobalConfCtx;  // TODO: hide internal implementation in separate namespace


struct MPIInitInfo {
    eckit::Optional<int> parentComm{};
    eckit::Optional<std::string> clientId{};
    eckit::Optional<int> defaultClientSplitColor{777};  // Hardcoded defaults may be overwritten
    eckit::Optional<int> defaultServerSplitColor{888};  // Hardcoded defaults may be overwritten
    mutable int* returnClientComm{nullptr};             // Hardcoded defaults may be overwritten
    mutable int* returnServerComm{nullptr};             // Hardcoded defaults may be overwritten
    bool allowWorldAsDefault{true};
};


class ConfigurationContext {
public:
    ConfigurationContext(const eckit::PathName& fileName, LocalPeerTag clientOrServer = LocalPeerTag::Client,
                         ComponentTag tag = ComponentTag::Unrelated);

    ConfigurationContext(const eckit::PathName& pathName, const eckit::PathName& fileName,
                         LocalPeerTag clientOrServer = LocalPeerTag::Client,
                         ComponentTag tag = ComponentTag::Unrelated);

    ConfigurationContext(const eckit::LocalConfiguration& config, const eckit::PathName& pathName,
                         const eckit::PathName& fileName, LocalPeerTag clientOrServer = LocalPeerTag::Client,
                         ComponentTag tag = ComponentTag::Unrelated);

    ConfigurationContext(const eckit::LocalConfiguration& config, const eckit::LocalConfiguration& globalConfig,
                         const eckit::PathName& pathName, const eckit::PathName& fileName,
                         LocalPeerTag clientOrServer = LocalPeerTag::Client,
                         ComponentTag tag = ComponentTag::Unrelated);

    eckit::LocalConfiguration& config();
    const eckit::LocalConfiguration& config() const;
    const eckit::LocalConfiguration& globalConfig() const;
    const eckit::PathName& pathName() const;
    const eckit::PathName& fileName() const;

    ConfigurationContext& setPathName(const eckit::PathName&);

    ComponentTag componentTag() const;
    ConfigurationContext& setComponentTag(ComponentTag);

    LocalPeerTag localPeerTag() const;
    bool isServer() const;
    bool isClient() const;

    ConfigurationContext& setLocalPeerTag(LocalPeerTag clientOrServer);
    ConfigurationContext& tagServer();
    ConfigurationContext& tagClient();
    

    using SubConfigurationContexts = MappedContainer<std::vector<eckit::LocalConfiguration>, SubContextIteratorMapper>;

    ConfigurationContext subContext(const std::string& subConfiguratinKey,
                                    ComponentTag tag = ComponentTag::Unrelated) const;
    SubConfigurationContexts subContexts(const std::string& subConfiguratinKey,
                                         ComponentTag tag = ComponentTag::Unrelated) const;
    ConfigurationContext recast(const eckit::LocalConfiguration& config,
                                ComponentTag tag = ComponentTag::Unrelated) const;
    ConfigurationContext recast(ComponentTag tag = ComponentTag::Unrelated) const;
    
    

    const eckit::Optional<MPIInitInfo>& getMPIInitInfo() const;
    eckit::Optional<MPIInitInfo>& getMPIInitInfo();
    ConfigurationContext& setMPIInitInfo(const eckit::Optional<MPIInitInfo>& val);


    // Allows loading and caching other configuration files related to the configured path
    const eckit::LocalConfiguration& getYAMLFile(const char*) const;
    const eckit::LocalConfiguration& getYAMLFile(const std::string&) const;

    const eckit::LocalConfiguration& getYAMLFile(const eckit::PathName&) const;
    
    

    const ParameterMappings& parameterMappings() const;

protected:
    ConfigurationContext(const eckit::LocalConfiguration& config, std::shared_ptr<GlobalConfCtx> globalConfCtx,
                         ComponentTag tag = ComponentTag::Unrelated);


private:
    eckit::LocalConfiguration config_;
    std::shared_ptr<GlobalConfCtx> globalConfCtx_;
    ComponentTag componentTag_;

    friend class SubContextIteratorMapper;
};




class GlobalConfCtx {
public:
    GlobalConfCtx(const eckit::PathName& fileName, LocalPeerTag clientOrServer = LocalPeerTag::Client);
    GlobalConfCtx(const eckit::PathName& pathName, const eckit::PathName& fileName,
                  LocalPeerTag clientOrServer = LocalPeerTag::Client);
    GlobalConfCtx(const eckit::LocalConfiguration& config, const eckit::PathName& pathName,
                  const eckit::PathName& fileName, LocalPeerTag clientOrServer = LocalPeerTag::Client);


    const eckit::LocalConfiguration& globalConfig() const;
    const eckit::PathName& pathName() const;
    const eckit::PathName& fileName() const;

    void setPathName(const eckit::PathName&);

    LocalPeerTag localPeerTag() const;
    void setLocalPeerTag(LocalPeerTag clientOrServer);
    

    const eckit::Optional<MPIInitInfo>& getMPIInitInfo() const;
    eckit::Optional<MPIInitInfo>& getMPIInitInfo();
    void setMPIInitInfo(const eckit::Optional<MPIInitInfo>& val);
    

    const eckit::LocalConfiguration& getYAMLFile(const char*) const;
    const eckit::LocalConfiguration& getYAMLFile(const std::string&) const;

    const eckit::LocalConfiguration& getYAMLFile(const eckit::PathName&) const;
    

    friend util::ParameterMappings;
    const ParameterMappings& parameterMappings() const;

private:
    eckit::LocalConfiguration globalConfig_;
    eckit::PathName pathName_;
    eckit::PathName fileName_;
    LocalPeerTag localPeerTag_;

    eckit::Optional<MPIInitInfo> mpiInitInfo_{MPIInitInfo{}};

    mutable std::unordered_map<std::string, eckit::LocalConfiguration> referencedConfigFiles_;
    mutable eckit::Optional<ParameterMappings> parameterMappings_;
};




class SubContextIteratorMapper {
public:
    SubContextIteratorMapper(const ConfigurationContext& confCtx, ComponentTag tag = ComponentTag::Unrelated);
    SubContextIteratorMapper(ConfigurationContext&& confCtx, ComponentTag tag = ComponentTag::Unrelated);

    ConfigurationContext operator()(const eckit::LocalConfiguration& config) const;

private:
    ConfigurationContext confCtx_;
    ComponentTag tag_;
};



namespace {
ConfigurationContext throwRecast_(const ConfigurationContext& confCtx, const std::string& key) {
    return confCtx.recast(([&]() {
        try {
            return confCtx.globalConfig().getSubConfiguration(key);
        }
        catch (...) {
            std::ostringstream oss;
            oss << "Configuration '" << key << "' not found in configuration file " << confCtx.fileName();
            std::throw_with_nested(eckit::Exception(oss.str()));
        }
    })());
}
}  // namespace



class ClientConfigurationContext : public ConfigurationContext {
public:
    ClientConfigurationContext(const ConfigurationContext& otherBase, const std::string& key = "client") :
        ConfigurationContext(throwRecast_(otherBase, key).tagClient()) {}
};




class ServerConfigurationContext : public ConfigurationContext {
public:
    ServerConfigurationContext(const ConfigurationContext& otherBase, const std::string& serverName = "server") :
        ConfigurationContext(throwRecast_(otherBase, serverName).tagServer()) {}
};


}  // namespace util
}  // namespace multio

#endif
