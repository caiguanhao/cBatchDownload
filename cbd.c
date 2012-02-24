#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <pcre.h>
#include <ctype.h>
#include <pthread.h>

int items_found = 0, items_to_dl = 0;

GtkWidget *window;
GtkWidget *table;

GtkWidget *labelSRC,*labelFIND,*labelFILTER,*labelSTATUS,*labelREALSTATUS;
GtkWidget *entrySRC,*entryREGEX,*entryREGEX2;
GtkWidget *buttonBROWSE,*buttonDL,*buttonFIND,*buttonBATCH;
GtkWidget *checkREGEX2_ENABLED, *checkREGEX2_REVERSE;

GtkWidget *scrollLIST,*listFILES;
GtkWidget *gFCD;

pthread_t thread_download;

enum
{
	FILENAME,
	PATH,
	PROGRESS_TEXT,
	PROGRESS,
	N_COLUMNS
};

static void init_list(GtkWidget *list)
{
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

static void add_to_list(GtkWidget *list, gchar *progt, int prog, gchar *path, gchar *str)
{
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(list)));

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, PROGRESS_TEXT, progt, PROGRESS, prog, PATH, path, FILENAME, str, -1);
}

/* URL_DECODE START
 * From: http://www.geekhideout.com/urlcode.shtml */
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

int last_slash(char *location){
	int i=0;
	char c[1];
	do {
		sprintf(c,"%.*s",1,&location[strlen(location)-i]);
		i++;
	} while(strcmp(c,"/")!=0);
	return i;
}

char *get_path (char *location)
{
	char *path=malloc((strlen(location)+1)*sizeof(char));
	sprintf(path,"%.*s",strlen(location)-last_slash(location)+2,&location[0]);
	return path;
}

char *get_file_name (char *location)
{
	char *filename=malloc((strlen(location)+1)*sizeof(char));
	sprintf(filename,"%s",url_decode(&location[strlen(location)-last_slash(location)+2]));
	return filename;
}

char *location_to_save (char *path)
{
	char *lts=malloc((strlen(path)+1)*sizeof(char));
	sprintf(lts,"%s%s","downloads/",get_file_name(path));
	return lts;
}

void browse (GtkWidget *widget, char *dir)
{
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

void warn (gchar *title, gchar *content)
{
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, NULL);
	gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), content);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

void find (GtkWidget *widget, gpointer window)
{
	items_found=0;
	items_to_dl=0;
	gtk_list_store_clear(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW (listFILES))));

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
					GtkWidget *dialog;
					dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "筛选：错误的正则表达式。");
					gtk_window_set_title(GTK_WINDOW(dialog), "错误");
					gtk_dialog_run(GTK_DIALOG(dialog));
					gtk_widget_destroy(dialog);
					break;
				} else {
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
						
						items_found+=1;
						
					}
					offset = ovector[1];
				}
			}
	   	}
		fclose(pFILE);
	} else {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "无法打开输入文件。");
		gtk_window_set_title(GTK_WINDOW(dialog), "错误");
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
	char status[50];
	sprintf(status, "找到%d/%d个项目可供下载。", items_to_dl, items_found);
	gtk_label_set_text(GTK_LABEL(labelREALSTATUS), status);
	gtk_widget_set_sensitive(buttonBATCH, items_to_dl);
}

size_t write_data (void *ptr, size_t size, size_t nmemb, FILE *stream) {
	return fwrite(ptr, size, nmemb, stream);
}

void chkREGEX2() {
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
	gtk_widget_set_sensitive(buttonBATCH, sensitive);
	if(sensitive)chkREGEX2();
}

static int progress(gchar *pathstring, double dltotal, double dlnow, double ultotal, double ulnow)
{
	gdk_threads_enter();
	
	int done=0;
	if(dlnow!=0&&dltotal!=0)done=(int)dlnow/dltotal*100;
	GtkTreeModel *model;
	GtkTreeIter iter;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(listFILES));
	gtk_tree_model_get_iter_from_string(model, &iter, pathstring);
	char pt[10];
	sprintf(pt, "%d%%", done);
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, PROGRESS_TEXT, pt, PROGRESS, done, -1);
	
	gdk_threads_leave();
	return 0;
}

int dl_simul = 5;

void *download(void *p) {
	
	CURLM *multi_handle;
	CURL *http_handle[dl_simul-1];
	FILE *fp[dl_simul-1];
	int still_running;
	
	curl_global_init(CURL_GLOBAL_ALL);
	multi_handle = curl_multi_init();
	
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean valid;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(listFILES));
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter);
	
	int i=0;
	
	while (valid && i<dl_simul) {
		gint *progcc;
		gtk_tree_model_get(model, &iter, PROGRESS, &progcc, -1);
		
		if((int)progcc!=(int)100){
			gchar *location, *filename;
			gtk_tree_model_get(model, &iter, PATH, &location, -1);
			gtk_tree_model_get(model, &iter, FILENAME, &filename, -1);
			
			char *urlss=malloc(strlen(location)+strlen(filename)+1);
			strcpy(urlss,location);
			strcat(urlss,filename);
			
			fp[i]=fopen(location_to_save(urlss),"wb");
			
			http_handle[i] = curl_easy_init();
			curl_easy_setopt(http_handle[i], CURLOPT_URL, urlss);
			curl_easy_setopt(http_handle[i], CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
			curl_easy_setopt(http_handle[i], CURLOPT_WRITEFUNCTION, write_data);
			curl_easy_setopt(http_handle[i], CURLOPT_WRITEDATA, fp[i]);
			curl_easy_setopt(http_handle[i], CURLOPT_NOPROGRESS, 0);
			curl_easy_setopt(http_handle[i], CURLOPT_PROGRESSFUNCTION, progress);
			curl_easy_setopt(http_handle[i], CURLOPT_PROGRESSDATA, gtk_tree_model_get_string_from_iter(model, &iter));
			curl_multi_add_handle(multi_handle, http_handle[i]);
			i++;
			items_to_dl-=1;
		}
		
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter);
	}
	if (i>0) {
		curl_multi_perform(multi_handle, &still_running);

		while(still_running) {
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
		}
	}
	int j=0;
	for(;j<i;j++){
		if (fp[j]) fclose(fp[j]);
		curl_multi_remove_handle(multi_handle, http_handle[j]);
		curl_easy_cleanup(http_handle[j]);
	}
	curl_multi_cleanup(multi_handle);
	curl_global_cleanup();
	if (items_to_dl>0) {
		return download(NULL);
	} else {
		gtk_label_set_text(GTK_LABEL(labelREALSTATUS), "下载完成。");
		batchsensitive(TRUE);
		pthread_exit(NULL);
		return NULL;
	}
}

void download_click() {
	/*GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean valid;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(listFILES));
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter);
	int count=0;
	while (valid) {
		gint *progcc;
		gtk_tree_model_get(model, &iter, PROGRESS, &progcc, -1);
		if((int)progcc!=(int)100){
			count+=1;
		}
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter);
	}
	*/
	if (items_to_dl==0) {
		warn("错误","找不到项目可以下载。");
	} else {
		gtk_label_set_text(GTK_LABEL(labelREALSTATUS), "正在下载...");
		batchsensitive(FALSE);
		
		pthread_create(&thread_download, NULL, download, NULL);
	}
}

int main (int argc, char *argv[]) {
	g_thread_init(NULL);
	gdk_threads_init();
	gdk_threads_enter();
	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	
	table = gtk_table_new(5, 4, FALSE);
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
	
	listFILES = gtk_tree_view_new();
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(listFILES), FALSE);
	init_list(listFILES);
	
	scrollLIST = gtk_scrolled_window_new(NULL, NULL);
	//gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollLIST), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
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

	g_signal_connect(G_OBJECT(buttonBROWSE), "clicked", G_CALLBACK(browse), argv[0]);
	g_signal_connect(G_OBJECT(buttonFIND), "clicked", G_CALLBACK(find), (gpointer) window);
	g_signal_connect(G_OBJECT(buttonBATCH), "clicked", G_CALLBACK(download_click), (gpointer) window);
	g_signal_connect(G_OBJECT(checkREGEX2_ENABLED), "clicked", G_CALLBACK(chkREGEX2), NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	gtk_widget_show_all(window);

	gtk_main();
	gdk_threads_leave();
	return 0;
}
