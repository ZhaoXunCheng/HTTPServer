#include<stdio.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<ctype.h>
#include<strings.h>
#include<string.h>
#include<sys/stat.h>
#include<pthread.h>
#include<sys/wait.h>
#include<stdlib.h>
#include<errno.h>

#define SERVER_STRING "Server: localhost"

void deal_request(int);
void bad_request(int);
void cat(int, FILE*);
void cannot_execute(int);
void error_die(const char*);
void execute_cgi(int, const char*, const char*, const char*);
int get_line(int, char*, int);
void headers(int);
void not_found(int);
void send_file(int, const char*);
int start(unsigned short*);
void unimplemented(int);

void deal_request(int sockfd){
    ssize_t numchars;
    char buf[1024];
    char path[512];
    char url[256];
    char method[256];
    size_t i, j;
    struct stat st;
    bool cgi = false;
    char*  query_string = NULL;

    numchars = get_line(sockfd, buf, sizeof(buf));

    i = 0, j = 0;

    while(!isspace((int)buf[j]) && i < sizeof(method)){
        method[i] = buf[j];
        ++i, ++j;
    }

    method[i] = '\0';

    if(strcasecmp(method, "GET") && strcasecmp(method, "POST")){
        unimplemented(sockfd);
        return;
    }

    while(isspace((int)buf[j]) && j < sizeof(buf)){
        ++j;
    }

    i = 0;

    while(!isspace((int)buf[j]) && i < sizeof(url) && j < sizeof(buf)){
        url[i] = buf[j];
        ++i, ++j;
    }

    url[i] = '\0';

    printf("url: %s\n", url);  //for debug

    if(strcasecmp(method, "GET") == 0){  //GET method

        query_string = url;

        while(*query_string != '?' && *query_string != '\0'){
            ++query_string;
        }

        if(*query_string == '?'){
            cgi = true;
            *query_string = '\0';
            ++query_string;
        }

        numchars = 1;

        while(numchars > 0 && strcmp(buf, "\n")){
            numchars = get_line(sockfd, buf, sizeof(buf));
        }

        if(cgi == false){  //for debug
            printf("cgi is false. get to execute next.\n");
        }
    }

    else{  //POST method
        printf("POST method. cgi is true.\n");  //for debug
        cgi = true;
    }

    sprintf(path, "Webdir%s", url);

    printf("path: %s\n", path);  //for debug

    if(path[strlen(path) - 1] == '/'){
        strcat(path, "index.html");
    }

    if(stat(path, &st) == -1){ //requested file does not exist
        not_found(sockfd);
        return;
    }

    else if(st.st_mode & S_IFMT == S_IFDIR){  //director file
        strcat(path, "/index.html");
    }

    else if((st.st_mode & S_IXGRP) || (st.st_mode & S_IXUSR) || (st.st_mode & S_IXOTH)){  //executive file
        cgi = true;
    }

    if(cgi == false){
        printf("yes, we are here. about to execute serve_file\n"); //for debug
        send_file(sockfd, path);
    }

    else{
        execute_cgi(sockfd, path, method, query_string);
    }

    close(sockfd);

    return;
}


void execute_cgi(int sockfd, const char* path, const char* method, const char* query_string){

    printf("we are now in execute cgi.\n");

    char buf[1024];
    int cgi_input[2];
    int cgi_output[2];
    pid_t pid;
    int numchars = 1;
    int content_length = -1;
    char c;
    int status;

    buf[0] = '\0';

    if(strcasecmp(method, "POST") == 0){

        while(numchars > 0 && strcmp(buf, "\n")){
            numchars = get_line(sockfd, buf, sizeof(buf));
            buf[15] = '\0';
            if(strcasecmp(buf, "Content-Length:") == 0){
                content_length = atoi(buf + 16);
            }
        }

        if(content_length == -1){
            bad_request(sockfd);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");

    send(sockfd, buf, strlen(buf), 0);

    if(pipe(cgi_input) < 0){
        cannot_execute(sockfd);
        return;
    }

    if(pipe(cgi_output) < 0){
        cannot_execute(sockfd);
        return;
    }

    if((pid = fork()) < 0){
        cannot_execute(sockfd);
        return;
    }

    if(pid == 0){  //child process
        printf("we are now in child process.\n"); //for debug
        close(cgi_input[1]);
        close(cgi_output[0]);

        dup2(cgi_input[0], 0);  //redirect stdin
        dup2(cgi_output[1], 1);  //redirect stdout

        char method_env[256];
        char length_env[256];
        char query_env[256];

        sprintf(method_env, "REQUEST_METHOD=%s", method);

        printf("we are about to putenv.\n");

        putenv(method_env);

        if(strcasecmp(method, "GET") == 0){  //GET method
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }

        else{  //POST method
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }

        printf("successfully set the env.\n");  //for debug

        printf("about to execute the cgi program.\n");//for debug

        execl(path, path, NULL);  //execute cgi

        printf("cgi done.\n");  //for debug

        exit(0);
    }

    else{  //father process

        close(cgi_input[0]);

        close(cgi_output[1]);

        printf("now we are in father process.\n");  //for debug

        if(strcasecmp(method, "POST") == 0){  //POST
            for(int i = 0; i < content_length; ++i){
                recv(sockfd, &c, 1, 0);
                printf("%c", c);  //for debug
                write(cgi_input[1], &c, 1);
            }
        }

        printf("\n");

        while(read(cgi_output[0], &c, 1) > 0){
            printf("%c", c);  //for debug
            send(sockfd, &c, 1, 0);
        }

        printf("\n");

        printf("father process done.\n");  //for debug

        close(cgi_input[1]);

        close(cgi_output[0]);  //close pipe

        waitpid(pid, &status, 0);
    }
    return;
}

void bad_request(int sockfd){
    printf("we are now in bad_request().\n");  //for debug
    int numchars;
    char buf[256];

    numchars = sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");

    send(sockfd, buf, numchars, 0);

    numchars = sprintf(buf, "Content-type: text/html\r\n");

    send(sockfd, buf, numchars, 0);

    numchars = sprintf(buf, "\r\n");

    send(sockfd, buf, numchars, 0);

    numchars = sprintf(buf, "<P>Your browser sent a bad request, ");

    send(sockfd, buf, numchars, 0);

    numchars = sprintf(buf, "such as a POST without a Content-Length.\r\n");

    send(sockfd, buf, numchars, 0);
}

void cat(int sockfd, FILE* fp){

    printf("now we are in cat().\n");  //for debug

    char buf[1024];

    while (fgets(buf, sizeof(buf), fp) != NULL)
    {
        send(sockfd, buf, strlen(buf), 0);
    }

    printf("cat done.\n");  //for debug
}

void cannot_execute(int sockfd){
    printf("we are in cannot_execute() now.\n");  //for debug
    char buf[256];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");

    send(sockfd, buf, strlen(buf), 0);

    sprintf(buf, "Content-type: text/html\r\n");

    send(sockfd, buf, strlen(buf), 0);

    sprintf(buf, "\r\n");

    send(sockfd, buf, strlen(buf), 0);

    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");

    send(sockfd, buf, strlen(buf), 0);
}

void error_die(const char* sc){
    perror(sc);
    exit(1);
}

int get_line(int sockfd, char* buf, int size){
    int i = 0;
    int n;
    char c = '\0';

    while(i < size - 1 && c != '\n'){

        n = recv(sockfd, &c, 1, 0);

        if(n <= 0){
            break;
        }

        if(c == '\r'){
            n = recv(sockfd, &c, 1, MSG_PEEK);
            if(c == '\n'){
                recv(sockfd, &c, 1, 0);
            }
            else{
                c = '\n';
            }
        }

        buf[i] = c;

        ++i;
    }

    buf[i] = '\0';

    return i;
}

void headers(int client)
{
 char buf[1024];
                //200
 strcpy(buf, "HTTP/1.0 200 OK\r\n");

 send(client, buf, strlen(buf), 0);

 strcpy(buf, SERVER_STRING);

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "Content-Type: text/html\r\n");

 send(client, buf, strlen(buf), 0);

 strcpy(buf, "\r\n");

 send(client, buf, strlen(buf), 0);
}

void not_found(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, SERVER_STRING);

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "Content-Type: text/html\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "<BODY><P>The server could not fulfill\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "your request because the resource specified\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "is unavailable or nonexistent.\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "</BODY></HTML>\r\n");

 send(client, buf, strlen(buf), 0);
}

void send_file(int sockfd, const char* filename){

    FILE* fp = fopen(filename, "r");

    if(fp == NULL){
        not_found(sockfd);
        return;
    }

    headers(sockfd);

    cat(sockfd, fp);

    fclose(fp);

    return;
}

int start(unsigned short* portp){
    int listenfd;
    sockaddr_in server_addr;

    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        error_die("socket");
    }

    bzero(&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;

    server_addr.sin_port = htons(*portp);

    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(listenfd, (sockaddr*)(&server_addr), sizeof(server_addr)) < 0){
        error_die("bind");
    }

    if(listen(listenfd, 500) < 0){
        error_die("listen");
    }

    return listenfd;
}

void unimplemented(int client)
{
 char buf[1024];
                //501
 sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, SERVER_STRING);

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "Content-Type: text/html\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "</TITLE></HEAD>\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");

 send(client, buf, strlen(buf), 0);

 sprintf(buf, "</BODY></HTML>\r\n");

 send(client, buf, strlen(buf), 0);
}

int main(){

    int listenfd, connfd;

    unsigned short port = 33333;

    sockaddr_in server_addr, client_addr;

    socklen_t len = sizeof(client_addr);

    pthread_t tid;

    listenfd = start(&port);

    printf("Server running on port %d\n", port);

    while(true){

        if((connfd = accept(listenfd, (sockaddr*)(&client_addr), &len)) < 0){
            error_die("accept:");
        }

        deal_request(connfd);

        close(connfd);
    }

    return 0;
}























