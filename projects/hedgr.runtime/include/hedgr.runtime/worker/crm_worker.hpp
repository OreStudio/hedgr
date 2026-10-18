/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#ifndef HEDGR_RUNTIME_WORKER_CRM_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_CRM_WORKER_HPP
#include "hedgr.model/command/channel.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/worker/session_task.hpp"
#include <boost/asio/io_context.hpp>
#include <string>
namespace hedgr::runtime::worker {
struct crm_job final {
    session_read session;
    std::string party_id;
    std::string crm_name;
    bool reciprocal = false;
};
HEDGR_RUNTIME_EXPORT void spawn_crm_read(boost::asio::io_context& context,
    crm_job job, model::command::event_queue& events);
}
#endif
