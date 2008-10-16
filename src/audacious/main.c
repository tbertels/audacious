/*  Audacious - Cross-platform multimedia player
 *  Copyright (C) 2005-2007  Audacious development team.
 *
 *  Based on BMP:
 *  Copyright (C) 2003-2004  BMP development team.
 *
 *  Based on XMMS:
 *  Copyright (C) 1998-2003  XMMS development team.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses>.
 *
 *  The Audacious team does not consider modular code linking to
 *  Audacious or using our public API to be a derived work.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "platform/smartinclude.h"

#include "main.h"

#include <glib/gprintf.h>

#ifdef USE_SAMPLERATE
#  include <samplerate.h>
#endif

#ifdef USE_DBUS
#  include "dbus-service.h"
#  include "audctrl.h"
#endif

#ifdef USE_EGGSM
#include "eggsmclient.h"
#include "eggdesktopfile.h"
#endif

#include "libSAD.h"

#include "build_stamp.h"
#include "configdb.h"
#include "input.h"
#include "logger.h"
#include "output.h"
#include "playback.h"
#include "playlist.h"
#include "pluginenum.h"
#include "signals.h"
#include "ui_manager.h"
#include "util.h"
#include "vfs.h"

#include "icons-stock.h"
#include "images/audacious_player.xpm"

#include "ui_new.h"
#include "ui_legacy.h"
#include "ui_headless.h"

static const gchar *application_name = N_("Audacious");

struct _AudCmdLineOpt {
    gchar **filenames;
    gint session;
    gboolean play, stop, pause, fwd, rew, play_pause, show_jump_box;
    gboolean enqueue, mainwin, remote, activate;
    gboolean no_log;
    gboolean enqueue_to_temp;
    gboolean version;
    gchar *previous_session_id;
    gchar *interface;
    gboolean macpack;
};
typedef struct _AudCmdLineOpt AudCmdLineOpt;

static AudCmdLineOpt options = {};

gchar *aud_paths[BMP_PATH_COUNT] = {};

GCond *cond_scan;
GMutex *mutex_scan;
#ifdef USE_DBUS
MprisPlayer *mpris;
#endif

static void
print_version(void)
{
    g_printf("%s %s [%s]\n", _(application_name), VERSION, svn_stamp);
}

static void
aud_make_user_dir(void)
{
    const mode_t mode755 = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

    make_directory(aud_paths[BMP_PATH_USER_DIR], mode755);
    make_directory(aud_paths[BMP_PATH_USER_PLUGIN_DIR], mode755);
    make_directory(aud_paths[BMP_PATH_USER_SKIN_DIR], mode755);
    make_directory(aud_paths[BMP_PATH_SKIN_THUMB_DIR], mode755);
    make_directory(aud_paths[BMP_PATH_PLAYLISTS_DIR], mode755);
}

static void
aud_free_paths(void)
{
    int i;

    for (i = 0; i < BMP_PATH_COUNT; i++)
    {
        g_free(aud_paths[i]);
        aud_paths[i] = 0;
    }
}

static void
aud_init_paths()
{
    char *xdg_config_home;
    char *xdg_data_home;
    char *xdg_cache_home;

    xdg_config_home = (getenv("XDG_CONFIG_HOME") == NULL
        ? g_build_filename(g_get_home_dir(), ".config", NULL)
        : g_strdup(getenv("XDG_CONFIG_HOME")));
    xdg_data_home = (getenv("XDG_DATA_HOME") == NULL
        ? g_build_filename(g_get_home_dir(), ".local", "share", NULL)
        : g_strdup(getenv("XDG_DATA_HOME")));
    xdg_cache_home = (getenv("XDG_CACHE_HOME") == NULL
        ? g_build_filename(g_get_home_dir(), ".cache", NULL)
        : g_strdup(getenv("XDG_CACHE_HOME")));

    aud_paths[BMP_PATH_USER_DIR] =
        g_build_filename(xdg_config_home, "audacious", NULL);
    aud_paths[BMP_PATH_USER_SKIN_DIR] =
        g_build_filename(xdg_data_home, "audacious", "Skins", NULL);
    aud_paths[BMP_PATH_USER_PLUGIN_DIR] =
        g_build_filename(xdg_data_home, "audacious", "Plugins", NULL);

    aud_paths[BMP_PATH_SKIN_THUMB_DIR] =
        g_build_filename(xdg_cache_home, "audacious", "thumbs", NULL);

    aud_paths[BMP_PATH_PLAYLISTS_DIR] =
        g_build_filename(aud_paths[BMP_PATH_USER_DIR], "playlists", NULL);

    aud_paths[BMP_PATH_CONFIG_FILE] =
        g_build_filename(aud_paths[BMP_PATH_USER_DIR], "config", NULL);
#ifdef HAVE_XSPF_PLAYLIST
    aud_paths[BMP_PATH_PLAYLIST_FILE] =
        g_build_filename(aud_paths[BMP_PATH_USER_DIR],
        "playlist.xspf", NULL);
#else
    aud_paths[BMP_PATH_PLAYLIST_FILE] =
        g_build_filename(aud_paths[BMP_PATH_USER_DIR],
        "playlist.m3u", NULL);
#endif
    aud_paths[BMP_PATH_ACCEL_FILE] =
        g_build_filename(aud_paths[BMP_PATH_USER_DIR], "accels", NULL);
    aud_paths[BMP_PATH_LOG_FILE] =
        g_build_filename(aud_paths[BMP_PATH_USER_DIR], "log", NULL);

    aud_paths[BMP_PATH_GTKRC_FILE] =
        g_build_filename(aud_paths[BMP_PATH_USER_DIR], "gtkrc", NULL);

    g_free(xdg_config_home);
    g_free(xdg_data_home);
    g_free(xdg_cache_home);

    g_atexit(aud_free_paths);
}

static void
aud_set_default_icon(void)
{
    GdkPixbuf *icon;

    icon = gdk_pixbuf_new_from_xpm_data((const gchar **) audacious_player_xpm);
    gtk_window_set_default_icon(icon);
    g_object_unref(icon);
}

static GOptionEntry cmd_entries[] = {
    {"rew", 'r', 0, G_OPTION_ARG_NONE, &options.rew, N_("Skip backwards in playlist"), NULL},
    {"play", 'p', 0, G_OPTION_ARG_NONE, &options.play, N_("Start playing current playlist"), NULL},
    {"pause", 'u', 0, G_OPTION_ARG_NONE, &options.pause, N_("Pause current song"), NULL},
    {"stop", 's', 0, G_OPTION_ARG_NONE, &options.stop, N_("Stop current song"), NULL},
    {"play-pause", 't', 0, G_OPTION_ARG_NONE, &options.play_pause, N_("Pause if playing, play otherwise"), NULL},
    {"fwd", 'f', 0, G_OPTION_ARG_NONE, &options.fwd, N_("Skip forward in playlist"), NULL},
    {"show-jump-box", 'j', 0, G_OPTION_ARG_NONE, &options.show_jump_box, N_("Display Jump to File dialog"), NULL},
    {"enqueue", 'e', 0, G_OPTION_ARG_NONE, &options.enqueue, N_("Don't clear the playlist"), NULL},
    {"enqueue-to-temp", 'E', 0, G_OPTION_ARG_NONE, &options.enqueue_to_temp, N_("Add new files to a temporary playlist"), NULL},
    {"show-main-window", 'm', 0, G_OPTION_ARG_NONE, &options.mainwin, N_("Display the main window"), NULL},
    {"activate", 'a', 0, G_OPTION_ARG_NONE, &options.activate, N_("Display all open Audacious windows"), NULL},
    {"no-log", 'N', 0, G_OPTION_ARG_NONE, &options.no_log, N_("Print all errors and warnings to stdout"), NULL},
    {"version", 'v', 0, G_OPTION_ARG_NONE, &options.version, N_("Show version"), NULL},
    {"interface", 'i', 0, G_OPTION_ARG_STRING, &options.interface, N_("Interface to use"), NULL},
#ifdef GDK_WINDOWING_QUARTZ
    {"macpack", 'n', 0, G_OPTION_ARG_NONE, &options.macpack, N_("Used in macpacking"), NULL}, /* Make this hidden */
#endif
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &options.filenames, N_("FILE..."), NULL},
    {NULL},
};

static gboolean
aud_start_playback(gpointer unused)
{
    drct_play();
    return FALSE;
}

static void
parse_cmd_line_options(gint *argc, gchar ***argv)
{
    GOptionContext *context;
    GError *error = NULL;

    memset(&options, '\0', sizeof(AudCmdLineOpt));
    options.session = -1;

    context = g_option_context_new(_("- play multimedia files"));
    g_option_context_add_main_entries(context, cmd_entries, PACKAGE_NAME);
    g_option_context_add_group(context, gtk_get_option_group(FALSE));
#ifdef USE_EGGSM
    g_option_context_add_group(context, egg_sm_client_get_option_group());
#endif
    if (!g_option_context_parse(context, argc, argv, &error))
        /* checking for MacOS X -psn_0_* errors*/
        if (error->message && !g_strrstr(error->message,"-psn_0_"))
        {
            g_printerr(_("%s: %s\nTry `%s --help' for more information.\n"),
                       (*argv)[0], error->message, (*argv)[0]);
            exit(EXIT_FAILURE);
        }
}

static void
handle_cmd_line_filenames(gboolean is_running)
{
    gchar **filenames = options.filenames;
#ifdef USE_DBUS
    DBusGProxy *session = audacious_get_dbus_proxy();
#endif

    if (filenames == NULL)
        return;

    gint pos = 0;
    gint i = 0;
    GList *fns = NULL;

    for (i = 0; filenames[i] != NULL; i++)
    {
        gchar *filename;
        gchar *current_dir = g_get_current_dir();

        if (!strstr(filenames[i], "://"))
        {
            if (filenames[i][0] == '/')
                filename = g_strdup_printf("file:///%s", filenames[i]);
            else
                filename = g_strdup_printf("file:///%s/%s", current_dir,
                                           filenames[i]);
        }
        else
            filename = g_strdup(filenames[i]);

        fns = g_list_prepend(fns, filename);

        g_free(current_dir);
    }

    fns = g_list_reverse(fns);

#ifdef USE_DBUS
    if (is_running)
    {
        GList *i;

        if (options.enqueue_to_temp)
            audacious_remote_playlist_enqueue_to_temp(session, filenames[0]);

        if (options.enqueue && options.play)
            pos = audacious_remote_get_playlist_length(session);

        if (!options.enqueue)
        {
            audacious_remote_playlist_clear(session);
            audacious_remote_stop(session);
        }

        for (i = fns; i != NULL; i = i->next)
            audacious_remote_playlist_add_url_string(session, i->data);

        if (options.enqueue && options.play &&
            audacious_remote_get_playlist_length(session) > pos)
            audacious_remote_set_playlist_pos(session, pos);

        if (!options.enqueue)
            audacious_remote_play(session);
    }
    else /* !is_running */
#endif
    {
        if (options.enqueue_to_temp)
            drct_pl_enqueue_to_temp(filenames[0]);

        if (options.enqueue && options.play)
            pos = drct_pl_get_length();

        if (!options.enqueue)
        {
            drct_pl_clear();
            drct_stop();
        }

        drct_pl_add(fns);

        if (options.enqueue && options.play &&
            drct_pl_get_length() > pos)
            drct_pl_set_pos(pos);

        if (!options.enqueue)
            g_idle_add(aud_start_playback, NULL);
    } /* !is_running */

    g_list_foreach(fns, (GFunc) g_free, NULL);
    g_list_free(fns);
}

static void
handle_cmd_line_options(gboolean skip)
{
    gboolean is_running = FALSE;

#ifdef USE_DBUS
    DBusGProxy *session = audacious_get_dbus_proxy();

    if (skip)
        is_running = audacious_remote_is_running(session);
#endif

    if (options.version)
    {
        print_version();
        exit(EXIT_SUCCESS);
    }

    if (options.interface == NULL)
        options.interface = g_strdup("default");

    handle_cmd_line_filenames(is_running);

#ifdef USE_DBUS
    if (is_running)
    {
        if (options.rew)
            audacious_remote_playlist_prev(session);

        if (options.play)
            audacious_remote_play(session);

        if (options.pause)
            audacious_remote_pause(session);

        if (options.stop)
            audacious_remote_stop(session);

        if (options.fwd)
            audacious_remote_playlist_next(session);

        if (options.play_pause)
            audacious_remote_play_pause(session);

        if (options.show_jump_box)
            audacious_remote_show_jtf_box(session);

        if (options.mainwin)
            audacious_remote_main_win_toggle(session, TRUE);

        if (options.activate)
            audacious_remote_activate(session);

        exit(EXIT_SUCCESS);
    } /* is_running */
    else
#endif
    {
        if (options.rew)
            drct_pl_prev();

        if (options.play)
            drct_play();

        if (options.pause)
            drct_pause();

        if (options.stop)
            drct_stop();

        if (options.fwd)
            drct_pl_next();

        if (options.play_pause)
        {
            if (drct_get_paused())
                drct_play();
            else
                drct_pause();
        }

        if (options.show_jump_box)
            drct_jtf_show();

        if (options.mainwin)
            drct_main_win_toggle(TRUE);

        if (options.activate)
            drct_activate();
    } /* !is_running */
}

static void
aud_setup_logger(void)
{
    if (!aud_logger_start(aud_paths[BMP_PATH_LOG_FILE]))
        return;

    g_atexit(aud_logger_stop);
}

static gboolean
load_extra_playlist(const gchar * path, const gchar * basename,
        gpointer def)
{
    Playlist *playlist = playlist_new();

    if (!playlist) {
        g_warning("Couldn't create new playlist for %s / %s\n", path, basename);
        return FALSE;
    }

    playlist_add_playlist(playlist);
    playlist_load(playlist, path);

    return FALSE; /* keep loading other playlists */
}

static void
playlist_system_init()
{
    Playlist *playlist;

    playlist_init();
    playlist = playlist_get_active();
    playlist_load(playlist, aud_paths[BMP_PATH_PLAYLIST_FILE]);
    playlist_set_position(playlist, cfg.playlist_position);

    /* Load extra playlists */
    if (!dir_foreach(aud_paths[BMP_PATH_PLAYLISTS_DIR], load_extra_playlist,
                     playlist, NULL))
        g_warning("Could not load extra playlists\n");
}

void
aud_quit(void)
{
    GList *playlists = NULL, *playlists_top = NULL;
    Interface *i = interface_get(options.interface);

    playlist_stop_get_info_thread();

    aud_config_save();

    interface_destroy(i);

    plugin_system_cleanup();

    /* free and clear each playlist */
    playlists = playlist_get_playlists();
    playlists_top = playlists;
    while ( playlists != NULL )
    {
        playlist_clear((Playlist*)playlists->data);
        playlist_free((Playlist*)playlists->data);
        playlists = g_list_next(playlists);
    }
    g_list_free( playlists_top );

    g_cond_free(cond_scan);
    g_mutex_free(mutex_scan);

    exit(EXIT_SUCCESS);
}

gint
main(gint argc, gchar ** argv)
{
    Interface *i;

    /* glib-2.13.0 requires g_thread_init() to be called before all
       other GLib functions */
    g_thread_init(NULL);
    if (!g_thread_supported()) {
        g_printerr(_("Sorry, threads aren't supported on your platform.\n"));
        exit(EXIT_FAILURE);
    }

    gdk_threads_init();
    mowgli_init();

    /* Setup l10n early so we can print localized error messages */
    gtk_set_locale();
    bindtextdomain(PACKAGE_NAME, LOCALEDIR);
    bind_textdomain_codeset(PACKAGE_NAME, "UTF-8");
    bindtextdomain(PACKAGE_NAME "-plugins", LOCALEDIR);
    bind_textdomain_codeset(PACKAGE_NAME "-plugins", "UTF-8");
    textdomain(PACKAGE_NAME);

#if !defined(_WIN32) && defined(USE_EGGSM)
    egg_set_desktop_file(AUDACIOUS_DESKTOP_FILE);
#endif
    aud_init_paths();
    aud_make_user_dir();

    cond_scan = g_cond_new();
    mutex_scan = g_mutex_new();
    gtk_rc_add_default_file(aud_paths[BMP_PATH_GTKRC_FILE]);

    parse_cmd_line_options(&argc, &argv);

    if (options.no_log == FALSE)
        aud_setup_logger();

    g_message("Initializing Gtk+");
    if (!gtk_init_check(&argc, &argv)) { /* XXX */
        /* GTK check failed, and no arguments passed to indicate
           that user is intending to only remote control a running
           session */
        g_printerr(_("%s: Unable to open display, exiting.\n"), argv[0]);
        exit(EXIT_FAILURE);
    }

    g_message("Setting up libSAD");
    g_random_set_seed(time(NULL));
    SAD_dither_init_rand((gint32)time(NULL));

    g_message("Loading configuration");
    aud_config_load();

    g_message("Initializing signal handlers");
    signal_handlers_init();

    g_message("Handling commandline options, part #1");
    handle_cmd_line_options(TRUE);

    if (g_ascii_strcasecmp(options.interface, "headless")) /* XXX */
    {
        g_message("Non-headless operation setup");
        ui_main_check_theme_engine();

        /* register icons in stock
           NOTE: should be called before UIManager */
        register_aud_stock_icons();

        /* UIManager
           NOTE: this needs to be called before plugin init, cause
           plugin init functions may want to add custom menu entries */
        ui_manager_init();
        ui_manager_create_menus();
        create_prefs_window();
    }

#ifdef USE_DBUS
    g_message("Initializing D-Bus");
    init_dbus();
#endif

    g_message("Initializing plugin subsystems...");
    plugin_system_init();

    g_message("Setting up playlists");
    playlist_system_init();

    g_message("Handling commandline options, part #2");
    handle_cmd_line_options(FALSE);

    g_message("Playlist scanner thread startup");
    playlist_start_get_info_thread();

    output_set_volume((cfg.saved_volume & 0xff00) >> 8,
                      (cfg.saved_volume & 0x00ff));

    g_message("Setting default icon");
    aud_set_default_icon();

    g_message("Populating included interfaces");
    ui_populate_default_interface();
    ui_populate_legacy_interface();
    ui_populate_headless_interface();

    g_message("Selecting interface %s", options.interface);
    i = interface_get(options.interface);

    g_message("Running interface %s@%p", options.interface, i);
    interface_run(i);

    aud_quit();
    return EXIT_SUCCESS;
}
