#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *address;
    GtkWidget *progress;
    WebKitWebContext *context;
    gboolean load_images;
} Browser;

typedef struct {
    Browser *browser;
    WebKitWebView *view;
    GtkWidget *tab_label;
    gboolean loading;
} BrowserTab;

static gchar *
normalize_address(const gchar *text)
{
    gchar *trimmed = g_strdup(text);
    g_strstrip(trimmed);
    if (!*trimmed) {
        g_free(trimmed);
        return g_strdup("about:blank");
    }

    if (strchr(trimmed, ' ') ||
        (!strstr(trimmed, "://") && !strchr(trimmed, '.') &&
         !g_str_has_prefix(trimmed, "localhost"))) {
        gchar *escaped = g_uri_escape_string(trimmed, NULL, FALSE);
        gchar *query = g_strdup_printf("https://www.google.com/search?q=%s", escaped);
        g_free(escaped);
        g_free(trimmed);
        return query;
    }

    gchar *uri = g_uri_parse_scheme(trimmed) ? g_strdup(trimmed)
                                              : g_strdup_printf("https://%s", trimmed);
    g_free(trimmed);
    return uri;
}

static BrowserTab *
current_tab(Browser *browser)
{
    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(browser->notebook),
                                                gtk_notebook_get_current_page(GTK_NOTEBOOK(browser->notebook)));
    if (!page) return NULL;
    return g_object_get_data(G_OBJECT(page), "browser-tab");
}

static void
sync_address(Browser *browser)
{
    BrowserTab *tab = current_tab(browser);
    if (!tab) return;
    const gchar *uri = webkit_web_view_get_uri(tab->view);
    gtk_entry_set_text(GTK_ENTRY(browser->address), uri ? uri : "");
}

static void
set_title(BrowserTab *tab, const gchar *title)
{
    gchar *short_title = g_strdup(title && *title ? title : "Nova guia");
    if (g_utf8_strlen(short_title, -1) > 28) {
        gchar *cut = g_utf8_offset_to_pointer(short_title, 27);
        *cut = '\0';
        gchar *with_ellipsis = g_strconcat(short_title, "…", NULL);
        g_free(short_title);
        short_title = with_ellipsis;
    }
    gtk_label_set_text(GTK_LABEL(tab->tab_label), short_title);
    g_free(short_title);
}

static void
update_window_title(BrowserTab *tab)
{
    if (current_tab(tab->browser) != tab) return;
    const gchar *uri = webkit_web_view_get_uri(tab->view);
    GError *error = NULL;
    GUri *parsed = uri ? g_uri_parse(uri, G_URI_FLAGS_NONE, &error) : NULL;
    const gchar *host = parsed ? g_uri_get_host(parsed) : NULL;
    const gchar *page_title = webkit_web_view_get_title(tab->view);
    const gchar *display = tab->loading ? "Carregando" :
                           (page_title && *page_title ? page_title : "Nova guia");
    gchar *window_title = host && *host
        ? g_strdup_printf("Hue Browser — %s — %s", host, display)
        : g_strdup_printf("Hue Browser — %s", display);
    gtk_window_set_title(GTK_WINDOW(tab->browser->window), window_title);
    g_free(window_title);
    if (parsed) g_uri_unref(parsed);
    g_clear_error(&error);
}

static void
on_title_changed(WebKitWebView *view, GParamSpec *spec, BrowserTab *tab)
{
    (void)spec;
    if (view == tab->view) {
        set_title(tab, webkit_web_view_get_title(view));
        update_window_title(tab);
    }
}

static void
on_load_changed(WebKitWebView *view, WebKitLoadEvent event, BrowserTab *tab)
{
    Browser *browser = tab->browser;
    if (view != tab->view) return;
    if (event == WEBKIT_LOAD_STARTED) {
        tab->loading = TRUE;
        update_window_title(tab);
    }
    if (event == WEBKIT_LOAD_COMMITTED && current_tab(browser) == tab)
        sync_address(browser);
    if (event == WEBKIT_LOAD_FINISHED) {
        tab->loading = FALSE;
        update_window_title(tab);
        if (current_tab(browser) == tab) gtk_widget_hide(browser->progress);
    }
}

static void
on_load_progress(GObject *object, GParamSpec *spec, BrowserTab *tab)
{
    (void)spec;
    Browser *browser = tab->browser;
    if (current_tab(browser) != tab) return;
    gdouble fraction = webkit_web_view_get_estimated_load_progress(WEBKIT_WEB_VIEW(object));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(browser->progress), fraction);
    if (fraction >= 1.0) gtk_widget_hide(browser->progress);
    else gtk_widget_show(browser->progress);
}

static void
on_uri_changed(WebKitWebView *view, GParamSpec *spec, BrowserTab *tab)
{
    (void)spec;
    if (current_tab(tab->browser) == tab) sync_address(tab->browser);
    update_window_title(tab);
    const gchar *uri = webkit_web_view_get_uri(view);
    if (uri && g_str_has_prefix(uri, "https://"))
        gtk_widget_set_tooltip_text(GTK_WIDGET(tab->tab_label), uri);
}

static void
on_address_activate(GtkEntry *entry, Browser *browser)
{
    BrowserTab *tab = current_tab(browser);
    if (!tab) return;
    gchar *uri = normalize_address(gtk_entry_get_text(entry));
    webkit_web_view_load_uri(tab->view, uri);
    g_free(uri);
}

static void
navigate_current(Browser *browser, int action)
{
    BrowserTab *tab = current_tab(browser);
    if (!tab) return;
    switch (action) {
    case 0: webkit_web_view_go_back(tab->view); break;
    case 1: webkit_web_view_go_forward(tab->view); break;
    case 2: webkit_web_view_reload(tab->view); break;
    case 3: webkit_web_view_load_uri(tab->view, "https://www.google.com"); break;
    }
}

static void
on_back(GtkButton *button, Browser *browser) { (void)button; navigate_current(browser, 0); }
static void
on_forward(GtkButton *button, Browser *browser) { (void)button; navigate_current(browser, 1); }
static void
on_reload(GtkButton *button, Browser *browser) { (void)button; navigate_current(browser, 2); }
static void
on_home(GtkButton *button, Browser *browser) { (void)button; navigate_current(browser, 3); }

static void
new_tab(Browser *browser, const gchar *uri)
{
    WebKitWebView *view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(browser->context));
    WebKitSettings *settings = webkit_web_view_get_settings(view);
    webkit_settings_set_auto_load_images(settings, browser->load_images);
    webkit_settings_set_enable_page_cache(settings, FALSE);

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(page), GTK_WIDGET(view), TRUE, TRUE, 0);
    GtkWidget *tab_label = gtk_label_new("Nova guia");
    gtk_label_set_ellipsize(GTK_LABEL(tab_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(tab_label, 100, -1);

    BrowserTab *tab = g_new0(BrowserTab, 1);
    tab->browser = browser;
    tab->view = view;
    tab->tab_label = tab_label;
    g_object_set_data_full(G_OBJECT(page), "browser-tab", tab, g_free);

    g_signal_connect(view, "notify::title", G_CALLBACK(on_title_changed), tab);
    g_signal_connect(view, "notify::uri", G_CALLBACK(on_uri_changed), tab);
    g_signal_connect(view, "notify::estimated-load-progress", G_CALLBACK(on_load_progress), tab);
    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), tab);

    gint index = gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook), page, tab_label);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(browser->notebook), page, TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), index);
    gtk_widget_show_all(page);
    webkit_web_view_load_uri(view, uri ? uri : "https://www.google.com");
    gtk_widget_grab_focus(browser->address);
    gtk_editable_select_region(GTK_EDITABLE(browser->address), 0, -1);
}

static void
on_new_tab(GtkButton *button, Browser *browser) { (void)button; new_tab(browser, "https://www.google.com"); }

static void
on_tab_close(GtkButton *button, Browser *browser)
{
    (void)button;
    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(browser->notebook),
                                                gtk_notebook_get_current_page(GTK_NOTEBOOK(browser->notebook)));
    if (!page) return;
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook)) == 1)
        new_tab(browser, "https://www.google.com");
    gtk_notebook_remove_page(GTK_NOTEBOOK(browser->notebook),
                             gtk_notebook_page_num(GTK_NOTEBOOK(browser->notebook), page));
}

static void
on_switch_page(GtkNotebook *notebook, GtkWidget *page, guint number, Browser *browser)
{
    (void)notebook; (void)page; (void)number;
    sync_address(browser);
    BrowserTab *tab = current_tab(browser);
    if (tab) update_window_title(tab);
    if (tab && webkit_web_view_is_loading(tab->view)) gtk_widget_show(browser->progress);
    else gtk_widget_hide(browser->progress);
}

static void
on_images_toggled(GtkCheckMenuItem *item, Browser *browser)
{
    browser->load_images = gtk_check_menu_item_get_active(item);
    gint count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook));
    for (gint i = 0; i < count; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(browser->notebook), i);
        BrowserTab *tab = g_object_get_data(G_OBJECT(page), "browser-tab");
        webkit_settings_set_auto_load_images(webkit_web_view_get_settings(tab->view), browser->load_images);
    }
}

static gboolean
on_key_press(GtkWidget *widget, GdkEventKey *event, Browser *browser)
{
    (void)widget;
    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_t) {
        new_tab(browser, "https://www.google.com");
        return TRUE;
    }
    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_w) {
        on_tab_close(NULL, browser);
        return TRUE;
    }
    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_l) {
        gtk_widget_grab_focus(browser->address);
        gtk_editable_select_region(GTK_EDITABLE(browser->address), 0, -1);
        return TRUE;
    }
    return FALSE;
}

static GtkWidget *
make_button(GtkWidget *toolbar, const gchar *text, const gchar *tooltip,
            GCallback callback, Browser *browser)
{
    GtkWidget *button = gtk_button_new_with_label(text);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", callback, browser);
    return button;
}

static void
activate(GtkApplication *app, gpointer data)
{
    (void)data;
    Browser *browser = g_new0(Browser, 1);
    browser->load_images = TRUE;

    gchar *data_dir = g_build_filename(g_get_user_data_dir(), "hue-browser", NULL);
    gchar *cache_dir = g_build_filename(g_get_user_cache_dir(), "hue-browser", NULL);
    g_mkdir_with_parents(data_dir, 0700);
    g_mkdir_with_parents(cache_dir, 0700);
    WebKitWebsiteDataManager *manager = webkit_website_data_manager_new(
        "base-data-directory", data_dir, "base-cache-directory", cache_dir, NULL);
    browser->context = webkit_web_context_new_with_website_data_manager(manager);
    g_object_unref(manager);
    g_free(data_dir);
    g_free(cache_dir);

    browser->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(browser->window), "Hue Browser");
    gtk_window_set_default_size(GTK_WINDOW(browser->window), 1180, 760);
    gtk_window_set_resizable(GTK_WINDOW(browser->window), TRUE);
    g_signal_connect(browser->window, "key-press-event", G_CALLBACK(on_key_press), browser);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(browser->window), root);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start(toolbar, 6);
    gtk_widget_set_margin_end(toolbar, 6);
    gtk_widget_set_margin_top(toolbar, 5);
    gtk_widget_set_margin_bottom(toolbar, 5);
    gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);
    make_button(toolbar, "‹", "Voltar", G_CALLBACK(on_back), browser);
    make_button(toolbar, "›", "Avançar", G_CALLBACK(on_forward), browser);
    make_button(toolbar, "↻", "Recarregar", G_CALLBACK(on_reload), browser);
    make_button(toolbar, "⌂", "Página inicial", G_CALLBACK(on_home), browser);
    browser->address = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(browser->address), "Pesquisar ou inserir endereço");
    gtk_widget_set_hexpand(browser->address, TRUE);
    gtk_box_pack_start(GTK_BOX(toolbar), browser->address, TRUE, TRUE, 4);
    g_signal_connect(browser->address, "activate", G_CALLBACK(on_address_activate), browser);
    make_button(toolbar, "+", "Nova aba (Ctrl+T)", G_CALLBACK(on_new_tab), browser);
    make_button(toolbar, "×", "Fechar aba (Ctrl+W)", G_CALLBACK(on_tab_close), browser);

    GtkWidget *menu_button = gtk_menu_button_new();
    gtk_button_set_relief(GTK_BUTTON(menu_button), GTK_RELIEF_NONE);
    gtk_button_set_label(GTK_BUTTON(menu_button), "⋮");
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *images = gtk_check_menu_item_new_with_label("Carregar imagens");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(images), TRUE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), images);
    g_signal_connect(images, "toggled", G_CALLBACK(on_images_toggled), browser);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(menu_button), menu);
    gtk_widget_show_all(menu);
    gtk_box_pack_start(GTK_BOX(toolbar), menu_button, FALSE, FALSE, 0);

    browser->progress = gtk_progress_bar_new();
    gtk_widget_set_size_request(browser->progress, -1, 2);
    gtk_box_pack_start(GTK_BOX(root), browser->progress, FALSE, FALSE, 0);

    browser->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(browser->notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(browser->notebook), FALSE);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(browser->notebook), TRUE);
    gtk_box_pack_start(GTK_BOX(root), browser->notebook, TRUE, TRUE, 0);
    g_signal_connect(browser->notebook, "switch-page", G_CALLBACK(on_switch_page), browser);
    new_tab(browser, "https://www.google.com");
    gtk_widget_show_all(browser->window);
    gtk_widget_hide(browser->progress);
}

int
main(int argc, char **argv)
{
    GtkApplication *app = gtk_application_new("org.hue.huebrowser", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
