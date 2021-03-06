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


#if !defined(__GAMI_H_INSIDE__) && !defined (GAMI_COMPILATION)
#  error "Only <gami.h> can be included directly."
#endif

#ifndef __GAMI_ENUMS_H__
#define __GAMI_ENUMS_H__

#include <glib.h>
#include <glib-object.h>

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
 * GamiLogLevelFlags:
 * @GAMI_LOG_LEVEL_NET_RX: log level for received network traffic
 * @GAMI_LOG_LEVEL_NET_TX: log level for transmitted network traffic
 *
 * Custom log levels where a #GamiManager dumps all network traffic.
 */
typedef enum {
	GAMI_LOG_LEVEL_NET_RX = 1 << (G_LOG_LEVEL_USER_SHIFT + 0),
	GAMI_LOG_LEVEL_NET_TX = 1 << (G_LOG_LEVEL_USER_SHIFT + 1)
} GamiLogLevelFlags;

/**
 * gami_module_load_type_get_type:
 *
 * Get the #GType of enum #GamiModuleLoadType
 *
 * Returns: The #GType of #GamiModuleLoadType
 */

/*
 * FIXME: define error conditions instead of mangling everything into
 *        a generic condition
 */

/**
 * GamiError:
 * @GAMI_ERROR_FAILED: Generic error condition when any action fails.
 *
 * Error codes returned by Gami functions.
 *
 **/
typedef enum {
	GAMI_ERROR_FAILED
} GamiError;

G_END_DECLS

#endif
