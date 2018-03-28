#include "web.h"
void P(sem_t *s){
	sem_wait(s);
}
void V(sem_t *s){
	sem_post(s);
}
void sbuf_init(sbuf_t *sp,int n){
	sp->buf = calloc(n,sizeof(int));    //n个单位的环形缓冲区
	sp->n = n;
	sp->front = sp->rear = 0;
	ADDTHREADNUM = THREADNUM;
	sem_init(&sp->mutex,0,1);
	sem_init(&sp->slots,0,n);    //空位数
	sem_init(&sp->items,0,0);    //已有的连接描述符个数
	for(int i = 0;i<THREAD_LIMIT;i++)
		sem_init(&ThreadMutex[i],0,1);    
}
//判断缓冲区是否为空
int sbuf_isempty(sbuf_t *sp){
	int e;
	P(&sp->mutex);
	e = (sp->front - sp->rear == 0);
	V(&sp->mutex);
	return e;
}
//判断缓冲区是否为满
int sbuf_isfull(sbuf_t *sp){
	int f;
	P(&sp->mutex);
	f = ((sp->front - sp->rear+1) %sp->n == 0);   
	V(&sp->mutex);
	return f;
}
//释放缓冲区的资源
void sbuf_deinit(sbuf_t *sp){
	free(sp->buf);
}
//主线程将描述符插入到缓冲区
void sbuf_insert(sbuf_t *sp,int connfd){
	P(&sp->slots);    //检测是否有空位
	P(&sp->mutex);    //获得修改缓冲区权限
	sp->buf[(++sp->front)%sp->n] = connfd;   //插入缓冲区队列尾
	V(&sp->mutex);    //释放权限
	V(&sp->items);    //已有元素数加一
}
//服务线程从缓冲区取出已连接描述符进行服务
int sbuf_remove(sbuf_t *sp){
	P(&sp->items);    //检测是否有元素
	P(&sp->mutex);    //获得修改缓冲区的权限
	int item = sp->buf[(++sp->rear)%sp->n];    //从缓冲区头部取出
	V(&sp->mutex);
	V(&sp->slots);    //空位数加一
	return item;
}
//回收僵死进程
void sigchld_hander(int sig){
	while(waitpid(-1,0,WNOHANG) > 0)
		;
	return;
}
//错误打印
void error_(const char *info){
	printf("%s\n",info );
	exit(0);
}
//线程池的线程对取出的描述符进行服务
void *thread_handler(void *arg){
	int tid = *((int *)arg);
	pthread_detach(pthread_self());    //结束自动释放资源
	free(arg);
	while(1){
		int connfd = sbuf_remove(&sbuf);   //从缓冲区取出描述符
		P(&ThreadMutex[tid]);    //保证服务时不会被终止
		doit(connfd);    //进行服务
		if(close(connfd)<0)   //服务完毕关闭该已连接描述符
			error_("close");
		V(&ThreadMutex[tid]);   //可以被终止
	}
	return NULL;
}
//检测线程，负责调整线程数量
void *adjustHandler(void *arg){
	while(1){
		if(sbuf_isfull(&sbuf)){   //如果缓冲区满，则增加现有线程为2倍
			NEWTHREADNUM = 2*ADDTHREADNUM;
			for(int i = ADDTHREADNUM;i<NEWTHREADNUM;i++){
			
				int *I = (int *)malloc(sizeof(int));//!!!
				*I = i;
				if(pthread_create(&ThreadId[i],NULL,thread_handler,I)<0)
					error_("pthread_create");
				sem_init(&ThreadMutex[i],0,1);
			}
			ADDTHREADNUM = NEWTHREADNUM;
		}
		else if(sbuf_isempty(&sbuf)){   //缓冲区空，则将目前服务完的线程终止
			ADDTHREADNUM = NEWTHREADNUM/2;
			if(ADDTHREADNUM > THREADNUM){   //保留最开始的线程，优先终止后面新建的线程
				for(int i = ADDTHREADNUM;i<NEWTHREADNUM;i++){
					P(&ThreadMutex[i]);
					if(pthread_cancel(ThreadId[i]) < 0)
						error_("pthread_cancel");
					V(&ThreadMutex[i]);
				}
				NEWTHREADNUM = ADDTHREADNUM;
			}
		}
	}
}
int main(int argc,char *argv[]){
	int listenfd,connfd,_port;
	char hostname[MAXN],port[MAXN];
	pthread_t  tid;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	if(argc != 2){
		fprintf(stderr,"usage:%s <port>\n",argv[0]);
		exit(1);
	}
	//为僵死进程提供信号处理器
	if(signal(SIGCHLD,sigchld_hander) == SIG_ERR){
		error_("signal");
	}
	if(signal(SIGPIPE,SIG_IGN) == SIG_ERR){
		error_("signal");
	}
	_port = atoi(argv[1]);   //char * 转int
	if((listenfd = open_listenfd(_port)) <0)
		error_("listenfd");
	for(int i = 0;i< THREADNUM;i++){   //创建线程池
		int *I = (int *)malloc(sizeof(int));
		*I = i;
		pthread_create(&ThreadId[i],NULL,thread_handler,I);
		sem_init(&ThreadMutex[i],0,1);
	}
	sbuf_init(&sbuf,1000);
	if(pthread_create(&tid,NULL,adjustHandler,NULL) < 0){   //创建监测线程，调整线程池的服务线程数量
		error_("pthread_create");
	}
	while(1){    //主线程将已连接描述符添加到缓冲区
		clientlen = sizeof(clientaddr);
		if((connfd = accept(listenfd,(struct sockaddr *)&clientaddr,&clientlen))<0)
			error_("accept");
		getnameinfo((struct sockaddr *)&clientaddr,clientlen,
				hostname,MAXN,
				port,MAXN,0);
		sbuf_insert(&sbuf,connfd);

	}

}
//建立服务器和客户端的连接
int open_listenfd(int port){   
	int listenfd,optval = 1;
	struct sockaddr_in serveraddr;
	if((listenfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
		error_("socket");
	}
	if(setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,
		(const void *)&optval,sizeof(int)) < 0)
		error_("setsockopt");
	bzero((char *)&serveraddr,sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)port);
	if(bind(listenfd,(struct sockaddr *)&serveraddr,sizeof(serveraddr))<0)
		error_("bind");
	if(listen(listenfd,LISTENQ) < 0)
		error_("listen");
	return listenfd;
/*	struct addrinfo hints,*listp,*p;
	int listenfd,optval = 1;
	memset(&hints,0,sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;	//任何IP地址
	hints.ai_flags |=AI_NUMERICSERV;			//使用端口
	getaddrinfo(NULL,port,&hints,&listp);
	//遍历返回的链表，寻找可以bind的项
	for(p = listp;p!=NULL;p = p->ai_next){
		if((listenfd = socket(p->ai_family,p->ai_socktype,p->ai_protocol))<0)
			continue;
		setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,(const void *)&optval,sizeof(int));		//配置服务器，使得能够立即重启，终止，开始接收请求
		if(bind(listenfd,p->ai_addr,p->ai_addrlen) == 0)
			break;	//connect successfully
		close(listenfd);	//bind failed,close this and try next
	}
	freeaddrinfo(listp);
	if(p == NULL)
		return -1;
	if(listen(listenfd,LISTENQ)<0){
		close(listenfd);
		return -1;
	}
	return listenfd;*/

}
ssize_t rio_readnb(rio_t *rp,char *usrbuf,size_t n){
	size_t nleft = n;
	ssize_t nread;
	char *bufp = usrbuf;
	while(nleft > 0){
		if((nread = rio_read(rp,bufp,nleft))<0){
			return -1;
		}
		else if(nread ==0)
			break;
		nleft -= nread;
		bufp += nread;
	}
	return (n-nleft);
}
/*
功能：
	每次从缓冲区中客户端发来的数据中取一行放到usrbuf中
参数：
	rp：和已连接描述符建立联系的缓冲区描述符
	usrbuf：用户缓冲区
	maxlen：每行的最大长度
*/
ssize_t rio_readlineb(rio_t *rp,char *usrbuf,size_t maxlen){
	int n,rc;
	char c,*bufp = usrbuf;
	for(n = 1;n<maxlen;n++){
		if((rc = rio_read(rp,&c,1)) == 1){   //每次读取一个字节，判断是不是换行符，以此判断是否读取了一行
			*bufp++ = c;
			if(c == '\n'){
				//n++;
				break;
			}

		}else if(rc == 0){
			if(n == 1)
				return 0;	//no data read;
			else
				break;		//EOF
		}else
			return -1;	//error
	}
	*bufp = 0;
	return n;   //读取的该行的长度
}
//将已连接描述符和自定义的缓冲区建立联系
void rio_readinitb(rio_t *rp,int fd){
	rp->rio_fd = fd;
	rp->rio_cnt = 0;
	rp->rio_bufptr = rp->rio_buf;
}

void doit(int fd){  //服务线程进行的服务
	int is_static;   //判断是动态请求还是静态请求
	struct stat sbuf;  //判断文件类型
	//buf：存储客户端发来的消息，method：存储HTTP的方法，uri：存储请求的文件uri，version：存储HTTP版本号
	char buf[MAXN],method[MAXN] ,uri[MAXN],version[MAXN];
	//requestBody：存储请求体，post时使用
	char requestBody[MAXN];
	//filename：存储文件名，cgiargs：存储参数
	char filename[MAXN],cgiargs[MAXN];
	rio_t rio;   //自定义的缓冲区
	int contentLen = -1;    //存储post请求时请求体的长度
	rio_readinitb(&rio,fd);   //将已连接描述与自定义的缓冲区联系起来
	rio_readlineb(&rio,buf,MAXN);   //从客户端发来的消息中读取一行
	bzero(requestBody,sizeof(requestBody));
	sscanf(buf,"%s %s %s",method,uri,version);   //将请求头格式化存储
	//根据method判断是哪种方法，如果都不是则报错
	if(!(strcasecmp(method,"GET") == 0 || strcasecmp(method,"POST") == 0 || strcasecmp(method,"HEAD") == 0)){
		clienterror(fd,method,"501","not implemented","tiny doesn't implement this method");
		return ;
	}
	read_requesthdrs(&rio,method,&contentLen);		//读取客户端请求头的剩余部分
	if(strcasecmp(method,"POST") == 0){   //如果是post请求，则将请求体中的数据读取到requestBody中
		//int this line the last char is not \r\n
		rio_readnb(&rio,requestBody,contentLen);   		//请求头之后的数据就是post请求的内容，读取指定长度
	}

	is_static = parse_uri(uri,filename,cgiargs);	//根据文件的uri判断是静态请求还是动态请求
	if(stat(filename,&sbuf)<0){    //判断文件是否存在
		clienterror(fd,filename,"404","not found","tiny couldn't find this file");
		return ;
	}
	if(is_static){	//静态请求则提供静态服务
		if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){  //权限是否满足
			clienterror(fd,filename,"403","frobidden","tiny couldn't read the file");
			return;
		}
		serve_static(fd,filename,sbuf.st_size,method);		//提供静态服务
	}
	else{	//提供动态服务
		if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){   //权限检测
			clienterror(fd,filename,"403","forbidden","tiny couldn't run the CGI program");
			return ;
		}
		if(strcasecmp(method,"GET") == 0)    //get方法的请求在请求头中，已将参数解析到cigargs
			serve_dynamic(fd,filename,cgiargs);	//提供get的动态服务
		else
			serve_dynamic(fd,filename,requestBody);  //提供post的动态服务，参数在请求体中，已被解析到requestBody中
	}
	

}
ssize_t rio_read(rio_t *rp,char *usrbuf,size_t n){
	int cnt;
	while(rp->rio_cnt<=0){   //自定义缓冲区没有数据时，从描述符中读取存储
		rp->rio_cnt = read(rp->rio_fd,rp->rio_buf,sizeof(rp->rio_buf));
		if(rp->rio_cnt<0){
			if(errno != EINTR)
				return -1;
		}
		else if(rp->rio_cnt == 0)   //没有数据可读了
			return 0;
		else
			rp->rio_bufptr = rp->rio_buf;
	}
	cnt = n;
	if(rp->rio_cnt<n)
		cnt = rp->rio_cnt;   //读取已有数据量和指定读取数据量中最小的那个值
	memcpy(usrbuf,rp->rio_bufptr,cnt);
	rp->rio_bufptr +=cnt;
	rp->rio_cnt -= cnt;   //剩余字节数
	return cnt;  //读取到的字节数
}
void Im_rio_writen(int fd,char *usrbuf,size_t n){
	if(rio_writen(fd,usrbuf,n) != n){   //发生EPIPE错误
		if(errno == EPIPE){
			fprintf(stderr, "EPIPE error" );
		}
		fprintf(stderr, "%s\n",strerror(errno) );
		error_("EPIPE");
	}
}
/*
功能：
	将usrbuf缓冲区中的数据写入描述符fd中，发送给客户端
*/
ssize_t rio_writen(int fd,char *usrbuf,size_t n){
	size_t nleft = n;   //剩余发送的数据字节数
	ssize_t nwritten;   //已经发送了的数据的字节数
	char *bufp = usrbuf;
	while(nleft >0){
		if((nwritten = write(fd,bufp,nleft))<=0){
			if(errno == EINTR)   //防止被信号处理程序中断后不继续发送
				nwritten = 0;
			else 
				return -1;
		}
		nleft -= nwritten;
		bufp += nwritten;
	}
	return n;
}
//给客户端发送错误消息
void clienterror(int fd,const char *cause,const char *errnum,const char *shortmsg,const char *longmsg){
	char buf[MAXN],body[MAXN];
	//build the http response body
	sprintf(body,"<html><title>tiny error</title>");
	sprintf(body,"%s<body bgcolor=""ffffff"">\r\n",body);
	sprintf(body,"%s%s:%s\r\n",body,errnum,shortmsg);
	sprintf(body,"%s<p>%s:%s\r\n",body,longmsg,cause);
	sprintf(body,"%s<hr><em>the tiny web server</em>\r\n",body);
	
	//print the http response
	sprintf(buf,"HTTP/1.0 %s %s \r\n",errnum,shortmsg);
	Im_rio_writen(fd,buf,strlen(buf));
	sprintf(buf,"Content-type: text/html\r\n");
	Im_rio_writen(fd,buf,strlen(buf));
	sprintf(buf,"Content-length: %d\r\n\r\n",(int)strlen(body));
	Im_rio_writen(fd,buf,strlen(buf));
	Im_rio_writen(fd,body,strlen(body));

}
//读取客户端发来的请求头剩余部分
void read_requesthdrs(rio_t *rp,char *method,int *contentLen){
	char buf[MAXN];
	do{
		rio_readlineb(rp,buf,MAXN);
		//如果是post请求，则获取其请求长度，存储到contentLen中
		if(strcasecmp(method,"POST") == 0 && 
			strncasecmp(buf,"Content-length:",15) == 0){
			*contentLen = atoi(buf+16);
		}
	}while(strcmp(buf,"\r\n"));   //请求头已空行结束
	return ;
}

/*
功能：
	从get请求头的uri中解析出文件名和参数

参数：
	uri：请求头中的文件和参数的结合
	filename：存储解析后的文件名
	cgiargs：如果是动态服务则存储其参数

*/
int parse_uri(char *uri,char *filename,char *cgiargs){
	char *ptr;
	if(!strstr(uri,"cgi-bin")){	//没有cgi-bin字段则为静态服务
		strcpy(cgiargs,"");
		strcpy(filename,".");    //在当前目录下
		strcat(filename,uri);
		if(uri[strlen(uri)-1] == '/')   //不加文件名则指定默认打开的文件
			strcat(filename,"index.html");
		return 1;
	}
	else{	//有cgi-bin字段，则为动态服务
		ptr = index(uri,'?');    //get请求头中文件名和参数用？分割，ptr存储？所在位置
		if(ptr){   //如果有？，则一定有参数
			strcpy(cgiargs,ptr+1);    //将参数列表存储到cgiargs中
			*ptr = '\0';
		}
		else
			strcpy(cgiargs,"");   //no parameter
		strcpy(filename,".");
		strcat(filename,uri);   //？前面的就是文件名
		return 0;
		
	}
}
/*
功能：
	提供静态服务，即将文件发送给客户端
参数：
	fd：已连接描述符
	filename：需要回显的文件名
	filesize：文件大小
	method：http方法
	srcfd：文件打开描述符
	srcp：文件映射后的虚拟内存地址
	filetype：文件类型
	buf：存储HTTP响应头

*/
void serve_static(int fd,char *filename,int filesize,char *method){
	int srcfd;
	char *srcp,filetype[MAXN],buf[MAXN];

	//send responce to client
	get_filetype(filename,filetype);   //根据文件名获取文件类型
	sprintf(buf,"HTTP/1.0 200 OK\r\n");   
	sprintf(buf,"%sServer:tiny wib Server \r\n",buf);
	sprintf(buf,"%sConnection:close\r\n",buf);
	sprintf(buf,"%sContent-length: %d\r\n",buf,filesize);
	sprintf(buf,"%sContent-type: %s\r\n\r\n",buf,filetype);
	Im_rio_writen(fd,buf,strlen(buf));   //发送HTTP响应头
	//send response body to client
	if(strcasecmp(method,"GET") == 0){
		srcfd = open(filename,O_RDONLY,0);   //打开文件
		srcp = (char *)mmap(0,filesize,PROT_READ,MAP_PRIVATE,srcfd,0);   //将文件映射到虚拟内存
		close(srcfd);
		if(strcasecmp(method,"GET") == 0)
			Im_rio_writen(fd,srcp,filesize);   //将文件发送给客户端
		munmap(srcp,filesize);   //解除映射
	}

}

/*
     {".css", "text/css"},
    {".gif", "image/gif"},
    {".htm", "text/html"},
    {".html", "text/html"},
    {".jpeg", "image/jpeg"},
    {".jpg", "image/jpeg"},
    {".ico", "image/x-icon"},
    {".js", "application/javascript"},
    {".pdf", "application/pdf"},
    {".mp4", "video/mp4"},
    {".png", "image/png"},
    {".svg", "image/svg+xml"},
    {".xml", "text/xml"},
*/
//获取文件类型，需要按指定格式发送给客户端的响应头
void get_filetype(char *filename,char *filetype){
	if(strstr(filename,".html"))
		strcpy(filetype,"text/html");
	else if(strstr(filename,".gif"))
		strcpy(filetype,"image/gif");
	else if(strstr(filename,".png"))
		strcpy(filetype,"image/png");
	else if(strstr(filename,".jpg"))
		strcpy(filetype,"image/jpeg");
	else if(strstr(filename,".css"))
		strcpy(filetype,"text/css");
	else if(strstr(filename,".js"))
		strcpy(filetype,"application/javascript");
	else if(strstr(filename,".mp4"))
		strcpy(filetype,"video/mp4");
	else if(strstr(filename,".pdf"))
		strcpy(filetype,"application/pdf");
	else
		strcpy(filetype,"text/plain");
}
/*
功能：
	提供get和post的动态服务
参数
	fd: connect file descriptor
	filename : the name of cgi program. eg: ./word
	cgiargs: the parameter of POST response body or GET respnse head. eg: name=Sam&passwd=123456 or word?_word=abandon
*/
void serve_dynamic(int fd,char *filename,char *cgiargs){
	char buf[MAXN],*emptylist[] = {NULL};
	bzero(buf,sizeof(buf));
	
	//return first part of http response
	sprintf(buf,"HTTP/1.0 200 OK\r\n");

	Im_rio_writen(fd,buf,strlen(buf));    //send buf to client
	sprintf(buf,"server: tiny web server\r\n");    
	Im_rio_writen(fd,buf,strlen(buf));   //send response head to client
	if(fork()==0){
		if(signal(SIGPIPE,SIG_DFL) == SIG_ERR){
			error_("signal SIGPIPE");
		}
		setenv("QUERY_STRING",cgiargs,1);  

		//printf("%s   %s\n",filename,cgiargs );
		dup2(fd,STDOUT_FILENO);
		execve(filename,emptylist,environ);		//run CGI program
		error_("execve");
	}
	
}
