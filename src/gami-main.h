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

#if !defined (__GAMI_H_INSIDE__) && !defined (GAMI_COMPILATION)
#  error "Only <gami.h> can be included directly."
#endif

#ifndef GAMI_MAIN_H
#define GAMI_MAIN_H

#include <glib.h>
#include <glib-object.h>

void gami_init (int *argc, char ***argv);
GOptionGroup *gami_get_option_group (void);
gboolean gami_parse_args (int *argc, char ***argv);

#endif
