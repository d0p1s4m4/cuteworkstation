#include <stdlib.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <vte/vte.h>

#if GLIB_CHECK_VERSION(2, 74, 0)
# define APP_FLAGS G_APPLICATION_DEFAULT_FLAGS
#else
# define APP_FLAGS G_APPLICATION_FLAGS_NONE
#endif

static char *registers[32] = {
  "x0",  "x1",  "x2",  "x3",  "x4",  "x5",
  "x6",  "x7",  "x8",  "x9",  "x10", "x11",
  "x12", "x13", "x14", "x15", "x16", "x17",
  "x18", "x19", "x20", "x21", "x22", "x23",
  "x24", "x25", "x26", "x27", "x28", "x29",
  "x30", "x31"
};

enum
  {
	REGVIEW_COL_NAME,
	REGVIEW_COL_VALUE,
	REGVIEW_COL_MAX
  };

static cairo_surface_t *surface = NULL;

static gboolean
setup_drawing_area_surface(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
  cairo_t *cr;

  if (surface != NULL)
	{
	  cairo_surface_destroy(surface);
	}

  surface = gdk_window_create_similar_surface(gtk_widget_get_window(widget), CAIRO_CONTENT_COLOR, gtk_widget_get_allocated_width(widget), \
      gtk_widget_get_allocated_height(widget));

  cr = cairo_create(surface);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  cairo_destroy(cr);

  return (TRUE);
}

static gboolean
drawing_area_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_paint(cr);
  
  return (FALSE);
}

static void
quit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GApplication *application = user_data;

  g_application_quit (application);
}

static void
about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GtkWidget *about_dialog;
  const gchar *authors[] = {"d0p1", NULL};

  about_dialog = gtk_about_dialog_new();
  gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(about_dialog), PACKAGE_NAME);
  gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(about_dialog), authors);
  gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(about_dialog), GTK_LICENSE_GPL_3_0);
  gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about_dialog), PACKAGE_VERSION);
  gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(about_dialog), PACKAGE_URL);

  gtk_widget_show (about_dialog);
}


static const GActionEntry actions[] = {
  {"about", about},
  {"quit", quit}
};

static void
app_startup(GtkApplication *app, gpointer data)
{
  GMenu *menu;

  g_action_map_add_action_entries(G_ACTION_MAP(app), actions, G_N_ELEMENTS(actions), app);
  menu = g_menu_new();
  g_menu_append(menu, "test", "app.test");
  g_menu_append(menu, "About", "app.about");
  g_menu_append(menu, "Quit", "app.quit");
  gtk_application_set_menubar(app, G_MENU_MODEL(menu));
  gtk_application_set_app_menu(app, G_MENU_MODEL(menu));
  g_object_unref(menu);
}

/*
 *  C = continue; SI = step in; SO = step over; A = abort
 *  +-------------------------------------------------------------------------+
 *  | C SI SO A                                                               |
 *  | +-----------+ +---------------------------------+ +-------------------+ |
 *  | | register  | |  vm framebuffer                 | | disas             | | 
 *  | | view      | |                                 | |                   | |
 *  | |           | |                                 | |                   | |
 *  | |           | |                                 | |                   | |
 *  | +-----------+ +---------------------------------+ +-------------------+ |
 *  | +---------------------------------------------------------------------+ |
 *  | | serial terminal                                                     | |
 *  | +---------------------------------------------------------------------+ |
 *  +-------------------------------------------------------------------------+
 */
static void
app_activate(GtkApplication *app, gpointer udata)
{
  GtkWidget *window;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkTreeStore *store;
  GtkWidget *tree;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkTreeIter iter;
  GtkWidget *vte_term;
  GtkWidget *drawing_area;
  GtkWidget *disas_view;
  GtkTextBuffer *buffer;
  GtkWidget *scrolled_win;
  int i;

  window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Cute Workstation");
  gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  /* upper area */
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  store = gtk_tree_store_new(REGVIEW_COL_MAX,
							 G_TYPE_STRING,
							 G_TYPE_UINT64);

  for (i = 0; i < 32; i++)
	{
	  gtk_tree_store_append(store, &iter, NULL);
	  gtk_tree_store_set(store, &iter, REGVIEW_COL_NAME, registers[i],
						 REGVIEW_COL_VALUE, (i < 30 ? i : (uint64_t)-1), -1);
	}

  tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(G_OBJECT(store));

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes("Register",
													renderer,
													"text",
													REGVIEW_COL_NAME,
													NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes("Value",
													renderer,
													"text",
													REGVIEW_COL_VALUE,
													NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  scrolled_win = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_win), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scrolled_win), tree);


  drawing_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(drawing_area, 640, 480);
  g_signal_connect(drawing_area, "configure-event", G_CALLBACK(setup_drawing_area_surface), NULL);
  g_signal_connect(drawing_area, "draw", G_CALLBACK(drawing_area_draw), NULL);

  disas_view = gtk_text_view_new();
  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(disas_view));
  gtk_text_buffer_set_text(buffer, "2000006\tj	0x200\n34011173\tcsrrw	sp,mscratch,sp\n1a010c63\tbeqz	sp,0x1c0", -1);

  gtk_box_pack_start(GTK_BOX(hbox), scrolled_win, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), drawing_area, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(hbox), disas_view, FALSE, FALSE, 0);

  /* lower area */
  vte_term = vte_terminal_new();

  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), vte_term, TRUE, TRUE, 0);

  gtk_container_add(GTK_CONTAINER(window), vbox);
  gtk_widget_show_all(window);
}

int
main(int argc, char **argv)
{
  GtkApplication *app;
  int status;

  app = gtk_application_new("engineering.cute.cuteworkstation", APP_FLAGS);

  g_signal_connect(app, "startup", G_CALLBACK(app_startup), NULL);
  g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return (status);
}
