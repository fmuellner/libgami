/* vi: se sw=4 ts=4 tw=80 fo+=t cin cino=(0t0 : */
/*
 * LIBGAMI - Library for using the Asterisk Manager Interface with GObject
 * Copyright (C) 2008-2009 Florian Müllner, EuropeSIP Communications S.L.
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


#if !defined(__GAMI_H_INSIDE__) && !defined (GAMI_COMPILATION)
#  error "Only <gami.h> can be included directly."
#endif

#ifndef __GAMI_MANAGER_H__
#define __GAMI_MANAGER_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#ifdef GAMI_COMPILATION
#  include <gami-enums.h>
#else
#  include <gami/gami-enums.h>
#endif

G_BEGIN_DECLS

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

struct _GamiManagerClass
{
    GObjectClass parent_class;
};


/**
 * GamiManagerNewAsyncFunc:
 * @gami: the newly created #GamiManager
 * @user_data: user data passed to the function
 *
 * Specifies the type of functions passed to gami_manager_new_async()
 */
typedef void (*GamiManagerNewAsyncFunc) (GamiManager *gami,
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
void         gami_manager_new_async (const gchar *host, const gchar *port,
									 GamiManagerNewAsyncFunc func,
									 gpointer user_data);
gboolean     gami_manager_connect (GamiManager *ami, GError **error);

gboolean gami_manager_login  (GamiManager *ami,
							  const gchar *username,
                              const gchar *secret,
							  const gchar *auth_type,
                              GamiEventMask events,
							  const gchar *action_id,
                              GError **error);
void gami_manager_login_async (GamiManager *ami,
							   const gchar *username,
							   const gchar *secret,
							   const gchar *auth_type,
							   GamiEventMask events,
							   const gchar *action_id,
							   GAsyncReadyCallback callback,
							   gpointer user_data);
gboolean gami_manager_login_finish (GamiManager *ami,
									GAsyncResult *result,
									GError **error);

gboolean gami_manager_logoff (GamiManager *ami,
							  const gchar *action_id, 
                              GError **error);
void gami_manager_logoff_async (GamiManager *ami,
								const gchar *action_id, 
								GAsyncReadyCallback callback,
								gpointer user_data);
gboolean gami_manager_logoff_finish (GamiManager *ami,
									 GAsyncResult *result,
									 GError **error);

gchar *gami_manager_get_var (GamiManager *ami,
                             const gchar *channel,
                             const gchar *variable,
                             const gchar *action_id,
                             GError **error); 
void gami_manager_get_var_async (GamiManager *ami,
                                 const gchar *channel,
								 const gchar *variable,
                                 const gchar *action_id,
								 GAsyncReadyCallback callback,
								 gpointer user_data);
gchar *gami_manager_get_var_finish (GamiManager *ami,
                                    GAsyncResult *result,
                                    GError **error);

gboolean gami_manager_set_var (GamiManager *ami,
                               const gchar *channel,
                               const gchar *variable,
                               const gchar *value,
                               const gchar *action_id,
                               GError **error);
void gami_manager_set_var_async (GamiManager *ami,
                                 const gchar *channel,
								 const gchar *variable,
                                 const gchar *value,
								 const gchar *action_id,
								 GAsyncReadyCallback callback,
								 gpointer user_data);
gboolean gami_manager_set_var_finish (GamiManager *ami,
                                      GAsyncResult *result,
                                      GError **error);

gboolean gami_manager_module_check (GamiManager *ami,
                                    const gchar *module,
                                    const gchar *action_id,
                                    GError **error);
void gami_manager_module_check_async (GamiManager *ami,
									  const gchar *module,
									  const gchar *action_id,
									  GAsyncReadyCallback callback,
									  gpointer user_data);
gboolean gami_manager_module_check_finish (GamiManager *ami,
                                           GAsyncResult *result,
                                           GError **error);

gboolean gami_manager_module_load (GamiManager *ami,
                                   const gchar *module,
                                   GamiModuleLoadType load_type,
                                   const gchar *action_id,
                                   GError **error);
void gami_manager_module_load_async (GamiManager *ami,
                                     const gchar *module,
									 GamiModuleLoadType load_type,
									 const gchar *action_id,
									 GAsyncReadyCallback callback,
									 gpointer user_data);
gboolean gami_manager_module_load_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);

gboolean gami_manager_monitor (GamiManager *ami,
                               const gchar *channel,
                               const gchar *file,
                               const gchar *format,
                               gboolean mix,
                               const gchar *action_id,
                               GError **error);
void gami_manager_monitor_async (GamiManager *ami,
                                 const gchar *channel,
								 const gchar *file,
                                 const gchar *format,
								 gboolean mix,
                                 const gchar *action_id,
								 GAsyncReadyCallback callback,
								 gpointer user_data);
gboolean gami_manager_monitor_finish (GamiManager *ami,
                                      GAsyncResult *result,
                                      GError **error);

gboolean gami_manager_change_monitor (GamiManager *ami,
                                      const gchar *channel,
                                      const gchar *file,
                                      const gchar *action_id,
                                      GError **error);
void gami_manager_change_monitor_async (GamiManager *ami,
										const gchar *channel,
                                        const gchar *file,
										const gchar *action_id,
										GAsyncReadyCallback callback,
										gpointer user_data);
gboolean gami_manager_change_monitor_finish (GamiManager *ami,
                                             GAsyncResult *result,
                                             GError **error);

gboolean gami_manager_stop_monitor (GamiManager *ami,
                                    const gchar *channel,
                                    const gchar *action_id,
                                    GError **error);
void gami_manager_stop_monitor_async (GamiManager *ami,
									  const gchar *channel,
									  const gchar *action_id,
									  GAsyncReadyCallback callback,
									  gpointer user_data);
gboolean gami_manager_stop_monitor_finish (GamiManager *ami,
                                           GAsyncResult *result,
                                           GError **error);

gboolean gami_manager_pause_monitor (GamiManager *ami,
                                     const gchar *channel,
                                     const gchar *action_id,
                                     GError **error);
void gami_manager_pause_monitor_async (GamiManager *ami,
									   const gchar *channel,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
									   gpointer user_data);
gboolean gami_manager_pause_monitor_finish (GamiManager *ami,
                                            GAsyncResult *result,
                                            GError **error);

gboolean gami_manager_unpause_monitor (GamiManager *ami,
                                       const gchar *channel,
                                       const gchar *action_id,
                                       GError **error);
void gami_manager_unpause_monitor_async (GamiManager *ami,
										 const gchar *channel,
                                         const gchar *action_id,
                                         GAsyncReadyCallback callback,
										 gpointer user_data);
gboolean gami_manager_unpause_monitor_finish (GamiManager *ami,
                                              GAsyncResult *result,
                                              GError **error);

gboolean gami_manager_meetme_mute (GamiManager *ami,
                                   const gchar *meetme,
                                   const gchar *user_num,
                                   const gchar *action_id,
                                   GError **error);
void gami_manager_meetme_mute_async (GamiManager *ami,
                                     const gchar *meetme,
									 const gchar *user_num,
									 const gchar *action_id,
                                     GAsyncReadyCallback callback,
									 gpointer user_data);
gboolean gami_manager_meetme_mute_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);

gboolean gami_manager_meetme_unmute (GamiManager *ami,
                                     const gchar *meetme,
                                     const gchar *user_num,
                                     const gchar *action_id,
                                     GError **error);
void gami_manager_meetme_unmute_async (GamiManager *ami,
									   const gchar *meetme,
									   const gchar *user_num,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
									   gpointer user_data);
gboolean gami_manager_meetme_unmute_finish (GamiManager *ami,
                                            GAsyncResult *result,
                                            GError **error);

GSList *gami_manager_meetme_list (GamiManager *ami,
                                  const gchar *meetme,
                                  const gchar *action_id,
                                  GError **error);
void gami_manager_meetme_list_async (GamiManager *ami,
                                     const gchar *conference,
									 const gchar *action_id,
                                     GAsyncReadyCallback callback,
									 gpointer user_data);
GSList *gami_manager_meetme_list_finish (GamiManager *ami,
                                         GAsyncResult *result,
                                         GError **error);

gboolean gami_manager_queue_add (GamiManager *ami,
                                 const gchar *queue,
                                 const gchar *iface,
                                 guint penalty,
                                 gboolean paused,
                                 const gchar *action_id,
                                 GError **error);
void gami_manager_queue_add_async (GamiManager *ami,
                                   const gchar *queue,
								   const gchar *iface,
                                   guint penalty,
								   gboolean paused,
                                   const gchar *action_id,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean gami_manager_queue_add_finish (GamiManager *ami,
                                        GAsyncResult *result,
                                        GError **error);

gboolean gami_manager_queue_remove (GamiManager *ami,
                                    const gchar *queue,
                                    const gchar *iface,
                                    const gchar *action_id,
                                    GError **error);
void gami_manager_queue_remove_async (GamiManager *ami,
                                      const gchar *queue,
									  const gchar *iface,
									  const gchar *action_id,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
gboolean gami_manager_queue_remove_finish (GamiManager *ami,
                                           GAsyncResult *result,
                                           GError **error);

gboolean gami_manager_queue_pause (GamiManager *ami,
                                   const gchar *queue,
                                   const gchar *iface,
                                   gboolean paused,
                                   const gchar *action_id,
                                   GError **error);
void gami_manager_queue_pause_async (GamiManager *ami,
                                     const gchar *queue,
									 const gchar *iface,
                                     gboolean paused,
									 const gchar *action_id,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
gboolean gami_manager_queue_pause_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);

gboolean gami_manager_queue_penalty (GamiManager *ami,
                                     const gchar *queue,
                                     const gchar *iface,
                                     guint penalty,
                                     const gchar *action_id,
                                     GError **error);
void gami_manager_queue_penalty_async (GamiManager *ami,
                                       const gchar *queue,
									   const gchar *iface,
                                       guint penalty,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
gboolean gami_manager_queue_penalty_finish (GamiManager *ami,
                                            GAsyncResult *result,
                                            GError **error);

GSList *gami_manager_queue_summary (GamiManager *ami,
                                    const gchar *queue,
                                    const gchar *action_id,
                                    GError **error);
void gami_manager_queue_summary_async (GamiManager *ami,
                                       const gchar *queue,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
GSList *gami_manager_queue_summary_finish (GamiManager *ami,
                                           GAsyncResult *result,
                                           GError **error);

gboolean gami_manager_queue_log (GamiManager *ami,
                                 const gchar *queue,
                                 const gchar *event,
                                 const gchar *action_id,
                                 GError **error);
void gami_manager_queue_log_async (GamiManager *ami,
                                   const gchar *queue,
								   const gchar *event,
                                   const gchar *action_id,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean gami_manager_queue_log_finish (GamiManager *ami,
                                        GAsyncResult *result,
                                        GError **error);

/*
GSList *gami_manager_queue_status (GamiManager *ami,
                                   const gchar *queue,
                                   const gchar *action_id,
                                   GError **error);
void gami_manager_queue_status_async (GamiManager *ami,
                                      const gchar *queue,
                                      const gchar *action_id,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
GSList *gami_manager_queue_status_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);
*/

gboolean gami_manager_zap_dial_offhook (GamiManager *ami,
                                        const gchar *zap_channel,
                                        const gchar *number,
                                        const gchar *action_id,
                                        GError **error);
void gami_manager_zap_dial_offhook_async (GamiManager *ami,
										  const gchar *zap_channel,
										  const gchar *number,
										  const gchar *action_id,
                                          GAsyncReadyCallback callback,
										  gpointer user_data);
gboolean gami_manager_zap_dial_offhook_finish (GamiManager *ami,
                                               GAsyncResult *result,
                                               GError **error);

gboolean gami_manager_zap_hangup (GamiManager *ami,
                                  const gchar *zap_channel,
                                  const gchar *action_id,
                                  GError **error);
void gami_manager_zap_hangup_async (GamiManager *ami,
									const gchar *zap_channel,
									const gchar *action_id,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
gboolean gami_manager_zap_hangup_finish (GamiManager *ami,
                                         GAsyncResult *result,
                                         GError **error);

gboolean gami_manager_zap_dnd_on (GamiManager *ami,
                                  const gchar *zap_channel,
                                  const gchar *action_id,
                                  GError **error);
void gami_manager_zap_dnd_on_async (GamiManager *ami,
									const gchar *zap_channel,
									const gchar *action_id,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
gboolean gami_manager_zap_dnd_on_finish (GamiManager *ami,
                                         GAsyncResult *result,
                                         GError **error);

gboolean gami_manager_zap_dnd_off (GamiManager *ami,
                                   const gchar *zap_channel,
                                   const gchar *action_id,
                                   GError **error);
void gami_manager_zap_dnd_off_async (GamiManager *ami,
									 const gchar *zap_channel,
									 const gchar *action_id,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
gboolean gami_manager_zap_dnd_off_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);

GSList *gami_manager_zap_show_channels (GamiManager *ami,
                                        const gchar *action_id,
                                        GError **error);
void gami_manager_zap_show_channels_async (GamiManager *ami,
										   const gchar *action_id,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
GSList *gami_manager_zap_show_channels_finish (GamiManager *ami,
                                               GAsyncResult *result,
                                               GError **error);

gboolean gami_manager_zap_transfer (GamiManager *ami,
                                    const gchar *zap_channel,
                                    const gchar *action_id,
                                    GError **error);
void gami_manager_zap_transfer_async (GamiManager *ami,
									  const gchar *zap_channel,
									  const gchar *action_id,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
gboolean gami_manager_zap_transfer_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);

gboolean gami_manager_zap_restart (GamiManager *ami,
                                   const gchar *action_id,
                                   GError **error);
void gami_manager_zap_restart_async (GamiManager *ami,
                                     const gchar *action_id,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
gboolean gami_manager_zap_restart_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);

gboolean gami_manager_dahdi_dial_offhook (GamiManager *ami,
                                          const gchar *dahdi_channel,
                                          const gchar *number,
                                          const gchar *action_id,
                                          GError **error);
void gami_manager_dahdi_dial_offhook_async (GamiManager *ami,
											const gchar *dahdi_channel,
											const gchar *number,
											const gchar *action_id,
                                            GAsyncReadyCallback callback,
											gpointer user_data);
gboolean gami_manager_dahdi_dial_offhook_finish (GamiManager *ami,
                                                 GAsyncResult *result,
                                                 GError **error);

gboolean gami_manager_dahdi_hangup (GamiManager *ami,
                                    const gchar *dahdi_channel,
                                    const gchar *action_id,
                                    GError **error);
void gami_manager_dahdi_hangup_async (GamiManager *ami,
									  const gchar *dahdi_channel,
									  const gchar *action_id,
                                      GAsyncReadyCallback callback,
									  gpointer user_data);
gboolean gami_manager_dahdi_hangup_finish (GamiManager *ami,
                                           GAsyncResult *result,
                                           GError **error);

gboolean gami_manager_dahdi_dnd_on (GamiManager *ami,
                                    const gchar *dahdi_channel,
                                    const gchar *action_id,
                                    GError **error);
void gami_manager_dahdi_dnd_on_async (GamiManager *ami,
									  const gchar *dahdi_channel,
									  const gchar *action_id,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
gboolean gami_manager_dahdi_dnd_on_finish (GamiManager *ami,
                                           GAsyncResult *result,
                                           GError **error);

gboolean gami_manager_dahdi_dnd_off (GamiManager *ami,
                                     const gchar *dahdi_channel,
                                     const gchar *action_id,
                                     GError **error);
void gami_manager_dahdi_dnd_off_async (GamiManager *ami,
									   const gchar *dahdi_channel,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
gboolean gami_manager_dahdi_dnd_off_finish (GamiManager *ami,
                                            GAsyncResult *result,
                                            GError **error);

GSList *gami_manager_dahdi_show_channels (GamiManager *ami,
										  const gchar *dahdi_channel,
										  const gchar *action_id,
										  GError **error);
void gami_manager_dahdi_show_channels_async (GamiManager *ami,
											 const gchar *dahdi_channel,
											 const gchar *action_id,
                                             GAsyncReadyCallback callback,
											 gpointer user_data);
GSList *gami_manager_dahdi_show_channels_finish (GamiManager *ami,
                                                 GAsyncResult *result,
                                                 GError **error);

gboolean gami_manager_dahdi_transfer (GamiManager *ami,
                                      const gchar *dahdi_channel,
                                      const gchar *action_id,
                                      GError **error);
void gami_manager_dahdi_transfer_async (GamiManager *ami,
										const gchar *dahdi_channel,
										const gchar *action_id,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean gami_manager_dahdi_transfer_finish (GamiManager *ami,
                                             GAsyncResult *result,
                                             GError **error);

gboolean gami_manager_dahdi_restart (GamiManager *ami,
                                     const gchar *action_id,
									 GError **error);
void gami_manager_dahdi_restart_async (GamiManager *ami,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
gboolean gami_manager_dahdi_restart_finish (GamiManager *ami,
                                            GAsyncResult *result,
                                            GError **error);

GSList *gami_manager_agents (GamiManager *ami,
                             const gchar *action_id,
							 GError **error);
void gami_manager_agents_async (GamiManager *ami,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
GSList *gami_manager_agents_finish (GamiManager *ami,
                                    GAsyncResult *result,
                                    GError **error);

gboolean gami_manager_agent_callback_login (GamiManager *ami,
											const gchar *agent,
											const gchar *exten,
											const gchar *context,
											gboolean ack_call,
											guint wrapup_time,
											const gchar *action_id,
											GError **error);
void gami_manager_agent_callback_login_async (GamiManager *ami,
											  const gchar *agent,
											  const gchar *exten,
											  const gchar *context,
											  gboolean ack_call,
											  guint wrapup_time,
											  const gchar *action_id,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
gboolean gami_manager_agent_callback_login_finish (GamiManager *ami,
                                                   GAsyncResult *result,
                                                   GError **error);

gboolean gami_manager_agent_logoff (GamiManager *ami,
                                    const gchar *agent,
									const gchar *action_id,
                                    GError **error);
void gami_manager_agent_logoff_async (GamiManager *ami,
                                      const gchar *agent,
									  const gchar *action_id,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
gboolean gami_manager_agent_logoff_finish (GamiManager *ami,
                                           GAsyncResult *result,
                                           GError **error);

gchar *gami_manager_db_get (GamiManager *ami,
                            const gchar *family,
							const gchar *key,
                            const gchar *action_id,
							GError **error);
void gami_manager_db_get_async (GamiManager *ami,
                                const gchar *family,
								const gchar *key,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gchar *gami_manager_db_get_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error);

gboolean gami_manager_db_put (GamiManager *ami,
                              const gchar *family,
							  const gchar *key,
                              const gchar *val,
							  const gchar *action_id,
                              GError **error);
void gami_manager_db_put_async (GamiManager *ami,
                                const gchar *family,
								const gchar *key,
                                const gchar *val,
								const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean gami_manager_db_put_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error);

gboolean gami_manager_db_del (GamiManager *ami,
                              const gchar *family,
							  const gchar *key,
                              const gchar *action_id,
							  GError **error);
void gami_manager_db_del_async (GamiManager *ami,
                                const gchar *family,
								const gchar *key,
                                const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean gami_manager_db_del_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error);

gboolean gami_manager_db_del_tree (GamiManager *ami,
                                   const gchar *family,
								   const gchar *action_id,
                                   GError **error);
void gami_manager_db_del_tree_async (GamiManager *ami,
                                     const gchar *family,
									 const gchar *action_id,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
gboolean gami_manager_db_del_tree_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);

gboolean gami_manager_park (GamiManager *ami,
                            const gchar *channel,
							const gchar *channel2,
                            guint timeout,
							const gchar *action_id,
                            GError **error);
void gami_manager_park_async (GamiManager *ami,
                              const gchar *channel,
							  const gchar *channel2,
                              guint timeout,
							  const gchar *action_id,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
gboolean gami_manager_park_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error);

GSList *gami_manager_parked_calls (GamiManager *ami,
                                   const gchar *action_id,
								   GError **error);
void gami_manager_parked_calls_async (GamiManager *ami,
									  const gchar *action_id,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
GSList *gami_manager_parked_calls_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);

GSList *gami_manager_voicemail_users_list (GamiManager *ami,
										   const gchar *action_id,
										   GError **error);
void gami_manager_voicemail_users_list_async (GamiManager *ami,
											  const gchar *action_id,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
GSList *gami_manager_voicemail_users_list_finish (GamiManager *ami,
                                                  GAsyncResult *result,
                                                  GError **error);

GHashTable *gami_manager_mailbox_count (GamiManager *ami,
                                        const gchar *mailbox,
										const gchar *action_id,
                                        GError **error);
void gami_manager_mailbox_count_async (GamiManager *ami,
									   const gchar *mailbox,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
GHashTable *gami_manager_mailbox_count_finish (GamiManager *ami,
                                               GAsyncResult *result,
                                               GError **error);

GHashTable *gami_manager_mailbox_status (GamiManager *ami,
                                         const gchar *mailbox,
										 const gchar *action_id,
										 GError **error);
void gami_manager_mailbox_status_async (GamiManager *ami,
										const gchar *mailbox,
										const gchar *action_id,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
GHashTable *gami_manager_mailbox_status_finish (GamiManager *ami,
                                                GAsyncResult *result,
                                                GError **error);

GHashTable *gami_manager_core_status (GamiManager *ami,
                                      const gchar *action_id,
									  GError **error);
void gami_manager_core_status_async (GamiManager *ami,
									 const gchar *action_id,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
GHashTable *gami_manager_core_status_finish (GamiManager *ami,
                                             GAsyncResult *result,
                                             GError **error);

GSList *gami_manager_core_show_channels (GamiManager *ami,
										 const gchar *action_id,
										 GError **error);
void gami_manager_core_show_channels_async (GamiManager *ami,
											const gchar *action_id,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
GSList *gami_manager_core_show_channels_finish (GamiManager *ami,
                                                GAsyncResult *result,
                                                GError **error);

GHashTable *gami_manager_core_settings (GamiManager *ami,
										const gchar *action_id,
                                        GError **error);
void gami_manager_core_settings_async (GamiManager *ami,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
GHashTable *gami_manager_core_settings_finish (GamiManager *ami,
                                               GAsyncResult *result,
                                               GError **error);

GSList *gami_manager_iax_peer_list (GamiManager *ami,
                                    const gchar *action_id,
									GError **error);
void gami_manager_iax_peer_list_async (GamiManager *ami,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
GSList *gami_manager_iax_peer_list_finish (GamiManager *ami,
                                           GAsyncResult *result,
                                           GError **error);

GSList *gami_manager_sip_peers (GamiManager *ami,
                                const gchar *action_id,
								GError **error);
void gami_manager_sip_peers_async (GamiManager *ami,
								   const gchar *action_id,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
GSList *gami_manager_sip_peers_finish (GamiManager *ami,
                                       GAsyncResult *result,
                                       GError **error);

GHashTable *gami_manager_sip_show_peer (GamiManager *ami,
                                        const gchar *peer,
										const gchar *action_id,
                                        GError **error);
void gami_manager_sip_show_peer_async (GamiManager *ami,
                                       const gchar *peer,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
GHashTable *gami_manager_sip_show_peer_finish (GamiManager *ami,
                                               GAsyncResult *result,
                                               GError **error);

GSList *gami_manager_sip_show_registry (GamiManager *ami,
										const gchar *action_id,
                                        GError **error);
void gami_manager_sip_show_registry_async (GamiManager *ami,
										   const gchar *action_id,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
GSList *gami_manager_sip_show_registry_finish (GamiManager *ami,
                                               GAsyncResult *result,
                                               GError **error);

GSList *gami_manager_status (GamiManager *ami,
							 const gchar *channel,
							 const gchar *action_id,
							 GError **error);
void gami_manager_status_async (GamiManager *ami,
								const gchar *channel,
								const gchar *action_id,
								GAsyncReadyCallback callback,
								gpointer user_data);
GSList *gami_manager_status_finish (GamiManager *ami,
									GAsyncResult *result,
									GError **error);

GHashTable *gami_manager_extension_state (GamiManager *ami,
                                          const gchar *exten,
										  const gchar *context,
										  const gchar *action_id,
										  GError **error);
void gami_manager_extension_state_async (GamiManager *ami,
										 const gchar *exten,
										 const gchar *context,
										 const gchar *action_id,
                                         GAsyncReadyCallback callback,
										 gpointer user_data);
GHashTable *gami_manager_extension_state_finish (GamiManager *ami,
                                                 GAsyncResult *result,
                                                 GError **error);

gboolean gami_manager_ping (GamiManager *ami,
                            const gchar *action_id,
							GError **error);
void gami_manager_ping_async (GamiManager *ami,
                              const gchar *action_id,
                              GAsyncReadyCallback callback,
							  gpointer user_data);
gboolean gami_manager_ping_finish (GamiManager *ami,
                                   GAsyncResult *result,
                                   GError **error);

gboolean gami_manager_absolute_timeout (GamiManager *ami,
										const gchar *channel,
                                        gint timeout,
										const gchar *action_id,
                                        GError **error);
void gami_manager_absolute_timeout_async (GamiManager *ami,
										  const gchar *channel,
										  gint timeout,
										  const gchar *action_id,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data);
gboolean gami_manager_absolute_timeout_finish (GamiManager *ami,
                                               GAsyncResult *result,
                                               GError **error);

gchar *gami_manager_challenge (GamiManager *ami,
                               const gchar *auth_type,
							   const gchar *action_id,
                               GError **error);
void gami_manager_challenge_async (GamiManager *ami,
								   const gchar *auth_type,
								   const gchar *action_id,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gchar *gami_manager_challenge_finish (GamiManager *ami,
                                      GAsyncResult *result,
                                      GError **error);

gboolean gami_manager_set_cdr_user_field (GamiManager *ami,
										  const gchar *channel,
										  const gchar *user_field,
										  gboolean append,
										  const gchar *action_id,
										  GError **error);
void gami_manager_set_cdr_user_field_async (GamiManager *ami,
											const gchar *channel,
											const gchar *user_field,
											gboolean append,
											const gchar *action_id,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean gami_manager_set_cdr_user_field_finish (GamiManager *ami,
                                                 GAsyncResult *result,
                                                 GError **error);

gboolean gami_manager_reload (GamiManager *ami,
                              const gchar *module,
							  const gchar *action_id,
                              GError **error);
void gami_manager_reload_async (GamiManager *ami,
                                const gchar *module,
								const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean gami_manager_reload_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error);

gboolean gami_manager_hangup (GamiManager *ami,
                              const gchar *channel,
							  const gchar *action_id,
                              GError **error);
void gami_manager_hangup_async (GamiManager *ami,
                                const gchar *channel,
								const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean gami_manager_hangup_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error);

gboolean gami_manager_redirect (GamiManager *ami,
                                const gchar *channel,
								const gchar *extra_channel,
								const gchar *exten,
                                const gchar *context,
								const gchar *priority,
                                const gchar *action_id,
								GError **error);
void gami_manager_redirect_async (GamiManager *ami,
                                  const gchar *channel,
								  const gchar *extra_channel,
								  const gchar *exten,
                                  const gchar *context,
								  const gchar *priority,
								  const gchar *action_id,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
gboolean gami_manager_redirect_finish (GamiManager *ami,
                                       GAsyncResult *result,
                                       GError **error);

gboolean gami_manager_bridge (GamiManager *ami,
                              const gchar *channel1,
							  const gchar *channel2,
                              gboolean tone,
							  const gchar *action_id,
                              GError **error);
void gami_manager_bridge_async (GamiManager *ami,
                                const gchar *channel1,
								const gchar *channel2,
                                gboolean tone,
								const gchar *action_id,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean gami_manager_bridge_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error);

gboolean gami_manager_agi (GamiManager *ami,
                           const gchar *channel,
						   const gchar *command,
                           const gchar *command_id,
						   const gchar *action_id,
                           GError **error);
void gami_manager_agi_async (GamiManager *ami,
                             const gchar *channel,
							 const gchar *command,
                             const gchar *command_id,
							 const gchar *action_id,
                             GAsyncReadyCallback callback,
                             gpointer user_data);
gboolean gami_manager_agi_finish (GamiManager *ami,
                                  GAsyncResult *result,
                                  GError **error);

gboolean gami_manager_send_text (GamiManager *ami,
                                 const gchar *channel,
								 const gchar *message,
                                 const gchar *action_id,
								 GError **error);
void gami_manager_send_text_async (GamiManager *ami,
                                   const gchar *channel,
								   const gchar *message,
								   const gchar *action_id,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean gami_manager_send_text_finish (GamiManager *ami,
                                        GAsyncResult *result,
                                        GError **error);

gboolean gami_manager_jabber_send (GamiManager *ami,
                                   const gchar *jabber,
								   const gchar *screen_name,
								   const gchar *message,
                                   const gchar *action_id,
								   GError **error);
void gami_manager_jabber_send_async (GamiManager *ami,
                                     const gchar *jabber,
									 const gchar *screen_name,
									 const gchar *message,
									 const gchar *action_id,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
gboolean gami_manager_jabber_send_finish (GamiManager *ami,
                                          GAsyncResult *result,
                                          GError **error);

gboolean gami_manager_play_dtmf (GamiManager *ami,
                                 const gchar *channel,
								 gchar digit,
                                 const gchar *action_id,
								 GError **error);
void gami_manager_play_dtmf_async (GamiManager *ami,
                                   const gchar *channel,
								   gchar digit,
                                   const gchar *action_id,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean gami_manager_play_dtmf_finish (GamiManager *ami,
                                        GAsyncResult *result,
                                        GError **error);

GHashTable *gami_manager_list_commands (GamiManager *ami,
										const gchar *action_id,
                                        GError **error);
void gami_manager_list_commands_async (GamiManager *ami,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
GHashTable *gami_manager_list_commands_finish (GamiManager *ami,
                                               GAsyncResult *result,
                                               GError **error);

GHashTable *gami_manager_list_categories (GamiManager *ami,
                                          const gchar *filename,
										  const gchar *action_id,
										  GError **error);
void gami_manager_list_categories_async (GamiManager *ami,
										 const gchar *filename,
										 const gchar *action_id,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
GHashTable *gami_manager_list_categories_finish (GamiManager *ami,
                                                 GAsyncResult *result,
                                                 GError **error);

GHashTable *gami_manager_get_config (GamiManager *ami,
                                     const gchar *filename,
									 const gchar *action_id,
                                     GError **error);
void gami_manager_get_config_async (GamiManager *ami,
									const gchar *filename,
									const gchar *action_id,
                                    GAsyncReadyCallback callback,
									gpointer user_data);
GHashTable *gami_manager_get_config_finish (GamiManager *ami,
                                            GAsyncResult *result,
                                            GError **error);

GHashTable *gami_manager_get_config_json (GamiManager *ami,
										  const gchar *filename,
										  const gchar *action_id,
										  GError **error);
void gami_manager_get_config_json_async (GamiManager *ami,
										 const gchar *filename,
										 const gchar *action_id,
                                         GAsyncReadyCallback callback,
										 gpointer user_data);
GHashTable *gami_manager_get_config_json_finish (GamiManager *ami,
                                                 GAsyncResult *result,
                                                 GError **error);

gboolean gami_manager_create_config (GamiManager *ami,
                                     const gchar *filename,
									 const gchar *action_id,
                                     GError **error);
void gami_manager_create_config_async (GamiManager *ami,
									   const gchar *filename,
									   const gchar *action_id,
                                       GAsyncReadyCallback callback,
									   gpointer user_data);
gboolean gami_manager_create_config_finish (GamiManager *ami,
                                            GAsyncResult *result,
                                            GError **error);

gboolean gami_manager_originate (GamiManager *ami,
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
                                 GError **error);
void gami_manager_originate_async (GamiManager *ami,
                                   const gchar *channel,
								   const gchar *application_exten,
								   const gchar *data_context,
								   const gchar *priority,
                                   guint timeout,
								   const gchar *caller_id,
								   const gchar *account,
								   const GHashTable *variables,
								   gboolean async, const gchar *action_id,
                                   GAsyncReadyCallback callback,
								   gpointer user_data);
gboolean gami_manager_originate_finish (GamiManager *ami,
                                        GAsyncResult *result,
                                        GError **error);

gboolean gami_manager_events (GamiManager *ami,
                              GamiEventMask event_mask,
							  const gchar *action_id,
                              GError **error);
void gami_manager_events_async (GamiManager *ami,
                                GamiEventMask event_mask,
								const gchar *action_id,
                                GAsyncReadyCallback callback,
								gpointer user_data);
gboolean gami_manager_events_finish (GamiManager *ami,
                                     GAsyncResult *result,
                                     GError **error);

gboolean gami_manager_user_event (GamiManager *ami,
                                  const gchar *user_event,
								  const GHashTable *headers,
								  const gchar *action_id,
                                  GError **error);
void gami_manager_user_event_async (GamiManager *ami,
									const gchar *user_event,
									const GHashTable *headers,
									const gchar *action_id,
                                    GAsyncReadyCallback callback,
									gpointer user_data);
gboolean gami_manager_user_event_finish (GamiManager *ami,
                                         GAsyncResult *result,
                                         GError **error);

gboolean gami_manager_wait_event (GamiManager *ami,
                                  guint timeout,
								  const gchar *action_id,
                                  GError **error);
void gami_manager_wait_event_async (GamiManager *ami,
                                    guint timeout,
									const gchar *action_id,
                                    GAsyncReadyCallback callback,
									gpointer user_data);
gboolean gami_manager_wait_event_finish (GamiManager *ami,
                                         GAsyncResult *result,
                                         GError **error);

G_END_DECLS

#endif /* _AMI_MANAGER_H_ */
