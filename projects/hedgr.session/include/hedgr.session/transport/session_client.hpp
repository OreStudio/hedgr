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
#ifndef HEDGR_SESSION_TRANSPORT_SESSION_CLIENT_HPP
#define HEDGR_SESSION_TRANSPORT_SESSION_CLIENT_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/state/image_cache.hpp"
#include "hedgr.session/export.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include "hedgr.session/transport/session_account.hpp"
#include "ores.marketdata.api/messaging/published_curves_operations_protocol.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "ores.nats/service/client.hpp"
#include "ores.nats/service/nats_client.hpp"
#include "ores.nats/service/subscription.hpp"

namespace hedgr::session::transport {

/**
 * @brief What a deployment says about its own setup.
 *
 * The answer to the one question that can be asked before anybody signs in, so
 * it is also the question a probe asks: a deployment that still needs its first
 * administrator is not ready, and one that has been set up is.
 */
struct bootstrap_status final {
    /**
     * @brief Whether the deployment still needs its first administrator.
     *
     * True means setup is incomplete. It is the deployment's own words, read
     * from the setting that governs whether the initial-admin screen is the
     * one a person belongs on.
     */
    bool is_in_bootstrap_mode = false;

    /** @brief Whether the deployment has a tenant that somebody set up. */
    bool has_tenant = false;

    /**
     * @brief What IAM said about its own state.
     *
     * Empty on the success path, because IAM only fills this in to explain a
     * failure.
     */
    std::string message;

    /** @brief The build the answering service runs, in full. */
    std::string version;
};

/**
 * @brief The rules a deployment says a new password must satisfy.
 *
 * The answer to the IAM read, in the deployment's own fields. It is not a
 * policy Hedgr holds: the rules live in the deployment's validator and reach a
 * screen only through that read, so a rule the deployment changes changes what
 * the screen assesses and a rule it does not state is not shown and not
 * required.
 */
struct password_policy final {
    /**
     * @brief The deployment's own verdict on whether it answered.
     *
     * A reply carrying @c false is the deployment refusing the read, and its
     * @c message says why. It is told apart from a transport failure because
     * the two are different instructions: one names work on the deployment and
     * the other a connection.
     */
    bool success = false;

    /** @brief Why the deployment refused, or empty when it did not. */
    std::string message;

    /** @brief The shortest password the deployment accepts. */
    int min_length = 0;

    /** @brief Whether an upper-case letter is required. */
    bool require_uppercase = false;

    /** @brief Whether a lower-case letter is required. */
    bool require_lowercase = false;

    /** @brief Whether a digit is required. */
    bool require_digit = false;

    /** @brief Whether a character from @c special_chars is required. */
    bool require_special = false;

    /** @brief The characters that satisfy the special-character rule. */
    std::string special_chars;
};

/**
 * @brief One tenant, as the command centre lists it.
 *
 * The deployment's own words, copied rather than translated. A status code the
 * deployment later renames is then renamed everywhere at once instead of being
 * mapped to a sentence this client would have to keep in step.
 */
struct tenant_summary final {
    /** @brief The tenant's own key, as the deployment spells it. */
    std::string id;

    /** @brief The tenant's display name. */
    std::string name;

    /** @brief The deployment's lifecycle status. */
    std::string status;

    /** @brief The deployment's tenant type. */
    std::string type;
};

/**
 * @brief One deployment, as the command centre's first screen reads it.
 *
 * The counts and the roster are one value because they are one moment. Drawing
 * them from separate reads would let the screen show a total that disagrees
 * with the rows beneath it, and a reader would have no way to tell which half
 * was stale.
 */
struct deployment_read final {
    /** @brief Every tenant the caller may see, the system tenant aside. */
    std::uint64_t tenant_total = 0;

    /** @brief Tenants whose lifecycle status is active. */
    std::uint64_t in_service = 0;

    /** @brief Tenants whose type is evaluation. */
    std::uint64_t on_evaluation = 0;

    /** @brief Tenants whose lifecycle status is bootstrapping. */
    std::uint64_t setting_up = 0;

    /** @brief Tenants whose lifecycle status is suspended. */
    std::uint64_t needing_attention = 0;

    /** @brief The first page of the roster. */
    std::vector<tenant_summary> tenants;
};

/**
 * @brief One market series as the deployment names it.
 *
 * The URI is the identity every request carries. The label is the interface's
 * reading of it, made once here so a screen does not parse URIs.
 */
struct market_series_summary final {
    /** @brief The series' own key, as the deployment spells it. */
    std::string id;

    /** @brief The canonical oresmd URI, which is the series' identity. */
    std::string oresmd_uri;

    /** @brief A person's name for the series, derived from the URI. */
    std::string label;

    /** @brief The asset class, upper-cased. */
    std::string asset_class;

    /** @brief The provider whose ids the series carries, for example ore. */
    std::string provider_code;

    /** @brief The party that owns the series. */
    std::string party_id;

    /** @brief The tenor or term the series carries, or empty. */
    std::string tenor;

    /** @brief The curve identity the series belongs to, or empty. */
    std::string curve_key;

    /** @brief The curve's name, from the URI without its term. */
    std::string curve_label;

    /** @brief The typed identity a tick read names the series by. */
    hedgr::model::domain::market_series_identity identity;

    /** @brief The schema's asset class, parsed with the identity. */
    hedgr::model::domain::market_asset asset
        = hedgr::model::domain::market_asset::unknown;
};

/** @brief One observation: when the market was observed and what it was. */
struct market_point final {
    /** @brief The observation time, in nanoseconds since the Unix epoch. */
    std::int64_t at_ns = 0;

    /** @brief The observed value, parsed from the deployment's decimal text. */
    double value = 0.0;

    /** @brief The term the observation names, or empty for a leaf series. */
    std::string term;
};

/** @brief One series' ticks as one read returned them, oldest first. */
struct market_series_read final {
    /** @brief The points of the coordinate plotted, in time order. */
    std::vector<market_point> points;

    /**
     * @brief Whether the read may be missing older ticks.
     *
     * True when a retry shortened the span or the reply held as many ticks as
     * the deployment's ceiling allows.
     */
    bool truncated = false;

    /** @brief Whether a retry shortened the span after a refusal as too large. */
    bool shortened = false;

    /** @brief The coordinate the points are of, or empty when there are none. */
    std::string coordinate;

    /** @brief How many coordinates the reply carried. */
    std::size_t coordinates = 0;

    /** @brief How many ticks the reply carried, of every coordinate. */
    std::size_t ticks = 0;

    /** @brief The start of the span the reply answers, epoch nanoseconds. */
    std::int64_t from_ns = 0;

    /** @brief The end of that span, or zero when it ran to the service clock. */
    std::int64_t to_ns = 0;

    /**
     * @brief Every point of every coordinate, each naming its own coordinate.
     *
     * A chart plots @c points, which is one coordinate's series. A term
     * structure is not: an interest-rate series carries its ladder as one
     * coordinate per term, so a read that kept only the first coordinate
     * answered with one tenor. These are the points of all of them, in time
     * order, each carrying the term its coordinate names.
     */
    std::vector<market_point> terms;
};

/**
 * @brief One composite object's shape and the object at each instant.
 *
 * The axes are declared once because they belong to the shape rather than to
 * the points: @c fields names them in order, every node holds one label per
 * field, and every instant holds one value per node, index for index. A value
 * that is empty is a hole the shape declares and no point filled.
 */
struct series_evolution final {
    /** @brief The axis names, in the order every node's coordinates use. */
    std::vector<std::string> fields;

    /** @brief The declared nodes, each with one label per field. */
    std::vector<std::vector<std::string>> nodes;

    /** @brief One instant's values, index for index with @c nodes. */
    struct instant final {
        std::int64_t as_of_ns = 0;
        std::vector<double> values;
    };

    /** @brief The instants, oldest first. */
    std::vector<instant> instants;
};

/** @brief One pillar of a published curve: its name and its value. */
struct curve_point final {
    /** @brief The pillar, as the deployment spells it ("10Y", "3M"). */
    std::string pillar;

    /** @brief The value as a number, or zero when @c numeric is false. */
    double value = 0.0;

    /** @brief Whether the deployment's text was a finite number. */
    bool numeric = false;

    /** @brief What the value means: discount, zero rate, and so on. */
    std::string value_kind;
};

/**
 * @brief One published curve at one instant.
 *
 * The value text is parsed here, at the boundary, so nothing above this layer
 * carries the deployment's decimal spelling; a pillar whose text is not a
 * finite number keeps its name with @c numeric false, because a curve with a
 * hole in it is still the curve the deployment stated.
 */
struct curve_instant final {
    /** @brief The curve's name. */
    std::string label;

    /** @brief The series the curve is published under. */
    std::string series_id;

    /** @brief The instant the curve was as of, epoch nanoseconds. */
    std::int64_t as_of_ns = 0;

    /** @brief The deployment's state word for this instant. */
    std::string state_code;

    /** @brief The pillars, in the order the deployment sent them. */
    std::vector<curve_point> points;
};

/**
 * @brief One blocking connection from one account to one instance.
 *
 * Every call here blocks until the instance answers or the request times
 * out, because the transport underneath is blocking and pretending otherwise
 * would be a lie. Concurrency is the caller's: the runtime runs each session
 * on a thread-pool thread, so a blocked call costs one pool thread rather than
 * the frame loop. Nothing in this class spawns a thread, a coroutine or an
 * executor, and nothing in it is safe to call from two threads at once.
 *
 * No exception escapes a public method: every failure, including the ones the
 * wrapped client throws, arrives as a session_error a window can render.
 */
/**
 * @brief One FX volatility surface blueprint the caller's party holds.
 *
 * A blueprint is the recipe a calibration fits: the pair, the smile model, the
 * interpolation between expiries, the output grid and the series the surface is
 * written to. A chooser lists the names; the grid it states is what a person
 * reads to know what the surface will be drawn on.
 */
struct surface_blueprint final {
    /** @brief The blueprint's identifier. */
    std::string id;

    /** @brief The name a person wrote for it, such as EURUSD-BROKER. */
    std::string name;

    /** @brief The pair's base currency. */
    std::string base_currency;

    /** @brief The pair's quote currency. */
    std::string quote_currency;

    /** @brief The model fitted at each expiry: ssvi, sabr or spline. */
    std::string smile_model;

    /** @brief The output grid's expiries as tenor codes, comma separated. */
    std::string expiries;

    /** @brief The output grid's strikes in the deployment's grammar. */
    std::string strikes;
};

/**
 * @brief One published surface, as a grid on two axes.
 *
 * The axes are the surface's two categories, and the values are row-major with
 * one row per expiry and one column per strike - the same shape a curve has,
 * with a second axis instead of a second instant. Nothing here is a point
 * carrying its own copy of an axis name.
 */
struct surface_grid final {
    /** @brief The blueprint the surface belongs to. */
    std::string blueprint_id;

    /** @brief The blueprint's name. */
    std::string name;

    /** @brief The instant the surface was published as of, epoch nanoseconds. */
    std::int64_t as_of_ns = 0;

    /** @brief The publish the answer came from. */
    std::string publish_id;

    /** @brief The expiries, shortest first. */
    std::vector<std::string> expiries;

    /** @brief The strikes, in the blueprint's order. */
    std::vector<std::string> strikes;

    /** @brief @c expiries.size() * @c strikes.size() volatilities, row-major. */
    std::vector<double> values;
};

class HEDGR_SESSION_EXPORT session_client final {
public:
    /**
     * @brief Binds a session to one instance's options.
     *
     * Nothing is opened: this only records where to connect and how.
     */
    explicit session_client(session_options options);

    /**
     * @brief Disconnects if the session is still open.
     *
     * A session owns its transport, so letting it go must not leave a socket
     * behind for the caller to remember to close.
     */
    ~session_client();

    session_client(const session_client&) = delete;
    session_client& operator=(const session_client&) = delete;

    /**
     * @brief Opens the connection to the instance.
     *
     * Idempotent: connecting a session that is already open succeeds and does
     * nothing, so a retry after a partial failure converges on one connection
     * rather than a second one.
     *
     * @return nothing on success, or why the connection could not be opened.
     */
    [[nodiscard]] result<void> connect();

    /**
     * @brief Drops the credential, then closes the connection.
     *
     * Safe to call twice, and safe on a session that was never connected, so a
     * teardown path need not ask whether there is anything to tear down.
     */
    void disconnect();

    /** @brief Whether a connection to the instance is open. */
    [[nodiscard]] bool is_connected() const noexcept;

    /** @brief Whether this session holds a credential the instance issued. */
    [[nodiscard]] bool is_authenticated() const noexcept;

    /**
     * @brief Opens a connection that presents a token issued elsewhere.
     *
     * For the reads that must not disturb the session that owns them. A
     * command centre's counts are read on a second connection so a slow
     * deployment cannot stall the session's own refresh loop, and that second
     * connection carries the token the first one was issued.
     *
     * @pre the session is connected.
     * @pre @p token is not empty.
     * @param token the bearer token a sign-in produced.
     * @param principal the name the token was issued to, for the transport's
     *        own record; it is never sent as an identity claim.
     */
    [[nodiscard]] result<void>
    resume(std::string_view token, std::string_view principal);

    /**
     * @brief Signs in, and holds the token the instance issues.
     *
     * The token is a secret kept for the session's lifetime and never
     * persisted. A refusal from the instance is reported as
     * session_errc::authentication_rejected carrying the instance's own
     * message, because that message names the reason a person must act on.
     *
     * An account with more than one party signs in with a single-use token
     * that only select_party may present; the caller follows this call with
     * select_party() in that case.
     *
     * @param principal the username presented to the instance.
     * @param password the secret presented to the instance.
     * @return who the instance says this session is, or why it refused.
     */
    [[nodiscard]] result<session_account>
    authenticate(std::string_view principal, std::string_view password);

    /**
     * @brief Replaces the held token with a fresh one from the instance.
     *
     * A token has a short life and the instance issues a replacement without
     * the password. Refreshing before it expires is what keeps a session that
     * has been open for hours reading the same as one that was just opened.
     * The replacement is held here and presented on every later request.
     *
     * @pre the session is authenticated.
     * @return the lifetime of the new token in seconds, or why the instance
     *         refused to refresh. A refusal means the session itself is over.
     */
    [[nodiscard]] result<int> refresh();

    /**
     * @brief Scopes an already signed-in session to one party.
     *
     * Exchanges the current token for one carrying the chosen party, which is
     * also what a multi-party sign-in must do before anything else.
     *
     * @pre the session is authenticated.
     * @param party_id the party to scope the session to.
     * @return nothing on success, or why the instance refused.
     */
    [[nodiscard]] result<void> select_party(std::string_view party_id);

    /**
     * @brief Moves a live session to another party.
     *
     * The instance's `switch_party` operation, which accepts an ordinary
     * session token. `select_party` is the login flow's single-use exchange
     * and only accepts the narrowly scoped token a sign-in issued, so a
     * session that is already running must come through here.
     *
     * @pre the session is authenticated.
     * @param party_id the party the session should act as.
     * @return nothing on success, or why the instance refused.
     */
    [[nodiscard]] result<void> switch_party(std::string_view party_id);

    /**
     * @brief Signs out, and gives up the token whatever the instance says.
     *
     * A sign-out is a state rather than an event: the point of it is that this
     * process no longer holds a credential, so the token is dropped even when
     * the request cannot be delivered, and signing out of a session that holds
     * no credential succeeds because that state already holds. The result
     * reports whether the instance also recorded the sign-out; a caller
     * that is signing out because the session is already broken may ignore it.
     *
     * The connection is left open, so a caller may sign in again over it.
     *
     * @return nothing when the instance recorded the sign-out.
     */
    [[nodiscard]] result<void> logout();

    /**
     * @brief Sends a request that needs no credential.
     *
     * For the calls that come before a sign-in, the sign-in among them.
     *
     * @pre the session is connected.
     * @param relative_subject the subject as the deployment names it, such as
     * "iam.v1.ops.login"; the instance's prefix is applied by the transport,
     * so pass subject()'s answer only when you want the full wire name.
     * @param json the request body, already encoded in the session's format.
     * @return the reply body.
     * @note this path has no timeout knob in the wrapped client, so it waits
     * on the transport's own default rather than on request_timeout.
     */
    [[nodiscard]] result<std::string>
    request(std::string_view relative_subject, std::string_view json) const;

    /**
     * @brief Sends a request carrying this session's bearer token.
     *
     * @pre the session is authenticated; without a credential this fails with
     * session_errc::not_authenticated rather than sending an anonymous call
     * that the instance would reject anyway.
     * @param relative_subject the subject as the deployment names it.
     * @param json the request body, already encoded in the session's format.
     * @return the reply body.
     */
    [[nodiscard]] result<std::string>
    authenticated_request(std::string_view relative_subject,
        std::string_view json) const;

    /**
     * @brief Asks the deployment whether it has been bootstrapped.
     *
     * The one call a probe makes, and the one call that needs no credential:
     * the deployment cannot ask who is calling before it has anywhere to look
     * the caller up, so this goes through the unauthenticated request path and
     * nowhere else. There is no second transport here and no second subject.
     *
     * @pre the session is connected.
     * @return what the deployment said about itself, or why it did not say.
     * A broker with no IAM behind it answers nothing at all, which arrives as
     * session_errc::services_unavailable: the broker is up, the services are
     * not, and the caller is owed that distinction.
     */
    [[nodiscard]] result<bootstrap_status> bootstrap_status() const;

    /**
     * @brief Asks the deployment what rules a new password must satisfy.
     *
     * The other call that needs no credential, and for the same reason as the
     * one above: the screen that shows the rules is the one a person signs in
     * on, so the deployment cannot ask who is calling before it has anywhere
     * to look the caller up. It goes through the unauthenticated request path
     * and nowhere else.
     *
     * @pre the session is connected.
     * @return what the deployment stated, or why it did not state it. A reply
     * it refused arrives as a value with @c success false, because that is the
     * deployment answering rather than failing to.
     */
    [[nodiscard]] result<password_policy> password_policy() const;

    /**
     * @brief Reads one deployment's tenants, as the command centre shows them.
     *
     * Four counts and one page, each count a list read that asks for its total
     * and no rows. The counts leave out the system tenant and the automation
     * tenant, which are the deployment's own machinery rather than tenants a
     * person set up, and the set of types that remain is read from the
     * deployment rather than assumed here.
     *
     * All or nothing: a read that fails returns the failure rather than a
     * partially filled value, because a screen that drew three counts and a
     * gap would be reporting a deployment state nobody measured.
     *
     * @pre the session is authenticated.
     * @return the deployment, or why it could not be read.
     */
    [[nodiscard]] result<deployment_read> deployment() const;

    /**
     * @brief Reads the market series this session may see.
     *
     * The deployment scopes the read by the session's tenant and party, so the
     * result is what this login may plot and nothing else. The label is derived
     * from each series' canonical URI, which is the only name the deployment
     * gives it.
     *
     * The list is paged until the deployment's own total is reached, because a
     * deployment holds more series than one answer carries and a truncated
     * catalogue is a chart that never finds its series.
     *
     * @param page_size the rows to ask for at a time.
     * @return the series, or why they could not be read.
     */
    [[nodiscard]] result<std::vector<market_series_summary>>
    market_series(std::uint32_t page_size = 500) const;

    /**
     * @brief The curves the deployment publishes for this session's party.
     *
     * A curve is an object the deployment calibrates and publishes, not a set
     * of series a client assembles. This is the chooser's list: a name, the
     * series the curve is published under and how many pillars it holds.
     *
     * @param limit the most curves to ask for.
     * @pre the session is authenticated.
     */
    [[nodiscard]] result<std::vector<hedgr::model::domain::published_curve>>
    published_curves(std::uint32_t limit = 50) const;

    /**
     * @brief One published curve's most recent instants, newest first.
     *
     * @param series_id the series the curve is published under.
     * @param limit how many instants to ask for.
     * @pre the session is authenticated.
     */
    [[nodiscard]] result<std::vector<curve_instant>> published_curve_sets(
        std::string_view series_id, std::uint32_t limit = 5) const;

    /**
     * @brief The FX volatility surface blueprints this session's party holds.
     *
     * Pages `refdata.v1.fx_vol_surface_blueprints.list`. The deployment scopes
     * the read to the caller's party, so this is what the login may calibrate
     * and read.
     *
     * @param limit the most blueprints to ask for.
     * @pre the session is authenticated.
     */
    [[nodiscard]] result<std::vector<surface_blueprint>>
    fx_vol_surface_blueprints(std::uint32_t limit = 100) const;

    /**
     * @brief The newest surface of one blueprint published at or before @p as_of_ns.
     *
     * Naming an expiry or a strike would slice it; this asks for the whole
     * grid. A blueprint with nothing published at or before the instant is a
     * refusal that says so, not an empty surface.
     *
     * @pre the session is authenticated.
     */
    [[nodiscard]] result<surface_grid> published_fx_vol_surface(
        std::string_view blueprint_id, std::int64_t as_of_ns) const;

    /**
     * @brief A series' observations over the last few calendar days.
     *
     * One observation per point of the series, each with its own coordinate and
     * its term. The term structure does not use this: `get_series_evolution`
     * states the object's shape outright, axes and nodes, where a window of
     * observations leaves the caller to infer which coordinate is which term.
     * This read is kept for what it is - the series' points over a few calendar
     * days - and is exercised by the live probe.
     *
     * @param uri the series' canonical oresmd URI.
     * @param provider_code the provider to read, or empty for the one that
     *        carries the series; two carriers are refused as ambiguous.
     * @param party_id whose observations, or empty for the parties this session
     *        can see.
     * @param days how many calendar days the window spans, ending today.
     * @pre the session is authenticated.
     */
    [[nodiscard]] result<std::vector<market_point>> market_series_observations(
        std::string_view uri, std::string_view provider_code,
        std::string_view party_id, int days) const;

    /**
     * @brief One composite object as instants by nodes: a curve's whole shape.
     *
     * The deployment states the object's axes and every node on them once, then
     * one value per node per instant, which is the term structure itself: the
     * axes are declared rather than inferred, so a ladder is read whole rather
     * than assembled from whichever coordinate a tick reply happened to carry.
     *
     * @param series_id the series' key, used when no identity is given.
     * @param identity the typed identity the catalogue parsed, or empty.
     * @param from_ns the first instant of the range, inclusive, epoch ns.
     * @param to_ns the last instant, inclusive; zero means now.
     * @pre the session is authenticated.
     */
    [[nodiscard]] result<series_evolution> market_series_evolution(
        std::string_view series_id,
        const hedgr::model::domain::market_series_identity& identity,
        std::int64_t from_ns, std::int64_t to_ns) const;

    /**
     * @brief Reads every market data provider and its kind.
     *
     * Pages `marketdata.v1.providers.list` until the deployment's total is
     * reached. The kinds are the deployment's words: `data_vendor`, `exchange`,
     * `broker` or `reference`. A reference provider publishes an instrument's
     * definition and never ticks.
     *
     * @pre the session is authenticated.
     * @return the providers, or why they could not be read.
     */
    [[nodiscard]] result<std::vector<hedgr::model::domain::market_provider_row>>
    list_providers() const;

    /**
     * @brief Reads the ticks of one span of one series, in one call.
     *
     * Calls the history call with the span half open: a tick at @p from_ns is
     * in and one at @p to_ns is out. A zero @p to_ns states no end, which the
     * deployment reads as its own clock. The reply may carry several
     * coordinates; the points are those of the first, and the others are
     * logged at info. A refusal is a failure whose message names the outcome
     * code, with a distinct @c session_errc for a span that is too large, has
     * no party, is inverted or names too many coordinates.
     *
     * @pre the session is authenticated.
     * @param series_id the series' key.
     * @return the points oldest first, or why they could not be read.
     */
    [[nodiscard]] result<market_series_read>
    market_series_ticks(std::string_view series_id, std::int64_t from_ns,
        std::int64_t to_ns,
        const hedgr::model::domain::market_series_identity& identity = {},
        std::chrono::milliseconds timeout = {}) const;

    /**
     * @brief Reads the first load of a series: its newest ticks for a chart.
     *
     * Widens the span from two hours until the reply holds enough points, see
     * @c read_first_load. The series' live ticks arrive on the subscription, so
     * the caller merges the two by time.
     *
     * @pre the session is authenticated.
     * @param series_id the series' key.
     * @param timeout how long the call may take, or zero for the session's own
     * request timeout. A chart read asks for less than the session's.
     * @return the points oldest first, or why they could not be read.
     */
    [[nodiscard]] result<market_series_read>
    market_series_points(std::string_view series_id,
        const hedgr::model::domain::market_series_identity& identity = {},
        std::chrono::milliseconds timeout = {}) const;

    /**
     * @brief Reads at least @p count of a series' newest ticks, if it has them.
     *
     * Starts at a minute and widens, see @c read_newest_ticks, so a read that
     * repeats on a cadence stays small. The read may hold more than @p count.
     *
     * @pre the session is authenticated.
     */
    [[nodiscard]] result<market_series_read>
    market_series_tail(std::string_view series_id, std::size_t count) const;

    /**
     * @brief One series' ticks from @p from_ns up to @p to_ns.
     *
     * Reads the span, and once on a span halved from the old end when the
     * deployment refuses it as too large. The read is marked truncated when the
     * span was shortened or the reply was at the ceiling, and then the oldest
     * ticks of the span are the ones missing.
     *
     * @pre the session is authenticated.
     * @param series_id the series' key.
     * @param from_ns the earliest instant wanted, epoch nanoseconds.
     * @param to_ns the end of the span, exclusive, epoch nanoseconds.
     * @param timeout how long the call may take, or zero for the session's own
     * request timeout.
     * @return the points oldest first, or why they could not be read.
     */
    [[nodiscard]] result<market_series_read>
    market_series_history(std::string_view series_id, std::int64_t from_ns,
        std::int64_t to_ns,
        const hedgr::model::domain::market_series_identity& identity = {},
        std::chrono::milliseconds timeout = {}) const;

    /**
     * @brief The full wire name of one of this instance's subjects.
     *
     * The prefix joined to the suffix, for a log line, a tooltip or a
     * subscription that must name the instance's namespace. Requests take the
     * suffix instead, since the transport applies the prefix itself.
     */
    [[nodiscard]] std::string subject(std::string_view suffix) const;

    /**
     * @brief Subscribes to a subject on this session's connection.
     *
     * The transport side of a long-lived read, such as the market tick stream.
     * The subject is a suffix; this method applies the instance's prefix. The
     * handle must not outlive the session, and the caller is responsible for
     * callbacks being safe on the NATS dispatch thread.
     *
     * @pre the session is connected.
     */
    [[nodiscard]] ores::nats::service::subscription
    subscribe(std::string_view subject,
        ores::nats::message_handler handler) const;

    /** @brief One image of the tenant, as the assets service has it. */
    using image_content = hedgr::model::state::image_content;
    /** @brief One cross-rate as the CRM batch read returned it. */
    struct crm_rate final {
        std::string crm_name;
        std::string base_currency_code;
        std::string quote_currency_code;
        double rate = 0.0;
        std::string status;
        std::string as_of;
        bool reciprocal = false;
        std::optional<double> delta_pct;
    };

    /** @brief Every configured CRM rate for a party, in one batch call. */
    [[nodiscard]] result<std::vector<crm_rate>> crm_rates(
        std::string_view party_id, std::string_view crm_name,
        bool reciprocal) const;

    /** @brief A currency pair's spot display convention. */
    struct pair_convention final {
        std::string pair_code;
        int decimal_places = 5;
        double pip_factor = 0.0001;
        double tick_size = 1.0;
    };

    /** @brief The enabled CRM names a party has configured. */
    [[nodiscard]] result<std::vector<std::string>> crm_names(
        std::string_view party_id) const;

    /** @brief Every pair convention the deployment publishes. */
    [[nodiscard]] result<std::vector<pair_convention>>
    currency_pair_conventions() const;

    /** @brief One currency's image identifier, empty when it has no image. */
    struct currency_image final {
        std::string currency;
        std::string image_id;
    };

    /** @brief Every currency the tenant has an image for, by ISO code. */
    [[nodiscard]] result<std::vector<currency_image>>
    currency_images() const;

    /** @brief One image of the tenant, by its identifier. */
    [[nodiscard]] result<image_content>
    image_by_id(std::string_view image_id) const;

    /** @brief Who this session is, empty until the instance says. */
    [[nodiscard]] const session_account& account() const noexcept;

    /** @brief Where this session connects and how. */
    [[nodiscard]] const session_options& options() const noexcept;

private:
    /**
     * @brief The shared body of select_party and switch_party.
     *
     * The two requests carry the same one field and are answered by the same
     * response, so the exchanging and the updating are written once; only the
     * subject and the request type differ.
     */
    template <typename Request>
    [[nodiscard]] result<void> choose_party(std::string_view party_id);

    /**
     * @brief The wrapped client, for the two request methods.
     *
     * The wrapped client qualifies its request methods non-const although a
     * request changes nothing a caller here can observe, which is what lets
     * the two request methods above be const and reachable through a const
     * session.
     */
    [[nodiscard]] ores::nats::service::nats_client& client() const;

    ores::nats::service::nats_client nats_;
    session_options options_;
    session_account account_;
    bool connected_ = false;
};

}

#endif
