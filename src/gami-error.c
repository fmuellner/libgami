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

#include "config.h"
#include "gami-error.h"

/**
 * SECTION: gami-error
 * @short_description: Error helper functions
 *
 * Contains helper functions for reporting errors to the user.
 **/

/**
 * gami_error_quark:
 *
 * Gets the Gami Error Quark.
 *
 * Return value: a #GQuark
 **/
GQuark
gami_error_quark ()
{
    return g_quark_from_static_string ("gami-error-quark");
}
