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

#ifndef __GAMI_MANAGER_TYPES_H__
#define __GAMI_MANAGER_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * GamiQueueRule:
 * @seconds: number of seconds when the rule should be applied
 * @max_penalty_change: relative or absolute change of the MAX_PENALTY property
 * @min_penalty_change: relative or absolute change of the MIN_PENALTY property
 *
 * #GamiQueueRule represents a queue rule as defined in queuerules.conf. The
 * gami_manager_queue_rule() action returns a #GHashTable which holds a #GSList 
 * of #GamiQueueRule as value.
 */
typedef struct _GamiQueueRule        GamiQueueRule;

struct _GamiQueueRule {
	gint   seconds;
	gchar *max_penalty_change;
	gchar *min_penalty_change;
};

/**
 * GAMI_TYPE_QUEUE_RULE:
 *
 * Get the #GType of #GamiQueueRule
 *
 * Returns: The #GType of #GamiQueueRule
 */
#define GAMI_TYPE_QUEUE_RULE (gami_queue_rule_get_type ())

/**
 * gami_queue_rule_get_type:
 *
 * Get the #GType of #GamiQueueRule
 *
 * Returns: The #GType of #GamiQueueRule
 */
GType gami_queue_rule_get_type (void) G_GNUC_CONST;

/**
 * GamiQueueStatusEntry:
 *
 * #GamiQueueStatusEntry represents a queue status entry as returned by the
 * gami_manager_queue_status() action.
 * It is an ref-counted opaque structure holding both queue properties and a
 * list of queue members, which should only be accessed by the corresponding
 * functions.
 */
typedef struct _GamiQueueStatusEntry GamiQueueStatusEntry;

/**
 * GAMI_TYPE_QUEUE_STATUS_ENTRY:
 *
 * Get the #GType of #GamiQueueStatusEntry
 *
 * Returns: The #GType of #GamiQueueStatusEntry
 */
#define GAMI_TYPE_QUEUE_STATUS_ENTRY (gami_queue_status_entry_get_type ())

/**
 * gami_queue_status_entry_get_type:
 *
 * Get the #GType of #GamiQueueStatusEntry
 *
 * Returns: The #GType of #GamiQueueStatusEntry
 */
GType gami_queue_status_entry_get_type (void) G_GNUC_CONST;

GamiQueueStatusEntry *gami_queue_status_entry_new (GHashTable *params);

GamiQueueStatusEntry *gami_queue_status_entry_ref (GamiQueueStatusEntry *entry);
void  gami_queue_status_entry_unref (GamiQueueStatusEntry *entry);

void gami_queue_status_entry_add_member (GamiQueueStatusEntry *entry,
										 GHashTable *member);

GHashTable *gami_queue_status_entry_get_params  (GamiQueueStatusEntry *entry);
GSList     *gami_queue_status_entry_get_members (GamiQueueStatusEntry *entry);

G_END_DECLS

#endif /* __GAMI_MANAGER_TYPES_H__ */
