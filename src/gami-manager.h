/* vi: se sw=4 ts=4 tw=80 fo+=t cin cinoptions=(0 : */
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

#if !defined(__GAMI_H_INSIDE__) && !defined (GAMI_COMPILATION)
#  error "Only <gami.h> can be included directly."
#endif

#ifndef __GAMI_MANAGER_H__
#define __GAMI_MANAGER_H__

#include <glib.h>
#include <glib-object.h>

#include "gami-response.h"

G_BEGIN_DECLS

/**
 * GamiEventMask:
 * @GAMI_EVENT_MASK_NONE: do not receive any events
 * @GAMI_EVENT_MASK_CALL: do receive 'call' events
 * @GAMI_EVENT_MASK_CDR:  do receive 'cdr' events
 * @GAMI_EVENT_MASK_SYSTEM: do receive 'system' events
 * @GAMI_EVENT_MASK_AGENT: do receive 'agent' events
 * @GAMI_EVENT_MASK_LOG: do receive 'log' events
 * @GAMI_EVENT_MASK_USER: do receive 'user' events
 * @GAMI_EVENT_MASK_ALL: do receive all events
 *
 * Flag values to specify any events your application is interested in as 
 * passed to gami_manager_login() and gami_manager_events().
 */
typedef enum
{
	GAMI_EVENT_MASK_NONE   = 0,
	GAMI_EVENT_MASK_CALL   = 1 << 0,
	GAMI_EVENT_MASK_CDR    = 1 << 1,
	GAMI_EVENT_MASK_SYSTEM = 1 << 2,
	GAMI_EVENT_MASK_AGENT  = 1 << 3,
	GAMI_EVENT_MASK_LOG    = 1 << 4,
	GAMI_EVENT_MASK_USER   = 1 << 5,
	GAMI_EVENT_MASK_ALL    = 1 << 6
} GamiEventMask;

/**
 * gami_event_mask_get_type:
 *
 * Get the #GType of enum #GamiEventMask
 *
 * Returns: #GType of #GamiEventMask
 */
GType gami_event_mask_get_type (void);

/**
 * GAMI_TYPE_EVENT_MASK:
 *
 * Get the #GType of enum #GamiEventMask
 *
 * Returns: #GType of #GamiEventMask
 */
#define GAMI_TYPE_EVENT_MASK (gami_event_mask_get_type ())

/**
 * GamiModuleLoadType:
 * @GAMI_MODULE_LOAD: use module operation 'load'
 * @GAMI_MODULE_RELOAD: use module operation 'reload'
 * @GAMI_MODULE_UNLOAD: use module operation 'unload'
 *
 * An enum type used to determine the operation mode in
 * gami_manager_module_load()
 */
typedef enum {
	GAMI_MODULE_LOAD,
	GAMI_MODULE_RELOAD,
	GAMI_MODULE_UNLOAD
} GamiModuleLoadType;

/**
 * gami_module_load_type_get_type:
 *
 * Get the #GType of enum #GamiModuleLoadType
 *
 * Returns: The #GType of #GamiModuleLoadType
 */
GType gami_module_load_type_get_type (void);

/**
 * GAMI_TYPE_MODULE_LOAD_TYPE:
 *
 * Get the #GType of enum #GamiModuleLoadType
 *
 * Returns: The #GType of #GamiModuleLoadType
 */
#define GAMI_TYPE_MODULE_LOAD_TYPE (gami_module_load_type_get_type ())

/**
 * GAMI_TYPE_MANAGER:
 *
 * Get the #GType of #GamiManager
 *
 * Returns: The #GType of #GamiManager
 */
#define GAMI_TYPE_MANAGER  (gami_manager_get_type ())
/**
 * GAMI_MANAGER:
 * @object: Object which is subject to casting
 *
 * Cast a #GamiManager derived pointer into a (GamiManager *) pointer
 */
#define GAMI_MANAGER(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), \
														  GAMI_TYPE_MANAGER, \
														  GamiManager))
/**
 * GAMI_MANAGER_CLASS:
 * @klass: a valid #GamiManagerClass
 *
 * Cast a derived #GamiManagerClass structure into a #GamiManagerClass structure
 */
#define GAMI_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), \
															GAMI_TYPE_MANAGER, \
															GamiManagerClass))
/**
 * GAMI_IS_MANAGER:
 * @object: Instance to check for being a %GAMI_TYPE_MANAGER
 *
 * Check whether a valid #GTypeInstance pointer is of type %GAMI_TYPE_MANAGER
 *
 * Returns: %FALSE or %TRUE, indicating whether @object is a %GAMI_TYPE_MANAGER
 */
#define GAMI_IS_MANAGER(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), \
															 GAMI_TYPE_MANAGER))
/**
 * GAMI_IS_MANAGER_CLASS:
 * @klass: a #GamiManager instance
 *
 * Get the class structure associated to a #GamiManager instance.
 *
 * Returns: pointer to object class structure
 */
#define GAMI_IS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), \
                                                             GAMI_TYPE_MANAGER))
/**
 * GAMI_MANAGER_GET_CLASS:
 * @object: Object to return the type id for
 *
 * Get the type id of an object
 *
 * Returns: Type id of @object
 */
#define GAMI_MANAGER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), \
                                                            GAMI_TYPE_MANAGER, \
                                                            GamiManagerClass))

/**
 * GamiManager:
 * @parent_instance: #GObject parent instance
 * @api_version: AMI API version string as sent by Asterisk
 * @api_major: Major number of AMI API version
 * @api_minor: Minor number of AMI API version
 *
 * #GamiManager represents a connection to an Asterisk server using the manager
 * API. It is used to send actions to the server and receive responses and
 * events.
 */
typedef struct _GamiManager GamiManager;

/**
 * GamiManagerClass:
 * @parent_class: #GamiManager's parent class (of type #GObjectClass)
 *
 * The class structure for the #GamiManager type
 */
typedef struct _GamiManagerClass GamiManagerClass;

struct _GamiManager
{
    GObject parent_instance;

    const gchar *api_version;
    guint  api_major;
    guint  api_minor;
};

/**
 * GamiResponseFunc:
 * @response: the action's #GamiResponse.
 * @user_data: user data passed to the action
 *
 * Specifies the type of functions passed as callback to manager actions
 */
typedef void (*GamiResponseFunc) (GamiResponse *response,
								  gpointer user_data);

/**
 * gami_manager_get_type:
 *
 * Get the #GType of #GamiManager
 *
 * Returns: The #GType of #GamiManager
 */
GType gami_manager_get_type (void) G_GNUC_CONST;

GamiManager *gami_manager_new (const gchar *host, const gchar *port);

GamiResponse *gami_manager_login  (GamiManager *ami, const gchar *username,
                                   const gchar *secret, GamiEventMask events,
                                   const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_logoff (GamiManager *ami, const gchar *action_id, 
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error);

GamiResponse *gami_manager_get_var (GamiManager *ami, const gchar *channel,
                                    const gchar *variable,
                                    const gchar *action_id,
                                    GamiResponseFunc response_func,
                                    gpointer response_data, GError **error); 
GamiResponse *gami_manager_set_var (GamiManager *ami, const gchar *channel,
                                    const gchar *variable, const gchar *value,
                                    const gchar *action_id,
                                    GamiResponseFunc response_func,
                                    gpointer response_data, GError **error);

GamiResponse *gami_manager_module_check (GamiManager *ami, const gchar *module,
                                         const gchar *action_id,
                                         GamiResponseFunc response_func,
                                         gpointer response_data,
                                         GError **error);
GamiResponse *gami_manager_module_load (GamiManager *ami, const gchar *module,
                                        GamiModuleLoadType load_type,
                                        const gchar *action_id,
                                        GamiResponseFunc response_func,
                                        gpointer response_data, GError **error);

GamiResponse *gami_manager_monitor (GamiManager *ami, const gchar *channel,
                                    const gchar *file, const gchar *format,
                                    gboolean mix, const gchar *action_id,
                                    GamiResponseFunc response_func,
                                    gpointer response_data, GError **error);
GamiResponse *gami_manager_change_monitor (GamiManager *ami,
                                           const gchar *channel,
                                           const gchar *file,
                                           const gchar *action_id,
                                           GamiResponseFunc response_func,
                                           gpointer response_data,
                                           GError **error);
GamiResponse *gami_manager_stop_monitor (GamiManager *ami, const gchar *channel,
                                         const gchar *action_id,
                                         GamiResponseFunc response_func,
                                         gpointer response_data,
                                         GError **error);
GamiResponse *gami_manager_pause_monitor (GamiManager *ami,
                                          const gchar *channel,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_unpause_monitor (GamiManager *ami,
                                            const gchar *channel,
                                            const gchar *action_id,
                                            GamiResponseFunc response_func,
                                            gpointer response_data,
                                            GError **error);

GamiResponse *gami_manager_meetme_mute (GamiManager *ami, const gchar *meetme,
                                        const gchar *user_num,
                                        const gchar *action_id,
                                        GamiResponseFunc response_func,
                                        gpointer response_data, GError **error);
GamiResponse *gami_manager_meetme_unmute (GamiManager *ami, const gchar *meetme,
                                          const gchar *user_num,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_meetme_list (GamiManager *ami, const gchar *meetme,
                                        const gchar *action_id,
                                        GamiResponseFunc response_func,
                                        gpointer response_data, GError **error);

GamiResponse *gami_manager_queue_add (GamiManager *ami, const gchar *queue,
                                      const gchar *interface, guint penalty,
                                      gboolean paused, const gchar *action_id,
                                      GamiResponseFunc response_func,
                                      gpointer response_data, GError **error);
GamiResponse *gami_manager_queue_remove (GamiManager *ami, const gchar *queue,
                                         const gchar *interface,
                                         const gchar *action_id,
                                         GamiResponseFunc response_func,
                                         gpointer response_data,
                                         GError **error);
GamiResponse *gami_manager_queue_pause (GamiManager *ami, const gchar *queue,
                                        const gchar *interface, gboolean paused,
                                        const gchar *action_id,
                                        GamiResponseFunc response_func,
                                        gpointer response_data, GError **error);
GamiResponse *gami_manager_queue_penalty (GamiManager *ami, const gchar *queue,
                                          const gchar *interface, guint penalty,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_queue_summary (GamiManager *ami, const gchar *queue,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_queue_log (GamiManager *ami, const gchar *queue,
                                      const gchar *event,
                                      const gchar *action_id,
                                      GamiResponseFunc response_func,
                                      gpointer response_data, GError **error);
/*
GamiResponse *gami_manager_queue_status (GamiManager *ami, const gchar *queue,
                                         const gchar *action_id,
                                         GamiResponseFunc response_func,
                                         gpointer response_data,
                                         GError **error);
*/

GamiResponse *gami_manager_zap_dial_offhook (GamiManager *ami,
                                             const gchar *zap_channel,
                                             const gchar *number,
                                             const gchar *action_id,
                                             GamiResponseFunc response_func,
                                             gpointer response_data,
                                             GError **error);
GamiResponse *gami_manager_zap_hangup (GamiManager *ami,
                                       const gchar *zap_channel,
                                       const gchar *action_id,
                                       GamiResponseFunc response_func,
                                       gpointer response_data, GError **error);
GamiResponse *gami_manager_zap_dnd_on (GamiManager *ami,
                                       const gchar *zap_channel,
                                       const gchar *action_id,
                                       GamiResponseFunc response_func,
                                       gpointer response_data, GError **error);
GamiResponse *gami_manager_zap_dnd_off (GamiManager *ami,
                                        const gchar *zap_channel,
                                        const gchar *action_id,
                                        GamiResponseFunc response_func,
                                        gpointer response_data, GError **error);
GamiResponse *gami_manager_zap_show_channels (GamiManager *ami,
                                              const gchar *action_id,
                                              GamiResponseFunc response_func,
                                              gpointer response_data,
                                              GError **error);
GamiResponse *gami_manager_zap_transfer (GamiManager *ami,
                                         const gchar *zap_channel,
                                         const gchar *action_id,
                                         GamiResponseFunc response_func,
                                         gpointer response_data,
                                         GError **error);
GamiResponse *gami_manager_zap_restart (GamiManager *ami,
                                        const gchar *action_id,
                                        GamiResponseFunc response_func,
                                        gpointer response_data, GError **error);

GamiResponse *gami_manager_dahdi_dial_offhook (GamiManager *ami,
                                               const gchar *dahdi_channel,
                                               const gchar *number,
                                               const gchar *action_id,
                                               GamiResponseFunc response_func,
                                               gpointer response_data,
                                               GError **error);
GamiResponse *gami_manager_dahdi_hangup (GamiManager *ami,
                                         const gchar *dahdi_channel,
                                         const gchar *action_id,
                                         GamiResponseFunc response_func,
                                         gpointer response_data,
                                         GError **error);
GamiResponse *gami_manager_dahdi_dnd_on (GamiManager *ami,
                                         const gchar *dahdi_channel,
                                         const gchar *action_id,
                                         GamiResponseFunc response_func,
                                         gpointer response_data,
                                         GError **error);
GamiResponse *gami_manager_dahdi_dnd_off (GamiManager *ami,
                                          const gchar *dahdi_channel,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_dahdi_show_channels (GamiManager *ami,
                                                const gchar *dahdi_channel,
                                                const gchar *action_id,
                                                GamiResponseFunc response_func,
                                                gpointer response_data,
                                                GError **error);
GamiResponse *gami_manager_dahdi_transfer (GamiManager *ami,
                                           const gchar *dahdi_channel,
                                           const gchar *action_id,
                                           GamiResponseFunc response_func,
                                           gpointer response_data,
                                           GError **error);
GamiResponse *gami_manager_dahdi_restart (GamiManager *ami,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);

GamiResponse *gami_manager_agents (GamiManager *ami, const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_agent_callback_login (GamiManager *ami,
                                                 const gchar *agent,
                                                 const gchar *exten,
                                                 const gchar *context,
                                                 gboolean ack_call,
                                                 guint wrapup_time,
                                                 const gchar *action_id,
                                                 GamiResponseFunc response_func,
                                                 gpointer response_data,
                                                 GError **error);
GamiResponse *gami_manager_agent_logoff (GamiManager *ami, const gchar *agent,
                                         const gchar *action_id,
                                         GamiResponseFunc response_func,
                                         gpointer response_data,
                                         GError **error);

GamiResponse *gami_manager_db_get (GamiManager *ami, const gchar *family,
                                   const gchar *key, const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_db_put (GamiManager *ami, const gchar *family,
                                   const gchar *key, const gchar *val,
                                   const gchar *action_id,
                                   GamiResponseFunc response_func, 
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_db_del (GamiManager *ami, const gchar *family,
                                   const gchar *key, const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_db_del_tree (GamiManager *ami, const gchar *family,
                                        const gchar *action_id,
                                        GamiResponseFunc response_func,
                                        gpointer response_data, GError **error);

GamiResponse *gami_manager_park (GamiManager *ami, const gchar *channel,
                                 const gchar *channel2, guint timeout,
                                 const gchar *action_id,
                                 GamiResponseFunc response_func,
                                 gpointer response_data, GError **error);
GamiResponse *gami_manager_parked_calls (GamiManager *ami,
                                         const gchar *action_id,
                                         GamiResponseFunc response_func,
                                         gpointer response_data,
                                         GError **error);

GamiResponse *gami_manager_voicemail_users_list (GamiManager *ami,
                                                 const gchar *action_id,
                                                 GamiResponseFunc response_func,
                                                 gpointer response_data,
                                                 GError **error);
GamiResponse *gami_manager_mailbox_count (GamiManager *ami,
                                          const gchar *mailbox,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_mailbox_status (GamiManager *ami,
                                           const gchar *mailbox,
                                           const gchar *action_id,
                                           GamiResponseFunc response_func,
                                           gpointer response_data,
                                           GError **error);

GamiResponse *gami_manager_core_status (GamiManager *ami,
                                        const gchar *action_id,
                                        GamiResponseFunc response_func,
                                        gpointer response_data,
                                        GError **error);
GamiResponse *gami_manager_core_show_channels (GamiManager *ami,
                                               const gchar *action_id,
                                               GamiResponseFunc response_func,
                                               gpointer response_data,
                                               GError **error);
GamiResponse *gami_manager_core_settings (GamiManager *ami,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);

GamiResponse *gami_manager_iax_peer_list (GamiManager *ami,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_sip_peers (GamiManager *ami, const gchar *action_id,
                                      GamiResponseFunc response_func,
                                      gpointer response_data, GError **error);
GamiResponse *gami_manager_sip_show_peer (GamiManager *ami, const gchar *peer,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_sip_show_registry (GamiManager *ami,
                                              const gchar *action_id,
                                              GamiResponseFunc response_func,
                                              gpointer response_data,
                                              GError **error);

GamiResponse *gami_manager_status (GamiManager *ami, const gchar *channel,
                                   const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_extension_state (GamiManager *ami,
                                            const gchar *exten,
                                            const gchar *context,
                                            const gchar *action_id,
                                            GamiResponseFunc response_func,
                                            gpointer response_data,
                                            GError **error);
GamiResponse *gami_manager_ping (GamiManager *ami, const gchar *action_id,
                                 GamiResponseFunc response_func,
                                 gpointer response_data, GError **error);
GamiResponse *gami_manager_absolute_timeout (GamiManager *ami,
                                             const gchar *channel, gint timeout,
                                             const gchar *action_id,
                                             GamiResponseFunc response_func,
                                             gpointer response_data,
                                             GError **error);
GamiResponse *gami_manager_challenge (GamiManager *ami, const gchar *auth_type,
                                      const gchar *action_id,
                                      GamiResponseFunc response_func,
                                      gpointer response_data, GError **error);
GamiResponse *gami_manager_set_cdr_user_field (GamiManager *ami,
                                               const gchar *channel,
                                               const gchar *user_field,
                                               gboolean append,
                                               const gchar *action_id,
                                               GamiResponseFunc response_func, 
                                               gpointer response_data,
                                               GError **error);
GamiResponse *gami_manager_reload (GamiManager *ami, const gchar *module,
                                   const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_hangup (GamiManager *ami, const gchar *channel,
                                   const gchar *action_id,
                                   GamiResponseFunc response_func, 
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_redirect (GamiManager *ami, const gchar *channel,
                                     const gchar *extra_channel,
                                     const gchar *exten, const gchar *context,
                                     const gchar *priority,
                                     const gchar *action_id,
                                     GamiResponseFunc response_func,
                                     gpointer response_data, GError **error);
GamiResponse *gami_manager_bridge (GamiManager *ami, const gchar *channel1,
                                   const gchar *channel2, gboolean tone,
                                   const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_agi (GamiManager *ami, const gchar *channel,
                                const gchar *command, const gchar *command_id,
                                const gchar *action_id,
                                GamiResponseFunc response_func,
                                gpointer response_data, GError **error);
GamiResponse *gami_manager_send_text (GamiManager *ami, const gchar *channel,
                                      const gchar *message,
                                      const gchar *action_id,
                                      GamiResponseFunc response_func,
                                      gpointer response_data, GError **error);
GamiResponse *gami_manager_jabber_send (GamiManager *ami, const gchar *jabber,
                                        const gchar *screen_name,
                                        const gchar *message,
                                        const gchar *action_id,
                                        GamiResponseFunc response_func,
                                        gpointer response_data, GError **error);
GamiResponse *gami_manager_play_dtmf (GamiManager *ami, const gchar *channel,
                                      gchar digit, const gchar *action_id,
                                      GamiResponseFunc response_func,
                                      gpointer response_data, GError **error);
GamiResponse *gami_manager_list_commands (GamiManager *ami,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_list_categories (GamiManager *ami,
                                            const gchar *filename,
                                            const gchar *action_id,
                                            GamiResponseFunc response_func,
                                            gpointer response_data,
                                            GError **error);
GamiResponse *gami_manager_get_config (GamiManager *ami, const gchar *filename,
                                       const gchar *action_id,
                                       GamiResponseFunc response_func,
                                       gpointer response_data, GError **error);
GamiResponse *gami_manager_get_config_json (GamiManager *ami,
                                            const gchar *filename,
                                            const gchar *action_id,
                                            GamiResponseFunc response_func,
                                            gpointer response_data,
                                            GError **error);
GamiResponse *gami_manager_create_config (GamiManager *ami,
                                          const gchar *filename,
                                          const gchar *action_id,
                                          GamiResponseFunc response_func,
                                          gpointer response_data,
                                          GError **error);
GamiResponse *gami_manager_originate (GamiManager *ami, const gchar *channel,
                                      const gchar *application_exten,
                                      const gchar *data_context,
                                      const gchar *priority, guint timeout,
                                      const gchar *caller_id,
                                      const gchar *account,
                                      const GHashTable *variables,
                                      gboolean async,
                                      const gchar *action_id,
                                      GamiResponseFunc response_func,
                                      gpointer response_data, GError **error);
GamiResponse *gami_manager_events (GamiManager *ami, GamiEventMask event_mask,
                                   const gchar *action_id,
                                   GamiResponseFunc response_func,
                                   gpointer response_data, GError **error);
GamiResponse *gami_manager_user_event (GamiManager *ami,
                                       const gchar *user_event,
                                       const GHashTable *headers,
                                       const gchar *action_id,
                                       GamiResponseFunc response_func,
                                       gpointer response_data, GError **error);
GamiResponse *gami_manager_wait_event (GamiManager *ami, guint timeout,
                                       const gchar *action_id,
                                       GamiResponseFunc response_func,
                                       gpointer response_data, GError **error);

G_END_DECLS

#endif /* _AMI_MANAGER_H_ */
