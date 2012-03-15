## cBatchDownload
cBatchDownload makes it easier to use [cURL](http://curl.haxx.se/libcurl/) to batch download files from a list of links extracted from a file using a [regular expression](http://en.wikipedia.org/wiki/Regular_expression). This program mainly uses [GTK+](http://www.gtk.org/) (as interface), [PCRE](http://www.pcre.org/) (as regular expression library) and [POSIX Threads](http://en.wikipedia.org/wiki/POSIX_Threads).

For more information, visit [caiguanhao.com/C/cBatchDownload](http://www.caiguanhao.com/C/cBatchDownload)

<img src="http://www.caiguanhao.com/C/images/cbatchdownload_screenshot.png" alt="cBatchDownload" width="528" height="412" /> <img src="http://www.caiguanhao.com/C/images/cbatchdownload_screenshot_2.png" alt="cBatchDownload downloading" width="528" height="412" />

running on Ubuntu 11.10

To compile in Ubuntu, you need to:

* $ sudo apt-get install build-essential
* $ sudo apt-get install libgtk2.0-dev
* $ sudo apt-get install libcurl4-openssl-dev
* $ sudo apt-get install libpcre3 libpcre3-dev

gcc -D_REENTRANT -Wall -o "cbd" "cbd.c" `pkg-config --cflags --libs libcurl gtk+-2.0` -lm -lpthread