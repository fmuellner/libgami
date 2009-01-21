/* vi: se sw=4 ts=4 tw=80 fo+=t cin cinoptions=(0: */
/*
 * gami
 * Copyright (C) Florian Müllner 2008 <florian.muellner@gmail.com>
 * 
 * gami is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * gami is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include <gami-manager.h>

/**
 * SECTION: gami-manager
 * @short_description: An GObject based implementation of the Asterisk
 *         Manager Interface
 * @title: GAMI Manager
 * @stability: Unstable
 *
 * GamiManager is an implementation of the Asterisk Manager Interface based 
 * on GObject. It supports both synchronious and asynchronious operation 
 * and integrates well with glib's signal / callback system.
 *
 * Each manager action returns an #GamiResponse and takes at least four 
 * parameters common to each action: An optional ActionID as supported by the
 * underlying asterisk API, a callback function for asynchronious operation,
 * optional user data to pass to said function and an optional #GError to
 * report underlying network errors. If used asynchroniously, the response will
 * contain a boolean value indicating whether the action request was send
 * successfully. Otherwise, the action response will be returned directly.
 * 
 * Asynchronious callbacks and events require the use of #GMainLoop (or derived
 * implementations as gtk_main().
 */

struct _GamiManagerClass
{
	GObjectClass parent_class;
};

typedef struct _GamiManagerPrivate GamiManagerPrivate;
struct _GamiManagerPrivate
{
	GIOChannel *socket;
    gboolean block_events;
	gchar *host;
	gchar *port;
    gchar *username;
    gchar *secret;

    GamiResponseFunc response_func;
    GamiResponse *(*response_value_func) (GamiManager *ami, GHashTable *resp);
    gpointer response_data;
};

#define GAMI_MANAGER_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                                            GAMI_TYPE_MANAGER, \
                                                            GamiManagerPrivate))

enum {
	CONNECTED,
	DISCONNECTED,
	EVENT,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL];

G_DEFINE_TYPE (GamiManager, gami_manager, G_TYPE_OBJECT);

static gchar *event_string_from_mask (GamiManager *ami, GamiEventMask mask);

static int connect_socket (const gchar *host, const gchar *port);

static GIOStatus send_command (GIOChannel *c, const gchar *cmd, GError **e);
static GIOStatus receive_packet (GIOChannel *c, GHashTable **p, GError **e);
static gboolean reconnect_socket (GIOChannel *chan,
                                  GIOCondition cond,
                                  GamiManager *ami);

static gboolean dispatch_ami (GIOChannel *chan,
                              GIOCondition cond,
                              GamiManager *ami);

/* return value in synchronious operation */
static GamiResponse *ami_wait_response (GamiManager *ami, GError **error);
static GamiResponse *action_response (GamiManager *ami, GIOStatus status,
                                     const gchar *action_id, GError **error);

/* response functions to feed callbacks to "response" signal */
static GamiResponse *get_bool_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_ping_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_events_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_logoff_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_hash_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_challenge_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_getvar_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_dbget_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_zaplist_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_dahdilist_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_agentlist_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_parklist_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_meetmelist_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_siplist_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_iaxlist_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_sipregistrylist_response (GamiManager *ami,
                                                  GHashTable *resp);
static GamiResponse *get_statuslist_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_showchannelslist_response (GamiManager *ami,
                                                   GHashTable *resp);
static GamiResponse *get_queuelist_response (GamiManager *ami, GHashTable *resp);
static GamiResponse *get_voicemaillist_response (GamiManager *ami,
                                                GHashTable *resp);

static gboolean check_response (GHashTable *p, const gchar *expected_value);
static GSList *get_response_list (GIOChannel *chan, gchar *list_event,
                                  gchar *stop_event, gchar *check_num,
                                  GError **error);
static void join_originate_vars (gchar *key, gchar *value, GString *s);
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
    GError  *error = NULL;
    gchar   *welcome_message;
    gchar  **split_version;
    int sock;

    sock = connect_socket (host, port);

    if (sock == -1)
        return NULL;

    ami = g_object_new (GAMI_TYPE_MANAGER, NULL);
    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->host = g_strdup (host);
    priv->port = g_strdup (port);

#ifdef G_OS_WIN32
    priv->socket = g_io_channel_win32_new_socket (sock);
#else
    priv->socket = g_io_channel_unix_new (sock);
#endif

    /* read welcome message and set API */
    g_io_channel_read_line (priv->socket, &welcome_message, NULL, NULL, &error);
    if (error) {
        g_debug ("Error connecting to manager: %s", error->message);
        g_error_free (error);
        g_free (welcome_message);
        return NULL;
    }
    ami->api_version = g_strdup (g_strchomp (g_strrstr (welcome_message,
                                                        "/") + 1));
    g_free (welcome_message);

    split_version = g_strsplit (ami->api_version, ".", 2);
    ami->api_major = atoi (split_version [0]);
    ami->api_minor = atoi (split_version [1]);
    g_free (split_version);

    g_io_add_watch (priv->socket, G_IO_IN | G_IO_PRI,
                                       (GIOFunc) dispatch_ami, ami);
    g_io_add_watch (priv->socket, G_IO_HUP | G_IO_ERR,
                                      (GIOFunc) reconnect_socket, ami);

    return ami;
}

/*
 * Login/Logoff
 */

/**
 * gami_manager_login:
 * @ami: #GamiManager
 * @username: Username to use for authentification
 * @secret: Password to use for authentification
 * @auth_type: AuthType to use for authentification - if set to "md5", @secret
 *             is expected to contain an MD5 hash of the result string of 
 *             gami_manager_challenge() and the user's password
 * @events: Flags of type %GamiEventMask, indicating which events should be
 *          received initially. It is possible to modify this setting using the
 *          gami_manager_events() action
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Authenticate to asterisk and open a new manager session
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_login (GamiManager *ami, const gchar *username,
                    const gchar *secret, const gchar *auth_type,
                    GamiEventMask events, const gchar *action_id,
                    GamiResponseFunc response_func, gpointer response_data,
                    GError **error)
{
    GamiManagerPrivate *priv;
    GString  *action;
    gchar    *action_str;
    gchar    *event_str;
    GIOStatus iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (username != NULL && secret != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    if (priv->username)
        g_free (priv->username);
    priv->username = g_strdup (username);

    if (priv->secret)
        g_free (priv->secret);
    priv->secret = g_strdup (secret);

    action = g_string_new ("Action: Login\r\n");
    if (auth_type)
        g_string_append_printf (action, "AuthType: %s\r\n", auth_type);

    g_string_append_printf (action, "Username: %s\r\n%s: %s\r\n",
                            username, (auth_type) ? "Key" : "Secret", secret);

    event_str = event_string_from_mask (ami, events);
    g_string_append_printf (action, "Events: %s\r\n", event_str);
    g_free (event_str);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_logoff:
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Close the manager session and disconnect from asterisk
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_logoff (GamiManager *ami, const gchar *action_id,
                     GamiResponseFunc response_func, gpointer response_data,
                     GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_logoff_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Logoff\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");
    
    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 *  Get/Set Variables
 */

/**
 * gami_manager_get_var:
 * @ami: #GamiManager
 * @channel: (optional) Channel to retrieve variable from
 * @variable: Name of the variable to retrieve
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Get value of @variable (either from @channel or as global)
 *
 * Returns: value of @variable or %FALSE
 */
GamiResponse *
gami_manager_get_var (GamiManager *ami, const gchar *channel,
                      const gchar *variable, const gchar *action_id,
                      GamiResponseFunc response_func, gpointer response_data,
                      GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (variable != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_getvar_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: GetVar\r\n");
    g_string_append_printf (action, "Variable: %s\r\n", variable);

    if (channel != NULL)
        g_string_append_printf (action, "Channel: %s\r\n", channel);
    if (action_id != NULL)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_set_var:
 * @ami: #GamiManager
 * @channel: (optional) Channel to set variable for
 * @variable: Name of the variable to set
 * @value: New value for @variable
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set @variable (optionally on channel @channel) to @value
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_set_var (GamiManager *ami, const gchar *channel,
                      const gchar *variable, const gchar *value,
                      const gchar *action_id, GamiResponseFunc response_func,
                      gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (variable != NULL && value != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: SetVar\r\n");

    if (channel)
        g_string_append_printf (action, "Channel: %s\r\n", channel);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\n\n", action_id);

    g_string_append_printf (action, "Variable: %s\r\nValue: %s\r\n\r\n",
                            variable, value);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * Module handling
 */
 
/**
 * gami_manager_module_check:
 * @ami: #GamiManager
 * @module: Asterisk module name (not including extension)
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Check whether @module is loaded
 *
 * Returns: %TRUE if @module is loaded, %FALSE otherwise
 */
GamiResponse *
gami_manager_module_check (GamiManager *ami, const gchar *module,
                           const gchar *action_id,
                           GamiResponseFunc response_func,
                           gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (module != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ModuleCheck\r\n");
    g_string_append_printf (action, "Module: %s\r\n", module);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_module_load:
 * @ami: #GamiManager
 * @module: Asterisk module name (not including extension)
 * @load_type: Load action to perform (load, reload or unload)
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Perform action indicated by @load_type for @module
 *
 * Returns: %TRUE if @module is loaded, %FALSE otherwise
 */
GamiResponse *
gami_manager_module_load (GamiManager *ami, const gchar *module,
                          GamiModuleLoadType load_type, const gchar *action_id,
                          GamiResponseFunc response_func,
                          gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ModuleCheck\r\n");

    if (module)
        g_string_append_printf (action, "Module: %s\r\n", module);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    switch (load_type) {
        case GAMI_MODULE_LOAD:
            g_string_append (action, "LoadType: load\r\n");
            break;
        case GAMI_MODULE_RELOAD:
            g_string_append (action, "LoadType: reload\r\n");
            break;
        case GAMI_MODULE_UNLOAD:
            g_string_append (action, "LoadType: unload\r\n");
            break;
    }

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * Monitor channels
 */

/**
 * gami_manager_monitor:
 * @ami: #GamiManager
 * @channel: Channel to start monitoring
 * @file: (optional) Filename to use for recording
 * @format: (optional) Format to use for recording
 * @mix: (optional) Whether to mix in / out channel into one file
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Start monitoring @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_monitor (GamiManager *ami, const gchar *channel, const gchar *file,
                      const gchar *format, gboolean mix, const gchar *action_id,
                      GamiResponseFunc response_func, gpointer response_data,
                      GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Monitor\r\n");
    g_string_append_printf (action, "Channel: %s\r\n", channel);

    if (file != NULL)
        g_string_append_printf (action, "File: %s\r\n", file);
    if (format != NULL)
        g_string_append_printf (action, "Format: %s\r\n", format);
    if (mix)
        g_string_append (action, "Mix: 1\r\n");
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_change_monitor:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @file: New filename to use for recording
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Change the file name of the recording occuring on @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_change_monitor (GamiManager *ami, const gchar *channel,
                             const gchar *file, const gchar *action_id,
                             GamiResponseFunc response_func,
                             gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL && file != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ChangeMonitor\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Channel: %s\r\nFile: %s\r\n\r\n",
                            channel, file);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_stop_monitor:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Stop monitoring @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_stop_monitor (GamiManager *ami, const gchar *channel,
                           const gchar *action_id,
                           GamiResponseFunc response_func,
                           gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: StopMonitor\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Channel: %s\r\n\r\n", channel);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_pause_monitor:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Pause monitoring of @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_pause_monitor (GamiManager *ami, const gchar *channel,
                            const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: PauseMonitor\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Channel: %s\r\n\r\n", channel);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_unpause_monitor:
 * @ami: #GamiManager
 * @channel: Monitored channel
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Continue monitoring of @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_unpause_monitor (GamiManager *ami, const gchar *channel,
                              const gchar *action_id,
                              GamiResponseFunc response_func, 
                              gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: UnpauseMonitor\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Channel: %s\r\n\r\n", channel);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * Meetme
 */

/**
 * gami_manager_meetme_mute:
 * @ami: #GamiManager
 * @meetme: The MeetMe conference bridge number
 * @user_num: The user number in the specified bridge
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Mutes @user_num in conference @meetme
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_meetme_mute (GamiManager *ami, const gchar *meetme,
                          const gchar *user_num, const gchar *action_id,
                          GamiResponseFunc response_func,
                          gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (meetme != NULL && user_num != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: MeetmeMute\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Meetme: %s\r\nUserNum: %s\r\n\r\n",
                            meetme, user_num);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_meetme_unmute:
 * @ami: #GamiManager
 * @meetme: The MeetMe conference bridge number
 * @user_num: The user number in the specified bridge
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Unmutes @user_num in conference @meetme
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_meetme_unmute (GamiManager *ami, const gchar *meetme,
                            const gchar *user_num, const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (meetme != NULL && user_num != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: MeetmeUnmute\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Meetme: %s\r\nUserNum: %s\r\n\r\n",
                            meetme, user_num);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_meetme_list:
 * @ami: #GamiManager
 * @meetme: The MeetMe conference bridge number
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * List al users in conference @meetme
 *
 * Returns: #GSList of user information (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_meetme_list (GamiManager *ami, const gchar *meetme,
                          const gchar *action_id,
                          GamiResponseFunc response_func,
                          gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_meetmelist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: MeetmeList\r\n");

    if (meetme)
        g_string_append_printf (action, "Conference: %s", meetme);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * Queue management
 */

/**
 * gami_manager_queue_add:
 * @ami: #GamiManager
 * @queue: Existing queue to add member
 * @interface: Member interface to add to @queue
 * @penalty: Penalty for new member
 * @paused: whether @interface should be initially paused
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Add @interface to @queue
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_queue_add (GamiManager *ami, const gchar *queue,
                        const gchar *interface, guint penalty, gboolean paused,
                        const gchar *action_id, GamiResponseFunc response_func,
                        gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (queue != NULL && interface != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: QueueAdd\r\n");
    g_string_append_printf (action, "Queue: %s\r\nInterface: %s\r\n",
                            queue, interface);

    if (penalty)
        g_string_append_printf (action, "Penalty: %d\r\n", penalty);
    if (paused)
        g_string_append (action, "Paused: 1\r\n");
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_queue_remove:
 * @ami: #GamiManager
 * @queue: Existing queue to remove member from
 * @interface: Member interface to remove from @queue
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Remove @interface from @queue
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_queue_remove (GamiManager *ami, const gchar *queue,
                           const gchar *interface, const gchar *action_id,
                           GamiResponseFunc response_func,
                           gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (queue != NULL && interface != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: QueueRemove\r\n");
    g_string_append_printf (action, "Queue: %s\r\nInterface: %s\r\n",
                            queue, interface);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_queue_pause:
 * @ami: #GamiManager
 * @queue: (optional) Existing queue for which @interface should be (un)paused
 * @interface: Member interface (un)pause
 * @paused: Whether to pause or unpause @interface
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * (Un)pause @interface
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_queue_pause (GamiManager *ami, const gchar *queue,
                          const gchar *interface, gboolean paused,
                          const gchar *action_id,
                          GamiResponseFunc response_func, 
                          gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (interface != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: QueueAdd\r\n");
    g_string_append_printf (action, "Interface: %s\r\nPaused: %d\r\n",
                            interface, paused ? 1: 0);

    if (queue)
        g_string_append_printf (action, "Queue: %s\r\n", queue);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_queue_penalty:
 * @ami: #GamiManager
 * @queue: (optional) Limit @penalty change to existing queue
 * @interface: Member interface change penalty for
 * @penalty: New penalty to set for @interface
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Change the penalty value of @interface
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_queue_penalty (GamiManager *ami, const gchar *queue,
                            const gchar *interface, guint penalty,
                            const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (interface != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: QueuePenalty\r\n");
    g_string_append_printf (action, "Interface: %s\r\nPenalty: %d\r\n",
                            interface, penalty);

    if (queue)
        g_string_append_printf (action, "Queue: %s\r\n", queue);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_queue_summary:
 * @ami: #GamiManager
 * @queue: (optional) Only send summary information for @queue
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Get summary of queue statistics
 *
 * Returns: #GSList of queue statistics (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_queue_summary (GamiManager *ami, const gchar *queue,
                            const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_queuelist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: QueueSummary\r\n");

    if (queue)
        g_string_append_printf (action, "Queue: %s\r\n", queue);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_queue_log:
 * @ami: #GamiManager
 * @queue: Queue to generate queue_log entry for
 * @event: Log event to generate
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Generate a queue_log entry for @queue
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_queue_log (GamiManager *ami, const gchar *queue,
                        const gchar *event, const gchar *action_id,
                        GamiResponseFunc response_func, gpointer response_data,
                        GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (queue != NULL && event != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: QueueLog\r\n");
    g_string_append_printf (action, "Queue: %s\r\nEvent: %s\r\n",
                            queue, event);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

#if 0
GSList *
gami_manager_queue_status (GamiManager *ami, const gchar *queue,
                          const gchar *action_id, GError **error)
{
    GamiManagerPrivate *priv;
    GString *action;
    gchar *action_str;

    GSList *list = NULL;
    gboolean list_complete = FALSE;

    g_return_val_if_fail (ami != NULL && GAMI_IS_MANAGER (ami), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    priv = GAMI_MANAGER_PRIVATE (ami);

    action = g_string_new ("Action: QueueStatus\r\n");

    if (queue)
        g_string_append_printf (action, "Queue: %s\r\n", queue);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    if (send_command (priv->socket, action_str, error) != G_IO_STATUS_NORMAL) {
        g_assert (error == NULL || *error != NULL);
        return NULL;
    }

    g_assert (error == NULL || *error == NULL);

    g_free (action_str);

    if (! check_response (priv->socket, "Response", "Success", error))
        return NULL;

    g_assert (error == NULL || *error == NULL);

    while (! list_complete) {
        GIOStatus status;
        GHashTable *packet = NULL;
        gchar *event;

        while ((status = receive_packet (priv->socket,
                                         &packet,
                                         error)) == G_IO_STATUS_AGAIN);

        if (status != G_IO_STATUS_NORMAL) {
            g_assert (error == NULL || *error != NULL);

			if (list) {
				g_slist_foreach (list, (GFunc)g_hash_table_destroy, NULL);
				g_slist_free (list);
			}

            return NULL;
        }

        g_assert (error == NULL || *error == NULL);

        event = g_hash_table_lookup (packet, "Event");
        if (! strcmp (event, "QueueParam")) {
            g_hash_table_remove (packet, "Event");
            list = g_slist_prepend (list, packet);
            packet = NULL;
        } else if (! strcmp (event, "QueueStatusComplete")) {
            list_complete = TRUE;
            g_hash_table_destroy (packet);
        } else {
            /* this is just a test, we should get rid of this longterm */
            //printf ("Ups, unexpected event in get_response_list(): %s\n",
            //        event);
            g_hash_table_destroy (packet);
            packet = NULL;
        }
    }

    list = g_slist_reverse (list);

    return list;
}
#endif


/*
 * ZAP Channels
 */

/**
 * gami_manager_zap_dial_offhook
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel on which to dial @number
 * @number: The number to dial
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Dial over ZAP channel while offhook
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_zap_dial_offhook (GamiManager *ami, const gchar *zap_channel,
                               const gchar *number, const gchar *action_id,
                               GamiResponseFunc response_func,
                               gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (zap_channel != NULL && number != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ZapDialOffhook\r\n");
    g_string_append_printf (action, "ZapChannel: %s\r\nNumber: %s\r\n",
                            zap_channel, number);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_zap_hangup
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel to hang up
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Hangup ZAP channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_zap_hangup (GamiManager *ami, const gchar *zap_channel,
                         const gchar *action_id, GamiResponseFunc response_func,
                         gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (zap_channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ZapHangup\r\n");
    g_string_append_printf (action, "ZapChannel: %s\r\n", zap_channel);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_zap_dnd_on
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel on which to turn on DND status
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set DND (Do Not Disturb) status on @zap_channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_zap_dnd_on (GamiManager *ami, const gchar *zap_channel,
                         const gchar *action_id, GamiResponseFunc response_func,
                         gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (zap_channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ZapDNDOn\r\n");
    g_string_append_printf (action, "ZapChannel: %s\r\n", zap_channel);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_zap_dnd_off
 * @ami: #GamiManager
 * @zap_channel: The ZAP channel on which to turn off DND status
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set DND (Do Not Disturb) status on @zap_channel to off
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_zap_dnd_off (GamiManager *ami, const gchar *zap_channel,
                          const gchar *action_id,
                          GamiResponseFunc response_func,
                          gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (zap_channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ZapDNDOff\r\n");
    g_string_append_printf (action, "ZapChannel: %s\r\n", zap_channel);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_zap_show_channels
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Show the status of all ZAP channels
 *
 * Returns: #GSList of ZAP channels (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_zap_show_channels (GamiManager *ami, const gchar *action_id,
                                GamiResponseFunc response_func,
                                gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_zaplist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ZapShowChannels\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_zap_transfer
 * @ami: #GamiManager
 * @zap_channel: The channel to be transferred
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Transfer ZAP channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_zap_transfer (GamiManager *ami, const gchar *zap_channel,
                           const gchar *action_id, 
                           GamiResponseFunc response_func,
                           gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (zap_channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ZapTransfer\r\n");
    g_string_append_printf (action, "ZapChannel: %s\r\n", zap_channel);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_zap_restart
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Restart ZAP channels. Any active calls will be terminated
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_zap_restart (GamiManager *ami, const gchar *action_id,
                          GamiResponseFunc response_func,
                          gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ZapRestart\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * DAHDI
 */

/**
 * gami_manager_dahdi_dial_offhook
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel on which to dial @number
 * @number: The number to dial
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Dial over DAHDI channel while offhook
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_dahdi_dial_offhook (GamiManager *ami, const gchar *dahdi_channel,
                                 const gchar *number, const gchar *action_id,
                                 GamiResponseFunc response_func,
                                 gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (dahdi_channel != NULL && number != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DAHDIDialOffhook\r\n");
    g_string_append_printf (action, "DAHDIChannel: %s\r\nNumber: %s\r\n",
                            dahdi_channel, number);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_dahdi_hangup
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel to hang up
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Hangup DAHDI channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_dahdi_hangup (GamiManager *ami, const gchar *dahdi_channel,
                           const gchar *action_id,
                           GamiResponseFunc response_func,
                           gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (dahdi_channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DAHDIHangup\r\n");
    g_string_append_printf (action, "DAHDIChannel: %s\r\n", dahdi_channel);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_dahdi_dnd_on
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel on which to turn on DND status
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set DND (Do Not Disturb) status on @dahdi_channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_dahdi_dnd_on (GamiManager *ami, const gchar *dahdi_channel,
                           const gchar *action_id,
                           GamiResponseFunc response_func,
                           gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (dahdi_channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DAHDIDNDOn\r\n");
    g_string_append_printf (action, "DAHDIChannel: %s\r\n", dahdi_channel);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_dahdi_dnd_off
 * @ami: #GamiManager
 * @dahdi_channel: The DAHDI channel on which to turn off DND status
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set DND (Do Not Disturb) status on @dahdi_channel to off
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_dahdi_dnd_off (GamiManager *ami, const gchar *dahdi_channel,
                            const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (dahdi_channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DAHDIDNDOff\r\n");
    g_string_append_printf (action, "DAHDIChannel: %s\r\n", dahdi_channel);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_dahdi_show_channels
 * @ami: #GamiManager
 * @dahdi_channel: (optional) Limit status information to this channel
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Show the status of all DAHDI channels
 *
 * Returns: #GSList of DAHDI channels (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_dahdi_show_channels (GamiManager *ami, const gchar *dahdi_channel,
                                  const gchar *action_id,
                                  GamiResponseFunc response_func,
                                  gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_dahdilist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DAHDIShowChannels\r\n");

    if (dahdi_channel)
        g_string_append_printf (action, "DAHDIChannel: %s\r\n", dahdi_channel);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_dahdi_transfer
 * @ami: #GamiManager
 * @dahdi_channel: The channel to be transferred
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Transfer DAHDI channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_dahdi_transfer (GamiManager *ami, const gchar *dahdi_channel,
                             const gchar *action_id,
                             GamiResponseFunc response_func,
                             gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (dahdi_channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DAHDITransfer\r\n");
    g_string_append_printf (action, "DAHDIChannel: %s\r\n", dahdi_channel);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_dahdi_restart
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Restart DAHDI channels. Any active calls will be terminated
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_dahdi_restart (GamiManager *ami, const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DAHDIRestart\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * Agents
 */

/**
 * gami_manager_agents
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * List information about all configured agents and their status
 *
 * Returns: #GSList of agents (stored as #GHashTable) on success,
 *           %NULL on failure
 */
GamiResponse *
gami_manager_agents (GamiManager *ami, const gchar *action_id, 
                     GamiResponseFunc response_func, gpointer response_data,
                     GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_agentlist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Agents\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_agent_callback_login
 * @ami: #GamiManager
 * @agent: The ID of the agent to log in
 * @exten: The extension to use as callback
 * @context: (optional) The context to use as callback
 * @ack_call: (optional) Whether calls should be acknowledged by the agent
 *            (by pressing #)
 * @wrapup_time: (optional) The minimum amount of time after hangup before the
 *            agent will receive a new call
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Log in @agent and register callback to @exten (note that the action has 
 * been deprecated in asterisk-1.4 and was removed in asterisk-1.6)
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_agent_callback_login (GamiManager *ami, const gchar *agent,
                                   const gchar *exten, const gchar *context,
                                   gboolean ack_call, guint wrapup_time,
                                   const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (agent != NULL && exten != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: AgentCallbackLogin\r\n");
    g_string_append_printf (action, "Agent: %s\r\nExten: %s\r\n", agent, exten);

    if (context)
        g_string_append_printf (action, "Context: %s\r\n", context);
    if (ack_call)
        g_string_append (action, "AckCall: 1\r\n");
    if (wrapup_time)
        g_string_append_printf (action, "WrapupTime: %d\r\n", wrapup_time);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_agent_logoff
 * @ami: #GamiManager
 * @agent: The ID of the agent to log off
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Log off @agent
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_agent_logoff (GamiManager *ami, const gchar *agent,
                           const gchar *action_id,
                           GamiResponseFunc response_func,
                           gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (agent != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: AgentLogoff\r\n");
    g_string_append_printf (action, "Agent: %s\r\n", agent);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/*
 * DB
 */

/**
 * gami_manager_db_get
 * @ami: #GamiManager
 * @family: The AstDB key family from which to retrieve the value
 * @key: The name of the AstDB key
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve value of AstDB entry @family/@key
 *
 * Returns: the value of @family/@key on success, %FALSE on failure
 */
GamiResponse *
gami_manager_db_get (GamiManager *ami, const gchar *family, const gchar *key,
                     const gchar *action_id,
                     GamiResponseFunc response_func, gpointer response_data,
                     GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (family != NULL && key != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_dbget_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DBGet\r\n");
    g_string_append_printf (action, "Family: %s\r\nKey: %s\r\n", family, key);

    if (action_id != NULL)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_db_put
 * @ami: #GamiManager
 * @family: The AstDB key family in which to set the value
 * @key: The name of the AstDB key
 * @val: The value to assign to the key
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set AstDB entry @family/@key to @value
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_db_put (GamiManager *ami, const gchar *family, const gchar *key,
                     const gchar *val, const gchar *action_id,
                     GamiResponseFunc response_func, gpointer response_data,
                     GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (family != NULL && key != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DBPut\r\n");
    g_string_append_printf (action, "Family: %s\r\nKey: %s\r\n", family, key);

    if (val)
        g_string_append_printf (action, "Val: %s\r\n", val);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_db_del
 * @ami: #GamiManager
 * @family: The AstDB key family in which to delete the key
 * @key: The name of the AstDB key to delete
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Remove AstDB entry @family/@key
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_db_del (GamiManager *ami, const gchar *family, const gchar *key,
                     const gchar *action_id, GamiResponseFunc response_func,
                     gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (family != NULL && key != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DBDel\r\n");
    g_string_append_printf (action, "Family: %s\r\nKey: %s\r\n", family, key);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_db_del_tree
 * @ami: #GamiManager
 * @family: The AstDB key family to delete
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Remove AstDB key family
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_db_del_tree (GamiManager *ami, const gchar *family,
                          const gchar *action_id,
                          GamiResponseFunc response_func,
                          gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (family != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: DBDelTree\r\n");
    g_string_append_printf (action, "Family: %s\r\n", family);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * Call Parking
 */

/**
 * gami_manager_park
 * @ami: #GamiManager
 * @channel: Channel name to park
 * @channel2: Channel to announce park info to (and return the call to if the
 *            parking times out)
 * @timeout: (optional) Milliseconds to wait before callback
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Park a channel in the parking lot
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_park (GamiManager *ami, const gchar *channel,
                   const gchar *channel2, guint timeout, const gchar *action_id,
                   GamiResponseFunc response_func, gpointer response_data,
                   GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL && channel2 != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Park\r\n");
    g_string_append_printf (action, "Channel: %s\r\nChannel2: %s\r\n",
                            channel, channel2);

    if (timeout)
        g_string_append_printf (action, "Timeout: %d\r\n", timeout);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_parked_calls
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of parked calls
 *
 * Returns: #GSList of parked calls (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_parked_calls (GamiManager *ami, const gchar *action_id,
                           GamiResponseFunc response_func,
                           gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_parklist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ParkedCalls\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * Mailboxes
 */

/**
 * gami_manager_voicemail_users_list
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of voicemail users
 *
 * Returns: #GSList of voicemail users (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_voicemail_users_list (GamiManager *ami, const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_voicemaillist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: VoicemailUsersList\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_mailbox_count
 * @ami: #GamiManager
 * @mailbox: The mailbox to check messages for
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve count of new and old messages in @mailbox
 *
 * Returns: #GHashTable with message counts on success, %NULL on failure
 */
GamiResponse *
gami_manager_mailbox_count (GamiManager *ami, const gchar *mailbox,
                            const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (mailbox != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: MailboxCount\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Mailbox: %s\r\n\r\n", mailbox);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_mailbox_status
 * @ami: #GamiManager
 * @mailbox: The mailbox to check status for
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Check the status of @mailbox
 *
 * Returns: #GHashTable with status variables on success, %NULL on failure
 */
GamiResponse *
gami_manager_mailbox_status (GamiManager *ami, const gchar *mailbox,
                             const gchar *action_id,
                             GamiResponseFunc response_func,
                             gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (mailbox != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: MailboxStatus\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Mailbox: %s\r\n\r\n", mailbox);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * Core
 */

/**
 * gami_manager_core_status
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve information about the current PBX core status (as active calls,
 * startup time etc.)
 *
 * Returns: #GHashTable with status variables on success, %NULL on failure
 */
GamiResponse *
gami_manager_core_status (GamiManager *ami, const gchar *action_id,
                          GamiResponseFunc response_func,
                          gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: CoreStatus\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_core_show_channels
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of currently active channels
 *
 * Returns: #GSList of active channels (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_core_show_channels (GamiManager *ami, const gchar *action_id,
                                 GamiResponseFunc response_func,
                                 gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_showchannelslist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: CoreShowChannels\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_core_settings
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve information about PBX core settings (as Asterisk/GAMI version etc.)
 *
 * Returns: #GHashTable with settings variables on success, %NULL on failure
 */
GamiResponse *
gami_manager_core_settings (GamiManager *ami, const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: CoreSettings\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/*
 * Misc (TODO: Sort these out and order properly)
 */

/**
 * gami_manager_iax_peer_list
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of IAX2 peers
 *
 * Returns: #GSList of IAX2 peers (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_iax_peer_list (GamiManager *ami, const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_iaxlist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: IAXpeerlist\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_sip_peers
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a list of SIP peers
 *
 * Returns: #GSList of SIP peers (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_sip_peers (GamiManager *ami, const gchar *action_id,
                        GamiResponseFunc response_func, gpointer response_data,
                        GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_siplist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: SIPpeers\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_sip_show_peer
 * @ami: #GamiManager
 * @peer: SIP peer to get status information for
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve status information for @peer
 *
 * Returns: #GHashTable of peer status information on success, %NULL on failure
 */
GamiResponse *
gami_manager_sip_show_peer (GamiManager *ami, const gchar *peer,
                            const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (peer != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: SIPShowPeer\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Peer: %s\r\n\r\n", peer);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_sip_show_registry
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve registry information of SIP peers
 *
 * Returns: #GSList of registry information (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_sip_show_registry (GamiManager *ami, const gchar *action_id,
                                GamiResponseFunc response_func,
                                gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_sipregistrylist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: SIPshowregistry\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_status
 * @ami: #GamiManager
 * @channel: (optional) Only retrieve status information for this channel
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve status information of active channels (or @channel)
 *
 * Returns: #GSList of status information (stored as #GHashTable) on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_status (GamiManager *ami, const gchar *channel,
                     const gchar *action_id,
                     GamiResponseFunc response_func, gpointer response_data, 
                     GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_statuslist_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Status\r\n");

    if (channel)
        g_string_append_printf (action, "Channel: %s\r\n", channel);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_extension_state
 * @ami: #GamiManager
 * @exten: The name of the extension to check
 * @context: The context of the extension to check
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Check extension state of @exten@@context - if hints are properly configured
 * on the server, the action will report the status of the device connected to
 * @exten
 *
 * Returns: #GHashTable of status information on success, %NULL on failure
 */
GamiResponse *
gami_manager_extension_state (GamiManager *ami, const gchar *exten,
                              const gchar *context, const gchar *action_id,
                              GamiResponseFunc response_func,
                              gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (exten != NULL && context != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ExtensionState\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Exten: %s\r\nContext: %s\r\n\r\n",
                            exten, context);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_ping
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Query the Asterisk server to make sure it is still responding. May be used
 * to keep the connection alive
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_ping (GamiManager *ami, const gchar *action_id,
                   GamiResponseFunc response_func, gpointer response_data,
                   GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = (ami->api_major
                                 && ami->api_minor) ? get_bool_response
                                                    : get_ping_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Ping\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_absolute_timeout
 * @ami: #GamiManager
 * @channel: The name of the channel to set the timeout for
 * @timeout: The maximum duration of the current call, in seconds
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set timeout for call on @channel to @timeout seconds
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_absolute_timeout (GamiManager *ami, const gchar *channel,
                               gint timeout, const gchar *action_id,
                               GamiResponseFunc response_func,
                               gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: AbsoluteTimeout\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Channel: %s\r\nTimeout: %d\r\n\r\n",
                            channel, timeout);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_challenge
 * @ami: #GamiManager
 * @auth_type: The authentification type to generate challenge for (e.g. "md5")
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Retrieve a challenge string to use for authentification of type @auth_type
 *
 * Returns: the generated challenge on success, %FALSE on failure
 */
GamiResponse *
gami_manager_challenge (GamiManager *ami, const gchar *auth_type,
                        const gchar *action_id, GamiResponseFunc response_func,
                        gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (auth_type != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_challenge_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Challenge\r\n");
    g_string_append_printf (action, "AuthType: %s\r\n", auth_type);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_set_cdr_user_field
 * @ami: #GamiManager
 * @channel: The name of the channel to set @user_field for
 * @user_field: The value for the CDR user field
 * @append: (optional) Whether to append @user_field to current value
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set CDR user field for @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_set_cdr_user_field (GamiManager *ami, const gchar *channel,
                                 const gchar *user_field, gboolean append,
                                 const gchar *action_id,
                                 GamiResponseFunc response_func,
                                 gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL && user_field != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: SetCDRUserField\r\n");

    if (append)
        g_string_append (action, "Append: 1\r\n");
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Channel: %s\r\nUserField: %s\r\n\r\n",
                            channel, user_field);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_reload
 * @ami: #GamiManager
 * @module: (optional) The name of the module to reload (not including
 *           extension)
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Reload @module or all modules
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_reload (GamiManager *ami, const gchar *module,
                     const gchar *action_id, GamiResponseFunc response_func,
                     gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Reload\r\n");

    if (module)
        g_string_append_printf (action, "Module: %s\r\n", module);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_hangup
 * @ami: #GamiManager
 * @channel: The name of the channel to hang up
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Hang up @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_hangup (GamiManager *ami, const gchar *channel,
                     const gchar *action_id,
                     GamiResponseFunc response_func, gpointer response_data,
                     GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Hangup\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Channel: %s\r\n\r\n", channel);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_redirect
 * @ami: #GamiManager
 * @channel: The name of the channel redirect
 * @extra_channel: (optional) Second call leg to transfer
 * @exten: The extension @channel should be redirected to
 * @context: The context @channel should be redirected to
 * @priority: The priority @channel should be redirected to
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Redirect @channel to @exten@@context:@priority
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_redirect (GamiManager *ami, const gchar *channel,
                       const gchar *extra_channel, const gchar *exten,
                       const gchar *context, const gchar *priority,
                       const gchar *action_id,
                       GamiResponseFunc response_func, gpointer response_data,
                       GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL);
    g_assert (exten != NULL && context != NULL && priority != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Redirect\r\n");
    g_string_append_printf (action, "Channel: %s\r\n", channel);

    if (extra_channel)
        g_string_append_printf (action, "ExtraChannel: %s\r\n", extra_channel);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Exten: %s\r\nContext: %s\r\n"
                            "Priority: %s\r\n\r\n", exten, context, priority);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_bridge
 * @ami: #GamiManager
 * @channel1: The name of the channel to bridge to @channel2
 * @channel2: The name of the channel to bridge to @channel1
 * @tone: Whether to play courtesy tone to @channel2
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Bridge together the existing channels @channel1 and @channel2
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_bridge (GamiManager *ami, const gchar *channel1,
                     const gchar *channel2, gboolean tone,
                     const gchar *action_id, GamiResponseFunc response_func,
                     gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel1 != NULL && channel2 != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Bridge\r\n");
    g_string_append_printf (action, "Channel1: %s\r\nChannel2: %s\r\n",
                            channel1, channel2);

    g_string_append_printf (action, "Tone: %s\r\n", tone ? "Yes" : "No");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_agi
 * @ami: #GamiManager
 * @channel: The name of the channel to execute @command in
 * @command: The name of the AGI command to execute
 * @command_id: (optional) CommandID for matching in AGI notification events
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Execute AGI command @command in @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_agi (GamiManager *ami, const gchar *channel, const gchar *command,
                  const gchar *command_id, const gchar *action_id,
                  GamiResponseFunc response_func, gpointer response_data, 
                  GError **error)
{
    GamiManagerPrivate *priv;
    GString *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL && command != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: AGI\r\n");
    g_string_append_printf (action, "Channel: %s\r\nCommand: %s\r\n",
                            channel, command);

    if (command_id)
        g_string_append_printf (action, "CommandID: %s\r\n", command_id);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_send_text
 * @ami: #GamiManager
 * @channel: The name of the channel to send @message to
 * @message: The message to send to @channel
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Send @message to @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_send_text (GamiManager *ami, const gchar *channel,
                        const gchar *message, const gchar *action_id,
                        GamiResponseFunc response_func, gpointer response_data,
                        GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL && message != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: SendText\r\n");
    g_string_append_printf (action, "Channel: %s\r\nMessage: %s\r\n",
                            channel, message);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_jabber_send
 * @ami: #GamiManager
 * @jabber: Jabber / GTalk account to send message from
 * @screen_name: Jabber / GTalk account to send message to
 * @message: The message to send
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Send @message from Jabber / GTalk account @jabber to account @screen_name
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_jabber_send (GamiManager *ami, const gchar *jabber,
                          const gchar *screen_name, const gchar *message,
                          const gchar *action_id,
                          GamiResponseFunc response_func,
                          gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (jabber != NULL && screen_name != NULL && message != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: JabberSend\r\n");
    g_string_append_printf (action, "Jabber: %s\r\nScreenName: %s\r\n",
                            jabber, screen_name);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Message: %s\r\n\r\n", message);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_play_dtmf
 * @ami: #GamiManager
 * @channel: The name of the channel to send @digit to
 * @digit: The DTMF digit to play on @channel
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Play a DTMF digit @digit on @channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_play_dtmf (GamiManager *ami, const gchar *channel, gchar digit,
                        const gchar *action_id,
                        GamiResponseFunc response_func, gpointer response_data,
                        GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: PlayDTMF\r\n");
    g_string_append_printf (action, "Channel: %s\r\n", channel);

    if (digit)
        g_string_append_printf (action, "Digit: %c\r\n", digit);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_list_commands
 * @ami: #GamiManager
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * List available Asterisk manager commands - the available actions may vary
 * between different versions of Asterisk and due to the set of loaded modules
 *
 * Returns: A #GHashTable of action names / descriptions on success, 
 *          %NULL on failure
 */
GamiResponse *
gami_manager_list_commands (GamiManager *ami, const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ListCommands\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_list_categories
 * @ami: #GamiManager
 * @filename: The name of the configuration file to list categories for
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * List categories in @filename
 *
 * Returns: A #GHashTable of category number / name on success, 
 *          %NULL on failure
 */
GamiResponse *
gami_manager_list_categories (GamiManager *ami, const gchar *filename,
                              const gchar *action_id,
                              GamiResponseFunc response_func,
                              gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (filename != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: ListCategories\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Filename: %s\r\n\r\n", filename);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_get_config
 * @ami: #GamiManager
 * @filename: The name of the configuration file to get content for
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Get content of configuration file @filename
 *
 * Returns: A #GHashTable of line number / values on success, 
 *          %NULL on failure
 */
GamiResponse *
gami_manager_get_config (GamiManager *ami, const gchar *filename,
                         const gchar *action_id,
                         GamiResponseFunc response_func,
                         gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (filename != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: GetConfig\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Filename: %s\r\n\r\n", filename);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_get_config_json
 * @ami: #GamiManager
 * @filename: The name of the configuration file to get content for
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Get content of configuration file @filename as JS hash for use with JSON
 *
 * Returns: A #GHashTable with file dump on success,
 *          %NULL on failure
 */
GamiResponse *
gami_manager_get_config_json (GamiManager *ami, const gchar *filename,
                              const gchar *action_id,
                              GamiResponseFunc response_func,
                              gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (filename != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_hash_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: GetConfigJSON\r\n");

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append_printf (action, "Filename: %s\r\n\r\n", filename);

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_create_config
 * @ami: #GamiManager
 * @filename: The name of the configuration file to create
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Create an empty configurion file @filename
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_create_config (GamiManager *ami, const gchar *filename,
                            const gchar *action_id,
                            GamiResponseFunc response_func,
                            gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (filename != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: CreateConfig\r\n");
    g_string_append_printf (action, "Filename: %s\r\n", filename);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_originate
 * @ami: #GamiManager
 * @channel: The name of the channel to call. Once the channel has answered,
 *           the call will be passed to the specified exten/context/priority or
 *           application/data
 * @application_exten: Extension to dial or application to call (depending on
 *                     @priority)
 * @data_context: Context to dial or data to pass to application (depending on
 *                @priority)
 * @priority: (optional) Priority to dial - if %NULL, @application_exten will
 *            be interpretated as application and @data_context as data
 * @timeout: (optional) Time to wait for @channel to answer in milliseconds
 * @caller_id: (optional) CallerID to set on the outgoing channel
 * @account: (optional) AccountCode to set for the call
 * @variables: (optional) A #GHashTable with name / value pairs to pass as 
 *             channel variables
 * @async: (optional) Whether to originate call asynchronously - this allows
 *         to originate further calls before a response is received
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Generate an outbound call from Asterisk and connect the channel to
 * Exten / Context / Priority or execute Application (Data) on the channel
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_originate (GamiManager *ami, const gchar *channel,
                        const gchar *application_exten,
                        const gchar *data_context, const gchar *priority,
                        guint timeout, const gchar *caller_id,
                        const gchar *account, const GHashTable *variables,
                        gboolean async, const gchar *action_id,
                        GamiResponseFunc response_func, gpointer response_data,
                        GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (channel != NULL);
    g_assert (application_exten != NULL && data_context != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Originate\r\n");
    g_string_append_printf (action, "Channel: %s\r\n", channel);

    if (priority)
        g_string_append_printf (action, "Exten: %s\r\nContext: %s\r\n"
                                "Priority: %s\r\n", application_exten,
                                data_context, priority);
    else
        g_string_append_printf (action, "Application: %s\r\nData: %s\r\n",
                                application_exten, data_context);
    if (timeout)
        g_string_append_printf (action, "Timeout: %d\r\n", timeout);
    if (caller_id)
        g_string_append_printf (action, "CallerID: %s\r\n", caller_id);
    if (account)
        g_string_append_printf (action, "Account: %s\r\n", account);
    if (variables) {
        GString *vars = g_string_new ("");
        gchar *var_str;

        g_hash_table_foreach ((GHashTable *) variables,
                              (GHFunc) join_originate_vars, vars);
        var_str = g_string_free (vars, FALSE);
        g_string_append_printf (action, "Variable: %s\r\n", var_str);
        g_free (var_str);
    }
    if (async)
        g_string_append (action, "Async: 1\r\n");
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
 * gami_manager_events
 * @ami: #GamiManager
 * @event_mask: #GamiEventMask to set for the connection
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Set #GamiEventMask for the connection to control which events shall be
 * received
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_events (GamiManager *ami, const GamiEventMask event_mask,
                     const gchar *action_id,
                     GamiResponseFunc response_func, gpointer response_data,
                     GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    gchar     *event_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = (ami->api_major
                                 && ami->api_minor) ? get_bool_response
                                                    : get_events_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: Events\r\n");

    event_str = event_string_from_mask (ami, event_mask);
    g_string_append_printf (action, "EventMask: %s\r\n", event_str);
    g_free (event_str);

    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
* gami_manager_user_event
 * @ami: #GamiManager
 * @user_event: The user defined event to send
 * @headers: (optional) Optional header to add to the event
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Send the user defined event @user_event with an optional payload of @headers
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_user_event (GamiManager *ami, const gchar *user_event,
                         const GHashTable *headers, const gchar *action_id,
                         GamiResponseFunc response_func,
                         gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));
    g_assert (user_event != NULL);

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: UserEvent\r\n");
    g_string_append_printf (action, "UserEvent: %s\r\n", user_event);

    if (headers) {
        GString *header = g_string_new ("");
        gchar *header_str;

        g_hash_table_foreach ((GHashTable *) headers,
                              (GHFunc) join_user_event_headers, header);
        header_str = g_string_free (header, FALSE);
        g_string_append_printf (action, "%s", header_str);
        g_free (header_str);
    }
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}

/**
* gami_manager_wait_event
 * @ami: #GamiManager
 * @timeout: (optional) Maximum time to wait for events in seconds
 * @action_id: (optional) ActionID to ease response matching
 * @response_func: Callback for asynchronious operation. Passing %NULL will 
 *           trigger synchronious mode
 * @response_data: User data to pass to the callback. If %NULL is passed for 
 *           @response_func, the parameter is ignored
 * @error: A location to return an error of type #GIOChannelError
 *
 * Wait for an event to occur
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
GamiResponse *
gami_manager_wait_event (GamiManager *ami, guint timeout,
                         const gchar *action_id,
                         GamiResponseFunc response_func,
                         gpointer response_data, GError **error)
{
    GamiManagerPrivate *priv;
    GString   *action;
    gchar     *action_str;
    GIOStatus  iostatus;

    g_assert (error == NULL || *error == NULL);
    g_assert (ami   != NULL && GAMI_IS_MANAGER (ami));

    priv = GAMI_MANAGER_PRIVATE (ami);

    priv->response_value_func = get_bool_response;
    priv->response_func = response_func;
    priv->response_data = response_data;

    action = g_string_new ("Action: WaitEvent\r\n");

    if (timeout)
        g_string_append_printf (action, "Timeout: %d\r\n", timeout);
    if (action_id)
        g_string_append_printf (action, "ActionID: %s\r\n", action_id);

    g_string_append (action, "\r\n");

    action_str = g_string_free (action, FALSE);

    iostatus = send_command (priv->socket, action_str, error);
    g_free (action_str);

    return action_response (ami, iostatus, action_id, error);
}


/*
 * Private API
 */

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

/* note: I have no idea if I got the w32 stuff right */
int
connect_socket (const gchar *host, const gchar *port)
{
    struct addrinfo hints;
    struct addrinfo *rp, *result;
#ifdef G_OS_WIN32
    SOCKET sock = INVALID_SOCKET;
#else
    int sock = -1;
#endif

#ifdef G_OS_WIN32
    ZeroMemory (&hints, sizeof (hints));
#else
    memset (&hints, 0, sizeof (struct addrinfo));
#endif
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    //hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo (host, port, &hints, &result) != 0) {
        g_warning ("Error resolving host '%s'", host);
        return sock;
    }

    for (rp = result; rp; rp = rp->ai_next) {
        sock = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);

#ifdef G_OS_WIN32
        if (sock == INVALID_SOCKET)
            continue;
#else
        if (sock == -1)
            continue;
#endif

        if (connect (sock, rp->ai_addr, rp->ai_addrlen) != -1)
            break;   /* Bingo! */

#ifdef G_OS_WIN32
        closesocket (sock);
        sock = INVALID_SOCKET;
#else
        close (sock);
        sock = -1;
#endif
    }

    if (rp == NULL) {
        /* Error */
    }

    freeaddrinfo (result);

    return sock;
}

static GIOStatus
send_command (GIOChannel *channel, const gchar *command, GError **error)
{
    GIOStatus status;
    gchar **cmd_lines, **cmd_line;

    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    g_debug ("Sending GAMI command");

    cmd_lines = g_strsplit (command, "\r", 0);
    for (cmd_line = cmd_lines; *cmd_line; cmd_line++)
        if (g_strchug (*cmd_line) && g_strcmp0 (*cmd_line, ""))
            g_debug ("   %s", *cmd_line);
    g_strfreev (cmd_lines);

    while ((status = g_io_channel_write_chars (channel,
                                               command,
                                               -1,
                                               NULL,
                                               error)) == G_IO_STATUS_AGAIN);

    g_debug ("GAMI command sent");

    if (status != G_IO_STATUS_NORMAL) {
        g_assert (error == NULL || *error != NULL);
        return status;
    }

    g_assert (error == NULL || *error == NULL);

    while ((status = g_io_channel_flush (channel, error)) == G_IO_STATUS_AGAIN);

    return status;
}

static GIOStatus
receive_packet (GIOChannel *chan, GHashTable **pkt, GError **error)
{
    GIOStatus status;
    gboolean package_received;

    g_assert (pkt != NULL && *pkt == NULL);
    g_assert (error == NULL || *error == NULL);

    *pkt = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    g_debug ("Reveiving an GAMI packet");

    package_received = FALSE;
    while (! package_received) {
        gchar *line;
        gchar **tokens;

        while ((status = g_io_channel_read_line (chan, &line,
                                                 NULL, NULL,
                                                 error)) == G_IO_STATUS_AGAIN);

        if (status != G_IO_STATUS_NORMAL) {
            g_assert (error == NULL || *error != NULL);
            g_warning ("An error occurred during package reception\n");
            g_free (line);
            break;
        }

        g_assert (error == NULL || *error == NULL);

        tokens = g_strsplit (line, ":", 2);
        if (g_strv_length (tokens) == 2) {
            gchar *key, *value;

            key = g_strdup (g_strstrip (tokens [0]));
            value = g_strdup (g_strstrip (tokens [1]));

            g_hash_table_insert (*pkt, key, value);
        }
        g_strfreev (tokens);

        if (strcmp (line, "\r\n") == 0)
            package_received = TRUE;
        else
            g_debug ("   %s", g_strchomp (line));


        g_free (line);
    }

    g_debug ("GAMI packet received");

    if (status != G_IO_STATUS_NORMAL) {
        g_hash_table_destroy (*pkt);
        *pkt = NULL;
    }

    return status;
}

static gboolean
reconnect_socket (GIOChannel *chan, GIOCondition cond, GamiManager *mgr)
{
    GamiManagerPrivate *priv;

    priv = GAMI_MANAGER_PRIVATE (mgr);

    if (cond == G_IO_HUP || cond == G_IO_ERR) {
        int sock;

        g_io_channel_shutdown (chan, TRUE, NULL);
        g_io_channel_unref (chan);

        sock = connect_socket (priv->host, priv->port);
#ifdef G_OS_WIN32
        priv->socket = g_io_channel_win32_new_socket (sock);
#else
        priv->socket = g_io_channel_unix_new (sock);
#endif

        g_io_add_watch (priv->socket, G_IO_IN | G_IO_PRI,
                        (GIOFunc) dispatch_ami, mgr);
        g_io_add_watch (priv->socket, G_IO_HUP | G_IO_ERR,
                        (GIOFunc) reconnect_socket, mgr);

        if (priv->username && priv->secret) {
            gami_manager_login (mgr, priv->username, priv->secret, NULL,
                               TRUE, NULL, NULL, NULL, NULL);
        }
    }

    return TRUE;
}

static gboolean
dispatch_ami (GIOChannel *chan, GIOCondition cond, GamiManager *mgr)
{
    GamiManagerPrivate *priv;

    priv = GAMI_MANAGER_PRIVATE (mgr);

    if (cond == G_IO_IN || cond == G_IO_PRI) {
        GHashTable *packet = NULL;
        GIOStatus status;

        while ((status = receive_packet (chan,
                                         &packet,
                                         NULL)) == G_IO_STATUS_AGAIN);

		if (packet) {
            if (status == G_IO_STATUS_NORMAL) {
                if (g_hash_table_lookup (packet, "Event"))
                    g_signal_emit (mgr, signals [EVENT], 0, packet);
                else if (g_hash_table_lookup (packet, "Response"))
                    if (priv->response_func) {
                        GamiResponse *resp;
                        resp = priv->response_value_func (mgr, packet);
                        priv->response_func (resp, priv->response_data);
                        gami_response_unref (resp);
                    }
            }

			g_hash_table_unref (packet);
        }
    }

    return TRUE;
}

static GamiResponse *
action_response (GamiManager *mgr, GIOStatus status, const gchar *action_id,
                 GError **error)
{
    GamiManagerPrivate *priv;

    priv = GAMI_MANAGER_PRIVATE (mgr);

    if (status != G_IO_STATUS_NORMAL || priv->response_func) {
        GValue      *value;

        value = g_new0 (GValue, 1);
        value = g_value_init (value, G_TYPE_BOOLEAN);

        g_value_set_boolean (value, status == G_IO_STATUS_NORMAL);

        return gami_response_new (value, NULL, (gchar *) action_id);
    }

    return ami_wait_response (mgr, error);
}

static GamiResponse *
ami_wait_response (GamiManager *mgr, GError **error)
{
    GamiManagerPrivate *priv;
    GamiResponse *response = NULL;
    GHashTable *pkt = NULL;
    GIOStatus status;

    priv = GAMI_MANAGER_PRIVATE (mgr);

    while (! response) {
        while ((status = receive_packet (priv->socket,
                                         &pkt,
                                         NULL)) == G_IO_STATUS_AGAIN);

        if (pkt) {
            if (status == G_IO_STATUS_NORMAL
                && g_hash_table_lookup (pkt, "Response"))
                response = priv->response_value_func (mgr, pkt);

            g_hash_table_unref (pkt);
            pkt = NULL;
        }
    }

    return response;
}

static GamiResponse *
get_bool_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *action_id;
    gchar       *message;

    value = g_new0 (GValue, 1);
    value = g_value_init (value, G_TYPE_BOOLEAN);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success"))
        g_value_set_boolean (value, FALSE);
    else
        g_value_set_boolean (value, TRUE);

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_ping_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;

    value = g_new0 (GValue, 1);
    value = g_value_init (value, G_TYPE_BOOLEAN);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Pong"))
        g_value_set_boolean (value, FALSE);
    else
        g_value_set_boolean (value, TRUE);

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_events_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;

    value = g_new0 (GValue, 1);
    value = g_value_init (value, G_TYPE_BOOLEAN);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Events Off"))
        g_value_set_boolean (value, FALSE);
    else
        g_value_set_boolean (value, TRUE);

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_logoff_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;

    value = g_new0 (GValue, 1);
    value = g_value_init (value, G_TYPE_BOOLEAN);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Goodbye"))
        g_value_set_boolean (value, FALSE);
    else
        g_value_set_boolean (value, TRUE);

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_hash_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;

    value = g_new0 (GValue, 1);

    message = g_strdup (g_hash_table_lookup (pkt, "Message"));
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        g_hash_table_remove (pkt, "Response");
        g_hash_table_remove (pkt, "Message");

        value = g_value_init (value, G_TYPE_HASH_TABLE);
        g_value_set_boxed (value, g_hash_table_ref (pkt));
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_challenge_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    gchar       *resp_value;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");
    resp_value = g_hash_table_lookup (pkt, "Challenge");

    if (! check_response (pkt, "Success") || ! resp_value) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        value = g_value_init (value, G_TYPE_STRING);
        g_value_set_string (value, strdup (resp_value));
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_getvar_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    gchar       *resp_value;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");
    resp_value = g_hash_table_lookup (pkt, "Value");

    if (! check_response (pkt, "Success") || ! resp_value) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        value = g_value_init (value, G_TYPE_STRING);
        g_value_set_string (value, strdup (resp_value));
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_dbget_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    gchar       *resp_value;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");
    resp_value = g_hash_table_lookup (pkt, "Val");

    if (! check_response (pkt, "Success") || ! resp_value) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        value = g_value_init (value, G_TYPE_STRING);
        g_value_set_string (value, strdup (resp_value));
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_zaplist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list  = get_response_list (priv->socket, "ZapShowChannels",
                                  "ZapShowChannelsComplete", NULL, &error);

       if (list) {
           value = g_value_init (value, G_TYPE_SLIST);
           g_value_set_boxed (value, list);
       } else {
           value = g_value_init (value, G_TYPE_BOOLEAN);
           g_value_set_boolean (value, FALSE);
           
           if (error) {
               message = g_strdup (error->message);
               g_error_free (error);
           }
       }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_dahdilist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list  = get_response_list (priv->socket, "DAHDIShowChannels",
                                  "DAHDIShowChannelsComplete", "Items", &error);

       if (list) {
           value = g_value_init (value, G_TYPE_SLIST);
           g_value_set_boxed (value, list);
       } else {
           value = g_value_init (value, G_TYPE_BOOLEAN);
           g_value_set_boolean (value, FALSE);
           
           if (error) {
               message = g_strdup (error->message);
               g_error_free (error);
           }
       }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_agentlist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list = get_response_list (priv->socket, "Agents", "AgentsComplete",
                                 NULL, &error);

       if (list) {
           value = g_value_init (value, G_TYPE_SLIST);
           g_value_set_boxed (value, list);
       } else {
           value = g_value_init (value, G_TYPE_BOOLEAN);
           g_value_set_boolean (value, FALSE);
           
           if (error) {
               message = g_strdup (error->message);
               g_error_free (error);
           }
       }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_parklist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;

        priv = GAMI_MANAGER_PRIVATE (mgr);
        list = get_response_list (priv->socket, "ParkedCall",
                                  "ParkedCallsComplete", NULL, &error);

        if (list) {
            value = g_value_init (value, G_TYPE_SLIST);
            g_value_set_boxed (value, list);
        } else {
            value = g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, FALSE);

            if (error) {
                message = g_strdup (error->message);
                g_error_free (error);
            }
        }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_meetmelist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list = get_response_list (priv->socket, "MeetmeList",
                                 "MeetmeListComplete",
                                 "ListItems", &error);

        if (list) {
            value = g_value_init (value, G_TYPE_SLIST);
            g_value_set_boxed (value, list);
        } else {
            value = g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, FALSE);

            if (error) {
                message = g_strdup (error->message);
                g_error_free (error);
            }
        }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_siplist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list = get_response_list (priv->socket, "PeerEntry", "PeerlistComplete",
                                 "ListItems", &error);

        if (list) {
            value = g_value_init (value, G_TYPE_SLIST);
            g_value_set_boxed (value, list);
        } else {
            value = g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, FALSE);

            if (error) {
                message = g_strdup (error->message);
                g_error_free (error);
            }
        }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_iaxlist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list = get_response_list (priv->socket, "PeerEntry", "PeerlistComplete",
                                 "ListItems", &error);

        if (list) {
            value = g_value_init (value, G_TYPE_SLIST);
            g_value_set_boxed (value, list);
        } else {
            value = g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, FALSE);

            if (error) {
                message = g_strdup (error->message);
                g_error_free (error);
            }
        }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_showchannelslist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list = get_response_list (priv->socket, NULL,
                                 "CoreShowChannelsComplete",
                                 "ListItems", &error);

        if (list) {
            value = g_value_init (value, G_TYPE_SLIST);
            g_value_set_boxed (value, list);
        } else {
            value = g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, FALSE);

            if (error) {
                message = g_strdup (error->message);
                g_error_free (error);
            }
        }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_sipregistrylist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list = get_response_list (priv->socket, "RegistryEntry",
                                 "RegistrationsComplete", "ListItems", &error);

        if (list) {
            value = g_value_init (value, G_TYPE_SLIST);
            g_value_set_boxed (value, list);
        } else {
            value = g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, FALSE);

            if (error) {
                message = g_strdup (error->message);
                g_error_free (error);
            }
        }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_statuslist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list = get_response_list (priv->socket, "Status", "StatusComplete",
                                 NULL, &error);

        if (list) {
            value = g_value_init (value, G_TYPE_SLIST);
            g_value_set_boxed (value, list);
        } else {
            value = g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, FALSE);

            if (error) {
                message = g_strdup (error->message);
                g_error_free (error);
            }
        }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_queuelist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list = get_response_list (priv->socket, "QueueSummary",
                                 "QueueSummaryComplete", NULL, &error);

        if (list) {
            value = g_value_init (value, G_TYPE_SLIST);
            g_value_set_boxed (value, list);
        } else {
            value = g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, FALSE);

            if (error) {
                message = g_strdup (error->message);
                g_error_free (error);
            }
        }
    }

    return gami_response_new (value, message, action_id);
}

static GamiResponse *
get_voicemaillist_response (GamiManager *mgr, GHashTable *pkt)
{
    GValue      *value;
    gchar       *message;
    gchar       *action_id;
    GError      *error = NULL;

    value = g_new0 (GValue, 1);

    message = g_hash_table_lookup (pkt, "Message");
    action_id = g_hash_table_lookup (pkt, "ActionID");

    if (! check_response (pkt, "Success")) {
        value = g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, FALSE);
    } else {
        GamiManagerPrivate *priv;
        GSList *list;
       
       priv = GAMI_MANAGER_PRIVATE (mgr);
       list = get_response_list (priv->socket, "VoicemailUserEntry",
                                 "VoicemailUserEntryComplete", NULL, &error);

        if (list) {
            value = g_value_init (value, G_TYPE_SLIST);
            g_value_set_boxed (value, list);
        } else {
            value = g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, FALSE);

            if (error) {
                message = g_strdup (error->message);
                g_error_free (error);
            }
        }
    }

    return gami_response_new (value, message, action_id);
}

static gboolean
check_response (GHashTable *pkt, const gchar *value)
{
    g_return_val_if_fail (pkt != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    if (g_strcmp0 (g_hash_table_lookup (pkt, "Response"), value) != 0) {
        return FALSE;
    }
    return TRUE;
}

static GSList *
get_response_list (GIOChannel *chan, gchar *list_event, gchar *stop_event,
                   gchar *check_num, GError **error)
{
    GSList *list = NULL;
    gint listitems = -1;
    gboolean list_complete = FALSE;

    g_assert (error == NULL || *error == NULL);

    while (! list_complete) {
        GIOStatus status;
        GHashTable *packet = NULL;
        gchar *event;

        while ((status = receive_packet (chan,
                                         &packet,
                                         error)) == G_IO_STATUS_AGAIN);

        if (status != G_IO_STATUS_NORMAL) {
            g_assert (error == NULL || *error != NULL);

			if (list) {
				g_slist_foreach (list, (GFunc)g_hash_table_destroy, NULL);
				g_slist_free (list);
			}

            return NULL;
        }

        g_assert (error == NULL || *error == NULL);

        event = g_hash_table_lookup (packet, "Event");
        if (event) {
            if (list_event && ! strcmp (event, list_event)) {
                g_hash_table_remove (packet, "Event");
                list = g_slist_prepend (list, packet);
                packet = NULL;
            } else if (! strcmp (event, stop_event)) {
                list_complete = TRUE;
                if (check_num)
                    listitems = atoi (g_hash_table_lookup (packet, check_num));
                g_hash_table_destroy (packet);
            } else {
                /* this is just a test, we should get rid of this longterm */
                //printf ("Ups, unexpected event in get_response_list(): %s\n",
                //        event);
                g_hash_table_destroy (packet);
                packet = NULL;
            }
        } else if (! list_event) {
            list = g_slist_prepend (list, packet);
            packet = NULL;
        }
    }

    if (listitems != -1 && listitems != g_slist_length (list)) {
        g_warning ("Wrong element number in list, expected %d, received %d",
                   listitems, g_slist_length (list));
    }

    list = g_slist_reverse (list);

    return list;
}

static void join_originate_vars (gchar *key, gchar *value, GString *s)
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

    priv->block_events = FALSE;
}

static void
gami_manager_finalize (GObject *object)
{
    GamiManagerPrivate *priv;

    priv = GAMI_MANAGER_PRIVATE (object);

    if (priv->socket) {
        while (g_source_remove_by_user_data (object));
        g_io_channel_shutdown (priv->socket, TRUE, NULL);
        g_debug ("Socket has been shut down");
        g_io_channel_unref (priv->socket);
        g_debug ("Channel has been unreffed");
    }
    g_free (priv->host);
    g_free (priv->port);
    if (GAMI_MANAGER (object)->api_version)
        g_free ((gchar *) GAMI_MANAGER (object)->api_version);
    g_debug ("Member variables have been freed");

	G_OBJECT_CLASS (gami_manager_parent_class)->finalize (object);
}

static void
gami_manager_class_init (GamiManagerClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (GamiManagerPrivate));

	object_class->finalize = gami_manager_finalize;

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

GType
gami_event_mask_get_type (void)
{
    static GType etype = 0;
    if (etype == 0) {
        static const GEnumValue values [] = {
            { GAMI_EVENT_MASK_NONE, "GAMI_EVENT_MASK_NONE", "none" },
            { GAMI_EVENT_MASK_CALL, "GAMI_EVENT_MASK_CALL", "call" },
            { GAMI_EVENT_MASK_SYSTEM, "GAMI_EVENT_MASK_SYSTEM", "system" },
            { GAMI_EVENT_MASK_AGENT, "GAMI_EVENT_MASK_AGENT", "agent" },
            { GAMI_EVENT_MASK_LOG, "GAMI_EVENT_MASK_LOG", "log" },
            { GAMI_EVENT_MASK_USER, "GAMI_EVENT_MASK_USER", "user" },
            { GAMI_EVENT_MASK_ALL, "GAMI_EVENT_MASK_ALL", "all"},
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static ("GamiEventMask", values);
    }
    return etype;
}

GType
gami_module_load_type_get_type (void)
{
    static GType etype = 0;
    if (etype == 0) {
        static const GEnumValue values [] = {
            { GAMI_MODULE_LOAD, "GAMI_MODULE_LOAD", "load" },
            { GAMI_MODULE_RELOAD, "GAMI_MODULE_RELOAD", "reload" },
            { GAMI_MODULE_UNLOAD, "GAMI_MODULE_UNLOAD", "unload" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static ("GamiModuleLoadType", values);
    }
    return etype;
}
