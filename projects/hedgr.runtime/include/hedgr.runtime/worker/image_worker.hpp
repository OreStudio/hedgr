#ifndef HEDGR_RUNTIME_WORKER_IMAGE_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_IMAGE_WORKER_HPP
#include "hedgr.model/command/channel.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <boost/asio/io_context.hpp>
#include <string>
namespace hedgr::runtime::worker {
struct image_job final {
    session::transport::session_options options;
    std::string instance_id;
    std::string account_id;
    std::string tenant_id;
    std::string token;
    std::string principal;
    std::string image_id;
};
HEDGR_RUNTIME_EXPORT void
spawn_image_read(boost::asio::io_context& context, image_job job,
    model::command::event_queue& events);
}
#endif
