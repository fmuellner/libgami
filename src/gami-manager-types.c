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

#include <gami-manager-types.h>

struct _GamiQueueStatusEntry {
	GHashTable    *params;
	GSList        *members;
	volatile gint  ref_count;
};

static gpointer
queue_status_entry_copy (gpointer boxed)
{
    GamiQueueStatusEntry *entry = boxed;
    return gami_queue_status_entry_ref (entry);
}

static void
queue_status_entry_free (gpointer boxed)
{
    GamiQueueStatusEntry *entry = boxed;
    gami_queue_status_entry_unref (entry);
}

GType
gami_queue_status_entry_get_type (void)
{
    static GType type_id = 0;
    if (! type_id)
        type_id = g_boxed_type_register_static (g_intern_static_string
                                                ("GamiQueueStatusEntry"),
                                                queue_status_entry_copy,
                                                queue_status_entry_free);
    return type_id;
}

GamiQueueStatusEntry *
gami_queue_status_entry_new (GHashTable *params) {
    GamiQueueStatusEntry *entry;

    entry = g_new (GamiQueueStatusEntry, 1);
    entry->params    = g_hash_table_ref (params);
    entry->members   = NULL;
    entry->ref_count = 1;

    return entry;
}

GamiQueueStatusEntry *
gami_queue_status_entry_ref (GamiQueueStatusEntry *entry)
{
    g_return_val_if_fail (entry != NULL, NULL);
    g_return_val_if_fail (entry->ref_count > 0, entry);

    g_atomic_int_add (&entry->ref_count, 1);
    return entry;
}

void
gami_queue_status_entry_unref (GamiQueueStatusEntry *entry)
{
    g_return_if_fail (entry != NULL);
    g_return_if_fail (entry->ref_count > 0);

    if (g_atomic_int_exchange_and_add (&entry->ref_count, -1) - 1 == 0) {
        g_hash_table_unref (entry->params);
        g_slist_foreach (entry->members, (GFunc) g_hash_table_unref, NULL);
        g_slist_free (entry->members);
        g_free (entry);
    }
}

void
gami_queue_status_entry_add_member (GamiQueueStatusEntry *entry,
                                    GHashTable *member)
{
    g_return_if_fail (entry != NULL);
    g_return_if_fail (entry->ref_count > 0);

    entry->members = g_slist_prepend (entry->members,
                                      g_hash_table_ref (member));
}

GSList *
gami_queue_status_entry_get_members (GamiQueueStatusEntry *entry)
{
    g_return_val_if_fail (entry != NULL, NULL);
    g_return_val_if_fail (entry->ref_count > 0, NULL);

    return g_slist_reverse (entry->members);
}

GHashTable *
gami_queue_status_entry_get_params  (GamiQueueStatusEntry *entry)
{
    g_return_val_if_fail (entry != NULL, NULL);
    g_return_val_if_fail (entry->ref_count > 0, NULL);

    return entry->params;
}
