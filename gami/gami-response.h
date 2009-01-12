/* vim: se sw=4 ts=4 tw=80 fo+=t cin cino=(0 :*/
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

#if !defined (__GAMI_H_INSIDE__) && !defined (GAMI_COMPILATION)
#  error "Only <gami.h> can be included directly."
#endif

#ifndef __GAMI_RESPONSE_H__
#define __GAMI_RESPONSE_H__

#include <glib.h>
#include <glib-object.h>

/**
 * GamiResponse:
 *
 * The data type used to encapsulate manager responses. All the fields in the
 * GamiResponse structure are private to the #GamiResponse implementation and 
 * should never be accessed directly.
 */
typedef struct _GamiResponse GamiResponse;

GamiResponse *gami_response_new (GValue *value,
                               gchar *message,
                               gchar *action_id);

GamiResponse *gami_response_ref   (GamiResponse *response);
void          gami_response_unref (GamiResponse *response);

gboolean     gami_response_success   (GamiResponse *response);
const gchar *gami_response_message   (GamiResponse *response);
const gchar *gami_response_action_id (GamiResponse *response);
GValue      *gami_response_value     (GamiResponse *response);

/**
 * GAMI_TYPE_RESPONSE:
 * 
 * Get the #GType of #GamiResponse
 *
 * Returns: the #GType of #GamiResponse
 */
#define GAMI_TYPE_RESPONSE (gami_response_get_type ())
GType gami_response_get_type (void) G_GNUC_CONST;

/* unfortunately, this is not part of gboxed ... */
/**
 * G_TYPE_SLIST:
 *
 * Get the #GType of #GSList. This will hopefully become part of gobject like
 * #GTypeHashTable - unfortunately, it is not.
 *
 * Returns: The #GType of #GSList
 */
#define G_TYPE_SLIST (g_slist_get_type ())
GType g_slist_get_type (void) G_GNUC_CONST;

#endif
