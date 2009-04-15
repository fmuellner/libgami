#ifndef _GAMI_MANAGER_PRIVATE_H
#define _GAMI_MANAGER_PRIVATE_H

#include <glib.h>
#include <glib-object.h>
#include <gami-manager.h>

typedef struct _GamiManagerPrivate GamiManagerPrivate;
struct _GamiManagerPrivate
{
    GIOChannel *socket;
    gboolean connected;
    gchar *host;
    gchar *port;

    GHashTable *action_hooks;
    GQueue *buffer;

    GAsyncResult *sync_result;
};

#define GAMI_MANAGER_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                                            GAMI_TYPE_MANAGER, \
                                                            GamiManagerPrivate))

typedef enum {
    GAMI_RESPONSE_TYPE_BOOL,
    GAMI_RESPONSE_TYPE_STRING,
    GAMI_RESPONSE_TYPE_HASH,
    GAMI_RESPONSE_TYPE_LIST
} GamiResponseType;

typedef struct _GamiActionHook GamiActionHook;
struct _GamiActionHook
{
    GamiResponseType type;
    gpointer  handler_data;

    GAsyncResult *result;
};

enum {
    CONNECTED,
    DISCONNECTED,
    EVENT,
    LAST_SIGNAL
};

static guint signals [LAST_SIGNAL];


/* prototypes for finish functions of asynchronous actions */
typedef gboolean (*GamiBoolFinishFunc) (GamiManager *,
                                        GAsyncResult *,
                                        GError **);
typedef gchar *(*GamiStringFinishFunc) (GamiManager *,
                                        GAsyncResult *,
                                        GError **);
typedef GHashTable *(*GamiHashFinishFunc) (GamiManager *,
                                           GAsyncResult *,
                                           GError **);
typedef GSList *(*GamiListFinishFunc) (GamiManager *,
                                       GAsyncResult *,
                                       GError **);

gboolean dispatch_ami (GIOChannel *chan,
                              GIOCondition cond,
                              GamiManager *ami);
gboolean process_packets (GamiManager *manager);

/* FIXME: _must_ we expose this one? */
void add_action_hook (GamiManager *mgr, gchar *action_id, GamiActionHook *hook);

/* initialize action hooks */
GamiActionHook *bool_action_hook_new (GAsyncResult *result,
                                      gpointer handler_data);
GamiActionHook *string_action_hook_new (GAsyncResult *result,
                                        gpointer handler_data);
GamiActionHook *hash_action_hook_new (GAsyncResult *result);
GamiActionHook *list_action_hook_new (GAsyncResult *result,
                                      gpointer handler_data);


typedef void (*GamiAsyncFunc)           (GamiManager *ami);

gchar *build_action_string_valist (const gchar *action,
                                   gchar **action_id,
                                   const gchar *first_prop_name,
                                   va_list varargs);

gchar *build_action_string (const gchar *action,
                            gchar **action_id,
                            const gchar *first_prop_name,
                            ...);

/* functions returning result of synchronous actions */
gboolean wait_bool_result (GamiManager *ami,
                           GamiBoolFinishFunc func,
                           GError **error);

gchar *wait_string_result (GamiManager *ami,
                           GamiStringFinishFunc func,
                           GError **error);

GHashTable *wait_hash_result (GamiManager *ami,
                              GamiHashFinishFunc func,
                              GError **error);

GSList *wait_list_result (GamiManager *ami,
                          GamiListFinishFunc func,
                          GError **error);

/* finish functions */
gboolean bool_action_finish (GamiManager *ami,
                             GAsyncResult *result,
                             GamiAsyncFunc func,
                             GError **error);

gchar *string_action_finish (GamiManager *ami,
                             GAsyncResult *result,
                             GamiAsyncFunc func,
                             GError **error);

GHashTable *hash_action_finish (GamiManager *ami,
                                GAsyncResult *result,
                                GamiAsyncFunc func,
                                GError **error);

GSList *list_action_finish (GamiManager *ami,
                            GAsyncResult *result,
                            GamiAsyncFunc func,
                            GError **error);

void
send_async_action (GamiManager *ami,
                   GamiAsyncFunc func,
                   GamiResponseType type,
                   gpointer handler_data,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   const gchar *action_name,
                   const gchar *first_param_name,
                   ...);

void
setup_action_hook (GamiManager *ami,
                   GamiAsyncFunc func,
                   GamiResponseType type,
                   gpointer handler_data,
                   gchar *action_id,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   GError *error);

void
send_action_string (const gchar *action,
                    GIOChannel *channel,
                    GError **error);
/* internal response functions to feed callbacks */
gboolean process_bool_response (GHashTable *packet, gpointer expected);
gchar *process_string_response (GHashTable *packet, gpointer return_key);
GHashTable *process_hash_response (GHashTable *packet);
gboolean process_list_response (GHashTable *packet,
                                gpointer stop_event, GSList **resp);

/* response callbacks used internally in synchronous mode */
void set_sync_result (GObject *ami, GAsyncResult *result, gpointer data);
gboolean check_response (GHashTable *p, const gchar *expected_value);


gboolean reconnect_socket (GamiManager *ami);

#endif
