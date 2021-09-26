#ifndef multio_MaestroEvent_H
#define multio_MaestroEvent_H

#include "multio/maestro/MaestroStatistics.h"
extern "C" {
#include <maestro.h>
}

namespace multio {

class MaestroEvent {
public:
    MaestroEvent(mstro_pool_event event, MaestroStatistics& statistics);
    MaestroEvent(const MaestroEvent&) = delete;
    MaestroEvent& operator=(const MaestroEvent&) = delete;
    MaestroEvent(MaestroEvent&&);
    MaestroEvent& operator=(MaestroEvent&&);
    ~MaestroEvent();
    bool isNull() { return event_ == nullptr; }
    operator bool() const { return event_ != nullptr; }
    mstro_pool_event raw_event() { return event_; }
private:
    void dispose();
    mstro_pool_event event_;
    MaestroStatistics& statistics_;
};

}  // namespace multio

#endif  // multio_MaestroEvent_H
