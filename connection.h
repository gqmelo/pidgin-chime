/*
 * Pidgin/libpurple Chime client plugin
 *
 * Copyright © 2017 Amazon.com, Inc. or its affiliates.
 *
 * Author: David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#ifndef __CHIME_CONNECTION_H__
#define __CHIME_CONNECTION_H__

#include <glib-object.h>
#include <prpl.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define CHIME_TYPE_CONNECTION (chime_connection_get_type ())
G_DECLARE_FINAL_TYPE (ChimeConnection, chime_connection, CHIME, CONNECTION, GObject)

// FIXME: hide this
struct _ChimeConnection {
	GObject parent_instance;

	PurpleConnection *prpl_conn;

	SoupSession *soup_sess;
	gchar *session_token;

	/* Messages queued for resubmission */
	GList *msg_queue;

	/* Juggernaut */
	SoupWebsocketConnection *ws_conn;
	gboolean jugg_connected;	/* For reconnecting, to abort on failed reconnect */
	gboolean jugg_resubscribe;	/* After reconnect we should use 'resubscribe' */
	gchar *ws_key;
	GHashTable *subscriptions;

	/* Buddies */
	GHashTable *contacts_by_id;
	GHashTable *contacts_by_email;
	GSList *contacts_needed;

	/* Rooms */
	GHashTable *rooms_by_id;
	GHashTable *rooms_by_name;
	GHashTable *live_chats;
	int chat_id;
	GRegex *mention_regex;

	/* Conversations */
	GHashTable *im_conversations_by_peer_id;
	GHashTable *conversations_by_id;
	GHashTable *conversations_by_name;

	/* Service config */
	JsonNode *reg_node;
	const gchar *session_id;
	const gchar *profile_id;
	const gchar *profile_channel;
	const gchar *presence_channel;

	const gchar *device_id;
	const gchar *device_channel;

	const gchar *presence_url;
	const gchar *websocket_url;
	const gchar *reachability_url;
	const gchar *profile_url;
	const gchar *contacts_url;
	const gchar *messaging_url;
	const gchar *conference_url;
};

typedef enum
{
	CHIME_CONNECTION_ERROR_NETWORK
} ChimeConnectionErrorEnum;

#define CHIME_CONNECTION_ERROR (chime_connection_error_quark())
GQuark chime_connection_error_quark (void);

ChimeConnection *chime_connection_new                        (PurpleConnection *connection);

void             chime_connection_register_device_async      (ChimeConnection    *self,
                                                              const gchar        *server,
                                                              const gchar        *token,
                                                              const gchar        *devtoken,
                                                              GCancellable       *cancellable,
                                                              GAsyncReadyCallback callback,
                                                              gpointer            user_data);

gboolean         chime_connection_register_device_finish     (ChimeConnection  *self,
                                                              GAsyncResult     *result,
                                                              GError          **error);

G_END_DECLS

#endif /* __CHIME_CONNECTION_H__ */