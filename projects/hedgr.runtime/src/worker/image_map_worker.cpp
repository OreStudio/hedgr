#include "hedgr.runtime/worker/image_map_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <algorithm>
#include <utility>
namespace hedgr::runtime::worker {
namespace {
boost::asio::awaitable<void> run_map(image_map_job job,
    model::command::event_queue& events) {
    co_await boost::asio::post(co_await boost::asio::this_coro::executor,
        boost::asio::use_awaitable);
    model::event::event fact;
    fact.kind = model::event::event_kind::currency_image_map;
    fact.instance_id = job.instance_id;
    fact.account_id = job.account_id;
    fact.tenant_id = job.tenant_id;
    session::transport::session_client client(std::move(job.options));
    if (const auto connected = client.connect(); !connected) {
        fact.message = connected.error().message;
    } else if (const auto resumed = client.resume(job.token, job.principal);
        !resumed) {
        fact.message = resumed.error().message;
    } else if (const auto images = client.currency_images(); images) {
        for (const auto& image : *images) {
            fact.image_codes.push_back(image.currency);
            fact.image_ids.push_back(image.image_id);
        }
    } else {
        fact.message = images.error().message;
    }
    std::fill(job.token.begin(), job.token.end(), '\0');
    job.token.clear();
    events.push(std::move(fact));
}
}
void spawn_image_map_read(boost::asio::io_context& context, image_map_job job,
    model::command::event_queue& events) {
    boost::asio::co_spawn(context, run_map(std::move(job), events),
        boost::asio::detached);
}
}
