/*
 * Pidgin/libpurple Chime client plugin
 *
 * Copyright © 2017 Amazon.com, Inc. or its affiliates.
 *
 * Author: Nicola Girardi <nicola@aloc.in>
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

#include <errno.h>
#include <glib/gi18n.h>
#include <debug.h>
#include "chime.h"

// According to http://docs.aws.amazon.com/chime/latest/ug/chime-ug.pdf this is the maximum allowed size for attachments.
// (The default limit for purple_util_fetch_url() is 512 kB.)
#define ATTACHMENT_MAX_SIZE (50*1000*1000)

/*
 * Writes to the IM conversation handling the case where the user sent message
 * from other client.
 */
static void write_conversation_message(const char *from, const char *im_email,
		PurpleConnection *conn, const gchar *msg, PurpleMessageFlags flags, time_t when)
{
	if (!strcmp(from, im_email)) {
		serv_got_im(conn, im_email, msg, flags | PURPLE_MESSAGE_RECV, when);
	} else {
		PurpleAccount *account = conn->account;
		PurpleConversation *pconv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM,
										  im_email, account);
		if (!pconv) {
			pconv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, im_email);
			if (!pconv) {
				purple_debug_error("chime", "NO CONV FOR %s\n", im_email);
				return;
			}
		}
		/* Just inject a message from ourselves, avoiding any notifications */
		purple_conversation_write(pconv, NULL, msg, flags | PURPLE_MESSAGE_SEND, when);
	}
}

static void img_message(AttachmentContext *ctx, int image_id)
{
	PurpleMessageFlags flags = PURPLE_MESSAGE_IMAGES;
	gchar *msg = g_strdup_printf("<br><img id=\"%u\">", image_id);
	if (ctx->chat_id != -1) {
		serv_got_chat_in(ctx->conn, ctx->chat_id, ctx->from, flags, msg, ctx->when);
	} else {
		write_conversation_message(ctx->from, ctx->im_email, ctx->conn, msg, flags, ctx->when);
	}
	g_free(msg);
}

static void sys_message(AttachmentContext *ctx, const gchar *msg, PurpleMessageFlags flags)
{
	flags |= PURPLE_MESSAGE_SYSTEM;
	if (ctx->chat_id != -1) {
		serv_got_chat_in(ctx->conn, ctx->chat_id, "", flags, msg, time(NULL));
	} else {
		write_conversation_message(ctx->from, ctx->im_email, ctx->conn, msg, flags, time(NULL));
	}
}

static void insert_image_from_file(AttachmentContext *ctx, const gchar *path)
{
	gchar *contents;
	gsize size;
	GError *err = NULL;

	if (!g_file_get_contents(path, &contents, &size, &err)) {
		sys_message(ctx, err->message, PURPLE_MESSAGE_ERROR);
		g_error_free(err);
		return;
	}

	/* The imgstore will take ownership of the contents. */
	int img_id = purple_imgstore_add_with_id(contents, size, path);
	if (img_id == 0) {
		gchar *msg = g_strdup_printf(_("Could not make purple image from %s"), path);
		sys_message(ctx, msg, PURPLE_MESSAGE_ERROR);
		g_free(msg);
		return;
	}
	img_message(ctx, img_id);
}

typedef struct _DownloadCallbackData {
	ChimeAttachment *att;
	AttachmentContext *ctx;
	gchar *path;
} DownloadCallbackData;

static void deep_free_download_data(DownloadCallbackData *data)
{
	g_free(data->att->message_id);
	g_free(data->att->filename);
	g_free(data->att->url);
	g_free(data->att->content_type);
	g_free(data->att);
	g_free(data->ctx);
	g_free(data->path);
	g_free(data);
}

static void download_callback(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message)
{
	DownloadCallbackData *data = user_data;

	if (error_message != NULL) {
		sys_message(data->ctx, error_message, PURPLE_MESSAGE_ERROR);
		deep_free_download_data(data);
		return;
	}

	if (len <= 0 || url_text == NULL ){
		sys_message(data->ctx, _("Downloaded empty contents."), PURPLE_MESSAGE_ERROR);
		deep_free_download_data(data);
		return;
	}

	GError *err = NULL;
	if (!g_file_set_contents(data->path, url_text, len, &err)) {
		sys_message(data->ctx, err->message, PURPLE_MESSAGE_ERROR);
		g_error_free(err);
		deep_free_download_data(data);
		return;
	}

	if (g_content_type_is_a(data->att->content_type, "image/*")) {
		insert_image_from_file(data->ctx, data->path);
	} else {
		gchar *msg = g_strdup_printf(_("%s has attached <a href=\"file://%s\">%s</a>"), data->ctx->from, data->path, data->att->filename);
		sys_message(data->ctx, msg, PURPLE_MESSAGE_SYSTEM);
		g_free(msg);
	}

	deep_free_download_data(data);
}

ChimeAttachment *extract_attachment(JsonNode *record)
{
	JsonObject *robj;
	JsonNode *node;
	const gchar *msg_id, *filename, *url, *content_type;

	g_return_val_if_fail(record != NULL, NULL);
	robj = json_node_get_object(record);
	g_return_val_if_fail(robj != NULL, NULL);
	node = json_object_get_member(robj, "Attachment");
	if (!node)
		return NULL;

	g_return_val_if_fail(parse_string(record, "MessageId", &msg_id), NULL);
	g_return_val_if_fail(parse_string(node, "FileName", &filename), NULL);
	g_return_val_if_fail(parse_string(node, "Url", &url), NULL);
	g_return_val_if_fail(parse_string(node, "ContentType", &content_type), NULL);

	ChimeAttachment *att = g_new0(ChimeAttachment, 1);
	att->message_id = g_strdup(msg_id);
	att->filename = g_strdup(filename);
	att->url = g_strdup(url);
	att->content_type = g_strdup(content_type);

	return att;
}

void download_attachment(ChimeConnection *cxn, ChimeAttachment *att, AttachmentContext *ctx)
{
	const gchar *username = chime_connection_get_email(cxn);
	gchar *dir = g_build_filename(purple_user_dir(), "chime", username, "downloads", NULL);
	if (g_mkdir_with_parents(dir, 0755) == -1) {
		gchar *msg = g_strdup_printf(_("Could not make dir %s,will not fetch file/image (errno=%d, errstr=%s)"), dir, errno, g_strerror(errno));
		sys_message(ctx, msg, PURPLE_MESSAGE_ERROR);
		g_free(dir);
		g_free(msg);
		return;
	}
	DownloadCallbackData *data = g_new0(DownloadCallbackData, 1);
	data->path = g_strdup_printf("%s/%s-%s", dir, att->message_id, att->filename);
	g_free(dir);
	data->att = att;
	data->ctx = ctx;
	purple_util_fetch_url_len(att->url, TRUE, NULL, FALSE, ATTACHMENT_MAX_SIZE, download_callback, data);
}
