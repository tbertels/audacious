/*
 * Audacious - a cross-platform multimedia player
 * Copyright (c) 2007  Audacious development team.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "widgets/widgetcore.h"
#include "ui_skinned_button.h"
#include "util.h"

#include <gtk/gtkmain.h>
#include <gtk/gtkmarshal.h>
#include <gtk/gtkimage.h>

#define UI_SKINNED_BUTTON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), UI_TYPE_SKINNED_BUTTON, UiSkinnedButtonPrivate))
typedef struct _UiSkinnedButtonPrivate UiSkinnedButtonPrivate;

static GMutex *mutex = NULL;

enum {
	PRESSED,
	RELEASED,
	CLICKED,
	RIGHT_CLICKED,
	DOUBLED,
	REDRAW,
	LAST_SIGNAL
};

struct _UiSkinnedButtonPrivate {
        //Skinned part
        GdkPixmap        *img;
        GdkGC            *gc;
        gint             w;
        gint             h;
        SkinPixmapId     skin_index1;
        SkinPixmapId     skin_index2;
        GtkWidget        *fixed;
        gboolean         double_size;
        gint             move_x, move_y;
};


static GtkWidgetClass *parent_class = NULL;
static void ui_skinned_button_class_init(UiSkinnedButtonClass *klass);
static void ui_skinned_button_init(UiSkinnedButton *button);
static void ui_skinned_button_destroy(GtkObject *object);
static void ui_skinned_button_realize(GtkWidget *widget);
static void ui_skinned_button_size_request(GtkWidget *widget, GtkRequisition *requisition);
static gint ui_skinned_button_expose(GtkWidget *widget,GdkEventExpose *event);

static void ui_skinned_button_size_allocate(GtkWidget *widget, GtkAllocation *allocation);
static void ui_skinned_button_update_state(UiSkinnedButton *button);

static guint button_signals[LAST_SIGNAL] = { 0 };
static gint ui_skinned_button_button_press(GtkWidget *widget, GdkEventButton *event);
static gint ui_skinned_button_button_release(GtkWidget *widget, GdkEventButton *event);
static void button_pressed(UiSkinnedButton *button);
static void button_released(UiSkinnedButton *button);
static void ui_skinned_button_pressed(UiSkinnedButton *button);
static void ui_skinned_button_released(UiSkinnedButton *button);
static void ui_skinned_button_clicked(UiSkinnedButton *button);
static void ui_skinned_button_set_pressed (UiSkinnedButton *button, gboolean pressed);

static void ui_skinned_button_toggle_doublesize(UiSkinnedButton *button);

static gint ui_skinned_button_enter_notify(GtkWidget *widget, GdkEventCrossing *event);
static gint ui_skinned_button_leave_notify(GtkWidget *widget, GdkEventCrossing *event);
static void ui_skinned_button_redraw(UiSkinnedButton *button);

GType ui_skinned_button_get_type() {
    static GType button_type = 0;
    if (!button_type) {
        static const GTypeInfo button_info = {
            sizeof (UiSkinnedButtonClass),
            NULL,
            NULL,
            (GClassInitFunc) ui_skinned_button_class_init,
            NULL,
            NULL,
            sizeof (UiSkinnedButton),
            0,
            (GInstanceInitFunc) ui_skinned_button_init,
        };
        button_type = g_type_register_static (GTK_TYPE_WIDGET, "UiSkinnedButton", &button_info, 0);
    }

    return button_type;
}

static void ui_skinned_button_class_init (UiSkinnedButtonClass *klass) {
    GObjectClass *gobject_class;
    GtkObjectClass *object_class;
    GtkWidgetClass *widget_class;

    gobject_class = G_OBJECT_CLASS(klass);
    object_class = (GtkObjectClass*) klass;
    widget_class = (GtkWidgetClass*) klass;
    parent_class = gtk_type_class (gtk_widget_get_type ());

    object_class->destroy = ui_skinned_button_destroy;

    widget_class->realize = ui_skinned_button_realize;
    widget_class->expose_event = ui_skinned_button_expose;
    widget_class->size_request = ui_skinned_button_size_request;
    widget_class->size_allocate = ui_skinned_button_size_allocate;
    widget_class->button_press_event = ui_skinned_button_button_press;
    widget_class->button_release_event = ui_skinned_button_button_release;
    widget_class->enter_notify_event = ui_skinned_button_enter_notify;
    widget_class->leave_notify_event = ui_skinned_button_leave_notify;

    klass->pressed = button_pressed;
    klass->released = button_released;
    klass->clicked = NULL;
    klass->right_clicked = NULL;
    klass->doubled = ui_skinned_button_toggle_doublesize;
    klass->redraw = ui_skinned_button_redraw;

    button_signals[PRESSED] = 
        g_signal_new ("pressed", G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (UiSkinnedButtonClass, pressed), NULL, NULL,
                      gtk_marshal_VOID__VOID, G_TYPE_NONE, 0);

    button_signals[RELEASED] = 
        g_signal_new ("released", G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (UiSkinnedButtonClass, released), NULL, NULL,
                      gtk_marshal_VOID__VOID, G_TYPE_NONE, 0);

    button_signals[CLICKED] = 
        g_signal_new ("clicked", G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (UiSkinnedButtonClass, clicked), NULL, NULL,
                      gtk_marshal_VOID__VOID, G_TYPE_NONE, 0);

    button_signals[RIGHT_CLICKED] = 
        g_signal_new ("right-clicked", G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (UiSkinnedButtonClass, right_clicked), NULL, NULL,
                      gtk_marshal_VOID__VOID, G_TYPE_NONE, 0);

    button_signals[DOUBLED] = 
        g_signal_new ("toggle-double-size", G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (UiSkinnedButtonClass, doubled), NULL, NULL,
                      gtk_marshal_VOID__VOID, G_TYPE_NONE, 0);

    button_signals[REDRAW] = 
        g_signal_new ("redraw", G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (UiSkinnedButtonClass, redraw), NULL, NULL,
                      gtk_marshal_VOID__VOID, G_TYPE_NONE, 0);

    g_type_class_add_private (gobject_class, sizeof (UiSkinnedButtonPrivate));
}

static void ui_skinned_button_init (UiSkinnedButton *button) {
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE (button);
    mutex = g_mutex_new();
    button->inside = FALSE;
    button->type = TYPE_NOT_SET;
    priv->move_x = 0;
    priv->move_y = 0;
    priv->img = NULL;
}

static void ui_skinned_button_destroy (GtkObject *object) {
    UiSkinnedButton *button;

    g_return_if_fail (object != NULL);
    g_return_if_fail (UI_SKINNED_IS_BUTTON (object));

    button = UI_SKINNED_BUTTON(object);

    if (GTK_OBJECT_CLASS (parent_class)->destroy)
        (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static void ui_skinned_button_realize (GtkWidget *widget) {
    g_return_if_fail (widget != NULL);
    g_return_if_fail (UI_SKINNED_IS_BUTTON(widget));
    UiSkinnedButton *button = UI_SKINNED_BUTTON (widget);
    GdkWindowAttr attributes;
    gint attributes_mask;

    GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

    attributes.x = widget->allocation.x;
    attributes.y = widget->allocation.y;
    attributes.width = widget->allocation.width;
    attributes.height = widget->allocation.height;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events(widget);
    attributes.visual = gtk_widget_get_visual(widget);
    attributes.colormap = gtk_widget_get_colormap(widget);
    attributes.event_mask |= GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK;

    if (button->type == TYPE_SMALL || button->type == TYPE_NOT_SET) {
        attributes.wclass = GDK_INPUT_ONLY;
        attributes_mask = GDK_WA_X | GDK_WA_Y;
    } else {
        attributes.wclass = GDK_INPUT_OUTPUT;
        attributes.event_mask |= GDK_EXPOSURE_MASK;
        attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
    }

    widget->window = gdk_window_new(widget->parent->window, &attributes, attributes_mask);

    widget->style = gtk_style_attach(widget->style, widget->window);

    gdk_window_set_user_data(widget->window, widget);
}

static void ui_skinned_button_size_request(GtkWidget *widget, GtkRequisition *requisition) {
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE(widget);
    requisition->width = priv->w*(1+priv->double_size);
    requisition->height = priv->h*(1+priv->double_size);
}

static void ui_skinned_button_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
    g_mutex_lock(mutex);
    UiSkinnedButton *button = UI_SKINNED_BUTTON (widget);
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE (button);
    widget->allocation = *allocation;
    if (GTK_WIDGET_REALIZED (widget))
        gdk_window_move_resize(widget->window, allocation->x, allocation->y, allocation->width, allocation->height);

    button->x = widget->allocation.x/(priv->double_size ? 2 : 1);
    button->y = widget->allocation.y/(priv->double_size ? 2 : 1);
    priv->move_x = 0;
    priv->move_y = 0;

    g_mutex_unlock(mutex);
}

static gboolean ui_skinned_button_expose(GtkWidget *widget, GdkEventExpose *event) {
    g_return_val_if_fail (widget != NULL, FALSE);
    g_return_val_if_fail (UI_SKINNED_IS_BUTTON (widget), FALSE);
    g_return_val_if_fail (event != NULL, FALSE);

    UiSkinnedButton *button = UI_SKINNED_BUTTON (widget);
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE (button);

    //TYPE_SMALL doesn't have its own face
    if (button->type == TYPE_SMALL || button->type == TYPE_NOT_SET)
        return FALSE;

    GdkPixmap *obj;
    obj = gdk_pixmap_new(NULL, priv->w, priv->h, gdk_rgb_get_visual()->depth);
    switch (button->type) {
        case TYPE_PUSH:
            skin_draw_pixmap(bmp_active_skin, obj, priv->gc,
                             button->pressed ? priv->skin_index2 : priv->skin_index1,
                             button->pressed ? button->px : button->nx,
                             button->pressed ? button->py : button->ny,
                             0, 0, priv->w, priv->h);
            break;
        case TYPE_TOGGLE:
            if (button->inside)
                skin_draw_pixmap(bmp_active_skin, obj, priv->gc,
                                 button->pressed ? priv->skin_index2 : priv->skin_index1,
                                 button->pressed ? button->ppx : button->pnx,
                                 button->pressed ? button->ppy : button->pny,
                                 0, 0, priv->w, priv->h);
            else
                skin_draw_pixmap(bmp_active_skin, obj, priv->gc,
                                 button->pressed ? priv->skin_index2 : priv->skin_index1,
                                 button->pressed ? button->px : button->nx,
                                 button->pressed ? button->py : button->ny,
                                 0, 0, priv->w, priv->h);
            break;
        default:
            break;
    }

    if (priv->img)
        g_object_unref(priv->img);
    priv->img = gdk_pixmap_new(NULL, priv->w*(1+priv->double_size),
                               priv->h*(1+priv->double_size),
                               gdk_rgb_get_visual()->depth);

    if (priv->double_size) {
        GdkImage *img, *img2x;
        img = gdk_drawable_get_image(obj, 0, 0, priv->w, priv->h);
        img2x = create_dblsize_image(img);
        gdk_draw_image (priv->img, priv->gc, img2x, 0, 0, 0, 0, priv->w*2, priv->h*2);
        g_object_unref(img2x);
        g_object_unref(img);
    } else
        gdk_draw_drawable (priv->img, priv->gc, obj, 0, 0, 0, 0, priv->w, priv->h);

    g_object_unref(obj);

    gdk_draw_drawable (widget->window, priv->gc, priv->img, 0, 0, 0, 0,
                       priv->w*(1+priv->double_size), priv->h*(1+priv->double_size));
    return FALSE;
}

GtkWidget* ui_skinned_button_new () {
    UiSkinnedButton *button = g_object_new (ui_skinned_button_get_type (), NULL);

    return GTK_WIDGET(button);
}

void ui_skinned_push_button_setup(GtkWidget *button, GtkWidget *fixed, GdkPixmap *parent, GdkGC *gc, gint x, gint y, gint w, gint h, gint nx, gint ny, gint px, gint py, SkinPixmapId si) {

    UiSkinnedButton *sbutton = UI_SKINNED_BUTTON(button);
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE(sbutton);
    priv->gc = gc;
    priv->w = w;
    priv->h = h;
    sbutton->x = x;
    sbutton->y = y;
    sbutton->nx = nx;
    sbutton->ny = ny;
    sbutton->px = px;
    sbutton->py = py;
    sbutton->type = TYPE_PUSH;
    priv->skin_index1 = si;
    priv->skin_index2 = si;
    priv->fixed = fixed;
    priv->double_size = FALSE;

    gtk_fixed_put(GTK_FIXED(priv->fixed),GTK_WIDGET(button), sbutton->x, sbutton->y);
}

void ui_skinned_toggle_button_setup(GtkWidget *button, GtkWidget *fixed, GdkPixmap *parent, GdkGC *gc, gint x, gint y, gint w, gint h, gint nx, gint ny, gint px, gint py, gint pnx, gint pny, gint ppx, gint ppy, SkinPixmapId si) {

    UiSkinnedButton *sbutton = UI_SKINNED_BUTTON(button);
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE(sbutton);
    priv->gc = gc;
    priv->w = w;
    priv->h = h;
    sbutton->x = x;
    sbutton->y = y;
    sbutton->nx = nx;
    sbutton->ny = ny;
    sbutton->px = px;
    sbutton->py = py;
    sbutton->pnx = pnx;
    sbutton->pny = pny;
    sbutton->ppx = ppx;
    sbutton->ppy = ppy;
    sbutton->type = TYPE_TOGGLE;
    priv->skin_index1 = si;
    priv->skin_index2 = si;
    priv->fixed = fixed;
    priv->double_size = FALSE;

    gtk_fixed_put(GTK_FIXED(priv->fixed),GTK_WIDGET(button), sbutton->x, sbutton->y);
}

void ui_skinned_small_button_setup(GtkWidget *button, GtkWidget *fixed, GdkPixmap *parent, GdkGC *gc, gint x, gint y, gint w, gint h) {

    UiSkinnedButton *sbutton = UI_SKINNED_BUTTON(button);
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE(sbutton);
    priv->gc = gc;
    priv->w = w;
    priv->h = h;
    sbutton->x = x;
    sbutton->y = y;
    sbutton->type = TYPE_SMALL;
    priv->fixed = fixed;
    priv->double_size = FALSE;

    gtk_fixed_put(GTK_FIXED(priv->fixed),GTK_WIDGET(button), sbutton->x, sbutton->y);
}

static void button_pressed(UiSkinnedButton *button) {
    button->button_down = TRUE;
    ui_skinned_button_update_state(button);
}

static void button_released(UiSkinnedButton *button) {
    button->button_down = FALSE;
    if(button->hover) ui_skinned_button_clicked(button);
    ui_skinned_button_update_state(button);
}

static void ui_skinned_button_update_state(UiSkinnedButton *button) {
    ui_skinned_button_set_pressed(button, button->button_down); 
}

static void ui_skinned_button_set_pressed (UiSkinnedButton *button, gboolean pressed) {
    if (pressed != button->pressed) {
        button->pressed = pressed;
        gtk_widget_queue_draw(GTK_WIDGET(button));
    }
}

static gboolean ui_skinned_button_button_press(GtkWidget *widget, GdkEventButton *event) {
    UiSkinnedButton *button;

    if (event->type == GDK_BUTTON_PRESS) {
        button = UI_SKINNED_BUTTON(widget);

        if (event->button == 1)
            ui_skinned_button_pressed (button);
    }

    return TRUE;
}

static gboolean ui_skinned_button_button_release(GtkWidget *widget, GdkEventButton *event) {
    UiSkinnedButton *button;
    if (event->button == 1) {
            button = UI_SKINNED_BUTTON(widget);
            ui_skinned_button_released(button);
    } else if (event->button == 3) {
            g_signal_emit(widget, button_signals[RIGHT_CLICKED], 0);
    }

    return TRUE;
}

static void ui_skinned_button_pressed(UiSkinnedButton *button) {
    g_return_if_fail(UI_SKINNED_IS_BUTTON(button));
    g_signal_emit(button, button_signals[PRESSED], 0);
}

static void ui_skinned_button_released(UiSkinnedButton *button) {
    g_return_if_fail(UI_SKINNED_IS_BUTTON(button));
    g_signal_emit(button, button_signals[RELEASED], 0);
}

static void ui_skinned_button_clicked(UiSkinnedButton *button) {
    g_return_if_fail(UI_SKINNED_IS_BUTTON(button));
    button->inside = !button->inside;
    g_signal_emit(button, button_signals[CLICKED], 0);
}

static gboolean ui_skinned_button_enter_notify(GtkWidget *widget, GdkEventCrossing *event) {
    UiSkinnedButton *button;

    button = UI_SKINNED_BUTTON(widget);
    button->hover = TRUE;
    if(button->button_down) ui_skinned_button_set_pressed(button, TRUE);

    return FALSE;
}

static gboolean ui_skinned_button_leave_notify(GtkWidget *widget, GdkEventCrossing *event) {
    UiSkinnedButton *button;

    button = UI_SKINNED_BUTTON (widget);
    button->hover = FALSE;
    if(button->button_down) ui_skinned_button_set_pressed(button, FALSE);

    return FALSE;
}

static void ui_skinned_button_toggle_doublesize(UiSkinnedButton *button) {
    GtkWidget *widget = GTK_WIDGET (button);
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE (button);
    priv->double_size = !priv->double_size;

    gtk_widget_set_size_request(widget, priv->w*(1+priv->double_size), priv->h*(1+priv->double_size));
    gtk_widget_set_uposition(widget, button->x*(1+priv->double_size), button->y*(1+priv->double_size));

    gtk_widget_queue_draw(widget);
}

static void ui_skinned_button_redraw(UiSkinnedButton *button) {
    g_mutex_lock(mutex);
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE (button);
    if (priv->move_x || priv->move_y)
        gtk_fixed_move(GTK_FIXED(priv->fixed), GTK_WIDGET(button), button->x+priv->move_x, button->y+priv->move_y);

    gtk_widget_queue_draw(GTK_WIDGET(button));
    g_mutex_unlock(mutex);
}


void ui_skinned_set_push_button_data(GtkWidget *button, gint nx, gint ny, gint px, gint py) {
    UiSkinnedButton *b = UI_SKINNED_BUTTON(button);
    if (nx > -1) b->nx = nx;
    if (ny > -1) b->ny = ny;
    if (px > -1) b->px = px;
    if (py > -1) b->py = py;
}

void ui_skinned_button_set_skin_index(GtkWidget *button, SkinPixmapId si) {
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE (button);
    priv->skin_index1 = priv->skin_index2 = si;
}

void ui_skinned_button_set_skin_index1(GtkWidget *button, SkinPixmapId si) {
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE (button);
    priv->skin_index1 = si;
}

void ui_skinned_button_set_skin_index2(GtkWidget *button, SkinPixmapId si) {
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE (button);
    priv->skin_index2 = si;
}

void ui_skinned_button_move_relative(GtkWidget *button, gint x, gint y) {
    g_mutex_lock(mutex);
    UiSkinnedButtonPrivate *priv = UI_SKINNED_BUTTON_GET_PRIVATE (button);
    priv->move_x += x;
    priv->move_y += y;
    g_mutex_unlock(mutex);
}
