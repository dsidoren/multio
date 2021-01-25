/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#include "MpiTransport.h"

#include <algorithm>
#include "unistd.h"

#include "eckit/config/Resource.h"
#include "eckit/exception/Exceptions.h"
#include "eckit/maths/Functions.h"
#include "eckit/serialisation/MemoryStream.h"

#include "multio/util/ScopedTimer.h"
#include "multio/util/print_buffer.h"

namespace multio {
namespace server {

namespace {
Message decodeMessage(eckit::Stream& stream) {
    unsigned t;
    stream >> t;

    std::string src_grp;
    eckit::Log::info() << " *** Tag = " << t << " -- Reading source group" << std::endl;
    stream >> src_grp;
    size_t src_id;
    stream >> src_id;

    std::string dest_grp;
    eckit::Log::info() << " *** Reading dest group" << std::endl;
    stream >> dest_grp;
    size_t dest_id;
    stream >> dest_id;

    std::string fieldId;
    eckit::Log::info() << " *** Reading field id" << std::endl;
    stream >> fieldId;

    unsigned long sz;
    stream >> sz;

    eckit::Buffer buffer(sz);
    stream >> buffer;

    return Message{Message::Header{static_cast<Message::Tag>(t), MpiPeer{src_grp, src_id},
                                MpiPeer{dest_grp, dest_id}, std::move(fieldId)},
                std::move(buffer)};
}
}  // namespace


BufferPool::BufferPool(size_t poolSize, size_t maxBufSize) :
    buffers_(makeBuffers(poolSize, maxBufSize)) {}

MpiBuffer& BufferPool::buffer(size_t idx) {
    return buffers_[idx];
}

size_t BufferPool::findAvailableBuffer() {
    // util::ScopedTimer scTimer{bufferWaitTiming_};

    auto it = std::end(buffers_);
    while (it == std::end(buffers_)) {
        it = std::find_if(std::begin(buffers_), std::end(buffers_), [](MpiBuffer& buf) {
            return buf.status == BufferStatus::available ||
                   (buf.status == BufferStatus::inTransit && buf.request.test());
        });
    }

    auto idx = static_cast<size_t>(std::distance(std::begin(buffers_), it));

    eckit::Log::info() << " *** -- Found available buffer with idx = " << idx << std::endl;

    return idx;
}

void BufferPool::printPoolStatus() const {
    std::for_each(std::begin(buffers_), std::end(buffers_), [](const MpiBuffer& buf) {
        const std::map<BufferStatus, std::string> st2str{{BufferStatus::available, "0"},
                                                         {BufferStatus::inUse, "1"},
                                                         {BufferStatus::inTransit, "2"}};
        eckit::Log::info() << st2str.at(buf.status);
    });
    eckit::Log::info() << std::endl;
}

MpiPeer::MpiPeer(const std::string& comm, size_t rank) : Peer{comm, rank} {}
MpiPeer::MpiPeer(Peer peer) : Peer{peer} {}

MpiTransport::MpiTransport(const eckit::Configuration& cfg) :
    Transport(cfg),
    local_{cfg.getString("group"), eckit::mpi::comm(cfg.getString("group").c_str()).rank()},
    buffer_{
        eckit::Resource<size_t>("multioMpiBufferSize;$MULTIO_MPI_BUFFER_SIZE", 64 * 1024 * 1024)},
    pool_{
        eckit::Resource<size_t>("multioMpiPoolSize;$MULTIO_MPI_POOL_SIZE", 128),
        eckit::Resource<size_t>("multioMpiBufferSize;$MULTIO_MPI_BUFFER_SIZE", 64 * 1024 * 1024)} {}

MpiTransport::~MpiTransport() {
    // TODO: check why eckit::Log::info() crashes here for the clients
    const std::size_t scale = 1024*1024;
    std::ostringstream os;
    os << " ******* " << *this
       << "\n         -- Waiting for buffers: " << bufferWaitTiming_
       << "s\n         -- Sending data:        " << bytesSent_ / scale << " MiB, " << sendTiming_
       << "s\n         -- Receiving data:      " << bytesReceived_ / scale << " MiB, " << receiveTiming_
       << "s" << std::endl;

    std::cout << os.str();
}

Message MpiTransport::receive() {

    while (not msgPack_.empty()) {
        auto msg = msgPack_.front();
        msgPack_.pop();
        return msg;
    }
    const auto& comm = eckit::mpi::comm(local_.group().c_str());

    auto status = comm.probe(comm.anySource(), comm.anyTag());

    auto sz = comm.getCount<void>(status);
    ASSERT(sz < buffer_.size());

//    ::sleep(1);

    eckit::Log::info() << " *** " << local_ << " -- Received " << sz
                       << " bytes (to put into buffer sized " << buffer_.size() << ") from source "
                       << status.source() << std::endl;

//    ::sleep(1);

    {
        util::ScopedTimer scTimer{receiveTiming_};
        comm.receive<void>(buffer_, sz, status.source(), status.tag());
    }

    bytesReceived_ += sz;

    eckit::ResizableMemoryStream stream{buffer_};

    while (stream.position() < sz) {
        auto msg = decodeMessage(stream);
        eckit::Log::info() << " *** " << local_ << " -- " << msg << std::endl;
        msgPack_.push(msg);
        eckit::Log::info() << " *** " << local_ << " -- position = " << stream.position()
                           << ", size = " << sz << std::endl;
    }

    auto msg = msgPack_.front();
    msgPack_.pop();
    return msg;
}

void MpiTransport::send(const Message& msg) {
    auto msg_tag = static_cast<int>(msg.tag());

    const auto& comm = eckit::mpi::comm(local_.group().c_str());

    pool_.printPoolStatus();

    if (streams_.find(msg.destination()) == std::end(streams_)) {
        // Find an available buffer
        auto idx = pool_.findAvailableBuffer();
        streams_.emplace(msg.destination(), pool_.buffer(idx).conent);
        streams_.at(msg.destination()).id(idx);
        pool_.buffer(idx).status = BufferStatus::inUse;
    }

    auto& strm = streams_.at(msg.destination());
    if (strm.buffer().size() < strm.position() + msg.size() + 4096) {
        util::ScopedTimer scTimer{sendTiming_};

        auto sz = static_cast<size_t>(strm.bytesWritten());
        auto dest = static_cast<int>(msg.destination().id());
        eckit::Log::info() << " *** " << local_ << " -- Sending " << sz << " bytes to destination "
                           << msg.destination() << std::endl;
        pool_.buffer(strm.id()).request = comm.iSend<void>(strm.buffer(), sz, dest, msg_tag);
        pool_.buffer(strm.id()).status = BufferStatus::inTransit;

        bytesSent_ += sz;

        streams_.erase(msg.destination());

        auto idx = pool_.findAvailableBuffer();
        streams_.emplace(msg.destination(), pool_.buffer(idx).conent);
        streams_.at(msg.destination()).id(idx);
        pool_.buffer(idx).status = BufferStatus::inUse;
    }

    eckit::Log::info() << " *** Encode " << msg << " into stream for " << msg.destination()
                       << std::endl;
    msg.encode(streams_.at(msg.destination()));
    if (msg.tag() == Message::Tag::Close) {  // Send it now
        blockingSend(msg);
    }
}

Peer MpiTransport::localPeer() const {
    return local_;
}

void MpiTransport::blockingSend(const Message& msg) {
    util::ScopedTimer scTimer{sendTiming_};
    // Shadow on purpuse
    auto& strm = streams_.at(msg.destination());
    auto sz = static_cast<size_t>(strm.bytesWritten());
    auto dest = static_cast<int>(msg.destination().id());
    eckit::Log::info() << " *** " << local_ << " -- Sending " << sz << " bytes to destination "
                       << msg.destination() << std::endl;
    eckit::mpi::comm(local_.group().c_str())
        .send<void>(strm.buffer(), sz, dest, static_cast<int>(msg.tag()));
    bytesSent_ += sz;
}

void MpiTransport::print(std::ostream& os) const {
    os << "MpiTransport(" << local_ << ")";
}

static TransportBuilder<MpiTransport> MpiTransportBuilder("mpi");

}  // namespace server
}  // namespace multio
