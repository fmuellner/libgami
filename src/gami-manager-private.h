#ifndef _GAMI_MANAGER_PRIVATE_H
#define _GAMI_MANAGER_PRIVATE_H

#include <glib.h>
#include <glib-object.h>
#include <gami-manager.h>
#include <gami-manager-types.h>
#include <gami-error.h>

struct _GamiManagerPrivate
{
    GIOChannel   *socket;
    gboolean      connected;
    gchar        *host;
    guint         port;

    gchar        *log_domain;

    GHookList     packet_hooks;
    GQueue       *packet_buffer;

    GAsyncResult *sync_result;
};

#define GAMI_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                                            GAMI_TYPE_MANAGER, \
                                                            GamiManagerPrivate))

enum {
    CONNECTED,
    DISCONNECTED,
    EVENT,
    LAST_SIGNAL
};

guint signals [LAST_SIGNAL];

typedef struct _GamiPacket GamiPacket;
struct _GamiPacket {
	gchar *raw;
	GHashTable *parsed;
	gboolean handled;
};

GamiPacket *
gami_packet_new (const gchar *raw_text);

void
gami_packet_free (GamiPacket *packet);

typedef struct _GamiHookData GamiHookData;
struct _GamiHookData {
	GamiPacket *packet;
	GAsyncResult *result;
    gchar *action_id;
	gpointer handler_data;
};

GamiHookData *
gami_hook_data_new (GAsyncResult *result,
                    gchar *action_id,
                    gpointer handler_data);
void
gami_hook_data_free (GamiHookData *data);

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

GSList *wait_queue_status_result (GamiManager *ami,
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
		   GHookCheckFunc handler,
                   gpointer handler_data,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   const gchar *action_name,
                   const gchar *first_param_name,
                   ...);

void
setup_action_hook (GamiManager *ami,
                   GamiAsyncFunc func,
		   GHookCheckFunc handler,
                   gpointer handler_data,
                   gchar *action_id,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   GError *error);

void
send_action_string (GamiManager *ami,
                    const gchar *action,
                    GError **error);

/* response callbacks used internally in synchronous mode */
void set_sync_result (GObject *ami, GAsyncResult *result, gpointer data);
gboolean check_response (GHashTable *p, const gchar *expected_value);

/* hook functions */
gboolean parse_packet      (gpointer data);
gboolean emit_event        (gpointer data);
gboolean bool_hook         (gpointer data);
gboolean string_hook       (gpointer data);
gboolean hash_hook         (gpointer data);
gboolean list_hook         (gpointer data);
gboolean text_hook         (gpointer data);
gboolean queues_hook       (gpointer data);
gboolean queue_rule_hook   (gpointer data);
gboolean queue_status_hook (gpointer data);
gboolean command_hook      (gpointer data);

gboolean reconnect_socket (GamiManager *ami);

#endif
