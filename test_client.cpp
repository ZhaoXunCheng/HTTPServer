#include"unp.h"

int main(int argc, char** argv){
    if(argc != 2){
        cout << "Invalid input.\n";
        exit(1);
    }

    int connfd;
    sockaddr_in server_addr;
    unsigned short port = 35977;
    char buf[1024];
    int n;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        cout << "error in socket.\n";
        exit(1);
    }
    sleep(1);
    if(connect(connfd, (sockaddr*)(&server_addr), sizeof(server_addr)) < 0){
        cout << "error in connect.\n";
        exit(1);
    }

    while(true){
        if(fgets(buf, sizeof(buf), stdin) == NULL){
            return 0;
        }
        send(connfd, buf, strlen(buf), 0);
        shutdown(connfd, SHUT_WR);
        while(true){
            n = recv(connfd, buf, sizeof(buf), 0);
            write(fileno(stdout), buf, n);
        }
        break;
    }
    return 0;
}
