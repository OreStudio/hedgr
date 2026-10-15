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
#ifndef HEDGR_MODEL_DOMAIN_INSTANCE_HPP
#define HEDGR_MODEL_DOMAIN_INSTANCE_HPP

#include "hedgr.model/export.hpp"
#include <string>

namespace hedgr::model::domain {

/**
 * @brief A deployment of OreStudio that Hedgr can talk to.
 *
 * An instance is configuration rather than live state: it does not change while
 * Hedgr runs, it is persisted in the local store, and one Hedgr process holds
 * many. The NATS address and the subject prefix together identify the
 * deployment, because the same broker serves many deployments.
 *
 * The struct is a plain value with an explicit key and no owning pointers, so
 * rows join by key rather than by object graph. In particular an instance
 * carries no labels of its own: a label is an entity attached by a
 * @c labelling junction, so a free-text field here would be a second way to say
 * the same thing.
 *
 * The TLS paths are the exception to "an instance is the deployment": a client
 * certificate identifies the caller rather than the thing called, and it is
 * here because a Hedgr process holds instances on several deployments and each
 * of those deployments may want a different caller. See @c tls_ca for what an
 * empty path means.
 */
struct instance final {
    /** @brief Stable key, unique in the local store. */
    std::string id;

    /** @brief Short human label, unique in the local store. */
    std::string name;

    /**
     * @brief Key of the folder this instance sits in.
     *
     * A key rather than a handle, because an instance is a persisted value
     * while a handle addresses a row of the live table. It is what the folder
     * tree is made of: the tree holds folders, and this is the edge that puts
     * an instance under one. The instances tree's root is the fallback, so the
     * field is never empty for a row that came from the store.
     */
    std::string folder_id;

    /** @brief NATS server address, for example nats://localhost:20405. */
    std::string nats_url;

    /** @brief Subject prefix the deployment's services listen on. */
    std::string subject_prefix;

    /** @brief Wire format of the deployment's messages. */
    std::string wire_format = "msgpack";

    /**
     * @brief Path to the CA certificate this deployment is trusted through.
     *
     * The material is the instance's own, because two instances on two
     * deployments are two different client identities and one machine-wide set
     * cannot be both. A path rather than the certificate itself: the private
     * key stays a file with file permissions and never enters the store, and a
     * file the operator can rotate by hand is what an administrator already
     * expects to manage.
     *
     * An empty path means "fall back to what the environment supplied", which
     * is what keeps a row written before this field existed working and leaves
     * an operator who never fills the form in seeing no change. The fallback is
     * per path, and an empty CA is still what turns TLS off for this instance,
     * because the CA is the switch rather than the whole triple.
     */
    std::string tls_ca;

    /** @brief Path to this client's certificate; empty falls back. */
    std::string tls_cert;

    /** @brief Path to this client's private key; empty falls back. */
    std::string tls_key;

    /** @brief ISO-8601 creation timestamp. Read only when shown. */
    std::string created_at;
};

}

#endif
