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


#include <gami-response.h>

/**
 * SECTION: libgami-response
 * @short_description: The response type returned by #GamiManager actions
 * @title: GamiResponse
 * @stability: Unstable
 *
 */

struct _GamiResponse {
    GValue        *value;
    gchar         *action_id;
    gchar         *message;
    volatile gint  ref_count;
};

static gpointer response_copy (gpointer boxed);
static void     response_free (gpointer boxed);

/**
 * gami_response_new:
 * @value: the #GValue of the response
 * @message: the message header of the response packet
 * @action_id: the ActionID header of the response packet
 *
 * This function creates an instance of %GAMI_TYPE_RESPONSE 
 *
 * Returns: A new #GamiResponse
 */
GamiResponse *
gami_response_new (GValue *value, gchar *message, gchar *action_id)
{
    GamiResponse *resp;

    resp = g_slice_new0 (GamiResponse);

    resp->value = value;
    resp->message = g_strdup (message);
    resp->action_id = g_strdup (action_id);

    resp->ref_count = 1;

    return resp;
}

/**
 * gami_response_ref:
 * @response: a valid #GamiResponse
 *
 * Atomically increments the reference count of @response by one.
 *
 * Returns: the passed in #GamiResponse
 */
GamiResponse *
gami_response_ref (GamiResponse *response)
{
    g_return_val_if_fail (response != NULL, NULL);
    g_return_val_if_fail (response->ref_count > 0, response);

    g_atomic_int_inc (&response->ref_count);
    return response;
}

/**
 * gami_response_unref:
 * @response: a valid #GamiResponse
 *
 * Atomically decrements the reference count of @response by one. If the 
 * reference count drops to 0, all elements will be destroyed, and all memory
 * allocated by the response is released.
 */
void
gami_response_unref   (GamiResponse *response)
{
    g_return_if_fail (response != NULL);
    g_return_if_fail (response->ref_count > 0);

    if (g_atomic_int_dec_and_test (&response->ref_count)) {
        if (G_VALUE_TYPE (response->value))
            g_value_unset (response->value);
        g_free (response->value);
        if (response->action_id)
            g_free (response->action_id);
        if (response->message)
            g_free (response->message);
        g_slice_free (GamiResponse, response);
    }
}

/**
 * gami_response_success:
 * @response: the #GamiResponse to check
 *
 * Check whether the action associated with @response indicated success
 *
 * Returns: %TRUE if the action indicated success, otherwise %FALSE
 */
gboolean
gami_response_success   (GamiResponse *resp)
{
    if (G_VALUE_TYPE (resp->value) == G_TYPE_BOOLEAN)
        return g_value_get_boolean (resp->value);

    return TRUE;
}

/**
 * gami_response_message:
 * @response: a valid #GamiResponse
 *
 * Get the message header of the response packet. The returned string belongs 
 * to the #GamiResponse structure and should not be freed or modified. Note 
 * that the Message header is not mandatory in older AMI API versions, so 
 * you should expect a return value of %NULL.
 *
 * Returns: the message header of @response if set, or %NULL otherwise
 */
const gchar *
gami_response_message (GamiResponse *response)
{
    return (const gchar *) response->message;
}

/**
 * gami_response_action_id:
 * @response: a valid #GamiResponse
 *
 * Get the ActionID header of the response packet. The string returned belongs 
 * to the #GamiResponse structure and must not be freed or modified. This 
 * function will only return a value if an action_id was passed to the 
 * associated action.
 *
 * Returns: the action_id associated with @response's action or %NULL
 */
const gchar *
gami_response_action_id (GamiResponse *response)
{
    return (const gchar *) response->action_id;
}

/**
 * gami_response_value
 * @response: a valid #GamiResponse
 *
 * Get the value of the response packet. The type of the #GValue depends
 * on the action associated with @response. It should be one of %G_TYPE_BOOLEAN,
 * %G_TYPE_SLIST, %G_TYPE_STRING or %G_TYPE_HASH_TABLE. Consult the
 * documentation for #AmiManager to see which type should be expected for each
 * action. If the action failed, the type will always be %G_TYPE_BOOLEAN with
 * a value of %FALSE.
 *
 * Returns: the value returned by the action associated with @response
 */
GValue *
gami_response_value     (GamiResponse *response)
{
    return response->value;
}

static gpointer
response_copy (gpointer boxed)
{
    GamiResponse *resp = boxed;
    return gami_response_ref (resp);
}

static void
response_free (gpointer boxed)
{
    GamiResponse *resp = boxed;
    return gami_response_unref (resp);
}

static gpointer
list_copy (gpointer boxed)
{
    GSList *list = boxed;
    return g_slist_copy (list);
}

static void
list_free (gpointer boxed)
{
    GSList *list = boxed;
    return g_slist_free (list);
}

/**
 * gami_response_get_type:
 *
 * Get the #GType of #GamiResponse.
 *
 * Returns: The #GType of #GamiResponse
 */
GType
gami_response_get_type ()
{
    static GType type_id = 0;
    if (! type_id)
        type_id = g_boxed_type_register_static (g_intern_static_string ("GamiResponse"),
                                                response_copy, response_free);
    return type_id;
}

/**
 * g_slist_get_type:
 *
 * Get the #GType of #GSList. This will hopefully become part of gobject like
 * #GTypeHashTable - unfortunately, it is not.
 *
 * Returns: The #GType of #GSList
 */
GType
g_slist_get_type (void)
{
    static GType type_id = 0;
    if (! type_id)
        type_id = g_boxed_type_register_static (g_intern_static_string ("GSList"),
                                                list_copy, list_free);
    return type_id;
}
