#include "hedgr.runtime/worker/image_worker.hpp"
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
boost::asio::awaitable<void> run_image(image_job job,
    model::command::event_queue& events) {
    co_await boost::asio::post(co_await boost::asio::this_coro::executor,
        boost::asio::use_awaitable);
    model::event::event fact;
    fact.kind = model::event::event_kind::image_loaded;
    fact.instance_id = job.instance_id;
    fact.account_id = job.account_id;
    fact.tenant_id = job.tenant_id;
    fact.image_id = job.image_id;
    session::transport::session_client client(std::move(job.options));
    if (const auto connected = client.connect(); !connected) {
        fact.message = connected.error().message;
    } else if (const auto resumed = client.resume(job.token, job.principal);
        !resumed) {
        fact.message = resumed.error().message;
    } else if (const auto image = client.image_by_id(job.image_id); image) {
        fact.mime_type = image->mime_type;
        fact.bytes = image->data;
    } else {
        fact.message = image.error().message;
    }
    std::fill(job.token.begin(), job.token.end(), '\0');
    job.token.clear();
    events.push(std::move(fact));
}
}
void spawn_image_read(boost::asio::io_context& context, image_job job,
    model::command::event_queue& events) {
    boost::asio::co_spawn(context, run_image(std::move(job), events),
        boost::asio::detached);
}
}
