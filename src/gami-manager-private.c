#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gami-manager-private.h>

typedef gpointer (*GamiPointerFinishFunc) (GamiManager *,
                                           GAsyncResult *,
                                           GError **);

static gchar *set_action_id (const gchar *action_id);

static gpointer wait_pointer_result (GamiManager *ami,
                                     GamiPointerFinishFunc finish,
                                     GError **error);

static gpointer
wait_pointer_result (GamiManager *ami,
                     GamiPointerFinishFunc finish,
                     GError **error)
{
    GamiManagerPrivate *priv;
    gpointer res;

    priv = GAMI_MANAGER_PRIVATE (ami);

    while (!priv->sync_result)
        g_main_context_iteration (NULL, TRUE);

    res = finish (ami, priv->sync_result, error);
    g_object_unref (priv->sync_result);
    priv->sync_result = NULL;

    return res;
}

gboolean
wait_bool_result (GamiManager *ami,
                  GamiBoolFinishFunc finish,
                  GError **error)
{
    GamiManagerPrivate *priv;
    gboolean res;

    priv = GAMI_MANAGER_PRIVATE (ami);

    while (!priv->sync_result)
        g_main_context_iteration (NULL, TRUE);

    res = finish (ami, priv->sync_result, error);
    g_object_unref (priv->sync_result);
    priv->sync_result = NULL;

    return res;
}

gchar *
wait_string_result (GamiManager *ami,
                    GamiStringFinishFunc finish,
                    GError **error)
{
    return (gchar *) wait_pointer_result (ami,
                                          (GamiPointerFinishFunc) finish,
                                          error);
}

GHashTable *
wait_hash_result (GamiManager *ami,
                  GamiHashFinishFunc finish,
                  GError **error)
{
    return (GHashTable *) wait_pointer_result (ami,
                                               (GamiPointerFinishFunc) finish,
                                               error);
}

GSList *wait_list_result (GamiManager *ami,
                          GamiListFinishFunc finish,
                          GError **error)
{
    return (GSList *) wait_pointer_result (ami,
                                           (GamiPointerFinishFunc) finish,
                                           error);
}

static gchar *
set_action_id (const gchar *action_id)
{
    gchar *template;

    if (action_id)
        return g_strdup (action_id);

#ifndef G_OS_WIN32
    template = g_strdup_printf ("XXXXXX");
    mktemp (template);
#else
    template = g_strdup_printf ("%d", g_random_int_range (100000, 999999));
#endif

    return template;
}

gchar *
build_action_string_valist (const gchar *action,
                            gchar **action_id,
                            const gchar *first_prop_name,
                            va_list varargs)
{
    GString *result;
    const gchar *name, *value;

    result = g_string_new ("Action: ");
    g_string_append_printf (result, "%s\r\n", action);
    g_debug ("   Action: %s", action);

    name   = first_prop_name;
    while (name) {
        value = va_arg (varargs, gchar *);
        if (! g_ascii_strcasecmp (name, "actionid")) {
            *action_id = set_action_id ((const gchar *) value);
            value = *action_id;
        }
        if (value) {
            g_debug ("   %s: %s", name, value);
            g_string_append_printf (result, "%s: %s\r\n", name, value);
        }
        name = va_arg (varargs, const gchar *);
    }
    g_string_append (result, "\r\n");

    return g_string_free (result, FALSE);
}

gchar *
build_action_string (const gchar *action,
                     gchar **action_id,
                     const gchar *first_prop_name, ...)
{
    gchar *result;
    va_list varargs;

    va_start (varargs, first_prop_name);
    result = build_action_string_valist (action,
                                         action_id,
                                         first_prop_name,
                                         varargs);
    va_end (varargs);

    return result;
}

gboolean
bool_action_finish (GamiManager *ami,
                    GAsyncResult *result,
                    GamiAsyncFunc func,
                    GError **error)
{
    GSimpleAsyncResult *simple;

    g_return_val_if_fail (GAMI_IS_MANAGER (ami), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

    simple = G_SIMPLE_ASYNC_RESULT (result);
    g_warn_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (ami),
                                                    func));
    if (g_simple_async_result_propagate_error (simple, error))
        return FALSE;

    return g_simple_async_result_get_op_res_gboolean (simple);
}

static gpointer
pointer_action_finish (GamiManager *ami,
                       GAsyncResult *result,
                       GamiAsyncFunc func,
                       GError **error)
{
    GSimpleAsyncResult *simple;

    g_return_val_if_fail (GAMI_IS_MANAGER (ami), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

    simple = G_SIMPLE_ASYNC_RESULT (result);
    g_warn_if_fail (g_simple_async_result_is_valid (result,
                                                    G_OBJECT (ami),
                                                    func));
    if (g_simple_async_result_propagate_error (simple, error))
        return NULL;

    return g_simple_async_result_get_op_res_gpointer (simple);
}

gchar *
string_action_finish (GamiManager *ami,
                      GAsyncResult *result,
                      GamiAsyncFunc func,
                      GError **error)
{
    return (gchar *) pointer_action_finish (ami, result, func, error);
}

GHashTable *
hash_action_finish (GamiManager *ami,
                    GAsyncResult *result,
                    GamiAsyncFunc func,
                    GError **error)
{
    return (GHashTable *) pointer_action_finish (ami, result, func, error);
}

GSList *
list_action_finish (GamiManager *ami,
                    GAsyncResult *result,
                    GamiAsyncFunc func,
                    GError **error)
{
    return (GSList *) pointer_action_finish (ami, result, func, error);
}

void
send_action_string (const gchar *action,
                    GIOChannel *channel,
                    GError **error)
{
    GIOStatus status;

    g_assert (error == NULL || *error == NULL);

    while (G_IO_STATUS_AGAIN == (status = g_io_channel_write_chars (channel,
                                                                    action,
                                                                    -1,
                                                                    NULL,
                                                                    error)));
    if (status != G_IO_STATUS_ERROR)
        while (G_IO_STATUS_AGAIN == g_io_channel_flush (channel,
                                                        error));
}

void
setup_action_hook (GamiManager *ami,
                   GamiAsyncFunc func,
                   GHookCheckFunc handler,
                   gpointer handler_data,
                   gchar *action_id,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   GError *error)
{
    if (error) {
        g_simple_async_report_gerror_in_idle (G_OBJECT (ami),
                                              callback,
                                              user_data,
                                              error);
        g_error_free (error);
        g_free (action_id);
    } else {
        GamiManagerPrivate *priv;
        GHook *action_hook;
        GamiHookData *hook_data;

        priv = GAMI_MANAGER_PRIVATE (ami);
        GSimpleAsyncResult *simple = g_simple_async_result_new (G_OBJECT (ami),
                                                                callback,
                                                                user_data,
                                                                func);
        action_hook = g_hook_alloc (&priv->packet_hooks);
        hook_data = gami_hook_data_new (G_ASYNC_RESULT (simple), action_id, handler_data);
        action_hook->data = hook_data;
        action_hook->func = handler;
        g_hook_append (&priv->packet_hooks, action_hook);
    }
}

static void send_async_action_valist (GamiManager *ami,
                               GamiAsyncFunc func,
                               GHookCheckFunc handler,
                               gpointer handler_data,
                               GAsyncReadyCallback callback,
                               gpointer user_data,
                               const gchar *action_name,
                               const gchar *first_param_name,
                               va_list varargs);

static void
send_async_action_valist (GamiManager *ami,
                          GamiAsyncFunc func,
                          GHookCheckFunc handler,
                          gpointer handler_data,
                          GAsyncReadyCallback callback,
                          gpointer user_data,
                          const gchar *action_name,
                          const gchar *first_param_name,
                          va_list varargs)
{
    GamiManagerPrivate *priv;
    gchar *action, *action_id = NULL;
    GError *error = NULL;

    g_return_if_fail (GAMI_IS_MANAGER (ami));
    g_return_if_fail (callback != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    g_assert (priv->connected);

    g_debug ("Sending GAMI command");

    action = build_action_string_valist (action_name,
                                         &action_id,
                                         first_param_name,
                                         varargs);

    send_action_string (action, priv->socket, &error);

    g_debug ("GAMI command sent");

    setup_action_hook (ami,
                       func,
                       handler,
                       handler_data,
                       action_id,
                       callback,
                       user_data,
                       error);
    g_free (action);
}

void
send_async_action (GamiManager *ami,
                   GamiAsyncFunc func,
                   GHookCheckFunc handler,
                   gpointer handler_data,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   const gchar *action_name,
                   const gchar *first_param_name,
                   ...)
{
    va_list varargs;

    va_start (varargs, first_param_name);
    send_async_action_valist (ami,
                              func,
                              handler,
                              handler_data,
                              callback,
                              user_data,
                              action_name,
                              first_param_name,
                              varargs);
    va_end (varargs);
}

gboolean
dispatch_ami (GIOChannel *chan, GIOCondition cond, GamiManager *ami)
{
    GamiManagerPrivate *priv;
    GIOStatus           status = G_IO_STATUS_NORMAL;

    priv = GAMI_MANAGER_PRIVATE (ami);

    if (cond & (G_IO_IN | G_IO_PRI)) {
        static gsize  buffer_size = 0;
        static gchar *response    = NULL;
        GError       *error       = NULL;

        do {
            gsize mem_avail,
                  bytes_read,
                  offset;

            if (! response) {
                buffer_size = g_io_channel_get_buffer_size (chan);
                response = g_malloc0 (buffer_size);
            }

            offset = strlen (response);
            mem_avail = buffer_size - offset;
            if (mem_avail < g_io_channel_get_buffer_size (chan)) {
                gsize mem_new;

                mem_new = g_io_channel_get_buffer_size (chan) - mem_avail;
                buffer_size += mem_new;
                if (response)
                    response = g_realloc (response, buffer_size);
                else
                    response = g_malloc0 (buffer_size);
            }

            status = g_io_channel_read_chars (chan,
                                              response + offset,
                                              buffer_size,
                                              &bytes_read,
                                              &error);
            response [offset + bytes_read] = '\0';

        } while (status == G_IO_STATUS_NORMAL);

        if (*response) {
            gchar **packets,
                  **packet;
            gchar  *shift = NULL;

            packets = g_strsplit (response, "\r\n\r\n", -1);
            for (packet = packets; g_strv_length (packet) > 1; packet++)
                g_queue_push_tail (priv->packet_buffer,
                                   gami_packet_new ((const gchar *) *packet));
            g_strfreev (packets);

            shift = g_strrstr (response, "\r\n\r\n");
            if (shift) {
                if (strlen (shift + 4))
                    g_memmove (response, shift + 4, strlen (shift + 4));
                else {
                    g_free (response);
                    buffer_size = 0;
                    response = NULL;
                }
            }
        }

        if (status == G_IO_STATUS_ERROR) {
            g_warning ("An error occurred during package reception%s%s\n",
                       error ? ": " : "",
                       error ? error->message : "");
            if (error)
                g_error_free (error);
        }

        if (! g_queue_is_empty (priv->packet_buffer))
            g_timeout_add (0, (GSourceFunc) process_packets, ami);
    }

    if (cond & (G_IO_HUP | G_IO_ERR) || status == G_IO_STATUS_EOF) {
        priv->connected = FALSE;
        //g_signal_emit (ami, signals [DISCONNECTED], 0);
        //g_idle_add ((GSourceFunc) reconnect_socket, ami);

        return FALSE;
    }

    return TRUE;
}

void
set_current_packet (GHook *hook, gpointer packet)
{
    GamiHookData *data;

    data = (GamiHookData *) hook->data;
    data->packet = (GamiPacket *) packet;
}

gboolean
process_packets (GamiManager *mgr)
{
    GamiManagerPrivate *priv;
    GamiPacket         *packet;

    priv = GAMI_MANAGER_PRIVATE (mgr);

    if (! (packet = g_queue_pop_head (priv->packet_buffer)))
        return FALSE;

    g_hook_list_marshal (&priv->packet_hooks,
                         FALSE,
                         set_current_packet,
                         packet);
    g_hook_list_invoke_check (&priv->packet_hooks,
                              FALSE);
    gami_packet_free (packet);

    return ! g_queue_is_empty (priv->packet_buffer);
}

void
set_sync_result (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GamiManager        *ami;
    GamiManagerPrivate *priv;

    ami = GAMI_MANAGER (source);
    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->sync_result = g_object_ref (result);
}

gboolean
reconnect_socket (GamiManager *ami)
{
    GamiManagerPrivate *priv;
    GError *error = NULL;

    priv = GAMI_MANAGER_PRIVATE (ami);

    close (g_io_channel_unix_get_fd (priv->socket));
    g_io_channel_shutdown (priv->socket, TRUE, NULL);
    g_io_channel_unref (priv->socket);

    return ! gami_manager_connect (ami, &error); /* try again if connection
                                                    failed */
}

GamiPacket *
gami_packet_new (const gchar *raw_text)
{
    GamiPacket *pkt;

    pkt = g_new0 (GamiPacket, 1);
    pkt->raw = g_strdup (raw_text);
    pkt->parsed = NULL;
    pkt->handled = FALSE;

    return pkt;
}

void
gami_packet_free (GamiPacket *packet)
{
    g_return_if_fail (packet != NULL);

    if (packet->parsed)
        g_hash_table_unref (packet->parsed);
    g_free (packet->raw);
    g_free (packet);
}

GamiHookData *
gami_hook_data_new (GAsyncResult *result,
                    gchar *action_id,
                    gpointer handler_data)
{
    GamiHookData *data;

    data = g_new0 (GamiHookData, 1);
    data->packet = NULL;
    data->result = result;
    data->action_id = action_id;
    data->handler_data = handler_data;

    return data;
}

void
gami_hook_data_free (GamiHookData *data)
{
    if (data->packet)
        gami_packet_free (data->packet);
    if (data->result)
        g_object_unref (data->result);
    if (data->action_id)
        g_free (data->action_id);
    /* FIXME: handler_data ? */
}

void
free_list_result (GSList *list)
{
    g_slist_foreach (list, (GFunc) g_hash_table_unref, NULL);
    g_slist_free (list);
}

/* hook functions */

/* parse raw packet string into hash table */
gboolean
parse_packet (gpointer data)
{
    GamiPacket *pkt;
    gchar **lines,
          **line;

    pkt = ((GamiHookData *) data)->packet;

    g_return_val_if_fail (pkt->raw != NULL, TRUE);
    g_return_val_if_fail (pkt->parsed == NULL, TRUE);

    pkt->parsed = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         g_free);

    g_debug ("Parsing packet string");
    lines = g_strsplit (pkt->raw, "\r\n", -1);
    for (line = lines; *line; line++) {
        gchar **tokens;

        tokens = g_strsplit (*line, ": ", 2);
        if (g_strv_length (tokens) == 2) {
            gchar *key, *value;

            key = g_strdup (tokens [0]);
            value = g_strdup (tokens [1]);

            g_hash_table_insert (pkt->parsed, key, value);
            g_debug ("   %s: %s", key, value);
        }
        g_strfreev (tokens);
    }
    g_strfreev (lines);
    g_debug ("Packet string parsed");

    return TRUE;
}

/* emit event */
gboolean
emit_event (gpointer data)
{
    GamiManager *ami;
    GHashTable  *pkt;

    pkt = ((GamiHookData *) data)->packet->parsed;
    ami = (GamiManager *) ((GamiHookData *) data)->handler_data;

    g_return_val_if_fail (pkt != NULL, TRUE);
    g_return_val_if_fail (ami != NULL && GAMI_IS_MANAGER (ami), TRUE);

    if (g_hash_table_lookup (pkt, "Response")
        || g_hash_table_lookup (pkt, "ActionID"))
        return TRUE;

    g_signal_emit (ami, signals [EVENT], 0, pkt);

    return TRUE;
}

gboolean
bool_hook (gpointer data)
{
    GHashTable *pkt;
    gchar *response, *action_id, *message;
    GSimpleAsyncResult *simple;
    gboolean success;

    pkt = ((GamiHookData *) data)->packet->parsed;

    g_return_val_if_fail (pkt != NULL, TRUE);

    response = g_hash_table_lookup (pkt, "Response");

    g_return_val_if_fail (response, TRUE);

    action_id = g_hash_table_lookup (pkt, "ActionID");
    if (action_id
        && g_strcmp0 (action_id, ((GamiHookData *) data)->action_id))
        return TRUE;

    success = ! g_strcmp0 (response, ((GamiHookData *) data)->handler_data);
    message = g_hash_table_lookup (pkt, "Message");

    simple = (GSimpleAsyncResult *) ((GamiHookData *) data)->result;

    if (success)
        g_simple_async_result_set_op_res_gboolean (simple, success);
    else
        g_simple_async_result_set_error (simple,
                                         GAMI_ERROR,
                                         GAMI_ERROR_FAILED,
                                         message ?  message : "Action failed");

    g_simple_async_result_complete_in_idle (simple);

    return FALSE;
}

gboolean
string_hook (gpointer data)
{
    GHashTable *pkt;
    gchar *response, *result, *action_id, *message;
    GSimpleAsyncResult *simple;

    pkt = ((GamiHookData *) data)->packet->parsed;

    g_return_val_if_fail (pkt != NULL, TRUE);

    response = g_hash_table_lookup (pkt, "Response");

    g_return_val_if_fail (response, TRUE);

    action_id = g_hash_table_lookup (pkt, "ActionID");
    if (action_id
        && g_strcmp0 (action_id, ((GamiHookData *) data)->action_id))
        return TRUE;

    result = g_hash_table_lookup (pkt, ((GamiHookData *) data)->handler_data);
    message = g_hash_table_lookup (pkt, "Message");

    simple = (GSimpleAsyncResult *) ((GamiHookData *) data)->result;

    if (! g_strcmp0 (g_hash_table_lookup (pkt, "Response"), "Success")
        && result)
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (result),
                                                   g_free);
    else
        g_simple_async_result_set_error (simple,
                                         GAMI_ERROR,
                                         GAMI_ERROR_FAILED,
                                         message ?  message : "Action failed");
    return FALSE;
}

gboolean
hash_hook (gpointer data)
{
    GHashTable *pkt;
    gchar *response, *action_id, *message;
    GSimpleAsyncResult *simple;

    pkt = ((GamiHookData *) data)->packet->parsed;

    g_return_val_if_fail (pkt != NULL, TRUE);

    response = g_hash_table_lookup (pkt, "Response");

    g_return_val_if_fail (response, TRUE);

    action_id = g_hash_table_lookup (pkt, "ActionID");
    if (action_id
        && g_strcmp0 (action_id, ((GamiHookData *) data)->action_id))
        return TRUE;

    message = g_hash_table_lookup (pkt, "Message");

    simple = (GSimpleAsyncResult *) ((GamiHookData *) data)->result;

    if (! g_strcmp0 (g_hash_table_lookup (pkt, "Response"), "Success")) {
        GDestroyNotify hash_free = (GDestroyNotify) g_hash_table_unref;

        g_hash_table_remove (pkt, "Response");
        g_hash_table_remove (pkt, "Message");
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_hash_table_ref (pkt),
                                                   hash_free);
    } else
        g_simple_async_result_set_error (simple,
                                         GAMI_ERROR,
                                         GAMI_ERROR_FAILED,
                                         message ?  message : "Action failed");
    return FALSE;
}

gboolean
list_hook (gpointer data)
{
    GHashTable *pkt;
    gchar *response, *action_id;
    GSimpleAsyncResult *simple;

    pkt = ((GamiHookData *) data)->packet->parsed;

    g_return_val_if_fail (pkt != NULL, TRUE);

    action_id = g_hash_table_lookup (pkt, "ActionID");
    if (action_id
        && g_strcmp0 (action_id, ((GamiHookData *) data)->action_id))
        return TRUE;

    simple = (GSimpleAsyncResult *) ((GamiHookData *) data)->result;

    if ((response = g_hash_table_lookup (pkt, "Response"))) {
        gchar *message;
        gboolean success;

        success = ! g_strcmp0 (response, "Success");
        message = g_hash_table_lookup (pkt, "Message");

        if (success) {
            return TRUE;
        } else {
            g_simple_async_result_set_error (simple,
                                             GAMI_ERROR,
                                             GAMI_ERROR_FAILED,
                                             message ? message
                                                     : "Action failed");
            return FALSE;
        }

    } else {
        GSList *list;
        gchar *event;
        gboolean finished;
        GDestroyNotify list_free = (GDestroyNotify) free_list_result;

        event = g_hash_table_lookup (pkt, "Event");
        list = (GSList *) g_simple_async_result_get_op_res_gpointer (simple);
        finished = ! g_strcmp0 (event, ((GamiHookData *) data)->handler_data);

        if (! finished) {
            g_hash_table_remove (pkt, "Event");
            list = g_slist_prepend (list, g_hash_table_ref (pkt));
            g_simple_async_result_set_op_res_gpointer (simple, list, list_free);
        } else {
            g_simple_async_result_set_op_res_gpointer (simple,
                                                       g_slist_reverse (list),
                                                       list_free);
            g_simple_async_result_complete_in_idle (simple);
        }

        return ! finished;
    }
}
