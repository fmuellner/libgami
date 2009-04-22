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

#ifndef __GAMI_ERROR_H__
#define __GAMI_ERROR_H__

#include <glib.h>
#include <gami-enums.h>

G_BEGIN_DECLS

/**
 * GAMI_ERROR:
 *
 * Error domain for Gami. Errors in this will be from the #
 * amiError enumeration.
 * See #GError for more information on error domains.
 **/
#define GAMI_ERROR gami_error_quark()

GQuark gami_error_quark (void);

G_END_DECLS

#endif /* __GAMI_ERROR_H__ */
