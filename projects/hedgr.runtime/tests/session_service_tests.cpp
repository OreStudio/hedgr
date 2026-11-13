/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "hedgr.runtime/service/session_service.hpp"
#include "hedgr.runtime/worker/read_request.hpp"
#include "hedgr.runtime/worker/session_actor.hpp"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace service = hedgr::runtime::service;
namespace worker = hedgr::runtime::worker;

/** @brief What a test keeps of the actor it handed out. */
struct actor_state final {
    worker::session_actor_options options;
    std::vector<worker::read_request> reads;
    int stops = 0;
    bool finished = false;
    bool destroyed = false;
    bool accept_reads = true;

    /** @brief How many times the session was asked to act as a party. */
    int party_asks = 0;

    /** @brief The last party the session was asked to act as. */
    std::string party;
};

/** @brief An actor that answers nothing and records what it was asked. */
class fake_actor final : public worker::tick_feed {
public:
    explicit fake_actor(std::shared_ptr<actor_state> state)
        : state_(std::move(state)) {}

    ~fake_actor() override { state_->destroyed = true; }

    void request_stop() override {
        ++state_->stops;
        state_->accept_reads = false;
    }

    [[nodiscard]] bool finished() const noexcept override {
        return state_->finished;
    }

    void set_token(std::string) override {}

    void select_party(std::string party_id) override {
        ++state_->party_asks;
        state_->party = std::move(party_id);
    }

    void watch(model::state::series_handle, std::string) override {}

    void unwatch(model::state::series_handle) override {}

    std::size_t drain(
        std::vector<model::state::series_bucket>&) override {
        return 0;
    }

    bool submit_read(worker::read_request request) override {
        if (!state_->accept_reads)
            return false;
        state_->reads.push_back(std::move(request));
        return true;
    }

private:
    std::shared_ptr<actor_state> state_;
};

/** @brief A service whose actors are fakes, and what they saw. */
struct world final {
    world() {
        service = std::make_unique<service::session_service>(events,
            [this](worker::session_actor_options options)
                -> std::unique_ptr<worker::tick_feed> {
                auto state = std::make_shared<actor_state>();
                state->options = std::move(options);
                actors.push_back(state);
                return std::make_unique<fake_actor>(state);
            });
    }

    [[nodiscard]] worker::session_actor_options options(
        std::string instance_id = "i-1",
        std::string account_id = "a-1") const {
        worker::session_actor_options result;
        result.key = model::domain::session_key{std::move(instance_id),
            std::move(account_id)};
        result.tenant_id = "t-1";
        result.token = "a-token";
        return result;
    }

    model::command::event_queue events;
    std::vector<std::shared_ptr<actor_state>> actors;
    std::unique_ptr<service::session_service> service;
};

}

TEST_CASE("an actor is opened for a session and found by it", "[sessions]") {
    world w;
    const auto opened = w.service->open(w.options());

    REQUIRE(opened != nullptr);
    CHECK(w.service->size() == 1);
    CHECK(w.service->feed("i-1", "a-1") == opened);
    CHECK(w.service->feed("i-1", "another") == nullptr);
    CHECK(w.service->feed("another", "a-1") == nullptr);
}

TEST_CASE("an opened actor is told where its answers go", "[sessions]") {
    world w;
    static_cast<void>(w.service->open(w.options()));

    REQUIRE(w.actors.size() == 1);
    CHECK(w.actors[0]->options.events == &w.events);
    CHECK(w.actors[0]->options.key.instance_id == "i-1");
    CHECK(w.actors[0]->options.key.account_id == "a-1");
}

TEST_CASE("a read goes to the session's actor", "[sessions]") {
    world w;
    static_cast<void>(w.service->open(w.options()));

    const auto queued = w.service->submit_read("i-1", "a-1",
        worker::image_request{.image_id = "img-1"});

    CHECK(queued);
    REQUIRE(w.actors.size() == 1);
    REQUIRE(w.actors[0]->reads.size() == 1);
    const auto* request =
        std::get_if<worker::image_request>(&w.actors[0]->reads[0]);
    REQUIRE(request != nullptr);
    CHECK(request->image_id == "img-1");
}

TEST_CASE("a read for a session with no actor is refused", "[sessions]") {
    world w;

    CHECK_FALSE(w.service->submit_read("i-1", "a-1",
        worker::image_map_request{}));
    CHECK(w.actors.empty());
}

TEST_CASE("a retired actor keeps running until it has finished",
    "[sessions]") {
    world w;
    static_cast<void>(w.service->open(w.options()));
    REQUIRE(w.actors.size() == 1);

    w.service->retire("i-1", "a-1");

    CHECK(w.service->size() == 0);
    CHECK(w.service->feed("i-1", "a-1") == nullptr);
    CHECK(w.actors[0]->stops == 1);
    CHECK_FALSE(w.actors[0]->destroyed);
    CHECK_FALSE(w.service->submit_read("i-1", "a-1",
        worker::image_map_request{}));

    /* The pump reaps it once its thread has closed the connection. */
    w.actors[0]->finished = true;
    w.service->reap();
    CHECK(w.actors[0]->destroyed);
}

TEST_CASE("a session signing in again retires the actor it had",
    "[sessions]") {
    world w;
    const auto first = w.service->open(w.options());
    const auto second = w.service->open(w.options());

    REQUIRE(w.actors.size() == 2);
    CHECK(w.service->size() == 1);
    CHECK(w.service->feed("i-1", "a-1") == second);
    CHECK(first != second);
    CHECK(w.actors[0]->stops == 1);

    /* A caller that still holds the old actor holds a live object, which
     * refuses reads because it has been asked to stop. */
    CHECK_FALSE(w.actors[0]->destroyed);
    CHECK_FALSE(first->submit_read(worker::image_map_request{}));
    CHECK(w.actors[0]->reads.empty());
}

TEST_CASE("cancelling stops every actor and forgets them", "[sessions]") {
    world w;
    static_cast<void>(w.service->open(w.options("i-1", "a-1")));
    static_cast<void>(w.service->open(w.options("i-2", "a-2")));
    REQUIRE(w.actors.size() == 2);

    w.service->cancel();

    CHECK(w.service->size() == 0);
    CHECK(w.actors[0]->stops == 1);
    CHECK(w.actors[1]->stops == 1);
    CHECK(w.actors[0]->destroyed);
    CHECK(w.actors[1]->destroyed);
}

namespace {

/**
 * @brief A service that supervises, over tables the test fills itself.
 *
 * The world above is for the actor map alone, which needs no catalogue. This one
 * hands the service the instance, account and session tables and an unlocked
 * store, so a command that has to decide *what* to sign in as can be driven end
 * to end and the actor's options inspected.
 */
struct supervised_world {
    supervised_world() {
        service = std::make_unique<service::session_service>(events,
            service::session_dependencies{
                .instances = instances,
                .accounts = accounts,
                .sessions = sessions,
                .seed = {},
                .database_path = [] { return std::filesystem::path{}; },
                .sealed_credential =
                    [](std::string_view)
                    -> std::expected<std::optional<std::string>,
                        hedgr::store::sqlite::store_error> {
                    return std::optional<std::string>{"a sealed password"};
                },
                .store_lock =
                    [] { return model::domain::store_lock::unlocked; },
                .master_password = [] {
                    return model::domain::secret_string{
                        std::string{"a master password"}};
                },
                .remember = [](std::string_view, std::string) {},
                .remember_party = [this](std::string_view account_id,
                                     std::string_view party_id) {
                    remembered.push_back(
                        {std::string{account_id}, std::string{party_id}});
                },
            },
            [this](worker::session_actor_options options)
                -> std::unique_ptr<worker::tick_feed> {
                auto state = std::make_shared<actor_state>();
                state->options = std::move(options);
                actors.push_back(state);
                return std::make_unique<fake_actor>(state);
            });
    }

    model::command::event_queue events;
    model::state::instance_table instances;
    model::state::account_table accounts;
    model::state::session_table sessions;
    std::vector<std::shared_ptr<actor_state>> actors;

    /** @brief The party choices the runtime asked the store to remember. */
    std::vector<std::pair<std::string, std::string>> remembered;
    std::unique_ptr<service::session_service> service;
};

}

TEST_CASE("a remembered password still signs in as the account it belongs to",
    "[sessions]") {
    supervised_world w;

    model::domain::instance instance;
    instance.id = "i-1";
    instance.name = "instance one";
    instance.wire_format = "msgpack";
    static_cast<void>(w.instances.upsert(std::move(instance)));

    model::domain::account account;
    account.id = "a-1";
    account.instance_id = "i-1";
    account.name = "tenant admin";
    account.principal = "tenant_admin@acme.test";
    static_cast<void>(w.accounts.upsert(std::move(account)));

    /*
     * No password: this is the sign-in that means "use the one you remember",
     * so the credential travels sealed and nothing else names the account.
     */
    model::command::command cmd;
    cmd.kind = model::command::command_kind::connect_session;
    cmd.instance_id = "i-1";
    cmd.account_id = "a-1";
    cmd.principal = "tenant_admin@acme.test";

    const auto effect = w.service->connect(cmd);
    CHECK(effect.refusals.empty());
    REQUIRE(w.actors.size() == 1);
    CHECK(w.actors[0]->options.credential.principal
        == "tenant_admin@acme.test");
    CHECK(w.actors[0]->options.credential.password.empty());
    CHECK(w.actors[0]->options.sealed_credential == "a sealed password");
    CHECK(w.actors[0]->options.master_password.view() == "a master password");
}

TEST_CASE("an account with no principal is refused before anything starts",
    "[sessions]") {
    supervised_world w;

    model::domain::instance instance;
    instance.id = "i-1";
    instance.name = "instance one";
    instance.wire_format = "msgpack";
    static_cast<void>(w.instances.upsert(std::move(instance)));

    model::domain::account account;
    account.id = "a-1";
    account.instance_id = "i-1";
    account.name = "tenant admin";
    static_cast<void>(w.accounts.upsert(std::move(account)));

    model::command::command cmd;
    cmd.kind = model::command::command_kind::connect_session;
    cmd.instance_id = "i-1";
    cmd.account_id = "a-1";

    const auto effect = w.service->connect(cmd);

    REQUIRE(effect.refusals.size() == 1);
    CHECK(effect.refusals.front().message
        == "cannot connect to instance one: account 'a-1' has no principal, "
           "which is the name the instance knows it by");
    CHECK(w.actors.empty());
    CHECK_FALSE(w.sessions.find("i-1", "a-1").has_value());
}

namespace {

/**
 * @brief A world with one signed-in session whose login may act as two parties.
 *
 * The parties are the shape IAM answers with: id, unit separator, name, record
 * separator between parties. The session is placed directly rather than through
 * a command, because what is being tested is what the runtime does with the
 * answer, not how the answer is asked for.
 */
struct party_world final : supervised_world {
    party_world() {
        model::domain::session_key key{"i-1", "a-1"};
        const auto session = sessions.insert(key.instance_id, key.account_id);
        sessions.set_account_result(session, "Acme", "System Party",
            "party-system", model::domain::secret_string{"a-token"});
        sessions.set_parties(session,
            "party-system\x1f" "System Party\x1e"
            "party-uk\x1f" "Acme Corporation UK plc");
        sessions.set_status(session,
            model::domain::session_status::authenticated);
        handle = session;
    }

    /** @brief The account, with the party it remembers written into it. */
    void remember_party(std::string party_id) {
        model::domain::account row;
        row.id = "a-1";
        row.instance_id = "i-1";
        row.name = "tenant admin";
        row.principal = "tenant_admin@acme.test";
        row.party_id = std::move(party_id);
        static_cast<void>(accounts.upsert(std::move(row)));
    }

    /** @brief The fact a confirmed party change arrives as. */
    model::event::event party_changed(std::string party_id) {
        model::event::event ev;
        ev.kind = model::event::event_kind::session_party_changed;
        ev.instance_id = "i-1";
        ev.account_id = "a-1";
        ev.tenant_name = "Acme";
        ev.party_name = "Acme Corporation UK plc";
        ev.party_id = std::move(party_id);
        ev.token = model::domain::secret_string{"a-new-token"};
        return ev;
    }

    model::state::session_handle handle;
};

}

TEST_CASE("a sign-in asks for the party the account acted as last time",
    "[sessions][party]") {
    party_world w;
    w.remember_party("party-uk");

    model::event::event sign_in;
    sign_in.kind = model::event::event_kind::session_authenticated;
    sign_in.instance_id = "i-1";
    sign_in.account_id = "a-1";
    sign_in.tenant_name = "Acme";
    sign_in.tenant_id = "t-1";
    sign_in.party_name = "System Party";
    sign_in.party_id = "party-system";
    sign_in.token = model::domain::secret_string{"a-token"};
    sign_in.parties = "party-system\x1f" "System Party\x1e"
                      "party-uk\x1f" "Acme Corporation UK plc";

    const auto effect = w.service->apply(sign_in);

    REQUIRE(effect.has_value());
    REQUIRE(effect->party.has_value());
    CHECK(effect->party->party_id == "party-uk");
    CHECK(effect->party->account_id == "a-1");
    /* The deployment's own choice is what the session says until it is asked. */
    CHECK(w.sessions.party_id(w.handle) == "party-system");
}

TEST_CASE("a sign-in whose account remembers no party asks for none",
    "[sessions][party]") {
    party_world w;

    model::event::event sign_in;
    sign_in.kind = model::event::event_kind::session_authenticated;
    sign_in.instance_id = "i-1";
    sign_in.account_id = "a-1";
    sign_in.party_id = "party-system";
    sign_in.parties = "party-system\x1f" "System Party\x1e"
                     "party-uk\x1f" "Acme Corporation UK plc";

    const auto effect = w.service->apply(sign_in);

    REQUIRE(effect.has_value());
    CHECK_FALSE(effect->party.has_value());
}

TEST_CASE("a remembered party the login may no longer act as is not asked for",
    "[sessions][party]") {
    party_world w;
    w.remember_party("party-gone");

    model::event::event sign_in;
    sign_in.kind = model::event::event_kind::session_authenticated;
    sign_in.instance_id = "i-1";
    sign_in.account_id = "a-1";
    sign_in.party_id = "party-system";
    sign_in.parties = "party-system\x1f" "System Party\x1e"
                     "party-uk\x1f" "Acme Corporation UK plc";

    const auto effect = w.service->apply(sign_in);

    REQUIRE(effect.has_value());
    CHECK_FALSE(effect->party.has_value());
}

TEST_CASE("a confirmed party change is remembered for the next sign-in",
    "[sessions][party]") {
    party_world w;

    const auto effect = w.service->apply(w.party_changed("party-uk"));

    REQUIRE(effect.has_value());
    REQUIRE(w.remembered.size() == 1);
    CHECK(w.remembered.front().first == "a-1");
    CHECK(w.remembered.front().second == "party-uk");
    /* The catalogue is read again, because it is scoped by party. */
    REQUIRE(effect->reads.size() == 1);
    CHECK(effect->reads.front().what
        == service::session_effect::read_start::kind::market_catalogue);
}

TEST_CASE("choosing the party the session already acts as remembers it",
    "[sessions][party]") {
    party_world w;

    model::command::command cmd;
    cmd.kind = model::command::command_kind::select_party;
    cmd.instance_id = "i-1";
    cmd.account_id = "a-1";
    cmd.party_id = "party-system";

    const auto effect = w.service->select_party(cmd);

    /*
     * Nothing to ask: the session already acts as that party, so the actor is
     * left alone and no party-change event will arrive. The choice is still
     * remembered, which is what "this is my party" means.
     */
    CHECK(effect.refusals.empty());
    CHECK_FALSE(effect.party.has_value());
    REQUIRE(w.remembered.size() == 1);
    CHECK(w.remembered.front().second == "party-system");
    for (const auto& actor : w.actors)
        CHECK(actor->party_asks == 0);
}
