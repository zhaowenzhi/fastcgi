fastcgi by c
=======

1、修改fcgi_un.c文件中unixSocket为本机socket文件位置

2、修改fcgi_un.c文件中scriptFile为本机请求的php文件位置

3、gcc -o fcgiun fcgi_un.c

4、run ./fcgiun 
