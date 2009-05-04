/* vi: se sw=4 ts=4 tw=80 fo+=t cin cino=(0t0 : */
/*
 * LIBGAMI - Library for using the Asterisk Manager Interface with GObject
 * Copyright (C) 2008-2009 Florian Müllner
 * 
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library;  if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include <glib.h>
#include <glib-object.h>

#ifdef G_OS_WIN32
#  include <windef.h>
#  include <ws2tcpip.h>
#  define CLOSESOCKET(S) closesocket (S)
#  define G_SOCKET_IO_CHANNEL_NEW(S) g_io_channel_win32_new_socket (S)
#else
#  include <sys/socket.h>
#  include <netdb.h>
#  define CLOSESOCKET(S) close (S)
#  define G_SOCKET_IO_CHANNEL_NEW(S) g_io_channel_unix_new (S)
#endif

#ifndef SOCKET
#  define SOCKET gint
#endif

#ifndef INVALID_SOCKET
#  define INVALID_SOCKET -1
#endif

#ifndef HAVE_GAI_STRERROR
#  undef gai_strerror
#  define gai_strerror(C) ""
#endif

#include <gami-manager.h>

#include <gami-manager-private.h>

/**
 * SECTION: libgami-manager
 * @short_description: An GObject based implementation of the Asterisk
 *         Manager Interface
 * @title: GamiManager
 * @stability: Unstable
 *
 * GamiManager is an implementation of the Asterisk Manager Interface based 
 * on GObject. It supports both synchronious and asynchronious operation 
 * and integrates well with glib's signal / callback system.
 *
 * Each manager action has both a synchronous and an asynchronous version.
 * Actions return either a #gboolean, a string (#gchar *), a #GHashTable or a
 * #GSList. The synchronous function returns these directly, while the 
 * asynchronous version takes a callback parameter, which will be called with 
 * the return value once the action has finished, along the user_data pointer
 * which may be passed to asynchronous actions to access your application's
 * objects.
 * All functions support an optional ActionID as supported by the underlying
 * Asterisk Manager API. Note that an ActionID will be randomly assigned if
 * not provided as a parameter.
 * Errors are reported via an optional #GError parameter.
 * 
 * Asynchronious callbacks and events require the use of #GMainLoop (or derived
 * implementations as gtk_main().
 */

typedef struct _GamiManagerNewAsyncData GamiManagerNewAsyncData;
struct _GamiManagerNewAsyncData {
    const gchar *host;
    const gchar *port;
    GamiManagerNewAsyncFunc func;
    gpointer data;
};

enum {
    HOST_PROP = 1,
    PORT_PROP
};

G_DEFINE_TYPE (GamiManager, gami_manager, G_TYPE_OBJECT);

static gboolean gami_manager_new_async_cb (GamiManagerNewAsyncData *data);
static gboolean parse_connection_string (GamiManager *ami, GError **error);
static gchar *event_string_from_mask (GamiManager *ami, GamiEventMask mask);

/* various helper funcs */
static void join_originate_vars (gchar *key, gchar *value, GString *s);
static void join_originate_vars_legacy (gchar *key, gchar *value, GString *s);
static void join_user_event_headers (gchar *key, gchar *value, GString *s);

/*
 * Public API
 */


/**
 * gami_manager_new:
 * @host: Asterisk manager host.
 * @port: Asterisk manager port.
 *
 * This function creates an instance of %GAMI_TYPE_MANAGER connected to
 * @host:@port.
 *
 * Returns: A new #GamiManager
 */
GamiManager *
gami_manager_new (const gchar *host, const gchar *port)
{
    GamiManager *ami;
    GamiManagerPrivate *priv;
    GHook *parser, *events;
    GError  *error = NULL;

    ami = g_object_new (GAMI_TYPE_MANAGER,
                        "host", host,
                        "port", port,
                        NULL);
    priv = GAMI_MANAGER_PRIVATE (ami);

    if (! gami_manager_connect (ami, &error)) {
        g_warning ("Failed to connect to the server%s%s",
                   error ? ": " : "",
                   error ? error->message : "");

        g_object_unref (ami);
        return NULL;
    }

    parser = g_hook_alloc (&priv->packet_hooks);
    parser->func = parse_packet;
    parser->data = gami_hook_data_new (NULL, NULL, NULL);
    parser->destroy = (GDestroyNotify) gami_hook_data_free;
    g_hook_append (&priv->packet_hooks, parser);

    events = g_hook_alloc (&priv->packet_hooks);
    events->func = emit_event;
    events->data = gami_hook_data_new (NULL, NULL, ami);
    events->destroy = (GDestroyNotify) gami_hook_data_free;
    g_hook_append (&priv->packet_hooks, events);

    return ami;
}

/**
 * gami_manager_new_async:
 * @host: Asterisk manager host.
 * @port: Asterisk manager port.
 * @func: Callback function called when object has been created
 * @user_data: data to pass to @func
 *
 * Asynchronously create a #GamiManager connected to @host:@port. The new 
 * object will be passed as a parameter to @func when finished.
 */
void
gami_manager_new_async (const gchar *host, const gchar *port,
                        GamiManagerNewAsyncFunc func, gpointer user_data)
{
    GamiManagerNewAsyncData *data;
    data = g_new0 (GamiManagerNewAsyncData, 1);
    data->host = host;
    data->port = port;
    data->func = func;
    data->data = user_data;

    if (g_thread_supported ())
        g_thread_create ((GThreadFunc) gami_manager_new_async_cb, data,
                         FALSE, NULL);
    else
        g_idle_add ((GSourceFunc) gami_manager_new_async_cb, data);
}

/**
 * gami_manager_connect:
 * @ami: #GamiManager
 * @error: A location to return an error of type #GIOChannelError
 *
 * Connect #GamiManager with the Asterisk server defined by the object 
 * properties #GamiManager:host and #GamiManager:port.
 *
 * Note that it is not usually necessary to call this function, as it is called
 * by gami_manager_new() and gami_manager_new_async(). Use it only in classes 
 * inheritting from #GamiManager.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_connect (GamiManager *ami, GError **error)
{
    GamiManagerPrivate *priv;
    struct addrinfo     hints;
    struct addrinfo    *rp,
                       *result = NULL;
    int                 s;

    SOCKET sock = INVALID_SOCKET;

    g_assert (error == NULL || *error == NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if ((s = getaddrinfo (priv->host, priv->port, &hints, &result)) != 0)
        g_warning ("Error resolving host '%s': %s", priv->host,
                   gai_strerror (s));

    for (rp = result; rp; rp = rp->ai_next) {
        sock = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sock == INVALID_SOCKET)
            continue;

        if (connect (sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;   /* Bingo! */

        CLOSESOCKET (sock);
        sock = INVALID_SOCKET;
    }

    freeaddrinfo (result);

    if (rp == NULL) {
        /* Error */

        return FALSE;
    }


    priv->socket = G_SOCKET_IO_CHANNEL_NEW (sock);

    if (parse_connection_string (ami, error)) {
        priv->connected = TRUE;
        g_signal_emit (ami, signals [CONNECTED], 0);
    }

    g_io_channel_set_flags (priv->socket, G_IO_FLAG_NONBLOCK, error);
    g_io_add_watch (priv->socket, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                    (GIOFunc) dispatch_ami, ami);

    return priv->connected;
}

/*
 * Login/Logoff
 */

/**
 * gami_manager_login:
 * @ami: #GamiManager
 * @username: Username to use for authentification
 * @secret: Password to use for authentification
 * @auth_type: AuthType to use for authentification - if set to "md5",
 *             @secret is expected to contain an MD5 hash of the result 
 *             string of gami_manager_challenge() and the user's password
 * @events: Flags of type %GamiEventMask, indicating which events should be
 *          received initially. It is possible to modify this setting using the
 *          gami_manager_events() action
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Authenticate to asterisk and open a new manager session
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_login (GamiManager *ami,
                    const gchar *username,
                    const gchar *secret,
                    const gchar *auth_type,
                    GamiEventMask events,
                    const gchar *action_id,
                    GError **error)
{
    gami_manager_login_async (ami, username,
                              secret,
                              auth_type,
                              events,
                              action_id,
                              set_sync_result,
                              NULL);

    return wait_bool_result (ami, gami_manager_login_finish, error);
}

/**
 * gami_manager_login_async:
 * @ami: #GamiManager
 * @username: Username to use for authentification
 * @secret: Password to use for authentification
 * @auth_type: AuthType to use for authentification - if set to "md5",
 *             @secret is expected to contain an MD5 hash of the result
 *             string of gami_manager_challenge() and the user's password
 * @events: Flags of type %GamiEventMask, indicating which events should be
 *          received initially. It is possible to modify this setting using the
 *          gami_manager_events() action
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Authenticate to asterisk and open a new manager session
 */
void
gami_manager_login_async (GamiManager *ami,
                          const gchar *username,
                          const gchar *secret,
                          const gchar *auth_type,
                          GamiEventMask events,
                          const gchar *action_id,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    gchar    *event_str;

    g_return_if_fail (username != NULL && secret != NULL);

    event_str = event_string_from_mask (ami, events);
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_login_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "Login",
                       "AuthType", auth_type,
                       "Username", username,
                       auth_type ? "Key" : "Secret", secret,
                       "Events", event_str,
                       "ActionID", action_id,
                       NULL);
    g_free (event_str);
}

/**
 * gami_manager_login_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_login_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_login_finish (GamiManager *ami,
                           GAsyncResult *result,
                           GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_login_async,
                               error);
}


/**
 * gami_manager_logoff:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Close the manager session and disconnect from asterisk
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_logoff (GamiManager *ami,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_logoff_async (ami, action_id, set_sync_result, NULL);

    return wait_bool_result (ami, gami_manager_logoff_finish, error);
}


/**
 * gami_manager_logoff_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Close the manager session and disconnect from asterisk
 *
 */
void
gami_manager_logoff_async (GamiManager *ami,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_logoff_async,
                       bool_hook,
                       (ami->api_major && ami->api_minor) ? "Success"
                                                          : "Goodbye",
                       callback,
                       user_data,
                       "Logoff",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_logoff_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_logoff_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_logoff_finish (GamiManager *ami,
                           GAsyncResult *result,
                           GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_logoff_async,
                               error);
}


/*
 *  Get/Set Variables
 */

/**
 * gami_manager_get_var:
 * @ami: #GamiManager
 * @channel: Channel to retrieve variable from
 * @variable: Name of the variable to retrieve
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Get value of @variable (either from @channel or as global)
 *
 * Returns: value of @variable or %NULL
 */
gchar *
gami_manager_get_var (GamiManager *ami,
                      const gchar *channel,
                      const gchar *variable,
                      const gchar *action_id,
                      GError **error)
{
    gami_manager_get_var_async (ami,
                                channel,
                                variable,
                                action_id,
                                set_sync_result,
                                NULL);
    return wait_string_result (ami, gami_manager_get_var_finish, error);
}

/**
 * gami_manager_get_var_async:
 * @ami: #GamiManager
 * @channel: Channel to retrieve variable from
 * @variable: Name of the variable to retrieve
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Get value of @variable (either from @channel or as global)
 *
 */
void
gami_manager_get_var_async (GamiManager *ami,
                            const gchar *channel,
                            const gchar *variable,
                            const gchar *action_id,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_assert (variable != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_get_var_async,
                       string_hook,
                       "Value",
                       callback,
                       user_data,
                       "GetVar",
                       "Variable", variable,
                       "Channel", channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_get_var_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_get_var_async()
 *
 * Returns: the value of the requested variable, or %NULL
 */
gchar *
gami_manager_get_var_finish (GamiManager *ami,
                             GAsyncResult *result,
                             GError **error)
{
    return string_action_finish (ami,
                                 result,
                                 (GamiAsyncFunc) gami_manager_set_var_async,
                                 error);
}

/**
 * gami_manager_set_var:
 * @ami: #GamiManager
 * @channel: Channel to set variable for
 * @variable: Name of the variable to set
 * @value: New value for @variable
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set @variable (optionally on channel @channel) to @value
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_set_var (GamiManager *ami,
                      const gchar *channel,
                      const gchar *variable,
                      const gchar *value,
                      const gchar *action_id,
                      GError **error)
{
    gami_manager_set_var_async (ami,
                                channel,
                                variable,
                                value,
                                action_id,
                                set_sync_result,
                                NULL);

    return wait_bool_result (ami, gami_manager_set_var_finish, error);
}

/**
 * gami_manager_set_var_async:
 * @ami: #GamiManager
 * @channel: Channel to set variable for
 * @variable: Name of the variable to set
 * @value: New value for @variable
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Set @variable (optionally on channel @channel) to @value
 *
 */
void
gami_manager_set_var_async (GamiManager *ami,
                            const gchar *channel,
                            const gchar *variable,
                            const gchar *value,
                            const gchar *action_id,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_assert (variable != NULL && value != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_set_var_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "Logoff",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_set_var_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_set_var_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_set_var_finish (GamiManager *ami,
                           GAsyncResult *result,
                           GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_set_var_async,
                               error);
}


/*
 * Module handling
 */

/**
 * gami_manager_module_check:
 * @ami: #GamiManager
 * @module: Asterisk module name (not including extension)
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Check whether @module is loaded
 *
 * Returns: %TRUE if @module is loaded, %FALSE otherwise
 */
gboolean
gami_manager_module_check (GamiManager *ami,
                           const gchar *module,
                           const gchar *action_id,
                           GError **error)
{
    gami_manager_module_check_async (ami,
                                     module,
                                     action_id,
                                     set_sync_result,
                                     NULL);
    return wait_bool_result (ami, gami_manager_module_check_finish, error);
}

/**
 * gami_manager_module_check_async:
 * @ami: #GamiManager
 * @module: Asterisk module name (not including extension)
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Check whether @module is loaded
 *
 */
void
gami_manager_module_check_async (GamiManager *ami,
                                 const gchar *module,
                                 const gchar *action_id,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_assert (module != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_module_check_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "ModuleCheck",
                       "Module", module,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_module_check_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_module_check_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_module_check_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_module_check_async,
                               error);
}

/**
 * gami_manager_module_load:
 * @ami: #GamiManager
 * @module: Asterisk module name (not including extension)
 * @load_type: Load action to perform (load, reload or unload)
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Perform action indicated by @load_type for @module
 *
 * Returns: %TRUE if @module is loaded, %FALSE otherwise
 */
gboolean
gami_manager_module_load (GamiManager *ami,
                          const gchar *module,
                          GamiModuleLoadType load_type,
                          const gchar *action_id,
                          GError **error)
{
    gami_manager_module_load_async (ami,
                                    module,
                                    load_type,
                                    action_id,
                                    set_sync_result,
                                    NULL);
    return wait_bool_result (ami, gami_manager_module_load_finish, error);
}

/**
 * gami_manager_module_load_async:
 * @ami: #GamiManager
 * @module: Asterisk module name (not including extension)
 * @load_type: Load action to perform (load, reload or unload)
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Perform action indicated by @load_type for @module
 *
 */
void
gami_manager_module_load_async (GamiManager *ami,
                                const gchar *module,
                                GamiModuleLoadType load_type,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    gchar *load = NULL;

    switch (load_type) {
        case GAMI_MODULE_LOAD:
            load = "load";
            break;
        case GAMI_MODULE_RELOAD:
            load = "reload";
            break;
        case GAMI_MODULE_UNLOAD:
            load = "unload";
            break;
    }

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_module_load_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "ModuleLoad",
                       "Module", module,
                       "LoadType", load,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_module_load_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_module_load_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_module_load_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_module_load_async,
                               error);
}

/*
 * Monitor channels
 */

/**
 * gami_manager_monitor:
 * @ami: #GamiManager
 * @channel: Channel to start monitoring
 * @file: Filename to use for recording
 * @format: Format to use for recording
 * @mix: Whether to mix in / out channel into one file
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Start monitoring @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_monitor (GamiManager *ami,
                      const gchar *channel,
                      const gchar *file,
                      const gchar *format,
                      gboolean mix,
                      const gchar *action_id,
                      GError **error)
{
    gami_manager_monitor_async (ami,
                                channel,
                                file,
                                format,
                                mix,
                                action_id,
                                set_sync_result,
                                NULL);
    return wait_bool_result (ami, gami_manager_monitor_finish, error);
}

/**
 * gami_manager_monitor_async:
 * @ami: #GamiManager
 * @channel: Channel to start monitoring
 * @file: Filename to use for recording
 * @format: Format to use for recording
 * @mix: Whether to mix in / out channel into one file
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Start monitoring @channel
 *
 */
void
gami_manager_monitor_async (GamiManager *ami,
                            const gchar *channel,
                            const gchar *file,
                            const gchar *format,
                            gboolean mix,
                            const gchar *action_id,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    gchar *do_mix = NULL;

    if (mix)
        do_mix = "1";

    g_assert (channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_monitor_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "Monitor",
                       "Channel", channel,
                       "File", file,
                       "Format", format,
                       "Mix", do_mix,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_monitor_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_monitor_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_monitor_finish (GamiManager *ami,
                             GAsyncResult *result,
                             GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_monitor_async,
                               error);
}


/**
 * gami_manager_change_monitor:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @file: New filename to use for recording
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Change the file name of the recording occuring on @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_change_monitor (GamiManager *ami,
                             const gchar *channel,
                             const gchar *file,
                             const gchar *action_id,
                             GError **error)
{
    gami_manager_change_monitor_async (ami,
                                       channel,
                                       file,
                                       action_id,
                                       set_sync_result,
                                       NULL);
    return wait_bool_result (ami, gami_manager_change_monitor_finish, error);
}

/**
 * gami_manager_change_monitor_async:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @file: New filename to use for recording
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Change the file name of the recording occuring on @channel
 *
 */
void
gami_manager_change_monitor_async (GamiManager *ami,
                                   const gchar *channel,
                                   const gchar *file,
                                   const gchar *action_id,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    g_assert (channel != NULL && file != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_change_monitor_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "ChangeMonitor",
                       "Channel", channel,
                       "File", file,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_change_monitor_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_change_monitor_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_change_monitor_finish (GamiManager *ami,
                                    GAsyncResult *result,
                                    GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_change_monitor_async;
    return bool_action_finish (ami,
                               result,
                               func,
                               error);
}


/**
 * gami_manager_stop_monitor:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Stop monitoring @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_stop_monitor (GamiManager *ami,
                           const gchar *channel,
                           const gchar *action_id,
                           GError **error)
{
    gami_manager_stop_monitor_async (ami,
                                     channel,
                                     action_id,
                                     set_sync_result,
                                     NULL);
    return wait_bool_result (ami, gami_manager_stop_monitor_finish, error);
}

/**
 * gami_manager_stop_monitor_async:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Stop monitoring @channel
 *
 */
void
gami_manager_stop_monitor_async (GamiManager *ami,
                                 const gchar *channel,
                                 const gchar *action_id,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_assert (channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_stop_monitor_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "StopMonitor",
                       "Channel", channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_stop_monitor_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_stop_monitor_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_stop_monitor_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_stop_monitor_async,
                               error);
}


/**
 * gami_manager_pause_monitor:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Pause monitoring of @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_pause_monitor (GamiManager *ami,
                            const gchar *channel,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_pause_monitor_async (ami,
                                      channel,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_bool_result (ami, gami_manager_pause_monitor_finish, error);
}

/**
 * gami_manager_pause_monitor_async:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Pause monitoring of @channel
 *
 */
void
gami_manager_pause_monitor_async (GamiManager *ami,
                                  const gchar *channel,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    g_assert (channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_pause_monitor_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "PauseMonitor",
                       "Channel", channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_pause_monitor_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_pause_monitor_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_pause_monitor_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_pause_monitor_async,
                               error);
}


/**
 * gami_manager_unpause_monitor:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Continue monitoring of @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_unpause_monitor (GamiManager *ami,
                              const gchar *channel,
                              const gchar *action_id,
                              GError **error)
{
    gami_manager_unpause_monitor_async (ami,
                                        channel,
                                        action_id,
                                        set_sync_result,
                                        NULL);
    return wait_bool_result (ami, gami_manager_unpause_monitor_finish, error);
}

/**
 * gami_manager_unpause_monitor_async:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Continue monitoring of @channel
 *
 */
void
gami_manager_unpause_monitor_async (GamiManager *ami,
                                    const gchar *channel,
                                    const gchar *action_id,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    g_assert (channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_unpause_monitor_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "UnpauseMonitor",
                       "Channel", channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_unpause_monitor_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_unpause_monitor_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_unpause_monitor_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_unpause_monitor_async;
    return bool_action_finish (ami, result, func, error);
}


/*
 * Meetme
 */

/**
 * gami_manager_meetme_mute:
 * @ami: #GamiManager
 * @meetme: The MeetMe conference bridge number
 * @user_num: The user number in the specified bridge
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Mutes @user_num in conference @meetme
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_meetme_mute (GamiManager *ami,
                          const gchar *meetme,
                          const gchar *user_num,
                          const gchar *action_id,
                          GError **error)
{
    gami_manager_meetme_mute_async (ami,
                                    meetme,
                                    user_num,
                                    action_id,
                                    set_sync_result,
                                    NULL);
    return wait_bool_result (ami, gami_manager_meetme_mute_finish, error);
}

/**
 * gami_manager_meetme_mute_async:
 * @ami: #GamiManager
 * @meetme: The MeetMe conference bridge number
 * @user_num: The user number in the specified bridge
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Mutes @user_num in conference @meetme
 *
 */
void
gami_manager_meetme_mute_async (GamiManager *ami,
                                const gchar *meetme,
                                const gchar *user_num,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    g_assert (meetme != NULL && user_num != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_meetme_mute_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "MeetmeMute",
                       "Meetme", meetme,
                       "UserNum", user_num,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_meetme_mute_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_meetme_mute_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_meetme_mute_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_meetme_mute_async,
                               error);
}


/**
 * gami_manager_meetme_unmute:
 * @ami: #GamiManager
 * @meetme: The MeetMe conference bridge number
 * @user_num: The user number in the specified bridge
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Unmutes @user_num in conference @meetme
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_meetme_unmute (GamiManager *ami,
                            const gchar *meetme,
                            const gchar *user_num,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_meetme_unmute_async (ami,
                                      meetme,
                                      user_num,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_bool_result (ami, gami_manager_meetme_unmute_finish, error);
}

/**
 * gami_manager_meetme_unmute_async:
 * @ami: #GamiManager
 * @meetme: The MeetMe conference bridge number
 * @user_num: The user number in the specified bridge
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Unmutes @user_num in conference @meetme
 */
void
gami_manager_meetme_unmute_async (GamiManager *ami,
                                  const gchar *meetme,
                                  const gchar *user_num,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    g_assert (meetme != NULL && user_num != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_meetme_unmute_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "MeetmeUnmute",
                       "Meetme", meetme,
                       "UserNum", user_num,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_meetme_unmute_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_meetme_unmute_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_meetme_unmute_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_meetme_unmute_async,
                               error);
}


/**
 * gami_manager_meetme_list:
 * @ami: #GamiManager
 * @meetme: The MeetMe conference bridge number
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * List al users in conference @meetme
 *
 * Returns: #GSList of user information (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_meetme_list (GamiManager *ami,
                          const gchar *meetme,
                          const gchar *action_id,
                          GError **error)
{
    gami_manager_meetme_list_async (ami,
                                    meetme,
                                    action_id,
                                    set_sync_result,
                                    NULL);
    return wait_list_result (ami, gami_manager_meetme_list_finish, error);
}

/**
 * gami_manager_meetme_list_async:
 * @ami: #GamiManager
 * @conference: The MeetMe conference bridge number
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * List all users in conference @meetme
 */
void
gami_manager_meetme_list_async (GamiManager *ami,
                                const gchar *conference,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_meetme_list_async,
                       list_hook,
                       "MeetMeListComplete",
                       callback,
                       user_data,
                       "MeetmeList",
                       "Conference", conference,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_meetme_list_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_meetme_list_async()
 *
 * Returns: #GSList of user information (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_meetme_list_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return list_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_meetme_list_async,
                               error);
}


/*
 * Queue management
 */

/**
 * gami_manager_queue_add:
 * @ami: #GamiManager
 * @queue: Existing queue to add member
 * @iface: Member interface to add to @queue
 * @penalty: Penalty for new member
 * @paused: whether @iface should be initially paused
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Add @iface to @queue
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_queue_add (GamiManager *ami,
                        const gchar *queue,
                        const gchar *iface,
                        guint penalty,
                        gboolean paused,
                        const gchar *action_id,
                        GError **error)
{
    gami_manager_queue_add_async (ami,
                                  queue,
                                  iface,
                                  penalty,
                                  paused,
                                  action_id,
                                  set_sync_result,
                                  NULL);
    return wait_bool_result (ami, gami_manager_queue_add_finish, error);
}

/**
 * gami_manager_queue_add_async:
 * @ami: #GamiManager
 * @queue: Existing queue to add member
 * @iface: Member interface to add to @queue
 * @penalty: Penalty for new member
 * @paused: whether @iface should be initially paused
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Add @iface to @queue
 */
void
gami_manager_queue_add_async (GamiManager *ami,
                              const gchar *queue,
                              const gchar *iface,
                              guint penalty,
                              gboolean paused,
                              const gchar *action_id,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    gchar *spenalty = NULL,
          *spaused  = NULL;

    g_assert (queue != NULL && iface != NULL);

    spenalty = g_strdup_printf ("%d", penalty);
    if (paused) spaused = "1";

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_queue_add_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "QueueAdd",
                       "Queue", queue,
                       "Interface", iface,
                       "Penalty", spenalty,
                       "Paused", spaused,
                       "ActionID", action_id,
                       NULL);
    g_free (spenalty);
}

/**
 * gami_manager_queue_add_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_queue_add_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_queue_add_finish (GamiManager *ami,
                               GAsyncResult *result,
                               GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_queue_add_async,
                               error);
}

/**
 * gami_manager_queue_remove:
 * @ami: #GamiManager
 * @queue: Existing queue to remove member from
 * @iface: Member interface to remove from @queue
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Remove @iface from @queue
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_queue_remove (GamiManager *ami,
                           const gchar *queue,
                           const gchar *iface,
                           const gchar *action_id,
                           GError **error)
{
    gami_manager_queue_remove_async (ami,
                                     queue,
                                     iface,
                                     action_id,
                                     set_sync_result,
                                     NULL);
    return wait_bool_result (ami, gami_manager_queue_remove_finish, error);
}

/**
 * gami_manager_queue_remove_async:
 * @ami: #GamiManager
 * @queue: Existing queue to remove member from
 * @iface: Member interface to remove from @queue
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Remove @iface from @queue
 */
void
gami_manager_queue_remove_async (GamiManager *ami,
                                 const gchar *queue,
                                 const gchar *iface,
                                 const gchar *action_id,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_assert (queue != NULL && iface != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_queue_remove_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "QueueRemove",
                       "Queue", queue,
                       "Interface", iface,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_queue_remove_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_queue_remove_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_queue_remove_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_queue_remove_async,
                               error);
}

/**
 * gami_manager_queue_pause:
 * @ami: #GamiManager
 * @queue: Existing queue for which @iface should be (un)paused
 * @iface: Member interface (un)pause
 * @paused: Whether to pause or unpause @iface
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * (Un)pause @iface
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_queue_pause (GamiManager *ami,
                          const gchar *queue,
                          const gchar *iface,
                          gboolean paused,
                          const gchar *action_id,
                          GError **error)
{
    gami_manager_queue_pause_async (ami,
                                    queue,
                                    iface,
                                    paused,
                                    action_id,
                                    set_sync_result,
                                    NULL);
    return wait_bool_result (ami, gami_manager_queue_pause_finish, error);
}

/**
 * gami_manager_queue_pause_async:
 * @ami: #GamiManager
 * @queue: Existing queue for which @iface should be (un)paused
 * @iface: Member interface (un)pause
 * @paused: Whether to pause or unpause @iface
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * (Un)pause @iface
 */
void
gami_manager_queue_pause_async (GamiManager *ami,
                                const gchar *queue,
                                const gchar *iface,
                                gboolean paused,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    gchar *spaused;

    spaused = paused ? "1" : "0";

    g_assert (iface != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_queue_pause_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "QueuePause",
                       "Queue", queue,
                       "Interface", iface,
                       "Paused", spaused,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_queue_pause_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_queue_pause_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_queue_pause_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_queue_pause_async,
                               error);
}

/**
 * gami_manager_queue_penalty:
 * @ami: #GamiManager
 * @queue: Limit @penalty change to existing queue
 * @iface: Member interface change penalty for
 * @penalty: New penalty to set for @iface
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Change the penalty value of @iface
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_queue_penalty (GamiManager *ami,
                            const gchar *queue,
                            const gchar *iface,
                            guint penalty,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_queue_penalty_async (ami,
                                      queue,
                                      iface,
                                      penalty,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_bool_result (ami, gami_manager_queue_penalty_finish, error);
}

/**
 * gami_manager_queue_penalty_async:
 * @ami: #GamiManager
 * @queue: Limit @penalty change to existing queue
 * @iface: Member interface change penalty for
 * @penalty: New penalty to set for @iface
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Change the penalty value of @iface
 */
void
gami_manager_queue_penalty_async (GamiManager *ami,
                                  const gchar *queue,
                                  const gchar *iface,
                                  guint penalty,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    gchar *spenalty;

    g_assert (iface != NULL);

    spenalty = g_strdup_printf ("%d", penalty);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_queue_penalty_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "QueuePenalty",
                       "Queue", queue,
                       "Interface", iface,
                       "Penalty", spenalty,
                       "ActionID", action_id,
                       NULL);
    g_free (spenalty);
}

/**
 * gami_manager_queue_penalty_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_queue_penalty_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_queue_penalty_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_queue_penalty_async,
                               error);
}


/**
 * gami_manager_queue_summary:
 * @ami: #GamiManager
 * @queue: Only send summary information for @queue
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Get summary of queue statistics
 *
 * Returns: #GSList of queue statistics (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_queue_summary (GamiManager *ami,
                            const gchar *queue,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_queue_summary_async (ami,
                                      queue,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_list_result (ami, gami_manager_queue_summary_finish, error);
}

/**
 * gami_manager_queue_summary_async:
 * @ami: #GamiManager
 * @queue: Only send summary information for @queue
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Get summary of queue statistics
 */
void
gami_manager_queue_summary_async (GamiManager *ami,
                                  const gchar *queue,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_queue_summary_async,
                       list_hook,
                       "QueueSummaryComplete",
                       callback,
                       user_data,
                       "QueueSummary",
                       "Queue", queue,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_queue_summary_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_queue_summary_async()
 *
 * Returns: #GSList of queue statistics (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_queue_summary_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    return list_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_queue_summary_async,
                               error);
}


/**
 * gami_manager_queue_log:
 * @ami: #GamiManager
 * @queue: Queue to generate queue_log entry for
 * @event: Log event to generate
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Generate a queue_log entry for @queue
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_queue_log (GamiManager *ami,
                        const gchar *queue,
                        const gchar *event,
                        const gchar *action_id,
                        GError **error)
{
    gami_manager_queue_log_async (ami,
                                  queue,
                                  event,
                                  action_id,
                                  set_sync_result,
                                  NULL);
    return wait_bool_result (ami, gami_manager_queue_log_finish, error);
}

/**
 * gami_manager_queue_log_async:
 * @ami: #GamiManager
 * @queue: Queue to generate queue_log entry for
 * @event: Log event to generate
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Generate a queue_log entry for @queue
 */
void
gami_manager_queue_log_async (GamiManager *ami,
                              const gchar *queue,
                              const gchar *event,
                              const gchar *action_id,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    g_assert (queue != NULL && event != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_queue_log_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "QueueLog",
                       "Queue", queue,
                       "Event", event,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_queue_log_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_queue_log_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_queue_log_finish (GamiManager *ami,
                               GAsyncResult *result,
                               GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_queue_log_async,
                               error);
}

/**
 * gami_manager_queue_rule:
 * @ami: #GamiManager
 * @rule: Limit list to entries under this rule
 * @action_id: ActionID to ease response matching
 * @error: a #GError, or %NULL
 *
 * List rules defined in queuerules.conf.
 *
 * Returns: #GHashTable of rule information (stored as #GamiQueueRule) on
 *          success, %NULL on failure
 */
GHashTable *
gami_manager_queue_rule (GamiManager *ami,
                         const gchar *rule,
                         const gchar *action_id,
                         GError **error)
{
    gami_manager_queue_rule_async (ami,
                                  rule,
                                  action_id,
                                  set_sync_result,
                                  NULL);
    return wait_hash_result (ami, gami_manager_queue_rule_finish, error);
}

/**
 * gami_manager_queue_rule_async:
 * @ami: #GamiManager
 * @rule: Limit list to entries under this rule
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * List rules defined in queuerules.conf.
 */
void
gami_manager_queue_rule_async (GamiManager *ami,
                               const gchar *rule,
                               const gchar *action_id,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_queue_rule_async,
                       queue_rule_hook,
                       NULL,
                       callback,
                       user_data,
                       "QueueRule",
                       "Rule", rule,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_queue_rule_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_queue_rule_async ()
 *
 * Returns: #GHashTable of rule information (stored as #GamiQueueRule) on
 *          success, %NULL on failure
 */
GHashTable *
gami_manager_queue_rule_finish (GamiManager *ami,
                                GAsyncResult *result,
                                GError **error)
{
    return hash_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_queue_rule_async,
                               error);
}


/**
 * gami_manager_queue_status:
 * @ami: #GamiManager
 * @queue: queue to limit list to
 * @action_id: ActionID to ease response matching
 * @error: a #GError, or %NULL
 *
 * List status information of queues and their members.
 *
 * Returns: #GSList of queues status information (stored as
 *          #GamiQueueStatusEntry) on success, %NULL on failure
 */
GSList *
gami_manager_queue_status (GamiManager *ami,
                           const gchar *queue,
                           const gchar *action_id,
                           GError **error)
{
    gami_manager_queue_status_async (ami,
                                     queue,
                                     action_id,
                                     set_sync_result,
                                     NULL);
    return wait_queue_status_result (ami,
                                     gami_manager_queue_status_finish,
                                     error);
}

/**
 * gami_manager_queue_status_async:
 * @ami: #GamiManager
 * @queue: queue to limit list to
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * List status information of queues and their members.
 */
void
gami_manager_queue_status_async (GamiManager *ami,
                                 const gchar *queue,
                                 const gchar *action_id,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_queue_status_async,
                       queue_status_hook,
                       "QueueStatusComplete",
                       callback,
                       user_data,
                       "QueueStatus",
                       "Queue", queue,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_queue_status_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_queue_status_async ()
 *
 * Returns: #GSList of queues status information (stored as
 *          #GamiQueueStatusEntry) on success, %NULL on failure
 */
GSList *
gami_manager_queue_status_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error)
{
    return list_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_queue_status_async,
                               error);
}


/**
 * gami_manager_queues:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: a #GError, or %NULL
 *
 * Receive a dump of queue statistics like the "show queues" CLI command
 *
 * Returns: queue statistics in text format on success or %NULL on failure
 */
gchar *
gami_manager_queues (GamiManager *ami,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_queues_async (ami, action_id, set_sync_result, NULL);
    return wait_string_result (ami, gami_manager_queues_finish, error);
}

/**
 * gami_manager_queues_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Receive a dump of queue statistics like the "show queues" CLI command
 */
void
gami_manager_queues_async (GamiManager *ami,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_queues_async,
                       queues_hook,
                       NULL,
                       callback,
                       user_data,
                       "Queues",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_queues_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_queues_async ()
 *
 * Returns: queue statistics in text format on success or %NULL on failure
 */
gchar *
gami_manager_queues_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return string_action_finish (ami,
                                 result,
                                 (GamiAsyncFunc) gami_manager_queues_async,
                                 error);
}


/*
 * ZAP Channels
 */

/**
 * gami_manager_zap_dial_offhook
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel on which to dial @number
 * @number: The number to dial
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Dial over ZAP channel while offhook
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_zap_dial_offhook (GamiManager *ami,
                               const gchar *zap_channel,
                               const gchar *number,
                               const gchar *action_id,
                               GError **error)
{
    gami_manager_zap_dial_offhook_async (ami,
                                         zap_channel,
                                         number,
                                         action_id,
                                         set_sync_result,
                                         NULL);
    return wait_bool_result (ami, gami_manager_zap_dial_offhook_finish, error);
}

/**
 * gami_manager_zap_dial_offhook_async
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel on which to dial @number
 * @number: The number to dial
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Dial over ZAP channel while offhook
 */
void
gami_manager_zap_dial_offhook_async (GamiManager *ami,
                                     const gchar *zap_channel,
                                     const gchar *number,
                                     const gchar *action_id,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    g_assert (zap_channel != NULL && number != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_zap_dial_offhook_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "ZapDialOffhook",
                       "ZapChannel", zap_channel,
                       "Number", number,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_zap_dial_offhook_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_zap_dial_offhook_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_zap_dial_offhook_finish (GamiManager *ami,
                                      GAsyncResult *result,
                                      GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_zap_dial_offhook_async;
    return bool_action_finish (ami, result, func, error);
}


/**
 * gami_manager_zap_hangup:
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel to hang up
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Hangup ZAP channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_zap_hangup (GamiManager *ami,
                         const gchar *zap_channel,
                         const gchar *action_id,
                         GError **error)
{
    gami_manager_zap_hangup_async (ami,
                                   zap_channel,
                                   action_id,
                                   set_sync_result,
                                   NULL);
    return wait_bool_result (ami, gami_manager_zap_hangup_finish, error);
}

/**
 * gami_manager_zap_hangup_async:
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel to hang up
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Hangup ZAP channel
 */
void
gami_manager_zap_hangup_async (GamiManager *ami,
                               const gchar *zap_channel,
                               const gchar *action_id,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    g_assert (zap_channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_zap_hangup_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "ZapHangup",
                       "ZapChannel", zap_channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_zap_hangup_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_zap_hangup_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_zap_hangup_finish (GamiManager *ami,
                                GAsyncResult *result,
                                GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_zap_hangup_async,
                               error);
}


/**
 * gami_manager_zap_dnd_on:
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel on which to turn on DND status
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set DND (Do Not Disturb) status on @zap_channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_zap_dnd_on (GamiManager *ami,
                         const gchar *zap_channel,
                         const gchar *action_id,
                         GError **error)
{
    gami_manager_zap_dnd_on_async (ami,
                                   zap_channel,
                                   action_id,
                                   set_sync_result,
                                   NULL);
    return wait_bool_result (ami, gami_manager_zap_dnd_on_finish, error);
}

/**
 * gami_manager_zap_dnd_on_async:
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel on which to turn on DND status
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Set DND (Do Not Disturb) status on @zap_channel
 */
void
gami_manager_zap_dnd_on_async (GamiManager *ami,
                               const gchar *zap_channel,
                               const gchar *action_id,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    g_assert (zap_channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_zap_dnd_on_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "ZapDNDOn",
                       "ZapChannel", zap_channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_zap_dnd_on_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_zap_dnd_on_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_zap_dnd_on_finish (GamiManager *ami,
                                GAsyncResult *result,
                                GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_zap_dnd_on_async,
                               error);
}


/**
 * gami_manager_zap_dnd_off:
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel on which to turn off DND status
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set DND (Do Not Disturb) status on @zap_channel to off
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_zap_dnd_off (GamiManager *ami,
                          const gchar *zap_channel,
                          const gchar *action_id,
                          GError **error)
{
    gami_manager_zap_dnd_off_async (ami,
                                    zap_channel,
                                    action_id,
                                    set_sync_result,
                                    NULL);
    return wait_bool_result (ami, gami_manager_zap_dnd_off_finish, error);
}

/**
 * gami_manager_zap_dnd_off_async:
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel on which to turn off DND status
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Set DND (Do Not Disturb) status on @zap_channel to off
 */
void
gami_manager_zap_dnd_off_async (GamiManager *ami,
                                const gchar *zap_channel,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    g_assert (zap_channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_zap_dnd_off_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "ZapDNDOff",
                       "ZapChannel", zap_channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_zap_dnd_off_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_zap_dnd_off_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_zap_dnd_off_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_zap_dnd_off_async,
                               error);
}


/**
 * gami_manager_zap_show_channels:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Show the status of all ZAP channels
 *
 * Returns: #GSList of ZAP channels (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_zap_show_channels (GamiManager *ami,
                                const gchar *action_id,
                                GError **error)
{
    gami_manager_zap_show_channels_async (ami,
                                          action_id,
                                          set_sync_result,
                                          NULL);
    return wait_list_result (ami, gami_manager_zap_show_channels_finish, error);
}

/**
 * gami_manager_zap_show_channels_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Show the status of all ZAP channels
 */
void
gami_manager_zap_show_channels_async (GamiManager *ami,
                                      const gchar *action_id,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_zap_show_channels_async,
                       list_hook,
                       "ZapShowChannelsComplete",
                       callback,
                       user_data,
                       "ZapShowChannels",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_zap_show_channels_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_zap_show_channels_async()
 *
 * Returns: #GSList of ZAP channels (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_zap_show_channels_finish (GamiManager *ami,
                                       GAsyncResult *result,
                                       GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_zap_show_channels_async;
    return list_action_finish (ami, result, func, error);
}


/**
 * gami_manager_zap_transfer:
 * @ami: #GamiManager
 * @zap_channel: The channel to be transferred
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Transfer ZAP channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_zap_transfer (GamiManager *ami,
                           const gchar *zap_channel,
                           const gchar *action_id,
                           GError **error)
{
    gami_manager_zap_transfer_async (ami,
                                     zap_channel,
                                     action_id,
                                     set_sync_result,
                                     NULL);
    return wait_bool_result (ami, gami_manager_zap_transfer_finish, error);
}

/**
 * gami_manager_zap_transfer_async:
 * @ami: #GamiManager
 * @zap_channel: The channel to be transferred
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Transfer ZAP channel
 */
void
gami_manager_zap_transfer_async (GamiManager *ami,
                                 const gchar *zap_channel,
                                 const gchar *action_id, 
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_assert (zap_channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_zap_transfer_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "ZapTransfer",
                       "ZapChannel", zap_channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_zap_transfer_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_zap_transfer_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_zap_transfer_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_zap_transfer_async,
                               error);
}


/**
 * gami_manager_zap_restart:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Restart ZAP channels. Any active calls will be terminated
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_zap_restart (GamiManager *ami,
                          const gchar *action_id,
                          GError **error)
{
    gami_manager_zap_restart_async (ami,
                                    action_id,
                                    set_sync_result,
                                    NULL);
    return wait_bool_result (ami, gami_manager_zap_restart_finish, error);
}

/**
 * gami_manager_zap_restart_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Restart ZAP channels. Any active calls will be terminated
 */
void
gami_manager_zap_restart_async (GamiManager *ami,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_zap_restart_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "ZapRestart",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_zap_restart_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_zap_restart_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_zap_restart_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_zap_restart_async,
                               error);
}


/*
 * DAHDI
 */

/**
 * gami_manager_dahdi_dial_offhook:
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel on which to dial @number
 * @number: The number to dial
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Dial over DAHDI channel while offhook
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_dahdi_dial_offhook (GamiManager *ami,
                                 const gchar *dahdi_channel,
                                 const gchar *number,
                                 const gchar *action_id,
                                 GError **error)
{
    gami_manager_dahdi_dial_offhook_async (ami,
                                           dahdi_channel,
                                           number,
                                           action_id,
                                           set_sync_result,
                                           NULL);
    return wait_bool_result (ami,
                             gami_manager_dahdi_dial_offhook_finish, error);
}

/**
 * gami_manager_dahdi_dial_offhook_async:
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel on which to dial @number
 * @number: The number to dial
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Dial over DAHDI channel while offhook
 */
void
gami_manager_dahdi_dial_offhook_async (GamiManager *ami,
                                       const gchar *dahdi_channel,
                                       const gchar *number,
                                       const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    g_assert (dahdi_channel != NULL && number != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_dahdi_dial_offhook_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "DAHDIDialOffhook",
                       "DAHDIChannel", dahdi_channel,
                       "Number", number,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_dahdi_dial_offhook_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_dahdi_dial_offhook_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_dahdi_dial_offhook_finish (GamiManager *ami,
                                        GAsyncResult *result,
                                        GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_dahdi_dial_offhook_async;
    return bool_action_finish (ami, result, func, error);
}


/**
 * gami_manager_dahdi_hangup:
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel to hang up
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Hangup DAHDI channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_dahdi_hangup (GamiManager *ami,
                           const gchar *dahdi_channel,
                           const gchar *action_id,
                           GError **error)
{
    g_assert (dahdi_channel != NULL);

    gami_manager_dahdi_hangup_async (ami,
                                     dahdi_channel,
                                     action_id,
                                     set_sync_result,
                                     NULL);
    return wait_bool_result (ami, gami_manager_dahdi_hangup_finish, error);
}

/**
 * gami_manager_dahdi_hangup_async:
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel to hang up
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Hangup DAHDI channel
 */
void
gami_manager_dahdi_hangup_async (GamiManager *ami,
                                 const gchar *dahdi_channel,
                                 const gchar *action_id,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_assert (dahdi_channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_dahdi_hangup_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "DAHDIHangup",
                       "DAHDIChannel", dahdi_channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_dahdi_hangup_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_dahdi_hangup_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_dahdi_hangup_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_dahdi_hangup_async,
                               error);
}


/**
 * gami_manager_dahdi_dnd_on:
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel on which to turn on DND status
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set DND (Do Not Disturb) status on @dahdi_channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_dahdi_dnd_on (GamiManager *ami,
                           const gchar *dahdi_channel,
                           const gchar *action_id,
                           GError **error)
{
    gami_manager_dahdi_dnd_on_async (ami,
                                     dahdi_channel,
                                     action_id,
                                     set_sync_result,
                                     NULL);
    return wait_bool_result (ami, gami_manager_dahdi_dnd_on_finish, error);
}

/**
 * gami_manager_dahdi_dnd_on_async:
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel on which to turn on DND status
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Set DND (Do Not Disturb) status on @dahdi_channel
 */
void
gami_manager_dahdi_dnd_on_async (GamiManager *ami,
                                 const gchar *dahdi_channel,
                                 const gchar *action_id,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_assert (dahdi_channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_dahdi_dnd_on_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "DAHDIDNDOn",
                       "DAHDIChannel", dahdi_channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_dahdi_dnd_on_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_dahdi_dnd_on_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_dahdi_dnd_on_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_dahdi_dnd_on_async,
                               error);
}


/**
 * gami_manager_dahdi_dnd_off:
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel on which to turn off DND status
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set DND (Do Not Disturb) status on @dahdi_channel to off
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_dahdi_dnd_off (GamiManager *ami,
                            const gchar *dahdi_channel,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_dahdi_dnd_off_async (ami,
                                      dahdi_channel,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_bool_result (ami, gami_manager_dahdi_dnd_off_finish, error);
}

/**
 * gami_manager_dahdi_dnd_off_async:
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel on which to turn off DND status
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Set DND (Do Not Disturb) status on @dahdi_channel to off
 */
void
gami_manager_dahdi_dnd_off_async (GamiManager *ami,
                                  const gchar *dahdi_channel,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    g_assert (dahdi_channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_dahdi_dnd_off_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "DAHDIDNDOff",
                       "DAHDIChannel", dahdi_channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_dahdi_dnd_off_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_dahdi_dnd_off_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_dahdi_dnd_off_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_dahdi_dnd_off_async,
                               error);
}


/**
 * gami_manager_dahdi_show_channels:
 * @ami: #GamiManager
 * @dahdi_channel: Limit status information to this channel
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Show the status of all DAHDI channels
 *
 * Returns: #GSList of DAHDI channels (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_dahdi_show_channels (GamiManager *ami,
                                  const gchar *dahdi_channel,
                                  const gchar *action_id,
                                  GError **error)
{
    gami_manager_dahdi_show_channels_async (ami,
                                            dahdi_channel,
                                            action_id,
                                            set_sync_result,
                                            NULL);
    return wait_list_result (ami,
                             gami_manager_dahdi_show_channels_finish, error);
}

/**
 * gami_manager_dahdi_show_channels_async:
 * @ami: #GamiManager
 * @dahdi_channel: Limit status information to this channel
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Show the status of all DAHDI channels
 */
void
gami_manager_dahdi_show_channels_async (GamiManager *ami,
                                        const gchar *dahdi_channel,
                                        const gchar *action_id,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_dahdi_show_channels_async,
                       list_hook,
                       "DAHDIShowChannelsComplete",
                       callback,
                       user_data,
                       "DAHDIShowChannels",
                       "DAHDIChannel", dahdi_channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_dahdi_show_channels_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_dahdi_show_channels_async()
 *
 * Returns: #GSList of DAHDI channels (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_dahdi_show_channels_finish (GamiManager *ami,
                                         GAsyncResult *result,
                                         GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_dahdi_show_channels_async;
    return list_action_finish (ami, result, func, error);
}


/**
 * gami_manager_dahdi_transfer:
 * @ami: #GamiManager
 * @dahdi_channel: The channel to be transferred
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Transfer DAHDI channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_dahdi_transfer (GamiManager *ami,
                             const gchar *dahdi_channel,
                             const gchar *action_id,
                             GError **error)
{
    gami_manager_dahdi_transfer_async (ami,
                                       dahdi_channel,
                                       action_id,
                                       set_sync_result,
                                       NULL);
    return wait_bool_result (ami, gami_manager_dahdi_transfer_finish, error);
}

/**
 * gami_manager_dahdi_transfer_async:
 * @ami: #GamiManager
 * @dahdi_channel: The channel to be transferred
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Transfer DAHDI channel
 */
void
gami_manager_dahdi_transfer_async (GamiManager *ami,
                                   const gchar *dahdi_channel,
                                   const gchar *action_id,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    g_assert (dahdi_channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_dahdi_transfer_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "DAHDITransfer",
                       "DAHDIChannel", dahdi_channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_dahdi_transfer_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_dahdi_transfer_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_dahdi_transfer_finish (GamiManager *ami,
                                    GAsyncResult *result,
                                    GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_dahdi_transfer_async;
    return bool_action_finish (ami, result, func, error);
}


/**
 * gami_manager_dahdi_restart:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Restart DAHDI channels. Any active calls will be terminated
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_dahdi_restart (GamiManager *ami,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_dahdi_restart_async (ami,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_bool_result (ami, gami_manager_dahdi_restart_finish, error);
}

/**
 * gami_manager_dahdi_restart_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Restart DAHDI channels. Any active calls will be terminated
 */
void
gami_manager_dahdi_restart_async (GamiManager *ami,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_dahdi_restart_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "DAHDIRestart",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_dahdi_restart_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_dahdi_restart_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_dahdi_restart_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_dahdi_restart_async,
                               error);
}


/*
 * Agents
 */

/**
 * gami_manager_agents:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * List information about all configured agents and their status
 *
 * Returns: #GSList of agents (stored as #GHashTable) on success,
 *           %NULL on failure
 */
GSList *
gami_manager_agents (GamiManager *ami,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_agents_async (ami,
                               action_id,
                               set_sync_result,
                               NULL);
    return wait_list_result (ami, gami_manager_agents_finish, error);
}

/**
 * gami_manager_agents_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * List information about all configured agents and their status
 */
void
gami_manager_agents_async (GamiManager *ami,
                           const gchar *action_id, 
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_agents_async,
                       list_hook,
                       "AgentsComplete",
                       callback,
                       user_data,
                       "Agents",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_agents_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_agents_async()
 *
 * Returns: #GSList of agents (stored as #GHashTable) on success,
 *           %NULL on failure
 */
GSList *
gami_manager_agents_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return list_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_agents_async,
                               error);
}


/**
 * gami_manager_agent_callback_login:
 * @ami: #GamiManager
 * @agent: The ID of the agent to log in
 * @exten: The extension to use as callback
 * @context: The context to use as callback
 * @ack_call: Whether calls should be acknowledged by the agent (by pressing #)
 * @wrapup_time: The minimum amount of time after hangup before the agent
 *               will receive a new call
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Log in @agent and register callback to @exten (note that the action has 
 * been deprecated in asterisk-1.4 and was removed in asterisk-1.6)
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_agent_callback_login (GamiManager *ami,
                                   const gchar *agent,
                                   const gchar *exten,
                                   const gchar *context,
                                   gboolean ack_call,
                                   guint wrapup_time,
                                   const gchar *action_id,
                                   GError **error)
{
    gami_manager_agent_callback_login_async (ami,
                                             agent,
                                             exten,
                                             context,
                                             ack_call,
                                             wrapup_time,
                                             action_id,
                                             set_sync_result,
                                             NULL);
    return wait_bool_result (ami,
                             gami_manager_agent_callback_login_finish,
                             error);
}

/**
 * gami_manager_agent_callback_login_async:
 * @ami: #GamiManager
 * @agent: The ID of the agent to log in
 * @exten: The extension to use as callback
 * @context: The context to use as callback
 * @ack_call: Whether calls should be acknowledged by the agent (by pressing #)
 * @wrapup_time: The minimum amount of time after hangup before the agent
 *               will receive a new call
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Log in @agent and register callback to @exten (note that the action has 
 * been deprecated in asterisk-1.4 and was removed in asterisk-1.6)
 */
void
gami_manager_agent_callback_login_async (GamiManager *ami,
                                         const gchar *agent,
                                         const gchar *exten,
                                         const gchar *context,
                                         gboolean ack_call,
                                         guint wrapup_time,
                                         const gchar *action_id,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    gchar *sack_call = NULL;
    gchar *swrapup_time;

    g_assert (agent != NULL && exten != NULL);

    if (ack_call) sack_call = "1";
    swrapup_time = g_strdup_printf ("%d", wrapup_time);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_agent_callback_login_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "AgentCallbackLogin",
                       "Agent", agent,
                       "Exten", exten,
                       "Context", context,
                       "AckCall", sack_call,
                       "WrapupTime", swrapup_time,
                       "ActionID", action_id,
                       NULL);
    g_free (swrapup_time);
}

/**
 * gami_manager_agent_callback_login_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_agent_callback_login_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_agent_callback_login_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error)
{
    GamiAsyncFunc func;

    func = (GamiAsyncFunc) gami_manager_agent_callback_login_async;
    return bool_action_finish (ami, result, func, error);
}


/**
 * gami_manager_agent_logoff:
 * @ami: #GamiManager
 * @agent: The ID of the agent to log off
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Log off @agent
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_agent_logoff (GamiManager *ami,
                           const gchar *agent,
                           const gchar *action_id,
                           GError **error)
{
    gami_manager_agent_logoff_async (ami,
                                     agent,
                                     action_id,
                                     set_sync_result,
                                     NULL);
    return wait_bool_result (ami, gami_manager_agent_logoff_finish, error);
}

/**
 * gami_manager_agent_logoff_async:
 * @ami: #GamiManager
 * @agent: The ID of the agent to log off
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Log off @agent
 */
void
gami_manager_agent_logoff_async (GamiManager *ami,
                                 const gchar *agent,
                                 const gchar *action_id,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_assert (agent != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_agent_logoff_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "AgentLogoff",
                       "Agent", agent,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_agent_logoff_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_agent_logoff_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_agent_logoff_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_agent_logoff_async,
                               error);
}


/*
 * DB
 */

/**
 * gami_manager_db_get:
 * @ami: #GamiManager
 * @family: The AstDB key family from which to retrieve the value
 * @key: The name of the AstDB key
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve value of AstDB entry @family/@key
 *
 * Returns: the value of @family/@key on success, %NULL on failure
 */
gchar *
gami_manager_db_get (GamiManager *ami,
                     const gchar *family,
                     const gchar *key,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_db_get_async (ami,
                               family,
                               key,
                               action_id,
                               set_sync_result,
                               NULL);
    return wait_string_result (ami, gami_manager_db_get_finish, error);
}

/**
 * gami_manager_db_get_async:
 * @ami: #GamiManager
 * @family: The AstDB key family from which to retrieve the value
 * @key: The name of the AstDB key
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve value of AstDB entry @family/@key
 */
void
gami_manager_db_get_async (GamiManager *ami,
                           const gchar *family,
                           const gchar *key,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_assert (family != NULL && key != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_db_get_async,
                       string_hook,
                       "Val",
                       callback,
                       user_data,
                       "DBGet",
                       "Family", family,
                       "Key", key,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_db_get_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_db_get_async()
 *
 * Returns: the value of the requested DB key, or %NULL
 */
gchar *
gami_manager_db_get_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return string_action_finish (ami,
                                 result,
                                 (GamiAsyncFunc) gami_manager_db_get_async,
                                 error);
}


/**
 * gami_manager_db_put:
 * @ami: #GamiManager
 * @family: The AstDB key family in which to set the value
 * @key: The name of the AstDB key
 * @val: The value to assign to the key
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set AstDB entry @family/@key to @value
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_db_put (GamiManager *ami,
                     const gchar *family,
                     const gchar *key,
                     const gchar *val,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_db_put_async (ami,
                               family,
                               key,
                               val,
                               action_id,
                               set_sync_result,
                               NULL);
    return wait_bool_result (ami, gami_manager_db_put_finish, error);
}

/**
 * gami_manager_db_put_async:
 * @ami: #GamiManager
 * @family: The AstDB key family in which to set the value
 * @key: The name of the AstDB key
 * @val: The value to assign to the key
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Set AstDB entry @family/@key to @value
 */
void
gami_manager_db_put_async (GamiManager *ami,
                           const gchar *family,
                           const gchar *key,
                           const gchar *val,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_assert (family != NULL && key != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_db_put_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "DBPut",
                       "Family", family,
                       "Key", key,
                       "Val", val,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_db_put_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_db_put_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_db_put_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_db_put_async,
                               error);
}


/**
 * gami_manager_db_del:
 * @ami: #GamiManager
 * @family: The AstDB key family in which to delete the key
 * @key: The name of the AstDB key to delete
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Remove AstDB entry @family/@key
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_db_del (GamiManager *ami,
                     const gchar *family,
                     const gchar *key,
                     const gchar
                     *action_id,
                     GError **error)
{
    gami_manager_db_del_async (ami,
                               family,
                               key,
                               action_id,
                               set_sync_result,
                               NULL);
    return wait_bool_result (ami, gami_manager_db_del_finish, error);
}

/**
 * gami_manager_db_del_async:
 * @ami: #GamiManager
 * @family: The AstDB key family in which to delete the key
 * @key: The name of the AstDB key to delete
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Remove AstDB entry @family/@key
 */
void
gami_manager_db_del_async (GamiManager *ami,
                           const gchar *family,
                           const gchar *key,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_assert (family != NULL && key != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_db_del_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "DBDel",
                       "Family", family,
                       "Key", key,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_db_del_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_db_del_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_db_del_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_db_del_async,
                               error);
}


/**
 * gami_manager_db_del_tree:
 * @ami: #GamiManager
 * @family: The AstDB key family to delete
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Remove AstDB key family
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_db_del_tree (GamiManager *ami,
                          const gchar *family,
                          const gchar *action_id,
                          GError **error)
{
    gami_manager_db_del_tree_async (ami,
                                    family,
                                    action_id,
                                    set_sync_result,
                                    NULL);
    return wait_bool_result (ami, gami_manager_db_del_tree_finish, error);
}

/**
 * gami_manager_db_del_tree_async:
 * @ami: #GamiManager
 * @family: The AstDB key family to delete
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Remove AstDB key family
 */
void
gami_manager_db_del_tree_async (GamiManager *ami,
                                const gchar *family,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    g_assert (family != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_db_del_tree_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "DBDelTree",
                       "Family", family,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_db_del_tree_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_db_del_tree_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_db_del_tree_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_db_del_tree_async,
                               error);
}


/*
 * Call Parking
 */

/**
 * gami_manager_park:
 * @ami: #GamiManager
 * @channel: Channel name to park
 * @channel2: Channel to announce park info to (and return the call to if the
 *            parking times out)
 * @timeout: Milliseconds to wait before callback
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Park a channel in the parking lot
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_park (GamiManager *ami,
                   const gchar *channel,
                   const gchar *channel2,
                   guint timeout,
                   const gchar *action_id,
                   GError **error)
{
    gami_manager_park_async (ami,
                             channel,
                             channel2,
                             timeout,
                             action_id,
                             set_sync_result,
                             NULL);
    return wait_bool_result (ami, gami_manager_park_finish, error);
}

/**
 * gami_manager_park_async:
 * @ami: #GamiManager
 * @channel: Channel name to park
 * @channel2: Channel to announce park info to (and return the call to if the
 *            parking times out)
 * @timeout: Milliseconds to wait before callback
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Park a channel in the parking lot
 */
void
gami_manager_park_async (GamiManager *ami,
                         const gchar *channel,
                         const gchar *channel2,
                         guint timeout,
                         const gchar *action_id,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    gchar *stimeout = NULL;

    g_assert (channel != NULL && channel2 != NULL);

    stimeout = g_strdup_printf ("%d", timeout);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_park_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "Park",
                       "Channel", channel,
                       "Channel2", channel2,
                       "Timeout", stimeout,
                       "ActionID", action_id,
                       NULL);
    g_free (stimeout);
}

/**
 * gami_manager_park_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_park_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_park_finish (GamiManager *ami,
                          GAsyncResult *result,
                          GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_park_async,
                               error);
}


/**
 * gami_manager_parked_calls:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of parked calls
 *
 * Returns: #GSList of parked calls (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_parked_calls (GamiManager *ami,
                           const gchar *action_id,
                           GError **error)
{
    gami_manager_parked_calls_async (ami,
                                     action_id,
                                     set_sync_result,
                                     NULL);
    return wait_list_result (ami, gami_manager_parked_calls_finish, error);
}

/**
 * gami_manager_parked_calls_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve a list of parked calls
 */
void
gami_manager_parked_calls_async (GamiManager *ami,
                                 const gchar *action_id,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_parked_calls_async,
                       list_hook,
                       "ParkedCallsComplete",
                       callback,
                       user_data,
                       "ParkedCalls",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_parked_calls_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_parked_calls_async()
 *
 * Returns: #GSList of parked calls (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_parked_calls_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error)
{
    return list_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_parked_calls_async,
                               error);
}


/*
 * Mailboxes
 */

/**
 * gami_manager_voicemail_users_list:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of voicemail users
 *
 * Returns: #GSList of voicemail users (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_voicemail_users_list (GamiManager *ami,
                                   const gchar *action_id,
                                   GError **error)
{
    gami_manager_voicemail_users_list_async (ami,
                                             action_id,
                                             set_sync_result,
                                             NULL);
    return wait_list_result (ami,
                             gami_manager_voicemail_users_list_finish,
                             error);
}

/**
 * gami_manager_voicemail_users_list_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve a list of voicemail users
 */
void
gami_manager_voicemail_users_list_async (GamiManager *ami,
                                         const gchar *action_id,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_voicemail_users_list_async,
                       list_hook,
                       "VoicemailUserEntryComplete",
                       callback,
                       user_data,
                       "VoicemailUsersList",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_voicemail_users_list_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_voicemail_users_list_async()
 *
 * Returns: #GSList of voicemail users (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_voicemail_users_list_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error)
{
    GamiAsyncFunc func;

    func = (GamiAsyncFunc) gami_manager_voicemail_users_list_async;
    return list_action_finish (ami, result, func, error);
}


/**
 * gami_manager_mailbox_count:
 * @ami: #GamiManager
 * @mailbox: The mailbox to check messages for
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve count of new and old messages in @mailbox
 *
 * Returns: #GHashTable with message counts on success, %NULL on failure
 */
GHashTable *
gami_manager_mailbox_count (GamiManager *ami,
                            const gchar *mailbox,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_mailbox_count_async (ami,
                                      mailbox,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_hash_result (ami, gami_manager_mailbox_count_finish, error);
}

/**
 * gami_manager_mailbox_count_async:
 * @ami: #GamiManager
 * @mailbox: The mailbox to check messages for
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve count of new and old messages in @mailbox
 */
void
gami_manager_mailbox_count_async (GamiManager *ami,
                                  const gchar *mailbox,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    g_assert (mailbox != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_mailbox_count_async,
                       hash_hook,
                       NULL,
                       callback,
                       user_data,
                       "MailboxCount",
                       "Mailbox", mailbox,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_mailbox_count_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_mailbox_count_async()
 *
 * Returns: #GHashTable with message counts on success, %NULL on failure
 */
GHashTable *
gami_manager_mailbox_count_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_mailbox_count_async;
    return hash_action_finish (ami, result, func, error);
}


/**
 * gami_manager_mailbox_status:
 * @ami: #GamiManager
 * @mailbox: The mailbox to check status for
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Check the status of @mailbox
 *
 * Returns: #GHashTable with status variables on success, %NULL on failure
 */
GHashTable *
gami_manager_mailbox_status (GamiManager *ami,
                             const gchar *mailbox,
                             const gchar *action_id,
                             GError **error)
{
    gami_manager_mailbox_status_async (ami,
                                       mailbox,
                                       action_id,
                                       set_sync_result,
                                       NULL);
    return wait_hash_result (ami, gami_manager_mailbox_status_finish, error);
}

/**
 * gami_manager_mailbox_status_async:
 * @ami: #GamiManager
 * @mailbox: The mailbox to check status for
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Check the status of @mailbox
 */
void
gami_manager_mailbox_status_async (GamiManager *ami,
                                   const gchar *mailbox,
                                   const gchar *action_id,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    g_assert (mailbox != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_mailbox_status_async,
                       hash_hook,
                       NULL,
                       callback,
                       user_data,
                       "MailboxStatus",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_mailbox_status_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_mailbox_status_async()
 *
 * Returns: #GHashTable with status variables on success, %NULL on failure
 */
GHashTable *
gami_manager_mailbox_status_finish (GamiManager *ami,
                                    GAsyncResult *result,
                                    GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_mailbox_status_async;
    return hash_action_finish (ami, result, func, error);
}


/*
 * Core
 */

/**
 * gami_manager_core_status:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve information about the current PBX core status (as active calls,
 * startup time etc.)
 *
 * Returns: #GHashTable with status variables on success, %NULL on failure
 */
GHashTable *
gami_manager_core_status (GamiManager *ami,
                          const gchar *action_id,
                          GError **error)
{
    gami_manager_core_status_async (ami,
                                    action_id,
                                    set_sync_result,
                                    NULL);
    return wait_hash_result (ami, gami_manager_core_status_finish, error);
}

/**
 * gami_manager_core_status_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve information about the current PBX core status (as active calls,
 * startup time etc.)
 */
void
gami_manager_core_status_async (GamiManager *ami,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_core_status_async,
                       hash_hook,
                       NULL,
                       callback,
                       user_data,
                       "CoreStatus",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_core_status_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_core_status_async()
 *
 * Returns: #GHashTable with status variables on success, %NULL on failure
 */
GHashTable *
gami_manager_core_status_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return hash_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_core_status_async,
                               error);
}


/**
 * gami_manager_core_show_channels:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of currently active channels
 *
 * Returns: #GSList of active channels (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_core_show_channels (GamiManager *ami,
                                 const gchar *action_id,
                                 GError **error)
{
    gami_manager_core_show_channels_async (ami,
                                           action_id,
                                           set_sync_result,
                                           NULL);
    return wait_list_result (ami,
                             gami_manager_core_show_channels_finish,
                             error);
}

/**
 * gami_manager_core_show_channels_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve a list of currently active channels
 */
void
gami_manager_core_show_channels_async (GamiManager *ami,
                                       const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_core_show_channels_async,
                       list_hook,
                       "CoreShowChannelsComplete",
                       callback,
                       user_data,
                       "CoreShowChannels",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_core_show_channels_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_core_show_channels_async()
 *
 * Returns: #GSList of active channels (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_core_show_channels_finish (GamiManager *ami,
                                        GAsyncResult *result,
                                        GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_core_show_channels_async;
    return list_action_finish (ami, result, func, error);
}


/**
 * gami_manager_core_settings:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve information about PBX core settings (as Asterisk/GAMI version etc.)
 *
 * Returns: #GHashTable with settings variables on success, %NULL on failure
 */
GHashTable *
gami_manager_core_settings (GamiManager *ami,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_core_settings_async (ami,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_hash_result (ami, gami_manager_core_settings_finish, error);
}

/**
 * gami_manager_core_settings_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve information about PBX core settings (as Asterisk/GAMI version etc.)
 */
void
gami_manager_core_settings_async (GamiManager *ami,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_core_settings_async,
                       hash_hook,
                       NULL,
                       callback,
                       user_data,
                       "CoreSettings",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_core_settings_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_core_settings_async()
 *
 * Returns: #GHashTable with settings variables on success, %NULL on failure
 */
GHashTable *
gami_manager_core_settings_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_core_settings_async;
    return hash_action_finish (ami, result, func, error);
}


/*
 * Misc (TODO: Sort these out and order properly)
 */

/**
 * gami_manager_iax_peer_list:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of IAX2 peers
 *
 * Returns: #GSList of IAX2 peers (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_iax_peer_list (GamiManager *ami,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_iax_peer_list_async (ami,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_list_result (ami, gami_manager_iax_peer_list_finish, error);
}

/**
 * gami_manager_iax_peer_list_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve a list of IAX2 peers
 */
void
gami_manager_iax_peer_list_async (GamiManager *ami,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_iax_peer_list_async,
                       list_hook,
                       "PeerlistComplete",
                       callback,
                       user_data,
                       "IAXpeerlist",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_iax_peer_list_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_iax_peer_list_async()
 *
 * Returns: #GSList of IAX2 peers (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_iax_peer_list_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_iax_peer_list_async;
    return list_action_finish (ami, result, func, error);
}


/**
 * gami_manager_sip_peers:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of SIP peers
 *
 * Returns: #GSList of SIP peers (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_sip_peers (GamiManager *ami,
                        const gchar *action_id,
                        GError **error)
{
    gami_manager_sip_peers_async (ami,
                                  action_id,
                                  set_sync_result,
                                  NULL);
    return wait_list_result (ami, gami_manager_sip_peers_finish, error);
}

/**
 * gami_manager_sip_peers_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve a list of SIP peers
 */
void
gami_manager_sip_peers_async (GamiManager *ami,
                              const gchar *action_id,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_sip_peers_async,
                       list_hook,
                       "PeerlistComplete",
                       callback,
                       user_data,
                       "SIPpeers",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_sip_peers_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_sip_peers_async()
 *
 * Returns: #GSList of SIP peers (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_sip_peers_finish (GamiManager *ami,
                               GAsyncResult *result,
                               GError **error)
{
    return list_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_sip_peers_async,
                               error);
}


/**
 * gami_manager_sip_show_peer:
 * @ami: #GamiManager
 * @peer: SIP peer to get status information for
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve status information for @peer
 *
 * Returns: #GHashTable of peer status information on success, %NULL on failure
 */
GHashTable *
gami_manager_sip_show_peer (GamiManager *ami,
                            const gchar *peer,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_sip_show_peer_async (ami,
                                      peer,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_hash_result (ami, gami_manager_sip_show_peer_finish, error);
}

/**
 * gami_manager_sip_show_peer_async:
 * @ami: #GamiManager
 * @peer: SIP peer to get status information for
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve status information for @peer
 */
void
gami_manager_sip_show_peer_async (GamiManager *ami,
                                  const gchar *peer,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    g_assert (peer != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_sip_show_peer_async,
                       hash_hook,
                       NULL,
                       callback,
                       user_data,
                       "SIPShowPeer",
                       "Peer", peer,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_sip_show_peer_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_sip_show_peer_async()
 *
 * Returns: #GHashTable of peer status information on success, %NULL on failure
 */
GHashTable *
gami_manager_sip_show_peer_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_sip_show_peer_async;
    return hash_action_finish (ami, result, func, error);
}


/**
 * gami_manager_sip_show_registry:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve registry information of SIP peers
 *
 * Returns: #GSList of registry information (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_sip_show_registry (GamiManager *ami,
                                const gchar *action_id,
                                GError **error)
{
    gami_manager_sip_show_registry_async (ami,
                                          action_id,
                                          set_sync_result,
                                          NULL);
    return wait_list_result (ami,
                             gami_manager_sip_show_registry_finish,
                             error);
}

/**
 * gami_manager_sip_show_registry_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve registry information of SIP peers
 */
void
gami_manager_sip_show_registry_async (GamiManager *ami,
                                      const gchar *action_id,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_sip_show_registry_async,
                       list_hook,
                       "RegistrationsComplete",
                       callback,
                       user_data,
                       "SIPshowregistry",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_sip_show_registry_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_sip_show_registry_async()
 *
 * Returns: #GSList of registry information (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_sip_show_registry_finish (GamiManager *ami,
                                       GAsyncResult *result,
                                       GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_sip_show_registry_async;
    return list_action_finish (ami, result, func, error);
}


/**
 * gami_manager_status:
 * @ami: #GamiManager
 * @channel: Only retrieve status information for this channel
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve status information of active channels (or @channel)
 *
 * Returns: #GSList of status information (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_status (GamiManager *ami,
                     const gchar *channel,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_status_async (ami,
                               channel,
                               action_id,
                               set_sync_result,
                               NULL);
    return wait_list_result (ami, gami_manager_status_finish, error);
}

/**
 * gami_manager_status_async:
 * @ami: #GamiManager
 * @channel: Only retrieve status information for this channel
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve status information of active channels (or @channel)
 *
 */
void
gami_manager_status_async (GamiManager *ami,
                           const gchar *channel,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_status_async,
                       list_hook,
                       "StatusComplete",
                       callback,
                       user_data,
                       "Status",
                       "Channel", channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_status_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_status_async()
 *
 * Returns: #GSList of status information (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GSList *
gami_manager_status_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return list_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_status_async,
                               error);
}


/**
 * gami_manager_extension_state:
 * @ami: #GamiManager
 * @exten: The name of the extension to check
 * @context: The context of the extension to check
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Check extension state of @exten@@context - if hints are properly configured
 * on the server, the action will report the status of the device connected to
 * @exten
 *
 * Returns: #GHashTable of status information on success, %NULL on failure
 */
GHashTable *
gami_manager_extension_state (GamiManager *ami,
                              const gchar *exten,
                              const gchar *context,
                              const gchar *action_id,
                              GError **error)
{
    gami_manager_extension_state_async (ami,
                                        exten,
                                        context,
                                        action_id,
                                        set_sync_result,
                                        NULL);
    return wait_hash_result (ami, gami_manager_extension_state_finish, error);
}

/**
 * gami_manager_extension_state_async:
 * @ami: #GamiManager
 * @exten: The name of the extension to check
 * @context: The context of the extension to check
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Check extension state of @exten@@context - if hints are properly configured
 * on the server, the action will report the status of the device connected to
 * @exten
 */
void
gami_manager_extension_state_async (GamiManager *ami,
                                    const gchar *exten,
                                    const gchar *context,
                                    const gchar *action_id,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    g_assert (exten != NULL && context != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_extension_state_async,
                       hash_hook,
                       NULL,
                       callback,
                       user_data,
                       "ExtensionState",
                       "Exten", exten,
                       "Context", context,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_extension_state_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_extension_state_async()
 *
 * Returns: #GHashTable of status information on success, %NULL on failure
 */
GHashTable *
gami_manager_extension_state_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_extension_state_async;
    return hash_action_finish (ami, result, func, error);
}


/**
 * gami_manager_ping:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Query the Asterisk server to make sure it is still responding. May be used
 * to keep the connection alive
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_ping (GamiManager *ami,
                   const gchar *action_id,
                   GError **error)
{
    gami_manager_ping_async (ami,
                             action_id,
                             set_sync_result,
                             NULL);
    return wait_bool_result (ami, gami_manager_ping_finish, error);
}

/**
 * gami_manager_ping_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Query the Asterisk server to make sure it is still responding. May be used
 * to keep the connection alive
 */
void
gami_manager_ping_async (GamiManager *ami,
                         const gchar *action_id,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_ping_async,
                       bool_hook,
                       (ami->api_major && ami->api_minor) ? "Success" : "Pong",
                       callback,
                       user_data,
                       "Ping",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_ping_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_ping_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_ping_finish (GamiManager *ami,
                          GAsyncResult *result,
                          GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_ping_async,
                               error);
}


/**
 * gami_manager_absolute_timeout:
 * @ami: #GamiManager
 * @channel: The name of the channel to set the timeout for
 * @timeout: The maximum duration of the current call, in seconds
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set timeout for call on @channel to @timeout seconds
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_absolute_timeout (GamiManager *ami,
                               const gchar *channel,
                               gint timeout,
                               const gchar *action_id,
                               GError **error)
{
    gami_manager_absolute_timeout_async (ami,
                                         channel,
                                         timeout,
                                         action_id,
                                         set_sync_result,
                                         NULL);
    return wait_bool_result (ami, gami_manager_absolute_timeout_finish, error);
}

/**
 * gami_manager_absolute_timeout_async:
 * @ami: #GamiManager
 * @channel: The name of the channel to set the timeout for
 * @timeout: The maximum duration of the current call, in seconds
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Set timeout for call on @channel to @timeout seconds
 */
void
gami_manager_absolute_timeout_async (GamiManager *ami,
                                     const gchar *channel,
                                     gint timeout,
                                     const gchar *action_id,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    gchar *stimeout;

    g_assert (channel != NULL);

    stimeout = g_strdup_printf ("%d", timeout);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_absolute_timeout_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "AbsoluteTimeout",
                       "Channel", channel,
                       "Timeout", stimeout,
                       "ActionID", action_id,
                       NULL);
    g_free (stimeout);
}

/**
 * gami_manager_absolute_timeout_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_absolute_timeout_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_absolute_timeout_finish (GamiManager *ami,
                                      GAsyncResult *result,
                                      GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_absolute_timeout_async;
    return bool_action_finish (ami, result, func, error);
}


/**
 * gami_manager_challenge:
 * @ami: #GamiManager
 * @auth_type: The authentification type to generate challenge for (e.g. "md5")
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a challenge string to use for authentification of type @auth_type
 *
 * Returns: the generated challenge on success, %NULL on failure
 */
gchar *
gami_manager_challenge (GamiManager *ami,
                        const gchar *auth_type,
                        const gchar *action_id,
                        GError **error)
{
    gami_manager_challenge_async (ami,
                                  auth_type,
                                  action_id,
                                  set_sync_result,
                                  NULL);
    return wait_string_result (ami, gami_manager_challenge_finish, error);
}

/**
 * gami_manager_challenge_async:
 * @ami: #GamiManager
 * @auth_type: The authentification type to generate challenge for (e.g. "md5")
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Retrieve a challenge string to use for authentification of type @auth_type
 */
void
gami_manager_challenge_async (GamiManager *ami,
                              const gchar *auth_type,
                              const gchar *action_id,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    g_assert (auth_type != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_challenge_async,
                       string_hook,
                       "Challenge",
                       callback,
                       user_data,
                       "Challenge",
                       "AuthType", auth_type,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_challenge_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_challenge_async()
 *
 * Returns: the generated challenge on success, %NULL on failure
 */
gchar *
gami_manager_challenge_finish (GamiManager *ami,
                               GAsyncResult *result,
                               GError **error)
{
    return string_action_finish (ami,
                                 result,
                                 (GamiAsyncFunc) gami_manager_challenge_async,
                                 error);
}


/**
 * gami_manager_set_cdr_user_field:
 * @ami: #GamiManager
 * @channel: The name of the channel to set @user_field for
 * @user_field: The value for the CDR user field
 * @append: Whether to append @user_field to current value
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set CDR user field for @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_set_cdr_user_field (GamiManager *ami,
                                 const gchar *channel,
                                 const gchar *user_field,
                                 gboolean append,
                                 const gchar *action_id,
                                 GError **error)
{
    gami_manager_set_cdr_user_field_async (ami,
                                           channel,
                                           user_field,
                                           append,
                                           action_id,
                                           set_sync_result,
                                           NULL);
    return wait_bool_result (ami,
                             gami_manager_set_cdr_user_field_finish,
                             error);
}

/**
 * gami_manager_set_cdr_user_field_async:
 * @ami: #GamiManager
 * @channel: The name of the channel to set @user_field for
 * @user_field: The value for the CDR user field
 * @append: Whether to append @user_field to current value
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Set CDR user field for @channel
 */
void
gami_manager_set_cdr_user_field_async (GamiManager *ami,
                                       const gchar *channel,
                                       const gchar *user_field,
                                       gboolean append,
                                       const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    gchar *do_append = NULL;

    g_assert (channel != NULL && user_field != NULL);

    if (append) do_append = "1";

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_set_cdr_user_field_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "SetCDRUserField",
                       "Channel", channel,
                       "UserField", user_field,
                       "Append", do_append,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_set_cdr_user_field_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_set_cdr_user_field_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_set_cdr_user_field_finish (GamiManager *ami,
                                        GAsyncResult *result,
                                        GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_set_cdr_user_field_async;
    return bool_action_finish (ami, result, func, error);
}


/**
 * gami_manager_reload:
 * @ami: #GamiManager
 * @module: The name of the module to reload (not including extension)
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Reload @module or all modules
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_reload (GamiManager *ami,
                     const gchar *module,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_reload_async (ami,
                               module,
                               action_id,
                               set_sync_result,
                               NULL);
    return wait_bool_result (ami, gami_manager_reload_finish, error);
}

/**
 * gami_manager_reload_async:
 * @ami: #GamiManager
 * @module: The name of the module to reload (not including extension)
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Reload @module or all modules
 */
void
gami_manager_reload_async (GamiManager *ami,
                           const gchar *module,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_reload_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "Reload",
                       "Module", module,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_reload_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_reload_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_reload_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_reload_async,
                               error);
}


/**
 * gami_manager_hangup:
 * @ami: #GamiManager
 * @channel: The name of the channel to hang up
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Hang up @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_hangup (GamiManager *ami,
                     const gchar *channel,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_hangup_async (ami,
                               channel,
                               action_id,
                               set_sync_result,
                               NULL);
    return wait_bool_result (ami, gami_manager_hangup_finish, error);
}

/**
 * gami_manager_hangup_async:
 * @ami: #GamiManager
 * @channel: The name of the channel to hang up
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Hang up @channel
 */
void
gami_manager_hangup_async (GamiManager *ami,
                           const gchar *channel,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_assert (channel != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_hangup_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "Hangup",
                       "Channel", channel,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_hangup_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_hangup_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_hangup_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_hangup_async,
                               error);
}


/**
 * gami_manager_redirect:
 * @ami: #GamiManager
 * @channel: The name of the channel redirect
 * @extra_channel: Second call leg to transfer
 * @exten: The extension @channel should be redirected to
 * @context: The context @channel should be redirected to
 * @priority: The priority @channel should be redirected to
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Redirect @channel to @exten@@context:@priority
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_redirect (GamiManager *ami,
                       const gchar *channel,
                       const gchar *extra_channel,
                       const gchar *exten,
                       const gchar *context,
                       const gchar *priority,
                       const gchar *action_id,
                       GError **error)
{
    gami_manager_redirect_async (ami,
                                 channel,
                                 extra_channel,
                                 exten,
                                 context,
                                 priority,
                                 action_id,
                                 set_sync_result,
                                 NULL);
    return wait_bool_result (ami, gami_manager_redirect_finish, error);
}

/**
 * gami_manager_redirect_async:
 * @ami: #GamiManager
 * @channel: The name of the channel redirect
 * @extra_channel: Second call leg to transfer
 * @exten: The extension @channel should be redirected to
 * @context: The context @channel should be redirected to
 * @priority: The priority @channel should be redirected to
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Redirect @channel to @exten@@context:@priority
 */
void
gami_manager_redirect_async (GamiManager *ami,
                             const gchar *channel,
                             const gchar *extra_channel,
                             const gchar *exten,
                             const gchar *context,
                             const gchar *priority,
                             const gchar *action_id,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    g_assert (channel != NULL);
    g_assert (exten != NULL && context != NULL && priority != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_redirect_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "Redirect",
                       "Channel", channel,
                       "ExtraChannel", extra_channel,
                       "Exten", exten,
                       "Context", context,
                       "Priority", priority,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_redirect_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_redirect_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_redirect_finish (GamiManager *ami,
                              GAsyncResult *result,
                              GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_redirect_async,
                               error);
}


/**
 * gami_manager_bridge:
 * @ami: #GamiManager
 * @channel1: The name of the channel to bridge to @channel2
 * @channel2: The name of the channel to bridge to @channel1
 * @tone: Whether to play courtesy tone to @channel2
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Bridge together the existing channels @channel1 and @channel2
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_bridge (GamiManager *ami,
                     const gchar *channel1,
                     const gchar *channel2,
                     gboolean tone,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_bridge_async (ami,
                               channel1,
                               channel2,
                               tone,
                               action_id,
                               set_sync_result,
                               NULL);
    return wait_bool_result (ami, gami_manager_bridge_finish, error);
}

/**
 * gami_manager_bridge_async:
 * @ami: #GamiManager
 * @channel1: The name of the channel to bridge to @channel2
 * @channel2: The name of the channel to bridge to @channel1
 * @tone: Whether to play courtesy tone to @channel2
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Bridge together the existing channels @channel1 and @channel2
 */
void
gami_manager_bridge_async (GamiManager *ami,
                           const gchar *channel1,
                           const gchar *channel2,
                           gboolean tone,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    gchar *stone;

    g_assert (channel1 != NULL && channel2 != NULL);

    stone = g_strdup_printf (tone ? "Yes" : "No");

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_bridge_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "Bridge",
                       "Channel1", channel1,
                       "Channel2", channel2,
                       "Tone", stone,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_bridge_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_bridge_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_bridge_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_bridge_async,
                               error);
}


/**
 * gami_manager_command:
 * @ami: #GamiManager
 * @command: the CLI command to execute
 * @action_id: ActionID to ease response matching
 * @error: a #GError, or %NULL
 *
 * Execute a CLI command and get its output
 *
 * Returns: the CLI output of @command
 */
gchar *
gami_manager_command (GamiManager *ami,
                      const gchar *command,
                      const gchar *action_id,
                      GError **error)
{
    gami_manager_command_async (ami,
                                command,
                                action_id,
                                set_sync_result,
                                NULL);
    return wait_string_result (ami, gami_manager_command_finish, error);
}

/**
 * gami_manager_command_async:
 * @ami: #GamiManager
 * @command: the CLI command to execute
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Execute a CLI command and get its output
 */
void
gami_manager_command_async (GamiManager *ami,
                            const gchar *command,
                            const gchar *action_id,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_assert (command != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_command_async,
                       command_hook,
                       "Follows",
                       callback,
                       user_data,
                       "Command",
                       "Command", command,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_command_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_command_async()
 *
 * Returns: the CLI output of the executed command
 */
gchar *
gami_manager_command_finish (GamiManager *ami,
                             GAsyncResult *result,
                             GError **error)
{
    return string_action_finish (ami,
                                 result,
                                 (GamiAsyncFunc) gami_manager_command_async,
                                 error);
}


/**
 * gami_manager_agi:
 * @ami: #GamiManager
 * @channel: The name of the channel to execute @command in
 * @command: The name of the AGI command to execute
 * @command_id: CommandID for matching in AGI notification events
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Execute AGI command @command in @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_agi (GamiManager *ami,
                  const gchar *channel,
                  const gchar *command,
                  const gchar *command_id,
                  const gchar *action_id,
                  GError **error)
{
    gami_manager_agi_async (ami,
                            channel,
                            command,
                            command_id,
                            action_id,
                            set_sync_result,
                            NULL);
    return wait_bool_result (ami, gami_manager_agi_finish, error);
}

/**
 * gami_manager_agi_async:
 * @ami: #GamiManager
 * @channel: The name of the channel to execute @command in
 * @command: The name of the AGI command to execute
 * @command_id: CommandID for matching in AGI notification events
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Execute AGI command @command in @channel
 */
void
gami_manager_agi_async (GamiManager *ami,
                        const gchar *channel,
                        const gchar *command,
                        const gchar *command_id,
                        const gchar *action_id,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_assert (channel != NULL && command != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_agi_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "AGI",
                       "Channel", channel,
                       "Command", command,
                       "CommandID", command_id,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_agi_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_agi_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_agi_finish (GamiManager *ami,
                         GAsyncResult *result,
                         GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_agi_async,
                               error);
}


/**
 * gami_manager_send_text:
 * @ami: #GamiManager
 * @channel: The name of the channel to send @message to
 * @message: The message to send to @channel
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Send @message to @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_send_text (GamiManager *ami,
                        const gchar *channel,
                        const gchar *message,
                        const gchar *action_id,
                        GError **error)
{
    gami_manager_send_text_async (ami,
                                  channel,
                                  message,
                                  action_id,
                                  set_sync_result,
                                  NULL);
    return wait_bool_result (ami, gami_manager_send_text_finish, error);
}

/**
 * gami_manager_send_text_async:
 * @ami: #GamiManager
 * @channel: The name of the channel to send @message to
 * @message: The message to send to @channel
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Send @message to @channel
 */
void
gami_manager_send_text_async (GamiManager *ami,
                              const gchar *channel,
                              const gchar *message,
                              const gchar *action_id,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    g_assert (channel != NULL && message != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_send_text_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "SendText",
                       "Channel", channel,
                       "Message", message,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_send_text_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_send_text_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_send_text_finish (GamiManager *ami,
                               GAsyncResult *result,
                               GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_send_text_async,
                               error);
}


/**
 * gami_manager_jabber_send:
 * @ami: #GamiManager
 * @jabber: Jabber / GTalk account to send message from
 * @screen_name: Jabber / GTalk account to send message to
 * @message: The message to send
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Send @message from Jabber / GTalk account @jabber to account @screen_name
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_jabber_send (GamiManager *ami,
                          const gchar *jabber,
                          const gchar *screen_name,
                          const gchar *message,
                          const gchar *action_id,
                          GError **error)
{
    gami_manager_jabber_send_async (ami,
                                    jabber,
                                    screen_name,
                                    message,
                                    action_id,
                                    set_sync_result,
                                    NULL);
    return wait_bool_result (ami, gami_manager_jabber_send_finish, error);
}

/**
 * gami_manager_jabber_send_async:
 * @ami: #GamiManager
 * @jabber: Jabber / GTalk account to send message from
 * @screen_name: Jabber / GTalk account to send message to
 * @message: The message to send
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Send @message from Jabber / GTalk account @jabber to account @screen_name
 */
void
gami_manager_jabber_send_async (GamiManager *ami,
                                const gchar *jabber,
                                const gchar *screen_name,
                                const gchar *message,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    g_assert (jabber != NULL && screen_name != NULL && message != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_jabber_send_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "JabberSend",
                       "Jabber", jabber,
                       "ScreenName", screen_name,
                       "Message", message,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_jabber_send_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_jabber_send_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_jabber_send_finish (GamiManager *ami,
                                 GAsyncResult *result,
                                 GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_jabber_send_async,
                               error);
}


/**
 * gami_manager_play_dtmf:
 * @ami: #GamiManager
 * @channel: The name of the channel to send @digit to
 * @digit: The DTMF digit to play on @channel
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Play a DTMF digit @digit on @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_play_dtmf (GamiManager *ami,
                        const gchar *channel,
                        gchar digit,
                        const gchar *action_id,
                        GError **error)
{
    gami_manager_play_dtmf_async (ami,
                                  channel,
                                  digit,
                                  action_id,
                                  set_sync_result,
                                  NULL);
    return wait_bool_result (ami, gami_manager_play_dtmf_finish, error);
}

/**
 * gami_manager_play_dtmf_async:
 * @ami: #GamiManager
 * @channel: The name of the channel to send @digit to
 * @digit: The DTMF digit to play on @channel
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Play a DTMF digit @digit on @channel
 */
void
gami_manager_play_dtmf_async (GamiManager *ami,
                              const gchar *channel,
                              gchar digit,
                              const gchar *action_id,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    gchar *sdigit;

    g_assert (channel != NULL);

    sdigit = g_strdup_printf ("%c", digit);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_play_dtmf_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "PlayDTMF",
                       "Channel", channel,
                       "Digit", sdigit,
                       "ActionID", action_id,
                       NULL);
    g_free (sdigit);
}

/**
 * gami_manager_play_dtmf_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_play_dtmf_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_play_dtmf_finish (GamiManager *ami,
                               GAsyncResult *result,
                               GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_play_dtmf_async,
                               error);
}


/**
 * gami_manager_list_commands:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * List available Asterisk manager commands - the available actions may vary
 * between different versions of Asterisk and due to the set of loaded modules
 *
 * Returns: A #GHashTable of action names / descriptions on success, 
 *          %NULL on failure
 */
GHashTable *
gami_manager_list_commands (GamiManager *ami,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_list_commands_async (ami,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_hash_result (ami, gami_manager_list_commands_finish, error);
}

/**
 * gami_manager_list_commands_async:
 * @ami: #GamiManager
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * List available Asterisk manager commands - the available actions may vary
 * between different versions of Asterisk and due to the set of loaded modules
 */
void
gami_manager_list_commands_async (GamiManager *ami,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_list_commands_async,
                       hash_hook,
                       NULL,
                       callback,
                       user_data,
                       "ListCommands",
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_list_commands_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_list_commands_async()
 *
 * Returns: A #GHashTable of action names / descriptions on success, 
 *          %NULL on failure
 */
GHashTable *
gami_manager_list_commands_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    return hash_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_list_commands_async,
                               error);
}


/**
 * gami_manager_list_categories:
 * @ami: #GamiManager
 * @filename: The name of the configuration file to list categories for
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * List categories in @filename
 *
 * Returns: A #GHashTable of category number / name on success, 
 *          %NULL on failure
 */
GHashTable *
gami_manager_list_categories (GamiManager *ami,
                              const gchar *filename,
                              const gchar *action_id,
                              GError **error)
{
    gami_manager_list_categories_async (ami,
                                        filename,
                                        action_id,
                                        set_sync_result,
                                        NULL);
    return wait_hash_result (ami, gami_manager_list_categories_finish, error);
}

/**
 * gami_manager_list_categories_async:
 * @ami: #GamiManager
 * @filename: The name of the configuration file to list categories for
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * List categories in @filename
 */
void
gami_manager_list_categories_async (GamiManager *ami,
                                    const gchar *filename,
                                    const gchar *action_id,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    g_assert (filename != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_list_categories_async,
                       hash_hook,
                       NULL,
                       callback,
                       user_data,
                       "ListCategories",
                       "Filename", filename,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_list_categories_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_list_categories_async()
 *
 * Returns: A #GHashTable of category number / name on success, 
 *          %NULL on failure
 */
GHashTable *
gami_manager_list_categories_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_list_categories_async;
    return hash_action_finish (ami, result, func, error);
}


/**
 * gami_manager_get_config:
 * @ami: #GamiManager
 * @filename: The name of the configuration file to get content for
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Get content of configuration file @filename
 *
 * Returns: A #GHashTable of line number / values on success, 
 *          %NULL on failure
 */
GHashTable *
gami_manager_get_config (GamiManager *ami,
                         const gchar *filename,
                         const gchar *action_id,
                         GError **error)
{
    gami_manager_get_config_async (ami,
                                   filename,
                                   action_id,
                                   set_sync_result,
                                   NULL);
    return wait_hash_result (ami, gami_manager_get_config_finish, error);
}

/**
 * gami_manager_get_config_async:
 * @ami: #GamiManager
 * @filename: The name of the configuration file to get content for
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Get content of configuration file @filename
 */
void
gami_manager_get_config_async (GamiManager *ami,
                               const gchar *filename,
                               const gchar *action_id,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    g_assert (filename != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_get_config_async,
                       hash_hook,
                       NULL,
                       callback,
                       user_data,
                       "GetConfig",
                       "Filename", filename,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_get_config_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_get_config_async()
 *
 * Returns: A #GHashTable of line number / values on success, 
 *          %NULL on failure
 */
GHashTable *
gami_manager_get_config_finish (GamiManager *ami,
                                GAsyncResult *result,
                                GError **error)
{
    return hash_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_get_config_async,
                               error);
}


/**
 * gami_manager_get_config_json:
 * @ami: #GamiManager
 * @filename: The name of the configuration file to get content for
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Get content of configuration file @filename as JS hash for use with JSON
 *
 * Returns: A #GHashTable with file dump on success,
 *          %NULL on failure
 */
GHashTable *
gami_manager_get_config_json (GamiManager *ami,
                              const gchar *filename,
                              const gchar *action_id,
                              GError **error)
{
    gami_manager_get_config_json_async (ami,
                                        filename,
                                        action_id,
                                        set_sync_result,
                                        NULL);
    return wait_hash_result (ami, gami_manager_get_config_json_finish, error);
}

/**
 * gami_manager_get_config_json_async:
 * @ami: #GamiManager
 * @filename: The name of the configuration file to get content for
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Get content of configuration file @filename as JS hash for use with JSON
 */
void
gami_manager_get_config_json_async (GamiManager *ami,
                                    const gchar *filename,
                                    const gchar *action_id,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    g_assert (filename != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_get_config_json_async,
                       hash_hook,
                       "Success",
                       callback,
                       user_data,
                       "GetConfigJSON",
                       "Filename", filename,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_get_config_json_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_get_config_json_async()
 *
 * Returns: A #GHashTable with file dump on success,
 *          %NULL on failure
 */
GHashTable *
gami_manager_get_config_json_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error)
{
    GamiAsyncFunc func = (GamiAsyncFunc) gami_manager_get_config_json_async;
    return hash_action_finish (ami, result, func, error);
}


/**
 * gami_manager_create_config:
 * @ami: #GamiManager
 * @filename: The name of the configuration file to create
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Create an empty configurion file @filename
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_create_config (GamiManager *ami,
                            const gchar *filename,
                            const gchar *action_id,
                            GError **error)
{
    gami_manager_create_config_async (ami,
                                      filename,
                                      action_id,
                                      set_sync_result,
                                      NULL);
    return wait_bool_result (ami, gami_manager_create_config_finish, error);
}

/**
 * gami_manager_create_config_async:
 * @ami: #GamiManager
 * @filename: The name of the configuration file to create
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Create an empty configurion file @filename
 */
void
gami_manager_create_config_async (GamiManager *ami,
                                  const gchar *filename,
                                  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    g_assert (filename != NULL);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_create_config_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "CreateConfig",
                       "Filename", filename,
                       "ActionID", action_id,
                       NULL);
}

/**
 * gami_manager_create_config_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with
 * gami_manager_create_config_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_create_config_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_create_config_async,
                               error);
}


/**
 * gami_manager_originate:
 * @ami: #GamiManager
 * @channel: The name of the channel to call. Once the channel has answered,
 *           the call will be passed to the specified exten/context/priority or
 *           application/data
 * @application_exten: Extension to dial or application to call (depending on
 *                     @priority)
 * @data_context: Context to dial or data to pass to application (depending on
 *                @priority)
 * @priority: Priority to dial - if %NULL, @application_exten will be
 *            interpretated as application and @data_context as data
 * @timeout: Time to wait for @channel to answer in milliseconds
 * @caller_id: CallerID to set on the outgoing channel
 * @account: AccountCode to set for the call
 * @variables: A #GHashTable with name / value pairs to pass as channel
 *             variables
 * @async: Whether to originate call asynchronously - this allows to originate
 *         further calls before a response is received
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Generate an outbound call from Asterisk and connect the channel to
 * Exten / Context / Priority or execute Application (Data) on the channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_originate (GamiManager *ami,
                        const gchar *channel,
                        const gchar *application_exten,
                        const gchar *data_context,
                        const gchar *priority,
                        guint timeout,
                        const gchar *caller_id,
                        const gchar *account,
                        const GHashTable *variables,
                        gboolean async,
                        const gchar *action_id,
                        GError **error)
{
    gami_manager_originate_async (ami,
                                  channel,
                                  application_exten,
                                  data_context,
                                  priority,
                                  timeout,
                                  caller_id,
                                  account,
                                  variables,
                                  async,
                                  action_id,
                                  set_sync_result,
                                  NULL);
    return wait_bool_result (ami, gami_manager_originate_finish, error);
}

/**
 * gami_manager_originate_async:
 * @ami: #GamiManager
 * @channel: The name of the channel to call. Once the channel has answered,
 *           the call will be passed to the specified exten/context/priority or
 *           application/data
 * @application_exten: Extension to dial or application to call (depending on
 *                     @priority)
 * @data_context: Context to dial or data to pass to application (depending on
 *                @priority)
 * @priority: Priority to dial - if %NULL, @application_exten will be
 *            interpretated as application and @data_context as data
 * @timeout: Time to wait for @channel to answer in milliseconds
 * @caller_id: CallerID to set on the outgoing channel
 * @account: AccountCode to set for the call
 * @variables: A #GHashTable with name / value pairs to pass as channel
 *             variables
 * @async: Whether to originate call asynchronously - this allows to originate
 *         further calls before a response is received
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Generate an outbound call from Asterisk and connect the channel to
 * Exten / Context / Priority or execute Application (Data) on the channel
 */
void
gami_manager_originate_async (GamiManager *ami,
                              const gchar *channel,
                              const gchar *application_exten,
                              const gchar *data_context,
                              const gchar *priority,
                              guint timeout,
                              const gchar *caller_id,
                              const gchar *account,
                              const GHashTable *variables,
                              gboolean async,
                              const gchar *action_id,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    GString *vars;
    gchar *stimeout, *sasync = NULL, *svariables;
    GHFunc join_vars_func;

    g_assert (channel != NULL);
    g_assert (application_exten != NULL && data_context != NULL);

    stimeout = g_strdup_printf ("%d", timeout);
    if (async) sasync = "1";

    join_vars_func = (ami->api_major
                      && ami->api_minor) ? (GHFunc) join_originate_vars
                                         : (GHFunc) join_originate_vars_legacy;
    vars = g_string_new ("");
    g_hash_table_foreach ((GHashTable *) variables,
                          join_vars_func,
                          vars);
    svariables = g_string_free (vars, FALSE);

    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_originate_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "Originate",
                       "Channel", channel,
                       priority ? "Exten"
                                : "Application", application_exten,
                       priority ? "Context"
                                : "Data", data_context,
                       "Priority", priority,
                       "Timeout", stimeout,
                       "CallerID", caller_id,
                       "Account", account,
                       "Variable", svariables,
                       "Async", sasync,
                       "ActionID", action_id,
                       NULL);
    g_free (stimeout);
    g_free (svariables);
}

/**
 * gami_manager_originate_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_originate_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_originate_finish (GamiManager *ami,
                               GAsyncResult *result,
                               GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_originate_async,
                               error);
}


/**
 * gami_manager_events:
 * @ami: #GamiManager
 * @event_mask: #GamiEventMask to set for the connection
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set #GamiEventMask for the connection to control which events shall be
 * received
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_events (GamiManager *ami,
                     const GamiEventMask event_mask,
                     const gchar *action_id,
                     GError **error)
{
    gami_manager_events_async (ami,
                               event_mask,
                               action_id,
                               set_sync_result,
                               NULL);
    return wait_bool_result (ami, gami_manager_events_finish, error);
}

/**
 * gami_manager_events_async:
 * @ami: #GamiManager
 * @event_mask: #GamiEventMask to set for the connection
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Set #GamiEventMask for the connection to control which events shall be
 * received
 */
void
gami_manager_events_async (GamiManager *ami,
                           const GamiEventMask event_mask,
                           const gchar *action_id,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    gchar *sevent_mask;

    sevent_mask = event_string_from_mask (ami, event_mask);
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_events_async,
                       bool_hook,
                       (ami->api_major && ami->api_minor) ? "Success"
                                                          : "Events Off",
                       callback,
                       user_data,
                       "Events",
                       "EventMask", sevent_mask,
                       "ActionID", action_id,
                       NULL);
    g_free (sevent_mask);
}

/**
 * gami_manager_events_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_events_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_events_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_events_async,
                               error);
}


/**
 * gami_manager_user_event:
 * @ami: #GamiManager
 * @user_event: The user defined event to send
 * @headers: Optional header to add to the event
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Send the user defined event @user_event with an optional payload of @headers
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_user_event (GamiManager *ami,
                         const gchar *user_event,
                         const GHashTable *headers,
                         const gchar *action_id,
                         GError **error)
{
    gami_manager_user_event_async (ami,
                                   user_event,
                                   headers,
                                   action_id,
                                   set_sync_result,
                                   NULL);
    return wait_bool_result (ami, gami_manager_user_event_finish, error);
}

/**
 * gami_manager_user_event_async:
 * @ami: #GamiManager
 * @user_event: The user defined event to send
 * @headers: Optional header to add to the event
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Send the user defined event @user_event with an optional payload of @headers
 */
void
gami_manager_user_event_async (GamiManager *ami,
                               const gchar *user_event,
                               const GHashTable *headers,
                               const gchar *action_id,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    /* FIXME: organize the internal API to handle this more gracefully */
    GamiManagerPrivate *priv;
    gchar *action, *action_complete = NULL, *action_id_new = NULL;
    GError *error = NULL;

    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (user_event != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    g_assert (priv->connected == TRUE);

    action = build_action_string ("UserEvent",
                                  &action_id_new,
                                  "UserEvent", user_event,
                                  "ActionID", action_id,
                                  NULL);

    if (headers) {
        GString *header = g_string_new ("");
        gchar *header_str;

        g_hash_table_foreach ((GHashTable *) headers,
                              (GHFunc) join_user_event_headers, header);
        header_str = g_string_free (header, FALSE);
        action_complete = g_strjoin (header_str, action, NULL);
        g_free (header_str);
    } else
        action_complete = g_strdup (action);

    g_free (action);

    send_action_string (action_complete, priv->socket, &error);

    g_debug ("GAMI command sent");

    setup_action_hook (ami,
                       (GamiAsyncFunc) gami_manager_user_event_async,
                       bool_hook,
                       "Success",
                       action_id_new,
                       callback,
                       user_data,
                       error);
    g_free (action_complete);
}

/**
 * gami_manager_user_event_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_user_event_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean
gami_manager_user_event_finish (GamiManager *ami,
                                GAsyncResult *result,
                                GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_user_event_async,
                               error);
}


/**
 * gami_manager_wait_event:
 * @ami: #GamiManager
 * @timeout: Maximum time to wait for events in seconds
 * @action_id: ActionID to ease response matching
 * @error: A location to return an error of type #GIOChannelError
 *
 * Wait for an event to occur
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gami_manager_wait_event (GamiManager *ami,
                         guint timeout,
                         const gchar *action_id,
                         GError **error)
{
    gami_manager_wait_event_async (ami,
                                   timeout,
                                   action_id,
                                   set_sync_result,
                                   NULL);
    return wait_bool_result (ami, gami_manager_wait_event_finish, error);
}

/**
 * gami_manager_wait_event_async:
 * @ami: #GamiManager
 * @timeout: Maximum time to wait for events in seconds
 * @action_id: ActionID to ease response matching
 * @callback: Callback for asynchronious operation.
 * @user_data: User data to pass to the callback.
 *
 * Wait for an event to occur
 */
void
gami_manager_wait_event_async (GamiManager *ami,
                               guint timeout,
                               const gchar *action_id,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    gchar *stimeout;

    stimeout = g_strdup_printf ("%d", timeout);
    
    send_async_action (ami,
                       (GamiAsyncFunc) gami_manager_wait_event_async,
                       bool_hook,
                       "Success",
                       callback,
                       user_data,
                       "WaitEvent",
                       "Timeout", stimeout,
                       "ActionID", action_id,
                       NULL);
    g_free (stimeout);
}

/**
 * gami_manager_wait_event_finish:
 * @ami: #GamiManager
 * @result: #GAsyncResult
 * @error: a #GError, or %NULL
 *
 * Finishes an asynchronous action started with gami_manager_wait_event_async()
 *
 * Returns: %TRUE if the action succeeded, otherwise %FALSE
 */
gboolean gami_manager_wait_event_finish (GamiManager *ami,
                                         GAsyncResult *result,
                                         GError **error)
{
    return bool_action_finish (ami,
                               result,
                               (GamiAsyncFunc) gami_manager_wait_event_async,
                               error);
}


/*
 * Private API
 */

static gboolean
gami_manager_new_async_cb (GamiManagerNewAsyncData *data)
{
    GamiManager *gami;

    gami = gami_manager_new (data->host, data->port);
    data->func (gami, data->data);

    return FALSE; /* for g_idle_add() */
}

static gboolean
parse_connection_string (GamiManager *ami, GError **error)
{
    GamiManagerPrivate *priv;
    GIOStatus status;
    /* read welcome message and set API */
    gchar   *welcome_message;
    gchar  **split_version;

    g_assert (ami != NULL && GAMI_IS_MANAGER (ami));
    g_assert (error == NULL || *error == NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    status = g_io_channel_read_line (priv->socket, &welcome_message,
                     NULL, NULL, error);

    if (status != G_IO_STATUS_NORMAL) {
        return FALSE;
    }

    ami->api_version = g_strdup (g_strchomp (g_strrstr (welcome_message,
                                                        "/") + 1));
    g_free (welcome_message);

    split_version = g_strsplit (ami->api_version, ".", 2);
    ami->api_major = atoi (split_version [0]);
    ami->api_minor = atoi (split_version [1]);
    g_strfreev (split_version);

    return TRUE;
}

static gchar *
event_string_from_mask (GamiManager *mgr, GamiEventMask mask)
{
    GString *events;

    events = g_string_new ("");
    if (mask == GAMI_EVENT_MASK_NONE)
        g_string_append (events, "off");
    else if (mask & GAMI_EVENT_MASK_ALL)
        g_string_append (events, "on");
    else if (mgr->api_major && mgr->api_minor) {
        gboolean first = TRUE;

        if (mask & GAMI_EVENT_MASK_CALL) {
            g_string_append_printf (events, "%scall", first ? "" : ",");
            first = FALSE;
        }
        if (mask & GAMI_EVENT_MASK_SYSTEM) {
            g_string_append_printf (events, "%ssystem", first ? "" : ",");
            first = FALSE;
        }
        if (mask & GAMI_EVENT_MASK_AGENT) {
            g_string_append_printf (events, "%sagent", first ? "" : ",");
            first = FALSE;
        }
        if (mask & GAMI_EVENT_MASK_LOG) {
            g_string_append_printf (events, "%slog", first ? "" : ",");
            first = FALSE;
        }
        if (mask & GAMI_EVENT_MASK_USER) {
            g_string_append_printf (events, "%suser", first ? "" : ",");
            first = FALSE;
        }
        if (mask & GAMI_EVENT_MASK_CDR) {
            g_string_append_printf (events, "%scdr", first ? "" : ",");
            first = FALSE;
        }
    } else switch (mask) {
        case GAMI_EVENT_MASK_CALL:
        case GAMI_EVENT_MASK_CDR:
            g_string_printf (events, "call");
            break;
        case GAMI_EVENT_MASK_SYSTEM:
            g_string_printf (events, "system");
            break;
        case GAMI_EVENT_MASK_AGENT:
            g_string_printf (events, "agent");
            break;
        case GAMI_EVENT_MASK_LOG:
            g_string_printf (events, "log");
            break;
        case GAMI_EVENT_MASK_USER:
            g_string_printf (events, "user");
            break;
        default:
            g_string_printf (events, "on");
            break;
    }

    return g_string_free (events, FALSE);
}

static void join_originate_vars (gchar *key, gchar *value, GString *s)
{
    g_string_append_printf (s, "%s%s=%s", (s->len == 0)?"":",", key, value);
}

static void join_originate_vars_legacy (gchar *key, gchar *value, GString *s)
{
    g_string_append_printf (s, "%s%s=%s", (s->len == 0)?"":"|", key, value);
}

static void join_user_event_headers (gchar *key, gchar *value, GString *s)
{
    g_string_append_printf (s, "%s: %s\r\n", key, value);
}


/*
 * GObject boilerplate
 */

static void
gami_manager_init (GamiManager *object)
{
    GamiManagerPrivate *priv;

    priv = GAMI_MANAGER_PRIVATE (object);
    priv->connected = FALSE;
    priv->packet_buffer = g_queue_new ();
    g_hook_list_init (&priv->packet_hooks, sizeof (GHook));
}

static void
gami_manager_finalize (GObject *object)
{
    GamiManagerPrivate *priv;

    priv = GAMI_MANAGER_PRIVATE (object);

    while (g_source_remove_by_user_data (object));

    if (priv->socket) {
        g_io_channel_shutdown (priv->socket, TRUE, NULL);
        g_io_channel_unref (priv->socket);
    }

    g_queue_foreach (priv->packet_buffer, (GFunc) gami_packet_free, NULL);
    g_queue_free (priv->packet_buffer);

    g_hook_list_clear (&priv->packet_hooks);

    g_free (priv->host);
    g_free (priv->port);

    if (GAMI_MANAGER (object)->api_version)
        g_free ((gchar *) GAMI_MANAGER (object)->api_version);

    G_OBJECT_CLASS (gami_manager_parent_class)->finalize (object);
}

static void
gami_manager_get_property (GObject *obj, guint prop_id,
                           GValue *value, GParamSpec *pspec)
{
    GamiManager        *gami = (GamiManager *) obj;
    GamiManagerPrivate *priv = GAMI_MANAGER_PRIVATE (gami);

    switch (prop_id) {
        case HOST_PROP:
            g_value_set_string (value, priv->host);
            break;
        case PORT_PROP:
            g_value_set_string (value, priv->port);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
}

static void
gami_manager_set_property (GObject *obj, guint prop_id,
                           const GValue *value, GParamSpec *pspec)
{
    GamiManager        *gami = (GamiManager *) obj;
    GamiManagerPrivate *priv = GAMI_MANAGER_PRIVATE (gami);

    switch (prop_id) {
        case HOST_PROP:
            g_free (priv->host);
            priv->host = g_value_dup_string (value);
            break;
        case PORT_PROP:
            g_free (priv->port);
            priv->port = g_value_dup_string (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
}

static void
gami_manager_class_init (GamiManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (GamiManagerPrivate));

    object_class->set_property = gami_manager_set_property;
    object_class->get_property = gami_manager_get_property;
    object_class->finalize = gami_manager_finalize;

    /**
     * GamiManager:host:
     *
     * The Asterisk manager host to connect to
     **/
    g_object_class_install_property (object_class,
                                     HOST_PROP,
                                     g_param_spec_string ("host",
                                                          "manager host",
                                                          "manager host",
                                                          "localhost",
                                                          G_PARAM_CONSTRUCT_ONLY
                                                          | G_PARAM_READWRITE));

    /**
     * GamiManager:port:
     *
     * The Asterisk manager port to connect to
     **/
    g_object_class_install_property (object_class,
                                     PORT_PROP,
                                     g_param_spec_string ("port",
                                                          "manager port",
                                                          "manager port",
                                                          "5038",
                                                          G_PARAM_CONSTRUCT_ONLY
                                                          | G_PARAM_READWRITE));

    /**
     * GamiManager::connected:
     * @ami: The #GamiManager that received the signal
     *
     * The ::connected signal is emitted after successfully establishing 
     * a connection to the Asterisk server
     */
    signals [CONNECTED] = g_signal_new ("connected",
                                        G_TYPE_FROM_CLASS (object_class),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL,
                                        NULL,
                                        g_cclosure_marshal_VOID__VOID,
                                        G_TYPE_NONE,
                                        0);

    /**
     * GamiManager::disconnected
     * @ami: The #GamiManager that received the signal
     *
     * The ::disconnected event is emitted each time the connection to the 
     * Asterisk server is lost
     */
    signals [DISCONNECTED] = g_signal_new ("disconnected",
                                           G_TYPE_FROM_CLASS (object_class),
                                           G_SIGNAL_RUN_LAST,
                                           0,
                                           NULL,
                                           NULL,
                                           g_cclosure_marshal_VOID__VOID,
                                           G_TYPE_NONE,
                                           0);

    /**
     * GamiManager::event:
     * @ami: The #GamiManager that received the signal
     * @event: The event that occurred (stored as a #GHashTable)
     *
     * The ::event signal is emitted each time Asterisk emits an event
     */
    signals [EVENT] = g_signal_new ("event",
                                    G_TYPE_FROM_CLASS (object_class),
                                    G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL,
                                    NULL,
                                    g_cclosure_marshal_VOID__BOXED,
                                    G_TYPE_NONE,
                                    1, G_TYPE_HASH_TABLE);
}
