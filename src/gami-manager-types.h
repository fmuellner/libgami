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

typedef struct _GamiQueueRule        GamiQueueRule;
typedef struct _GamiQueueStatusEntry GamiQueueStatusEntry;

gint gami_queue_rule_seconds (GamiQueueRule *rule);
const gchar *gami_queue_rule_get_max_penalty_change (GamiQueueRule *rule);
const gchar *gami_queue_rule_get_min_penalty_change (GamiQueueRule *rule);

struct _GamiQueueRule {
	gint   seconds;
	gchar *max_penalty_change;
	gchar *min_penalty_change;
};

#define G_TYPE_QUEUE_STATUS_ENTRY (gami_queue_status_entry_get_type ())

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
