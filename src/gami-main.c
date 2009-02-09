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

#include "config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <locale.h>
#include <gami-main.h>

#ifdef G_OS_WIN32
#  include <ws2tcpip.h>
#endif


/**
 * SECTION: gami-main
 * @short_description: Library initialization
 * @title: General
 * @stability: Unstable
 *
 * This section describes the Gami initialization functions
 */

static gboolean g_fatal_warnings = FALSE;

static const GOptionEntry gami_args[] = {
    { "g-fatal-warnings", 0, 0, G_OPTION_ARG_NONE, &g_fatal_warnings,
        N_("Make all warnings fatal"), NULL },
    { NULL }
};

/* default log handler - does absolutely nothing :) */
static void null_log (const gchar *log_domain, GLogLevelFlags log_level,
                      const gchar *message, gpointer user_data);


static void
gettext_initialization (void)
{
    setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, GAMI_LOCALEDIR);
#endif
}

static gboolean
post_parse_hook (GOptionContext *context, GOptionGroup *group,
                 gpointer data, GError **error)
{
    gettext_initialization ();

    if (g_fatal_warnings) {
        GLogLevelFlags fatal_mask;

        fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
        fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
        g_log_set_always_fatal (fatal_mask);
    }

    g_type_init ();

    return TRUE;
}

/**
 * gami_get_option_group:
 *
 * Returns a #GOptionGroup for the commandline arguments recognized by
 * Gami. You should add this group to your #GOptionContext with
 * g_option_context_add_group(), if you are using g_option_context_parse() to
 * parse your commandline arguments
 *
 * Returns: a #GOptionGroup for the commandline arguments recognized by Gami
 */
GOptionGroup *
gami_get_option_group (void)
{
    GOptionGroup *group;

    gettext_initialization ();

    group = g_option_group_new ("gami", _("Gami Options"),
                                _("Show Gami Options"), NULL, NULL);
    g_option_group_set_parse_hooks (group, NULL, post_parse_hook);
    g_option_group_add_entries (group, gami_args);
    g_option_group_set_translation_domain (group, GETTEXT_PACKAGE);

    return group;
}

/**
 * gami_init:
 * @argc: Address of the <parameter>argc</parameter> parameter of main().
 *        Changed if any arguments were handled.
 * @argv: Address of the <parameter>argv</parameter> parameter of main().
 *        Any parameters understood by gami_init() are stripped before return.
 *
 * Call this function before using any other Gami functions in your application.
 * It will initialize the underlying type system and parses some standard 
 * command line options. @argc and @argv are adjusted accordingly so your own
 * code will never see those arguments.
 *
 * Debug messages in the library will be disabled by default, to get them
 * back, install a log handler using g_log_set_handler() for the domain "Gami".
 *
 * On Windows, the network stack is initialized as well.
 */
void
gami_init (int *argc, char ***argv)
{
#ifdef G_OS_WIN32
    WSADATA wsaData;

    if (WSAStartup (MAKEWORD (2, 0), &wsaData) != 0)
        g_error ("Failed to initialize WinSock stack");

    if (LOBYTE (wsaData.wVersion) != 2 || HIBYTE (wsaData.wVersion) != 0) {
        WSACleanup ();
        g_error ("No usable version of WinSock DLL found.");
    }
#endif

    g_log_set_handler ("Gami", G_LOG_LEVEL_DEBUG, null_log, NULL);
    gami_parse_args (argc, argv);
}

/**
 * gami_parse_args:
 * @argc: a pointer to the number of command line arguments.
 * @argv: a pointer to the array of command line arguments.
 *
 * Parses command line arguments, and initializes global attributes of Gami.
 * Any arguments used by Gami are removed from the array and @argc and @argv
 * are updated accordingly.
 *
 * Return value: %TRUE if initialization succeeded, otherwise %FALSE.
 */
gboolean
gami_parse_args (int *argc, char ***argv)
{
    GOptionContext *option_context;
    GOptionGroup   *gami_group;
    GError         *error = NULL;

    gettext_initialization ();

    gami_group = gami_get_option_group ();
    option_context = g_option_context_new (NULL);

    g_option_context_set_ignore_unknown_options (option_context, TRUE);
    g_option_context_set_help_enabled (option_context, FALSE);
    g_option_context_set_main_group (option_context, gami_group);

    if (! g_option_context_parse (option_context, argc, argv, &error)) {
        g_warning ("%s", error->message);
        g_error_free (error);
    }

    g_option_context_free (option_context);

    return TRUE;
}

static void
null_log (const gchar *log_domain, GLogLevelFlags log_level,
          const gchar *message, gpointer user_data)
{
    /* don't log anything by default */
}
