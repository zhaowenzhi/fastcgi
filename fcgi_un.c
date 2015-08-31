#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/un.h>

struct result{
	char *str;//字符串
	int len;//字符串对应的长度
};
//名值对结构体
struct nvpairStr{
	char *name;//相关于key
	char *value;//value
};


#define	MAXBUFFSIZE	256
#define	PORT 6666
#define HOST_ADDR "127.0.0.1"
#define FCGI_REQUEST_COMPLETE 0
#define FCGI_BEGIN_REQUEST 1
#define END_REQUEST 3
#define PARAMS 4
#define STDIN 5
#define FCGI_STDOUT 6
#define FCGI_STDERR 7
#define HEADER_LEN 8  //由于fastcgi协议是8字节对齐，所有程序中有关头信息长度等都用该常量代替
char * buildPacket(int type, char *cont, int id, int len);
char * mystrncpy(char *dest, char *src, int len);
struct result buildNvpairS(struct nvpairStr pStr[], int size);
int main(int argc, char *argv[])
{
	int sockfd,id;
	char s[HEADER_LEN]={0};
	struct result nvpair;
	//struct sockaddr_in	servaddr;//通过ip连接
	struct sockaddr_un	servaddr;//通过Unix socket连接
	//sockfd=socket(AF_INET,SOCK_STREAM,0);
	sockfd=socket(AF_UNIX,SOCK_STREAM,0);
	if(sockfd<0)
	{
		printf("Socket created failed.\n");
		return -1;
	}
	char *unixSocket = "/dev/shm/php-cgi.sock";//unix socket 文件位置
	char *scriptFile = "/usr/local/nginx/html/index.php";//请求php的脚本
	int i=0;
	//servaddr.sin_family=AF_INET;
	//servaddr.sin_port=htons(9000);
	//servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sun_family = AF_UNIX;
	strcpy(servaddr.sun_path, unixSocket);
	
	printf("connecting...\n");
	if(connect(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr))<0)
	{
		printf("Connect server failed.\n");
		return -1;
	}
	id = 1;//ID
	s[0]=(char)(0);s[1]=(char)(1);s[2]=(char)(0);
	s[3]=(char)(0);s[4]=(char)(0);
	s[5]=(char)(0);s[6]=(char)(0);
	s[7]=(char)(0);
	
    char *content = "key=value&arr[0]=1&arr[1]=2&arr[2]=3&arr[\"a\"]=4";
    char *tmp ,*pBegin ,*pParams ,*pParamsEnd ,*pStdIn ,*pStdInEnd;int index =0;
    int bugLen = 0;
	pBegin = tmp = buildPacket(FCGI_BEGIN_REQUEST, s, id, sizeof(s));

	bugLen += sizeof(s) + HEADER_LEN;
	index += HEADER_LEN*2;
	char sLen[128] = {0};//post内容长度值
	sprintf(sLen, "%d", strlen(content));
	
	//要发送的包信息
	struct nvpairStr nvs[] = {
		{"REQUEST_METHOD", "POST"}, {"CONTENT_TYPE", "application/x-www-form-urlencoded"},
		{"SCRIPT_FILENAME", scriptFile}, {"CONTENT_LENGTH", sLen}
		};
	nvpair = buildNvpairS(nvs, sizeof(nvs)/sizeof(struct nvpairStr));
	if(nvpair.len == -1) {
		exit(-1);
	}

	int pairlen = nvpair.len;
	pParams = tmp = buildPacket(PARAMS, nvpair.str, id, pairlen);
	index += pairlen+HEADER_LEN;
	free(nvpair.str);
	pParamsEnd = tmp = buildPacket(PARAMS, NULL, id, 0);
	index += HEADER_LEN;
	
	pStdIn = tmp = buildPacket(STDIN, content, id, strlen(content));
	index += (strlen(content)+HEADER_LEN);
	pStdInEnd = tmp = buildPacket(STDIN, NULL, id, 0);
	index += HEADER_LEN;

	int indexSend = 0;
	char *bufSend = (char*)calloc(index,1);
	mystrncpy(bufSend+indexSend, pBegin, HEADER_LEN*2);//请求开始，固定16个字节(相当于2个包头changd)
	indexSend += HEADER_LEN*2;
	mystrncpy(bufSend+indexSend, pParams, pairlen+HEADER_LEN);//请求参数+8个字节的头信息
	indexSend += pairlen+HEADER_LEN;
	mystrncpy(bufSend+indexSend, pParamsEnd, HEADER_LEN);//请求参数结束+8个字节的头信息
	indexSend += HEADER_LEN;
	mystrncpy(bufSend+indexSend, pStdIn, strlen(content)+HEADER_LEN);//请求输入流，这里指的是post的数据+8个字节的头信息
	indexSend += strlen(content)+HEADER_LEN;
	mystrncpy(bufSend+indexSend, pStdInEnd, HEADER_LEN);//请求输入流结束+8个字节的头信息
	indexSend += HEADER_LEN;

	int res = 0;
	res = write(sockfd,bufSend,indexSend);//写入socket
	free(bufSend);
	//开始解包
	char resp[HEADER_LEN] = {0};
	read(sockfd, resp, HEADER_LEN);//先读一个头信息
	if(resp[1] != FCGI_STDOUT) {//返回异常
		printf("return error %d\n", resp[1]);
		unsigned char resposeLen = ((resp[4] << 8) + resp[5]) ;
		char *resp1 = (char*)calloc(resposeLen,1);
		read(sockfd, resp1, resposeLen);
		printf("errorInfo:%s\n",resp1);
		free(resp1);
		exit(1);
	}
	
	unsigned char l = 0;
	unsigned char resposeLen = ((resp[4] << 8) + resp[5]) ;
	unsigned char len = resposeLen;
	char *resp1 = (char*)calloc(resposeLen,1);//fastcgi正真返回的正文数据
    while(len && (l = read(sockfd, resp1+l, len)) >= 0) {//防止粘包
		len -= l;
	}
	printf("%s\n",resp1);
    free(resp1);
    char * resp2 = (char*)calloc(HEADER_LEN,1);
    int r1 = read(sockfd, resp2, (unsigned char)resp[6]);//paddingData
    
    read(sockfd, resp2, HEADER_LEN);//FCGI_END_REQUEST
 
    read(sockfd, resp2, HEADER_LEN);//EndRequestBody
    if(resp2[4] !=FCGI_REQUEST_COMPLETE) {//FCGI_CANT_MPX_CONN,FCGI_OVERLOADED ,FCGI_UNKNOWN_ROLE 
    	printf("FCGI_REQUEST error %d\n", resp2[4]);
    	exit(2);
    }
    free(resp2);
	return 0;
}

//构建各种类型(type)的包
char * buildPacket(int type, char *cont, int requestId, int len) {
	char s[HEADER_LEN] = {0};//包头
	char *ret = (char*)calloc(len+HEADER_LEN,1);
	s[0]=(char)(1);s[1]=(char)(type);s[2]=(char)((requestId >> 8) & 0xff);
	s[3]=(char)((requestId) & 0xff);s[4]=(char)((len >> 8) & 0xff);
	s[5]=(char)((len) & 0xff);s[6]=(char)(0);
	s[7]=(char)(0);
	int i=0;
	mystrncpy(ret,s,HEADER_LEN);
	mystrncpy((char *)(ret+HEADER_LEN),(char *)cont,len);
	return ret;
}
 
 //strncpy变体，strncpy如果遇到\0会停止复制，这里自定义一个函数，但没做超界处理,待改善
char * mystrncpy(char *dest,char *src, int size) {
	int i = 0;
	while(i < size) {
		*(dest+i) = *(src+i);
		i++;
	}
	return dest;
}

//构建名值对， 返回结构体
struct result buildNvpairS(struct nvpairStr pStr[], int size) {
	struct result r;
	int len = 0, i = 0;
	while(i < size) {//计算总长度
		struct nvpairStr nv = pStr[i];
		int nlen = strlen(nv.name);
		int vlen = strlen(nv.value);
		if(nlen < 128) {
		len += 1;
		}else {
			len += 4;
		}
		if(vlen < 128) {
			len += 1;
		}else {
			len += 4;
		}
		len += nlen;
		len += vlen;
		i ++;
	}
	char *ret = (char *)calloc(len, 1);
	i = 0;int j = 0;
	while(i < size) {
		struct nvpairStr nv = pStr[i];
		int nlen = strlen(nv.name);
		int vlen = strlen(nv.value);
		if(nlen < 128) {
			ret[j] = (char)nlen;
			j ++;
		}else {
			ret[j] = (char) ((nlen >> 24) | 0x80);    // name LengthB3
			j ++;
			ret[j] = (char) ((nlen >> 16) & 0xff);    // name LengthB2
			j ++;
			ret[j] = (char) ((nlen >> 8) & 0xff);    // name LengthB1
			j ++;
			ret[j] = (char) (nlen & 0xff);
			j ++;
		}
		if(vlen < 128) {
			ret[j] = (char)vlen;
			j ++;
		}else {
			ret[j] = (char) ((vlen >> 24) | 0x80);    // name LengthB3
			j ++;
			ret[j] = (char) ((vlen >> 16) & 0xff);    // name LengthB2
			j ++;
			ret[j] = (char) ((vlen >> 8) & 0xff);    // name LengthB1
			j ++;
			ret[j] = (char) (vlen & 0xff);
			j ++;
		}
		mystrncpy(ret+j, nv.name, nlen);
		j += nlen;
		mystrncpy(ret+j, nv.value, vlen);
		j += vlen;
		i ++;
	}
	r.str = ret;
	r.len = len;
	if(j != len) {
		len = -1;//长度不对
		printf("content len error!%d\n", len);
	}
	return r;
}
