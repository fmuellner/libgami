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
/*static */void add_action_hook (GamiManager *manager, gchar *action_id,
                             GamiActionHook *hook);

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
                   GamiResponseType type,
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
        GamiActionHook *hook = NULL;
        GSimpleAsyncResult *simple = g_simple_async_result_new (G_OBJECT (ami),
                                                                callback,
                                                                user_data,
                                                                func);
        switch (type) {
            case GAMI_RESPONSE_TYPE_BOOL:
                hook = bool_action_hook_new (G_ASYNC_RESULT (simple),
                                             handler_data);
                break;
            case GAMI_RESPONSE_TYPE_STRING:
                hook = string_action_hook_new (G_ASYNC_RESULT (simple),
                                               handler_data);
                break;
            case GAMI_RESPONSE_TYPE_HASH:
                hook = hash_action_hook_new (G_ASYNC_RESULT (simple));
                break;
            case GAMI_RESPONSE_TYPE_LIST:
                hook = list_action_hook_new (G_ASYNC_RESULT (simple),
                                             handler_data);
                break;
        }
        add_action_hook (ami, action_id, hook);
    }
}

static void send_async_action_valist (GamiManager *ami,
                               GamiAsyncFunc func,
                               GamiResponseType type,
                               gpointer handler_data,
                               GAsyncReadyCallback callback,
                               gpointer user_data,
                               const gchar *action_name,
                               const gchar *first_param_name,
                               va_list varargs);

static void
send_async_action_valist (GamiManager *ami,
                          GamiAsyncFunc func,
                          GamiResponseType type,
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
                       type,
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
                   GamiResponseType type,
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
                              type,
                              handler_data,
                              callback,
                              user_data,
                              action_name,
                              first_param_name,
                              varargs);
    va_end (varargs);
}

GamiActionHook *
bool_action_hook_new (GAsyncResult *result, gpointer handler_data)
{
    GamiActionHook *hook;

    hook = g_new0 (GamiActionHook, 1);
    hook->type = GAMI_RESPONSE_TYPE_BOOL;
    hook->result = result;
    hook->handler_data = handler_data;

    return hook;
}

GamiActionHook *
string_action_hook_new (GAsyncResult *result, gpointer handler_data)
{
    GamiActionHook *hook;

    hook = g_new0 (GamiActionHook, 1);
    hook->type = GAMI_RESPONSE_TYPE_STRING;
    hook->handler_data = handler_data;
    hook->result = result;

    return hook;
}

GamiActionHook *
hash_action_hook_new (GAsyncResult *result)
{
    GamiActionHook *hook;

    hook = g_new0 (GamiActionHook, 1);
    hook->type = GAMI_RESPONSE_TYPE_HASH;
    hook->handler_data = NULL;
    hook->result = result;

    return hook;
}

GamiActionHook *
list_action_hook_new (GAsyncResult *result, gpointer handler_data)
{
    GamiActionHook *hook;

    hook = g_new0 (GamiActionHook, 1);
    hook->type = GAMI_RESPONSE_TYPE_LIST;
    hook->result = result;
    hook->handler_data = handler_data;

    return hook;
}

void
add_action_hook (GamiManager *mgr, gchar *action_id, GamiActionHook *hook)
{
    GamiManagerPrivate *priv;

    priv = GAMI_MANAGER_PRIVATE (mgr);
        
    g_hash_table_insert (priv->action_hooks, action_id, hook);
    g_hash_table_insert (priv->action_hooks, g_strdup ("current"),
                         g_memdup (hook, sizeof (GamiActionHook)));
}

gboolean
dispatch_ami (GIOChannel *chan, GIOCondition cond, GamiManager *mgr)
{
    GamiManagerPrivate *priv;
    GIOStatus           status = G_IO_STATUS_NORMAL;

    priv = GAMI_MANAGER_PRIVATE (mgr);

    if (cond & (G_IO_IN | G_IO_PRI)) {
        GError *error  = NULL;

        do {
            static GHashTable *packet = NULL;
            gchar             *line;

            status = g_io_channel_read_line (chan, &line, NULL, NULL, &error);

            if (status == G_IO_STATUS_NORMAL) {
                gchar **tokens;

                if (! packet) {
                    packet = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free, g_free);

                    g_debug ("Reveiving an GAMI packet");
                }

                tokens = g_strsplit (line, ": ", 2);
                if (g_strv_length (tokens) == 2) {
                    gchar *key, *value;

                    key = g_strdup (g_strchomp (tokens [0]));
                    value = g_strdup (g_strchomp (tokens [1]));

                    g_debug ("   %s: %s", key, value);

                    g_hash_table_insert (packet, key, value);
                }
                g_strfreev (tokens);

                if (g_str_has_prefix (line, "\r\n")) {
                    g_debug ("GAMI packet received.");

                    g_queue_push_tail (priv->buffer, packet);
                    packet = NULL;
                }
            }

            g_free (line);

        } while (g_io_channel_get_buffer_condition (chan) & G_IO_IN);

        if (status == G_IO_STATUS_ERROR) {
            g_warning ("An error occurred during package reception%s%s\n",
                       error ? ": " : "",
                       error ? error->message : "");
            if (error)
                g_error_free (error);
        }

        if (! g_queue_is_empty (priv->buffer))
            g_timeout_add (0, (GSourceFunc) process_packets, mgr);
    }

    if (cond & (G_IO_HUP | G_IO_ERR) || status == G_IO_STATUS_EOF) {

        priv->connected = FALSE;
        g_signal_emit (mgr, signals [DISCONNECTED], 0);
        g_idle_add ((GSourceFunc) reconnect_socket, mgr);

        return FALSE;

    }

    return TRUE;
}

gboolean
process_packets (GamiManager *mgr)
{
    GamiManagerPrivate *priv;
    GHashTable         *packet;
    gchar              *action_id;

    priv = GAMI_MANAGER_PRIVATE (mgr);

    if (! (packet = g_queue_pop_head (priv->buffer)))
		return FALSE;

    action_id = g_hash_table_lookup (packet, "ActionID");
    if (action_id || g_hash_table_lookup (packet, "Response")) {
        GamiActionHook *hook;

        hook = action_id ? g_hash_table_lookup (priv->action_hooks, action_id)
                         : g_hash_table_lookup (priv->action_hooks, "current");
        if (hook) {
            GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (hook->result);

            switch (hook->type) {
                gboolean    bool_resp;
                gchar      *str_resp;
                GHashTable *hash_resp;
                GSList     *list_resp;

                case GAMI_RESPONSE_TYPE_BOOL:
                    bool_resp = process_bool_response (packet,
                                                       hook->handler_data);
                    g_simple_async_result_set_op_res_gboolean (simple,
                                                               bool_resp);
                    break;
                case GAMI_RESPONSE_TYPE_STRING:
                    str_resp = process_string_response (packet,
                                                        hook->handler_data);
                    g_simple_async_result_set_op_res_gpointer (simple,
                                                               str_resp,
                                                               g_free);
                    break;
                case GAMI_RESPONSE_TYPE_HASH:
                    hash_resp = process_hash_response (packet);
                    g_simple_async_result_set_op_res_gpointer (simple,
                                                               hash_resp,
                                                               (GDestroyNotify) g_hash_table_unref);
                    break;
                case GAMI_RESPONSE_TYPE_LIST:
                    list_resp = NULL;
                    if (! process_list_response (packet, hook->handler_data,
                                                 &list_resp))
                        return ! g_queue_is_empty (priv->buffer);

                    g_simple_async_result_set_op_res_gpointer (simple,
                                                               list_resp,
                                                               (GDestroyNotify) g_slist_free);
                    break;
            }
            g_simple_async_result_complete_in_idle (simple);
            if (action_id)
                g_hash_table_remove (priv->action_hooks, action_id);
            g_hash_table_remove (priv->action_hooks, "current");
        }
    } else if (g_hash_table_lookup (packet, "Event"))
        g_signal_emit (mgr, signals [EVENT], 0, packet);

    return ! g_queue_is_empty (priv->buffer);
}

gboolean
process_bool_response (GHashTable *packet, gpointer expected)
{
    return check_response (packet, (gchar *) expected);
}

gchar *
process_string_response (GHashTable *packet, gpointer return_key)
{
    if (! check_response (packet, "Success"))
        return NULL;

    return g_strdup (g_hash_table_lookup (packet, (gchar *) return_key));
}

GHashTable *
process_hash_response (GHashTable *packet)
{
    if (! check_response (packet, "Success"))
        return NULL;

    g_hash_table_remove (packet, "Response");
    g_hash_table_remove (packet, "Message");

    return g_hash_table_ref (packet);
}

gboolean
process_list_response (GHashTable *packet, gpointer stop_event, GSList **resp)
{
    static GSList *list = NULL;
    gchar         *event;

    if (g_hash_table_lookup (packet, "Response")) {
        if (list) {              /* clean up left overs */
            g_slist_foreach (list, (GFunc) g_hash_table_destroy, NULL);
            g_slist_free (list);

            list = NULL;
        }

        if (! check_response (packet, "Success"))
            return TRUE;   /* FIXME: errors, empty lists */

        return FALSE;
    }

    event = g_hash_table_lookup (packet, "Event");
    if (! strcmp (event, (gchar *) stop_event)) {

        *resp = g_slist_reverse (list);
        list = NULL;

        return TRUE;

    } else {
        if (event)
            g_hash_table_remove (packet, "Event");

        list = g_slist_prepend (list, g_hash_table_ref (packet));
    }

    return FALSE; /* list not complete, wait for more packets */
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
check_response (GHashTable *pkt, const gchar *value)
{
    g_return_val_if_fail (pkt != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    if (g_strcmp0 (g_hash_table_lookup (pkt, "Response"), value) != 0) {
        return FALSE;
    }
    return TRUE;
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
    gchar **lines, *line;

    pkt = ((GamiHookData *) data)->packet;

    g_return_val_if_fail (pkt->raw != NULL, TRUE);
    g_return_val_if_fail (pkt->parsed == NULL, TRUE);

    pkt->parsed = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         g_free);

    lines = g_strsplit (pkt->raw, "\r\n", -1);
    for (line = *lines; line; line++) {
        gchar **tokens;

        tokens = g_strsplit (line, ": ", 2);
        if (g_strv_length (tokens) == 2) {
            gchar *key, *value;

            key = g_strdup (tokens [0]);
            value = g_strdup (tokens [1]);

            g_hash_table_insert (pkt->parsed, key, value);
        }
        g_strfreev (tokens);
    }
    g_strfreev (lines);

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
                                         G_IO_ERROR,
                                         42,
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
                                         G_IO_ERROR,
                                         42,
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
                                         G_IO_ERROR,
                                         42,
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
                                             G_IO_ERROR,
                                             42,
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

        if (finished)
            list = g_slist_reverse (list);
        else {
            g_hash_table_remove (pkt, "Event");
            list = g_slist_prepend (list, g_hash_table_ref (pkt));
        }

        g_simple_async_result_set_op_res_gpointer (simple, list, list_free);

        return finished;
    }
}
