#include <stdlib.h>
#include <gami-manager-private.h>

typedef gpointer (*GamiPointerFinishFunc) (GamiManager *,
                                           GAsyncResult *,
                                           GError **);

static gchar *set_action_id (const gchar *action_id);

static gpointer wait_pointer_result (GamiManager *ami,
                                     GamiPointerFinishFunc finish,
                                     GError **error);
static void add_action_hook (GamiManager *manager, gchar *action_id,
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
    GSimpleAsyncResult *simple;

    g_return_if_fail (GAMI_IS_MANAGER (ami));
    g_return_if_fail (callback != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    g_assert (priv->connected);

    g_debug ("Sending GAMI command");

    action = build_action_string_valist (action_name,
                                         &action_id,
                                         first_param_name,
                                         varargs);

    while ((G_IO_STATUS_AGAIN == g_io_channel_write_chars (priv->socket,
                                                           action,
                                                           -1,
                                                           NULL,
                                                           &error)));
    if (! error)
        while (G_IO_STATUS_AGAIN == g_io_channel_flush (priv->socket,
                                                        &error));

    g_debug ("GAMI command sent");

    if (error) {
        g_simple_async_report_gerror_in_idle (G_OBJECT (ami),
                                              callback,
                                              user_data,
                                              error);
        g_error_free (error);
        g_free (action_id);
    } else {
        GamiActionHook *hook = NULL;
        simple = g_simple_async_result_new (G_OBJECT (ami),
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
