#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <pcre.h>

int items_found = 0;

GtkWidget *window;
GtkWidget *table;

GtkWidget *labelSRC,*labelFIND,*labelFILTER,*labelSTATUS,*labelREALSTATUS;
GtkWidget *entrySRC,*entryREGEX,*entryREGEX2;
GtkWidget *buttonBROWSE,*buttonDL,*buttonFIND,*buttonBATCH;
GtkWidget *checkREGEX2_ENABLED, *checkREGEX2_REVERSE;

GtkWidget *scrollLIST,*listFILES;

enum
{
	LIST_ITEM = 0,
	N_COLUMNS
};

static void
init_list(GtkWidget *list)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkListStore *store;

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("List Items", renderer, "text", LIST_ITEM, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

	store = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING);

	gtk_tree_view_set_model(GTK_TREE_VIEW(list), GTK_TREE_MODEL(store));

	g_object_unref(store);
}

static void
add_to_list(GtkWidget *list, const gchar *str)
{
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(list)));

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, LIST_ITEM, str, -1);
}

void find (GtkWidget *widget, gpointer window)
{
	FILE *pFILE;
	
	pFILE=fopen(gtk_entry_get_text(GTK_ENTRY(entrySRC)),"r");
	if (pFILE!=NULL) {
		fseek(pFILE, 0L, SEEK_END);
		int sz = ftell(pFILE);
		char str[sz];
		fseek(pFILE, 0L, SEEK_SET);
		items_found=0;
		
		gtk_list_store_clear(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW (listFILES))));
		
		while (fgets(str, sz, pFILE) != NULL) {
			const char *error;
			int erroffset, rc, rc2, i, ovector[100], ovector2[100];
			pcre *re, *re2;
			
			const char *regex = gtk_entry_get_text(GTK_ENTRY(entryREGEX));
			const char *regex2 = gtk_entry_get_text(GTK_ENTRY(entryREGEX2));
			int filter_enabled=0;
			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkREGEX2_ENABLED))) {
				if ((re2 = pcre_compile (regex2, 0, &error, &erroffset, 0))==NULL) {
					GtkWidget *dialog;
					dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "筛选：错误的正则表达式。");
					gtk_window_set_title(GTK_WINDOW(dialog), "错误");
					gtk_dialog_run(GTK_DIALOG(dialog));
					gtk_widget_destroy(dialog);
					break;
				}else{
					filter_enabled=1;
					if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkREGEX2_REVERSE)))filter_enabled=2;
				}
			}
			
			if ((re = pcre_compile (regex, 0, &error, &erroffset, 0))==NULL) {
				GtkWidget *dialog;
				dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "查找：错误的正则表达式。");
				gtk_window_set_title(GTK_WINDOW(dialog), "错误");
				gtk_dialog_run(GTK_DIALOG(dialog));
				gtk_widget_destroy(dialog);
				break;
			} else {
				unsigned int offset = 0, len = strlen(str);
				while (offset < len && (rc = pcre_exec(re, 0, str, len, offset, 0, ovector, sizeof(ovector))) >= 0) {
					for(i = 0; i < rc; ++i) {
						int length = ovector[2*i+1]-ovector[2*i];
						char buffer[length];
						sprintf(buffer, "%.*s", length, str + ovector[2*i]);
						
						if (filter_enabled>0) {
							rc2 = pcre_exec(re2, 0, buffer, strlen(buffer), 0, 0, ovector2, sizeof(ovector2));
							if (filter_enabled==1&&rc2<0) break;
							if (filter_enabled==2&&rc2>=0) break;
						}
						add_to_list(listFILES, buffer);
						items_found+=1;
						
					}
					offset = ovector[1];
				}
			}
	   	}
		fclose(pFILE);
		char status[50];
		sprintf(status, "找到%d个项目。", items_found);
		gtk_label_set_text(GTK_LABEL(labelREALSTATUS), status);
	} else {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "找不到输入文件。");
		gtk_window_set_title(GTK_WINDOW(dialog), "错误");
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
}

void chkREGEX2(){
	gboolean chked=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkREGEX2_ENABLED));
	gtk_widget_set_sensitive(checkREGEX2_REVERSE, chked);
	gtk_widget_set_sensitive(entryREGEX2, chked);
}

int main (int argc, char *argv[])
{
	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	
	table = gtk_table_new(5, 4, FALSE);
	gtk_container_add(GTK_CONTAINER(window), table);

	gtk_window_set_title(GTK_WINDOW(window), "cBatchDownload");
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);
	GError *err = NULL;
	gtk_window_set_icon_from_file(GTK_WINDOW(window), "download.png", &err);

	labelSRC = gtk_label_new("来源:");
	labelFIND = gtk_label_new("查找:");
	labelFILTER = gtk_label_new("筛选:");
	labelSTATUS = gtk_label_new("状态:");
	labelREALSTATUS = gtk_label_new("找到0个项目。");
	gtk_misc_set_alignment(GTK_MISC(labelREALSTATUS),0.0,0.5);
	
	entrySRC = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entrySRC), "starred-items-jas.json");
	gtk_widget_set_size_request(entrySRC, 300, 30);
	
	buttonBROWSE = gtk_button_new_with_label("浏览...");
	gtk_widget_set_size_request(buttonBROWSE, 80, 30);
	
	buttonDL = gtk_button_new_with_label("下载自...");
	gtk_widget_set_size_request(buttonDL, 80, 30);
	
	entryREGEX = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entryREGEX), "(http\\:\\/\\/[a-zA-Z0-9\\-\\.]+\\.[a-zA-Z]{2,3}(?:\\/\\S*)?(?:[a-zA-Z0-9_])+\\.(?:jpg|jpeg|gif|png))");
	
	checkREGEX2_ENABLED = gtk_check_button_new_with_label("启用筛选");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkREGEX2_ENABLED), TRUE);
	checkREGEX2_REVERSE = gtk_check_button_new_with_label("反筛选");
	entryREGEX2 = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entryREGEX2), "tumblr");
	gtk_widget_set_size_request(entryREGEX2, 100, 30);
	
	buttonFIND = gtk_button_new_with_label("查找");
	gtk_widget_set_size_request(buttonFIND, 80, 30);
	
	buttonBATCH = gtk_button_new_with_label("批量下载");
	gtk_widget_set_size_request(buttonBATCH, 80, 30);
	gtk_widget_set_sensitive(buttonBATCH, FALSE);
	
	listFILES = gtk_tree_view_new();
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(listFILES), FALSE);
	init_list(listFILES);
	
	scrollLIST = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(scrollLIST, -1, 200);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrollLIST), listFILES);
	
	gtk_table_attach(GTK_TABLE(table), labelSRC, 0, 1, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), entrySRC, 1, 2, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), buttonBROWSE, 2, 3, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), buttonDL, 3, 4, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	
	gtk_table_attach(GTK_TABLE(table), labelFIND, 0, 1, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), entryREGEX, 1, 4, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	
	gtk_table_attach(GTK_TABLE(table), labelFILTER, 0, 1, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), entryREGEX2, 1, 2, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), checkREGEX2_ENABLED, 2, 3, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), checkREGEX2_REVERSE, 3, 4, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	
	gtk_table_attach(GTK_TABLE(table), labelSTATUS, 0, 1, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), labelREALSTATUS, 1, 2, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), buttonFIND, 2, 3, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table), buttonBATCH, 3, 4, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	
	gtk_table_attach(GTK_TABLE(table), scrollLIST, 0, 4, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 5);

	g_signal_connect(G_OBJECT(buttonFIND), "clicked", G_CALLBACK(find), (gpointer) window);
	g_signal_connect(G_OBJECT(checkREGEX2_ENABLED), "clicked", G_CALLBACK(chkREGEX2), NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	gtk_widget_show_all(window);

	gtk_main();

	return 0;
}
