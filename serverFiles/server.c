#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <dirent.h>
#include "myqueue.h"

#define SERVERPORT 8989
#define BUFSIZE 5096
#define SOCKETERROR (-1)
#define SERVER_BACKLOG 100
#define 	MAXLINE 	4096
#define		LISTENQ		1024
#define		TRUE		1
#define		FALSE		0

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void * handle_connection(void* p_connfd);
int check(int expo, const char *msg);
void * thread_function(void *arg);
int LISTENFD;
int clientsize,serversize;
int activeUsers =0, onHold = 0;

//function trims leading and trailing whitespaces
void trim(char *str)
{

	int i;
    int begin = 0;

    int end = strlen(str) - 1;

    while (isspace((unsigned char) str[begin]))
        begin++;

    while ((end >= begin) && isspace((unsigned char) str[end]))
        end--;

    // Shift all characters back to the start of the string array.
    for (i = begin; i <= end; i++)
        str[i - begin] = str[i];

    str[i - begin] = '\0'; // Null terminate string.
}

int get_client_ip_port(char *str, char *client_ip, int *client_port){
	char *n1, *n2, *n3, *n4, *n5, *n6;
	int x5, x6;

	strtok(str, " ");
	n1 = strtok(NULL, ",");
	n2 = strtok(NULL, ",");
	n3 = strtok(NULL, ",");
	n4 = strtok(NULL, ",");
	n5 = strtok(NULL, ",");
	n6 = strtok(NULL, ",");

	sprintf(client_ip, "%s.%s.%s.%s", n1, n2, n3, n4);

	x5 = atoi(n5);
	x6 = atoi(n6);
	*client_port = (256*x5)+x6;

	printf("client_ip: %s client_port: %d\n", client_ip, *client_port);
	return 1;
}

int setup_data_connection(int *fd, char *client_ip, int client_port, int server_port){

	struct sockaddr_in cliaddr, tempaddr;

	if ( (*fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    	perror("socket error");
    	return -1;
    }

	//bind port for data connection to be server port - 1 by using a temporary struct sockaddr_in
	bzero(&tempaddr, sizeof(tempaddr));
    tempaddr.sin_family = AF_INET;
    tempaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    tempaddr.sin_port   = htons(server_port-1);

    while((bind(*fd, (struct sockaddr*) &tempaddr, sizeof(tempaddr))) < 0){
    	//perror("bind error");
    	server_port--;
    	tempaddr.sin_port   = htons(server_port);
    }


	//initiate data connection fd with client ip and client port
    bzero(&cliaddr, sizeof(cliaddr));
    cliaddr.sin_family = AF_INET;
    cliaddr.sin_port   = htons(client_port);
    if (inet_pton(AF_INET, client_ip, &cliaddr.sin_addr) <= 0){
    	perror("inet_pton error");
    	return -1;
    }

    if (connect(*fd, (struct sockaddr *) &cliaddr, sizeof(cliaddr)) < 0){
    	perror("connect error");
    	return -1;
    }

    return 1;
}

int get_filename(char *input, char *fileptr){

    char *filename = NULL;
    filename = strtok(input, " ");
    filename = strtok(NULL, " ");
    if(filename == NULL){
        return -1;
    }else{
    	strncpy(fileptr, filename, strlen(filename));
        return 1;
    }
}

int get_command(char *command){
	char cpy[1024];
	strcpy(cpy, command);
	char *str = strtok(cpy, " ");
	int value;

	//populated value valriable to indicate back to main which input was entered
    if(strcmp(str, "LIST") == 0){value = 1;}
    else if(strcmp(str, "RETR") == 0){value = 2;}
    else if(strcmp(str, "STOR") == 0){value = 3;}
    else if(strcmp(str, "SKIP") == 0){value = 4;}
    else if(strcmp(str, "ABOR") == 0){value = 5;}

    return value;
}

int do_list(int controlfd, int datafd, char *input){
	char filelist[1024], sendline[MAXLINE+1], str[MAXLINE+1];
	bzero(filelist, (int)sizeof(filelist));

	if(get_filename(input, filelist) > 0){
		printf("Filelist Detected\n");
		sprintf(str, "ls %s", filelist);
		printf("Filelist: %s\n", filelist);
		trim(filelist);
		//verify that given input is valid
		/*struct stat statbuf;
		stat(filelist, &statbuf);
		if(!(S_ISDIR(statbuf.st_mode))) {
			sprintf(sendline, "550 No Such File or Directory\n");
    		write(controlfd, sendline, strlen(sendline));
    		return -1;
		}*/
    	DIR *dir = opendir(filelist);
    	if(!dir){
    		sprintf(sendline, "550 No Such File or Directory\n");
    		write(controlfd, sendline, strlen(sendline));
    		return -1;
    	}else{closedir(dir);}

	}else{
		sprintf(str, "ls");
	}

	 //initiate file pointer for popen()
    FILE *in;
    extern FILE *popen();

    if (!(in = popen(str, "r"))) {
    	sprintf(sendline, "451 Requested action aborted. Local error in processing\n");
    	write(controlfd, sendline, strlen(sendline));
        return -1;
    }

    while (fgets(sendline, MAXLINE, in) != NULL) {
        write(datafd, sendline, strlen(sendline));
        printf("%s", sendline);
        bzero(sendline, (int)sizeof(sendline));
    }

    sprintf(sendline, "200 Command OK");
    write(controlfd, sendline, strlen(sendline));
    pclose(in);

    return 1;
}

int do_retr(int controlfd, int datafd, char *input){
	char filename[1024], sendline[MAXLINE+1], str[MAXLINE+1];
	bzero(filename, (int)sizeof(filename));
	bzero(sendline, (int)sizeof(sendline));
	bzero(str, (int)sizeof(str));


	if(get_filename(input, filename) > 0){
		sprintf(str, "cat %s", filename);

		if((access(filename, F_OK)) != 0){
			sprintf(sendline, "550 No Such File or Directory\n");
    		write(controlfd, sendline, strlen(sendline));
    		return -1;
		}
	}else{
		printf("Filename Not Detected\n");
		sprintf(sendline, "450 Requested file action not taken.\nFilename Not Detected\n");
    	write(controlfd, sendline, strlen(sendline));
		return -1;
	}

	FILE *in;
    extern FILE *popen();

    if (!(in = popen(str, "r"))) {
    	sprintf(sendline, "451 Requested action aborted. Local error in processing\n");
    	write(controlfd, sendline, strlen(sendline));
        return -1;
    }

    while (fgets(sendline, MAXLINE, in) != NULL) {
        write(datafd, sendline, strlen(sendline));
        //printf("%s", sendline);
        bzero(sendline, (int)sizeof(sendline));
    }

    sprintf(sendline, "200 Command OK");
    write(controlfd, sendline, strlen(sendline));
    pclose(in);
    return 1;
}

int do_stor(int controlfd, int datafd, char *input){
	char filename[1024], sendline[MAXLINE+1], recvline[MAXLINE+1], str[MAXLINE+1], temp1[1024];
	bzero(filename, (int)sizeof(filename));
	bzero(sendline, (int)sizeof(sendline));
	bzero(recvline, (int)sizeof(recvline));
	bzero(str, (int)sizeof(str));

	int n = 0, p = 0;

	if(get_filename(input, filename) > 0){
		sprintf(str, "%s-rcv", filename);
	}else{
		printf("Filename Not Detected\n");
		sprintf(sendline, "450 Requested file action not taken.\n");
    	write(controlfd, sendline, strlen(sendline));
		return -1;
	}

	sprintf(temp1, "%s-rcv", filename);
	FILE *fp;
    if((fp = fopen(temp1, "w")) == NULL){
        perror("file error");
        return -1;
    }


    while((n = read(datafd, recvline, MAXLINE)) > 0){
        fseek(fp, p, SEEK_SET);
        fwrite(recvline, 1, n, fp);
        p = p + n;
        //printf("%s", recvline);
        bzero(recvline, (int)sizeof(recvline));
    }

    sprintf(sendline, "200 Command OK");
    write(controlfd, sendline, strlen(sendline));
    fclose(fp);
    return 1;
}


int main(int argc, char **argv)
{


  int listenfd, connfd, addr_size, port, threadsize, ipnumber;
  SA_IN server_addr, client_addr;

  sscanf(argv[6], "%d", &port);
  sscanf(argv[4], "%d", &ipnumber);
  sscanf(argv[2], "%d", &threadsize);

	if (threadsize!= 0 && port!=0){
		pthread_t thread_pool[threadsize];
		for (int i=0; i<threadsize; i++) {

			pthread_create(&thread_pool[i], NULL, thread_function, NULL);
		}

		check((listenfd = socket(AF_INET, SOCK_STREAM, 0 )),
					"Failed to create socket");
					//Iniciar addres struct
	  server_addr.sin_family = AF_INET;
	  server_addr.sin_addr.s_addr = INADDR_ANY;
	  server_addr.sin_port = htons(port);
		check(bind(listenfd,(SA*)&server_addr, sizeof(server_addr)),
	    "Bind Failed!");
	  check(listen(listenfd, SERVER_BACKLOG),
	    "Listen Failed!");


    while(true){



      addr_size = sizeof(SA_IN);


			if(onHold == 0){
				printf("Waiting for connections... \n");
				check(connfd =
					accept(listenfd, (SA*)&client_addr, (socklen_t*)&addr_size),
						"accept failed");
			}

			if(activeUsers < threadsize){
				onHold = 0;
				printf("A new client just connected! \n");


	      LISTENFD == listenfd;
	      int *pclient = malloc(sizeof(int));
	      *pclient = connfd;
	      pthread_mutex_lock(&mutex);
	      enqueue(pclient);
	      pthread_mutex_unlock(&mutex);

			}

			else{
				int cola = activeUsers-threadsize+1;
				if(onHold == 0){
					printf("\n Server overloaded\n");
				}
				onHold=1;
			}


    } //fin de while

    return 0;
}

}



int check(int exp, const char *msg) {
  if(exp == SOCKETERROR) {
    perror(msg);
    exit(1);
  }
  return exp;
}

void * thread_function(void *arg){
  while (true){
    int *pclient;
    pthread_mutex_lock(&mutex);
    pclient = dequeue();
    pthread_mutex_unlock(&mutex);

    if (pclient != NULL){
      handle_connection(pclient);
			activeUsers--;
    }
  }
	pthread_exit(NULL);
}


void * handle_connection(void* p_connfd){
  int connfd = *((int*)p_connfd);
  pid_t pid;

  free(p_connfd);
  //child process---------------------------------------------------------------
  if((pid = fork()) == 0){
    close(LISTENFD);

    int datafd, code, x = 0, client_port = 0;
    char recvline[MAXLINE+1];
    char client_ip[50], command[1024];




    while(1){
      bzero(recvline, (int)sizeof(recvline));
      bzero(command, (int)sizeof(command));

      //get client's data connection port
        if((x = read(connfd, recvline, MAXLINE)) < 0){
          break;
        }
        printf("*****************\n%s \n", recvline);
              if(strcmp(recvline, "QUIT") == 0){
                  printf("Quitting...\n");
                  char goodbye[1024];
                  sprintf(goodbye,"221 Goodbye");
                  write(connfd, goodbye, strlen(goodbye));
                  close(connfd);
                  break;
              }
        get_client_ip_port(recvline, client_ip, &client_port);

        if((setup_data_connection(&datafd, client_ip, client_port, 8989)) < 0){ //aqui el port
          break;
        }

        if((x = read(connfd, command, MAXLINE)) < 0){
          break;
        }

        printf("-----------------\n%s \n", command);

        code = get_command(command);
        if(code == 1){
          do_list(connfd, datafd, command);
        }else if(code == 2){
          do_retr(connfd, datafd, command);
        }else if(code == 3){
          do_stor(connfd, datafd, command);
        }else if(code == 4){
                  char reply[1024];
                  sprintf(reply, "550 Filename Does Not Exist");
                  write(connfd, reply, strlen(reply));
                  close(datafd);
                  continue;
              }

        close(datafd);


    }
      printf("Exiting Child Process...\n");
      close(connfd);
      _exit(1);
  }
  //end child process-------------------------------------------------------------
  close(connfd);



}
