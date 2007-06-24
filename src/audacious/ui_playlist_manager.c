/*
 * audacious: Cross-platform multimedia player.
 * ui_playlist_manager.c: Playlist Management UI.
 *
 * Copyright (c) 2005-2007 Audacious development team.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ui_playlist_manager.h"
#include "ui_playlist.h"
#include "playlist.h"
#include "ui_main.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>


#define DISABLE_MANAGER_UPDATE() g_object_set_data(G_OBJECT(listview),"opt1",GINT_TO_POINTER(1))
#define ENABLE_MANAGER_UPDATE() g_object_set_data(G_OBJECT(listview),"opt1",GINT_TO_POINTER(0))


static GtkWidget *playman_win = NULL;


/* in this enum, place the columns according to visualization order
   (information not displayed in columns should be placed right before PLLIST_NUMCOLS) */
enum
{
    PLLIST_COL_NAME = 0,
    PLLIST_COL_ENTRIESNUM,
    PLLIST_COL_PLPOINTER,
    PLLIST_NUMCOLS
};


static void
playlist_manager_populate ( GtkListStore * store )
{
    GList *playlists = NULL;
    GtkTreeIter iter;

    playlists = playlist_get_playlists();
    while ( playlists != NULL )
    {
        GList *entries = NULL;
        gint entriesnum = 0;
        gchar *pl_name = NULL;
        Playlist *playlist = (Playlist*)playlists->data;

        PLAYLIST_LOCK(playlist->mutex);
        /* for each playlist, pick name and number of entries */
        pl_name = (gchar*)playlist_get_current_name( playlist );
        for (entries = playlist->entries; entries; entries = g_list_next(entries))
            entriesnum++;
        PLAYLIST_UNLOCK(playlist->mutex);

        gtk_list_store_append( store , &iter );
        gtk_list_store_set( store, &iter,
                            PLLIST_COL_NAME , pl_name ,
                            PLLIST_COL_ENTRIESNUM , entriesnum ,
                            PLLIST_COL_PLPOINTER , playlist , -1 );
        playlists = g_list_next(playlists);
    }
    return;
}


static void
playlist_manager_cb_new ( gpointer listview )
{
    GList *playlists = NULL;
    Playlist *newpl = NULL;
    GtkTreeIter iter;
    GtkListStore *store;
    gchar *pl_name = NULL;

    /* this ensures that playlist_manager_update() will
       not perform update, since we're already doing it here */
    DISABLE_MANAGER_UPDATE();

    newpl = playlist_new();
    pl_name = (gchar*)playlist_get_current_name( newpl );
    playlists = playlist_get_playlists();
    playlist_add_playlist( newpl );

    store = (GtkListStore*)gtk_tree_view_get_model( GTK_TREE_VIEW(listview) );
    gtk_list_store_append( store , &iter );
    gtk_list_store_set( store, &iter,
                        PLLIST_COL_NAME , pl_name ,
                        PLLIST_COL_ENTRIESNUM , 0 ,
                        PLLIST_COL_PLPOINTER , newpl , -1 );

    ENABLE_MANAGER_UPDATE();

    return;
}


static void
playlist_manager_cb_del ( gpointer listview )
{
    GtkTreeSelection *listsel = gtk_tree_view_get_selection( GTK_TREE_VIEW(listview) );
    GtkTreeModel *store;
    GtkTreeIter iter;

    if ( gtk_tree_selection_get_selected( listsel , &store , &iter ) == TRUE )
    {
        Playlist *playlist = NULL;
        gtk_tree_model_get( store, &iter, PLLIST_COL_PLPOINTER , &playlist , -1 );

        if ( gtk_tree_model_iter_n_children( store , NULL ) < 2 )
        {
            /* let playlist_manager_update() handle the deletion of the last playlist */
            playlist_remove_playlist( playlist );
        }
        else
        {
            gtk_list_store_remove( (GtkListStore*)store , &iter );
            /* this ensures that playlist_manager_update() will
               not perform update, since we're already doing it here */
            DISABLE_MANAGER_UPDATE();
            playlist_remove_playlist( playlist );
            ENABLE_MANAGER_UPDATE();
        }
    }

    return;
}


static void
playlist_manager_cb_lv_dclick ( GtkTreeView * lv , GtkTreePath * path ,
                                GtkTreeViewColumn * col , gpointer userdata )
{
    GtkTreeModel *store;
    GtkTreeIter iter;

    store = gtk_tree_view_get_model( GTK_TREE_VIEW(lv) );
    if ( gtk_tree_model_get_iter( store , &iter , path ) == TRUE )
    {
        Playlist *playlist = NULL;
        gtk_tree_model_get( store , &iter , PLLIST_COL_PLPOINTER , &playlist , -1 );
        playlist_select_playlist( playlist );
    }

    return;
}


static void
playlist_manager_cb_lv_pmenu_rename ( GtkMenuItem *menuitem , gpointer lv )
{
    GtkTreeSelection *listsel = gtk_tree_view_get_selection( GTK_TREE_VIEW(lv) );
    GtkTreeModel *store;
    GtkTreeIter iter;

    if ( gtk_tree_selection_get_selected( listsel , &store , &iter ) == TRUE )
    {
        GtkTreePath *path = gtk_tree_model_get_path( GTK_TREE_MODEL(store) , &iter );
        GtkCellRenderer *rndrname = g_object_get_data( G_OBJECT(lv) , "rn" );
        /* set the name renderer to editable and start editing */
        g_object_set( G_OBJECT(rndrname) , "editable" , TRUE , NULL );
        gtk_tree_view_set_cursor_on_cell( GTK_TREE_VIEW(lv) , path ,
                                          gtk_tree_view_get_column( GTK_TREE_VIEW(lv) , PLLIST_COL_NAME ) , rndrname , TRUE );
        gtk_tree_path_free( path );
    }
}

static void
playlist_manager_cb_lv_name_edited ( GtkCellRendererText *cell , gchar *path_string ,
                                     gchar *new_text , gpointer lv )
{
    /* this is currently used to change playlist names */
    GtkTreeModel *store = gtk_tree_view_get_model( GTK_TREE_VIEW(lv) );
    GtkTreeIter iter;

    if ( gtk_tree_model_get_iter_from_string( store , &iter , path_string ) == TRUE )
    {
        Playlist *playlist = NULL;
        gtk_tree_model_get( GTK_TREE_MODEL(store), &iter, PLLIST_COL_PLPOINTER , &playlist , -1 );
        playlist_set_current_name( playlist , new_text );
        gtk_list_store_set( GTK_LIST_STORE(store), &iter, PLLIST_COL_NAME , new_text , -1 );
    }
    /* set the renderer uneditable again */
    g_object_set( G_OBJECT(cell) , "editable" , FALSE , NULL );
}


static gboolean
playlist_manager_cb_lv_btpress ( GtkWidget *lv , GdkEventButton *event )
{
    if (( event->type == GDK_BUTTON_PRESS ) && ( event->button == 3 ))
    {
        GtkWidget *pmenu = (GtkWidget*)g_object_get_data( G_OBJECT(lv) , "menu" );
        gtk_menu_popup( GTK_MENU(pmenu) , NULL , NULL , NULL , NULL ,
                        (event != NULL) ? event->button : 0,
                        gdk_event_get_time((GdkEvent*)event));
        return TRUE;
    }

    return FALSE;
}


static gboolean
playlist_manager_cb_keypress ( GtkWidget *win , GdkEventKey *event )
{
    switch (event->keyval)
    {
        case GDK_Escape:
            gtk_widget_destroy( playman_win );
            return TRUE;
        default:
            return FALSE;
    }
}


void
playlist_manager_ui_show ( void )
{
    GtkWidget *playman_vbox;
    GtkWidget *playman_pl_lv, *playman_pl_lv_frame, *playman_pl_lv_sw;
    GtkCellRenderer *playman_pl_lv_textrndr_name, *playman_pl_lv_textrndr_entriesnum;
    GtkTreeViewColumn *playman_pl_lv_col_name, *playman_pl_lv_col_entriesnum;
    GtkListStore *pl_store;
    GtkWidget *playman_pl_lv_pmenu, *playman_pl_lv_pmenu_rename;
    GtkWidget *playman_bbar_hbbox;
    GtkWidget *playman_bbar_bt_new, *playman_bbar_bt_del, *playman_bbar_bt_close;
    GdkGeometry playman_win_hints;

    if ( playman_win != NULL )
    {
        gtk_window_present( GTK_WINDOW(playman_win) );
        return;
    }

    playman_win = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_type_hint( GTK_WINDOW(playman_win), GDK_WINDOW_TYPE_HINT_DIALOG );
    gtk_window_set_transient_for( GTK_WINDOW(playman_win) , GTK_WINDOW(mainwin) );
    gtk_window_set_position( GTK_WINDOW(playman_win), GTK_WIN_POS_CENTER );
    gtk_window_set_title( GTK_WINDOW(playman_win), _("Playlist Manager") );
    gtk_container_set_border_width( GTK_CONTAINER(playman_win), 10 );
    g_signal_connect( G_OBJECT(playman_win) , "destroy" ,
                      G_CALLBACK(gtk_widget_destroyed) , &playman_win );
    g_signal_connect( G_OBJECT(playman_win) , "key-press-event" ,
                      G_CALLBACK(playlist_manager_cb_keypress) , NULL );
    playman_win_hints.min_width = 400;
    playman_win_hints.min_height = 250;
    gtk_window_set_geometry_hints( GTK_WINDOW(playman_win) , GTK_WIDGET(playman_win) ,
                                   &playman_win_hints , GDK_HINT_MIN_SIZE );

    playman_vbox = gtk_vbox_new( FALSE , 10 );
    gtk_container_add( GTK_CONTAINER(playman_win) , playman_vbox );

    /* current liststore model
       ----------------------------------------------
       G_TYPE_STRING -> playlist name
       G_TYPE_UINT -> number of entries in playlist
       G_TYPE_POINTER -> playlist pointer (Playlist*)
       ----------------------------------------------
       */
    pl_store = gtk_list_store_new( PLLIST_NUMCOLS , G_TYPE_STRING , G_TYPE_UINT , G_TYPE_POINTER );
    playlist_manager_populate( pl_store );

    playman_pl_lv_frame = gtk_frame_new( NULL );
    playman_pl_lv = gtk_tree_view_new_with_model( GTK_TREE_MODEL(pl_store) );
    g_object_unref( pl_store );
    g_object_set_data( G_OBJECT(playman_win) , "lv" , playman_pl_lv );
    g_object_set_data( G_OBJECT(playman_pl_lv) , "opt1" , GINT_TO_POINTER(0) );
    playman_pl_lv_textrndr_entriesnum = gtk_cell_renderer_text_new(); /* uneditable */
    playman_pl_lv_textrndr_name = gtk_cell_renderer_text_new(); /* can become editable */
    g_signal_connect( G_OBJECT(playman_pl_lv_textrndr_name) , "edited" ,
                      G_CALLBACK(playlist_manager_cb_lv_name_edited) , playman_pl_lv );
    g_object_set_data( G_OBJECT(playman_pl_lv) , "rn" , playman_pl_lv_textrndr_name );
    playman_pl_lv_col_name = gtk_tree_view_column_new_with_attributes(
                                                                      _("Playlist") , playman_pl_lv_textrndr_name , "text" , PLLIST_COL_NAME , NULL );
    gtk_tree_view_column_set_expand( GTK_TREE_VIEW_COLUMN(playman_pl_lv_col_name) , TRUE );
    gtk_tree_view_append_column( GTK_TREE_VIEW(playman_pl_lv), playman_pl_lv_col_name );
    playman_pl_lv_col_entriesnum = gtk_tree_view_column_new_with_attributes(
                                                                            _("Entries") , playman_pl_lv_textrndr_entriesnum , "text" , PLLIST_COL_ENTRIESNUM , NULL );
    gtk_tree_view_column_set_expand( GTK_TREE_VIEW_COLUMN(playman_pl_lv_col_entriesnum) , FALSE );
    gtk_tree_view_append_column( GTK_TREE_VIEW(playman_pl_lv), playman_pl_lv_col_entriesnum );
    playman_pl_lv_sw = gtk_scrolled_window_new( NULL , NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(playman_pl_lv_sw) ,
                                    GTK_POLICY_NEVER , GTK_POLICY_ALWAYS );
    gtk_container_add( GTK_CONTAINER(playman_pl_lv_sw) , playman_pl_lv );
    gtk_container_add( GTK_CONTAINER(playman_pl_lv_frame) , playman_pl_lv_sw );
    gtk_box_pack_start( GTK_BOX(playman_vbox) , playman_pl_lv_frame , TRUE , TRUE , 0 );

    /* listview popup menu */
    playman_pl_lv_pmenu = gtk_menu_new();
    playman_pl_lv_pmenu_rename = gtk_menu_item_new_with_mnemonic( _( "_Rename" ) );
    g_signal_connect( G_OBJECT(playman_pl_lv_pmenu_rename) , "activate" ,
                      G_CALLBACK(playlist_manager_cb_lv_pmenu_rename) , playman_pl_lv );
    gtk_menu_shell_append( GTK_MENU_SHELL(playman_pl_lv_pmenu) , playman_pl_lv_pmenu_rename );
    gtk_widget_show_all( playman_pl_lv_pmenu );
    g_object_set_data( G_OBJECT(playman_pl_lv) , "menu" , playman_pl_lv_pmenu );
    g_signal_connect_swapped( G_OBJECT(playman_win) , "destroy" ,
                              G_CALLBACK(gtk_widget_destroy) , playman_pl_lv_pmenu );

    /* button bar */
    playman_bbar_hbbox = gtk_hbutton_box_new();
    gtk_button_box_set_layout( GTK_BUTTON_BOX(playman_bbar_hbbox) , GTK_BUTTONBOX_END );
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(playman_bbar_hbbox), 5);
    playman_bbar_bt_close = gtk_button_new_from_stock( GTK_STOCK_CLOSE );
    playman_bbar_bt_del = gtk_button_new_from_stock( GTK_STOCK_DELETE );
    playman_bbar_bt_new = gtk_button_new_from_stock( GTK_STOCK_NEW );
    gtk_container_add( GTK_CONTAINER(playman_bbar_hbbox) , playman_bbar_bt_close );
    gtk_container_add( GTK_CONTAINER(playman_bbar_hbbox) , playman_bbar_bt_del );
    gtk_container_add( GTK_CONTAINER(playman_bbar_hbbox) , playman_bbar_bt_new );
    gtk_button_box_set_child_secondary( GTK_BUTTON_BOX(playman_bbar_hbbox) ,
                                        playman_bbar_bt_close , TRUE );
    gtk_box_pack_start( GTK_BOX(playman_vbox) , playman_bbar_hbbox , FALSE , FALSE , 0 );

    g_signal_connect( G_OBJECT(playman_pl_lv) , "button-press-event" ,
                      G_CALLBACK(playlist_manager_cb_lv_btpress) , NULL );
    g_signal_connect( G_OBJECT(playman_pl_lv) , "row-activated" ,
                      G_CALLBACK(playlist_manager_cb_lv_dclick) , NULL );
    g_signal_connect_swapped( G_OBJECT(playman_bbar_bt_new) , "clicked" ,
                              G_CALLBACK(playlist_manager_cb_new) , playman_pl_lv );
    g_signal_connect_swapped( G_OBJECT(playman_bbar_bt_del) , "clicked" ,
                              G_CALLBACK(playlist_manager_cb_del) , playman_pl_lv );
    g_signal_connect_swapped( G_OBJECT(playman_bbar_bt_close) , "clicked" ,
                              G_CALLBACK(gtk_widget_destroy) , playman_win );

    gtk_widget_show_all( playman_win );
}


void
playlist_manager_update ( void )
{
    /* this function is called whenever there is a change in playlist, such as
       playlist created/deleted or entry added/deleted in a playlist; if the playlist
       manager is active, it should be updated to keep consistency of information */

    /* CAREFUL! this currently locks/unlocks all the playlists */

    if ( playman_win != NULL )
    {
        GtkWidget *lv = (GtkWidget*)g_object_get_data( G_OBJECT(playman_win) , "lv" );
        if ( GPOINTER_TO_INT(g_object_get_data(G_OBJECT(lv),"opt1")) == 0 )
        {
            GtkListStore *store = (GtkListStore*)gtk_tree_view_get_model( GTK_TREE_VIEW(lv) );
            /* TODO: this re-populates everything... there's definitely room for optimization */
            gtk_list_store_clear( store );
            playlist_manager_populate( store );
        }
        return;
    }
    else
        return; /* if the playlist manager is not active, simply return */
}
