#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <pcre.h>
#include <ctype.h>
#include <pthread.h>
#ifdef _WIN32
#include <gdk/gdkwin32.h>
#endif

int items_found = 0, items_to_dl = 0;
int dldone=-1;
int dl_simul = 5;
gboolean dling = FALSE;

GtkWidget *window;
GtkWidget *table;

GtkWidget *labelSRC,*labelFIND,*labelFILTER,*labelSTATUS,*labelREALSTATUS,*labelAUTHOR;
GtkWidget *entrySRC,*entryREGEX,*entryREGEX2;
GtkWidget *buttonBROWSE,*buttonDL,*buttonFIND,*buttonBATCH,*buttonSTOP;
GtkWidget *checkREGEX2_ENABLED, *checkREGEX2_REVERSE;

GtkWidget *scrollLIST,*listFILES;
GtkWidget *gFCD;
GtkWidget *menu, *miVIEW, *miCOPY, *miDEL, *miCLEAR;

GtkAccelGroup *accel_group;

pthread_t thread_download;

enum {
	FILENAME,
	PATH,
	PROGRESS_TEXT,
	PROGRESS,
	N_COLUMNS
};

static void init_list (GtkWidget *list) {
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkListStore *store;

	renderer = gtk_cell_renderer_progress_new();
	column = gtk_tree_view_column_new_with_attributes("Progress", renderer, "text", PROGRESS_TEXT, "value", PROGRESS, NULL);
	gtk_tree_view_column_set_expand(column, FALSE);
	gtk_tree_view_column_set_min_width(column, 60);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
	
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("File Name", renderer, "text", FILENAME, NULL);
	gtk_tree_view_column_set_expand(column, FALSE);
	gtk_tree_view_column_set_max_width(column, 300);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
	
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Path", renderer, "text", PATH, NULL);
	gtk_tree_view_column_set_expand(column, FALSE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
	
	store = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

	gtk_tree_view_set_model(GTK_TREE_VIEW(list), GTK_TREE_MODEL(store));

	g_object_unref(store);
}

static void add_to_list (GtkWidget *list, gchar *progt, int prog, gchar *path, gchar *str) {
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(list)));

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, PROGRESS_TEXT, progt, PROGRESS, prog, PATH, path, FILENAME, str, -1);
}

/* URL_DECODE START
 * From: http://www.geekhideout.com/urlcode.shtml */
char *url_encode (char *str) {
	static char hex[] = "0123456789ABCDEF";
	char *pstr = str, *buf = malloc(strlen(str) * 3 + 1), *pbuf = buf;
	while (*pstr) {
		if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') 
			*pbuf++ = *pstr;
		else if (*pstr == ' ') 
			*pbuf++ = '+';
		else 
			*pbuf++ = '%', *pbuf++ = hex[(*pstr >> 4) & 15], *pbuf++ = hex[(*pstr & 15) & 15];
		pstr++;
	}
	*pbuf = '\0';
	return buf;
}
char *url_decode (const char *str) {
	char *pstr = (char *)str, *buf = malloc(strlen(str) + 1), *pbuf = buf;
	while (*pstr) {
		if (*pstr == '%') {
			if (pstr[1] && pstr[2]) {
				*pbuf++ = (isdigit(pstr[1]) ? pstr[1] - '0' : tolower(pstr[1]) - 'a' + 10) << 4 | (isdigit(pstr[2]) ? pstr[2] - '0' : tolower(pstr[2]) - 'a' + 10);
				pstr += 2;
			}
		} else if (*pstr == '+') {
			*pbuf++ = ' ';
		} else {
			*pbuf++ = *pstr;
		}
		pstr++;
	}
	*pbuf = '\0';
	return buf;
}
/* URL_DECODE END */

int last_slash (char *location) {
	int i=0;
	char c[1];
	do {
		sprintf(c,"%.*s",1,&location[strlen(location)-i]);
		i++;
	} while(strcmp(c,"/")!=0);
	return i;
}

char *get_path (char *location) {
	char *path=malloc((strlen(location)+1)*sizeof(char));
	sprintf(path,"%.*s",strlen(location)-last_slash(location)+2,&location[0]);
	return path;
}

char *get_file_name (char *location) {
	char *filename=malloc((strlen(location)+1)*sizeof(char));
	sprintf(filename,"%s",url_decode(&location[strlen(location)-last_slash(location)+2]));
	return filename;
}

char *location_to_save (char *path) {
	char *lts=malloc((strlen(path)+1)*sizeof(char));
	sprintf(lts,"%s%s","downloads/",get_file_name(path));
	return lts;
}

void browse (GtkWidget *widget, char *dir) {
	int i=0;
	char c[1];
	do {
		sprintf(c,"%.*s",1,&dir[strlen(dir)-i]);
		i++;
	} while(strcmp(c,"/")!=0);
	sprintf(dir,"%.*s",strlen(dir)-i+2,&dir[0]);
	gFCD = gtk_file_chooser_dialog_new("选择一个文件", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	if (gtk_dialog_run(GTK_DIALOG(gFCD))==GTK_RESPONSE_ACCEPT) {
		char *filename, check[strlen(dir)];
		filename=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(gFCD));
		sprintf(check,"%.*s",strlen(dir),&filename[0]);
		if(strcmp(check,dir)==0)sprintf(filename,"%s",&filename[strlen(dir)]);
		gtk_entry_set_text(GTK_ENTRY(entrySRC), filename);
	}
	gtk_widget_destroy (gFCD);
	
}

void warn (gchar *title, gchar *content) {
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, NULL);
	gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), content);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

void update_status() {
	char status[50];
	sprintf(status, "找到%d/%d个项目可供下载。", items_to_dl, items_found);
	gtk_label_set_text(GTK_LABEL(labelREALSTATUS), status);
}

void clearlist() {
	items_found=0;
	items_to_dl=0;
	gtk_list_store_clear(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(listFILES))));
	update_status();
}

void find (GtkWidget *widget, gpointer window) {
	clearlist();

	FILE *pFILE;
	pFILE=fopen(gtk_entry_get_text(GTK_ENTRY(entrySRC)),"r");
	if (pFILE!=NULL) {
		fseek(pFILE, 0L, SEEK_END);
		int sz = ftell(pFILE);
		char str[sz];
		fseek(pFILE, 0L, SEEK_SET);
		while (fgets(str, sz, pFILE) != NULL) {
			const char *error;
			int erroffset, rc, rc2, i, ovector[100], ovector2[100];
			pcre *re, *re2;
			
			const char *regex = gtk_entry_get_text(GTK_ENTRY(entryREGEX));
			const char *regex2 = gtk_entry_get_text(GTK_ENTRY(entryREGEX2));
			int filter_enabled=0;
			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkREGEX2_ENABLED))) {
				if ((re2 = pcre_compile (regex2, 0, &error, &erroffset, 0))==NULL) {
					warn("错误","筛选：错误的正则表达式。");
					break;
				} else {
					filter_enabled=1;
					if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkREGEX2_REVERSE)))filter_enabled=2;
				}
			}
			
			if ((re = pcre_compile (regex, 0, &error, &erroffset, 0))==NULL) {
				warn("错误","查找：错误的正则表达式。");
				break;
			} else {
				unsigned int offset = 0, len = strlen(str);
				while (offset < len && (rc = pcre_exec(re, 0, str, len, offset, 0, ovector, sizeof(ovector))) >= 0) {
					for(i = 0; i < rc-1; ++i) {
						int length = ovector[2*i+1]-ovector[2*i];
						char buffer[length];
						sprintf(buffer, "%.*s", length, str + ovector[2*i]);
						
						if (filter_enabled>0) {
							rc2 = pcre_exec(re2, 0, buffer, strlen(buffer), 0, 0, ovector2, sizeof(ovector2));
							if (filter_enabled==1&&rc2<0) break;
							if (filter_enabled==2&&rc2>=0) break;
						}
						FILE *file = fopen(location_to_save(buffer), "r");
						if (file) {
							fclose(file);
							add_to_list(listFILES, "已存在", 100, get_path(buffer), get_file_name(buffer));
						} else {
							add_to_list(listFILES, "准备", 0, get_path(buffer), get_file_name(buffer));
							items_to_dl+=1;
						}
						file = NULL;
						items_found+=1;
						
					}
					offset = ovector[1];
				}
			}
	   	}
		fclose(pFILE);
	} else {
		warn("错误","无法打开输入文件。");
	}
	pFILE = NULL;
	update_status();
	gtk_widget_set_sensitive(buttonBATCH, items_to_dl);
}

size_t write_data (void *ptr, size_t size, size_t nmemb, FILE *stream) {
	return fwrite(ptr, size, nmemb, stream);
}

void chkREGEX2 () {
	gboolean chked=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkREGEX2_ENABLED));
	gtk_widget_set_sensitive(checkREGEX2_REVERSE, chked);
	gtk_widget_set_sensitive(entryREGEX2, chked);
}

void batchsensitive (gboolean sensitive) {
	gtk_widget_set_sensitive(entryREGEX, sensitive);
	gtk_widget_set_sensitive(entryREGEX2, sensitive);
	gtk_widget_set_sensitive(entrySRC, sensitive);
	gtk_widget_set_sensitive(checkREGEX2_ENABLED, sensitive);
	gtk_widget_set_sensitive(checkREGEX2_REVERSE, sensitive);
	gtk_widget_set_sensitive(buttonBROWSE, sensitive);
	gtk_widget_set_sensitive(buttonDL, sensitive);
	gtk_widget_set_sensitive(buttonFIND, sensitive);
	gtk_widget_set_visible(buttonBATCH, sensitive);
	gtk_widget_set_visible(buttonSTOP, !sensitive);
	gtk_widget_set_sensitive(miDEL, sensitive);
	gtk_widget_set_sensitive(miCLEAR, sensitive);
	if(sensitive)chkREGEX2();
}

struct pro {
	gchar *pathstring;
	int index;
};

static int progress (void *p, double dltotal, double dlnow, double ultotal, double ulnow) {
	gdk_threads_enter();
	struct pro *prog = (struct pro *)p;
	int done=0;
	if(dlnow!=0&&dltotal!=0)done=(int)dlnow/dltotal*100;
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(listFILES));
	gtk_tree_model_get_iter_from_string(model, &iter, prog->pathstring);
	char pt[10];
	sprintf(pt, "%d%%", done);
	gint *progcc;
	gtk_tree_model_get(model, &iter, PROGRESS, &progcc, -1);
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, PROGRESS_TEXT, pt, PROGRESS, done, -1);
	if ((int)progcc!=(int)100 && done==100) {
		items_to_dl-=1;
		update_status();
		dldone=prog->index;
	}
	gdk_threads_leave();
	return 0;
}

void *download (void *p) {
	
	CURLM *multi_handle;
	CURL *http_handle[dl_simul-1];
	FILE *fp[dl_simul-1];
	int still_running;
	struct pro prog[dl_simul-1];
	
	curl_global_init(CURL_GLOBAL_ALL);
	multi_handle = curl_multi_init();
	
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean valid;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(listFILES));
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter);
	
	int i=0;
	int useindex=-1;
	
	start:
	
	while (valid && (i<dl_simul || useindex>-1)) {
		gint *progcc;
		gtk_tree_model_get(model, &iter, PROGRESS, &progcc, -1);
		
		if((int)progcc!=(int)100){
			if (useindex==-1) useindex=i;
			
			gchar *location, *filename;
			gtk_tree_model_get(model, &iter, PATH, &location, -1);
			gtk_tree_model_get(model, &iter, FILENAME, &filename, -1);
			
			char *urlss=malloc(strlen(location)+strlen(filename)+1);
			strcpy(urlss,location);
			strcat(urlss,filename);
			
			prog[useindex].pathstring = gtk_tree_model_get_string_from_iter(model, &iter);
			prog[useindex].index = useindex;
			
			fp[useindex]=fopen(location_to_save(urlss),"wb");
			
			http_handle[useindex] = curl_easy_init();
			curl_easy_setopt(http_handle[useindex], CURLOPT_URL, urlss);
			curl_easy_setopt(http_handle[useindex], CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
			curl_easy_setopt(http_handle[useindex], CURLOPT_WRITEFUNCTION, write_data);
			curl_easy_setopt(http_handle[useindex], CURLOPT_WRITEDATA, fp[useindex]);
			curl_easy_setopt(http_handle[useindex], CURLOPT_NOPROGRESS, 0);
			curl_easy_setopt(http_handle[useindex], CURLOPT_PROGRESSFUNCTION, progress);
			curl_easy_setopt(http_handle[useindex], CURLOPT_PROGRESSDATA, &prog[useindex]);
			curl_multi_add_handle(multi_handle, http_handle[useindex]);
			i++;
			useindex=-1;
			
			free(urlss);
			free(location);
			free(filename);
		}
		
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter);
	}
	if (i>0) {
		dling = TRUE;
		curl_multi_perform(multi_handle, &still_running);

		while (still_running) {
			struct timeval timeout;
			int rc;

			fd_set fdread;
			fd_set fdwrite;
			fd_set fdexcep;
			int maxfd = -1;

			long curl_timeo = -1;

			FD_ZERO(&fdread);
			FD_ZERO(&fdwrite);
			FD_ZERO(&fdexcep);

			timeout.tv_sec = 1;
			timeout.tv_usec = 0;

			curl_multi_timeout(multi_handle, &curl_timeo);
			if(curl_timeo >= 0) {
				timeout.tv_sec = curl_timeo / 1000;
				if(timeout.tv_sec > 1)
					timeout.tv_sec = 1;
				else
					timeout.tv_usec = (curl_timeo % 1000) * 1000;
			}

			curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

			rc = select(maxfd+1, &fdread, &fdwrite, &fdexcep, &timeout);

			switch(rc) {
				case -1:
					break;
				case 0:
				default:
					curl_multi_perform(multi_handle, &still_running);
					break;
			}
			
			if (!dling) break;
			
			if (still_running<i && dldone>-1) {
				printf("完成第%d/%d个下载\n",dldone+1,dl_simul);
				if (fp[dldone]) {
					fclose(fp[dldone]);
				}
				fp[dldone]=NULL;
				if (http_handle[dldone]) {
					curl_multi_remove_handle(multi_handle, http_handle[dldone]);
					curl_easy_cleanup(http_handle[dldone]);
				}
				http_handle[dldone]=NULL;
				if (items_to_dl>0) {
					useindex=dldone;
					dldone=-1;
					i-=1;
					goto start;
				} else {
					dldone=-1;
				}
				
			}

		}
	}
	curl_multi_cleanup(multi_handle);
	curl_global_cleanup();
	gtk_label_set_text(GTK_LABEL(labelREALSTATUS), "下载完成。");
	batchsensitive(TRUE);
	pthread_exit(NULL);
	return NULL;
}

void download_click () {
	if (items_to_dl==0) {
		warn("错误","找不到项目可以下载。");
	} else {
		gtk_label_set_text(GTK_LABEL(labelREALSTATUS), "正在下载...");
		gtk_widget_set_sensitive(buttonBATCH, FALSE);
		gtk_widget_set_sensitive(buttonSTOP, TRUE);
		batchsensitive(FALSE);
		pthread_create(&thread_download, NULL, download, NULL);
	}
}

void stop_download () {
	gtk_widget_set_sensitive(buttonBATCH, TRUE);
	gtk_widget_set_sensitive(buttonSTOP, FALSE);
	dldone = -1;
	dling = FALSE;
}

gboolean key_press(GtkWindow *win, GdkEventKey *event, gpointer user_data) {
	if (!GTK_IS_ENTRY(gtk_window_get_focus(GTK_WINDOW(win))) && event->keyval == 0xff1b ) { //ESC
		gtk_main_quit();
	} else if (gtk_window_get_focus(GTK_WINDOW(win))) {
		if (event->keyval == 0xffff || //DELETE
		((event->state & GDK_CONTROL_MASK) && (
			event->keyval == 0x06f || //CTRL+O
			event->keyval == 0x063 || //CTRL+C
			event->keyval == 0xffff //CTRL+DELETE
			))
		) {
		return gtk_widget_event(gtk_window_get_focus(GTK_WINDOW(win)), (GdkEvent *)event);
		}
	}
	return FALSE;
}

void deleteurl(GtkWindow *win, GdkEventKey *event, gpointer user_data) {
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(listFILES));
	if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(listFILES)), &model, &iter)) {
		gint *progcc;
		gtk_tree_model_get(model, &iter, PROGRESS, &progcc, -1);
		items_found-=1;
		if((int)progcc!=(int)100){
			items_to_dl-=1;
		}
		if (items_found>0) {
			GtkTreePath *path;
			path = gtk_tree_model_get_path(model, &iter);
			if (gtk_tree_path_prev(path)) {
				GtkTreeIter prev_iter;
				gtk_tree_model_get_iter(model, &prev_iter, path);
				gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(listFILES)), &prev_iter);
			}
		}
		gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
		if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(model), &iter)) {
			gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(listFILES)), &iter);
		}
		update_status();
	}
}

void openurl() {
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(listFILES));
	if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(listFILES)), &model, &iter)) {
		gchar *location, *filename;
		gtk_tree_model_get(model, &iter, PATH, &location, -1);
		gtk_tree_model_get(model, &iter, FILENAME, &filename, -1);
		filename=url_encode(filename);
		char *url=malloc(strlen(location)+strlen(filename)+1);
		strcpy(url,location);
		strcat(url,filename);
		#if defined(_WIN32)
			ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOW) > 32;
		#elif defined(MACOSX)
			GError* error = NULL;
			const gchar *argv[] = {"open", (gchar*) url, NULL};
			g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
		#else
			GError* error = NULL;
			gchar *argv[] = {"xdg-open", (gchar*) url, NULL};
			g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
		#endif
		free(filename);
		free(url);
		free(location);
	}
}

void copyurl () {
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(listFILES));
	if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(listFILES)), &model, &iter)) {
		gchar *location, *filename;
		gtk_tree_model_get(model, &iter, PATH, &location, -1);
		gtk_tree_model_get(model, &iter, FILENAME, &filename, -1);
		filename=url_encode(filename);
		char *urlss=malloc(strlen(location)+strlen(filename)+1);
		strcpy(urlss,location);
		strcat(urlss,filename);
		gtk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(listFILES), GDK_SELECTION_CLIPBOARD), urlss, -1);
		free(urlss);
		free(filename);
		free(location);
	}
}

gboolean list_popup (void *p, GdkEventButton *event, gpointer userdata) {
	if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3) {
		gtk_widget_set_sensitive(miVIEW, items_found);
		gtk_widget_set_sensitive(miCOPY, items_found);
		gtk_widget_set_sensitive(miDEL, items_found);
		gtk_widget_set_sensitive(miCLEAR, items_found);		
		g_object_ref((gpointer)menu);
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 3, GDK_CURRENT_TIME);
	}
	return FALSE;
}

int main (int argc, char *argv[]) {
	g_thread_init(NULL);
	gdk_threads_init();
	gdk_threads_enter();
	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	
	table = gtk_table_new(6, 4, FALSE);
	gtk_container_add(GTK_CONTAINER(window), table);

	gtk_window_set_title(GTK_WINDOW(window), "cBatchDownload");
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);
	GError *err = NULL;
	gtk_window_set_icon_from_file(GTK_WINDOW(window), "resources/download.png", &err);

	labelSRC = gtk_label_new("来源:");
	labelFIND = gtk_label_new("查找:");
	labelFILTER = gtk_label_new("筛选:");
	labelSTATUS = gtk_label_new("状态:");
	labelREALSTATUS = gtk_label_new("找到0个项目。");
	gtk_misc_set_alignment(GTK_MISC(labelREALSTATUS),0.0,0.5);
	
	entrySRC = gtk_entry_new();
	if (argv[1]) {
		gtk_entry_set_text(GTK_ENTRY(entrySRC), argv[1]);
	} else {
		gtk_entry_set_text(GTK_ENTRY(entrySRC), "resources/test");
	}
	
	gtk_widget_set_size_request(entrySRC, 300, 30);
	
	buttonBROWSE = gtk_button_new_with_label("浏览...");
	gtk_widget_set_size_request(buttonBROWSE, 80, 30);
	
	buttonDL = gtk_button_new_with_label("下载自...");
	gtk_widget_set_size_request(buttonDL, 80, 30);
	
	entryREGEX = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entryREGEX), "(http\\:\\/\\/[a-zA-Z0-9\\-\\.]+\\.[a-zA-Z]{2,3}(?:\\/\\S*)?(?:[a-zA-Z0-9_])+\\.(?:jpg|jpeg|gif|png))");
	
	checkREGEX2_ENABLED = gtk_check_button_new_with_label("启用筛选");
	//gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkREGEX2_ENABLED), TRUE);
	checkREGEX2_REVERSE = gtk_check_button_new_with_label("反筛选");
	gtk_widget_set_sensitive(checkREGEX2_REVERSE, FALSE);
	entryREGEX2 = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entryREGEX2), "tumblr");
	gtk_widget_set_size_request(entryREGEX2, 100, 30);
	gtk_widget_set_sensitive(entryREGEX2, FALSE);
	
	buttonFIND = gtk_button_new_with_label("查找");
	gtk_widget_set_size_request(buttonFIND, 80, 30);
	
	buttonBATCH = gtk_button_new_with_label("批量下载");
	gtk_widget_set_size_request(buttonBATCH, 80, 30);
	gtk_widget_set_sensitive(buttonBATCH, FALSE);
	
	buttonSTOP = gtk_button_new_with_label("停止");
	gtk_widget_set_size_request(buttonSTOP, 80, 30);
	gtk_widget_set_sensitive(buttonSTOP, TRUE);
	gtk_widget_set_visible(buttonSTOP, FALSE);
	
	listFILES = gtk_tree_view_new();
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(listFILES), FALSE);
	init_list(listFILES);
	
	accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
	menu = gtk_menu_new();
	
	miVIEW = gtk_image_menu_item_new_from_stock(GTK_STOCK_NETWORK, NULL);
	gtk_label_set_markup(GTK_LABEL(GTK_BIN(miVIEW)->child), "<b>在默认浏览器中查看</b>");
	gtk_widget_add_accelerator(miVIEW, "activate", accel_group, 0x06f, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), miVIEW);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	
	miCOPY = gtk_image_menu_item_new_from_stock(GTK_STOCK_COPY, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(miCOPY), "复制网址");
	gtk_widget_add_accelerator(miCOPY, "activate", accel_group, 0x063, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), miCOPY);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	
	miDEL = gtk_image_menu_item_new_from_stock(GTK_STOCK_DELETE, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(miDEL), "排除这个项目");
	gtk_widget_add_accelerator(miDEL, "activate", accel_group, 0xffff, 0, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), miDEL);
	
	miCLEAR = gtk_image_menu_item_new_from_stock(GTK_STOCK_CLEAR, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(miCLEAR), "清空列表");
	gtk_widget_add_accelerator(miCLEAR, "activate", accel_group, 0xffff, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), miCLEAR);
	
	scrollLIST = gtk_scrolled_window_new(NULL, NULL);
	//gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollLIST), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(scrollLIST, -1, 200);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrollLIST), listFILES);
	
	labelAUTHOR = gtk_label_new("");
	gtk_label_set_markup(GTK_LABEL(labelAUTHOR), "<small><a href=\"https://github.com/caiguanhao/cBatchDownload\">cBatchDownload</a> by "\
	"<a href=\"http://www.caiguanhao.com/\">caiguanhao</a></small>");
	gtk_misc_set_alignment(GTK_MISC(labelAUTHOR),0.0,0.5);
	
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
	gtk_table_attach(GTK_TABLE(table), buttonSTOP, 3, 4, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	
	gtk_table_attach(GTK_TABLE(table), scrollLIST, 0, 4, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 5);
	
	gtk_table_attach(GTK_TABLE(table), labelAUTHOR, 0, 4, 5, 6, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 0);

	g_signal_connect(G_OBJECT(buttonBROWSE), "clicked", G_CALLBACK(browse), argv[0]);
	g_signal_connect(G_OBJECT(buttonFIND), "clicked", G_CALLBACK(find), (gpointer) window);
	g_signal_connect(G_OBJECT(buttonBATCH), "clicked", G_CALLBACK(download_click), (gpointer) window);
	g_signal_connect(G_OBJECT(buttonSTOP), "clicked", G_CALLBACK(stop_download), (gpointer) window);
	g_signal_connect(G_OBJECT(checkREGEX2_ENABLED), "clicked", G_CALLBACK(chkREGEX2), NULL);
	g_signal_connect(G_OBJECT(miVIEW), "activate", G_CALLBACK(openurl), NULL);
	g_signal_connect(G_OBJECT(miCOPY), "activate", G_CALLBACK(copyurl), NULL);
	g_signal_connect(G_OBJECT(miDEL), "activate", G_CALLBACK(deleteurl), NULL);
	g_signal_connect(G_OBJECT(miCLEAR), "activate", G_CALLBACK(clearlist), NULL);
	g_signal_connect(G_OBJECT(listFILES), "button-press-event", G_CALLBACK(list_popup), NULL);
	g_signal_connect(G_OBJECT(listFILES), "row-activated", G_CALLBACK(openurl), NULL);
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(G_OBJECT(window), "key-press-event", G_CALLBACK(key_press), window);
	
	gtk_widget_grab_focus(buttonFIND);
	gtk_widget_show_all(menu);
	gtk_widget_show_all(window);

	gtk_main();
	gdk_threads_leave();
	return 0;
}
