#include <stdio.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <dirent.h>

int WORKERS = 30;
int WORKER_BUSY = 1;
int WORKER_IDLE = 2;
int MAX_LEN_FILE = 256;
int MAX_LEN_ZAP = 512;
int MAX_BUF = 1024;

char WWW[512];
char DEFAULT_PAGE[256];

typedef struct worker_info{
    int pid;
    int status;
} WORKER_INFO;


int worker(int listener, int localsock);
pid_t hire_worker();
void fire_worker(pid_t pid);
int Send_data(int client, char *buffer);
int zapros(char * zap_str,char * file_res);
char* anti_registr(char *file_name);
int reader(FILE *fp,char *buf);

int worker(int listener, int localsock)
{
    int sock, r;
    char buf[MAX_BUF];
    int bytes_read;

    WORKER_INFO msg;
    signal(SIGPIPE, SIG_IGN); // отлавливаем записи в уже закрытый сокет
    msg.pid = getpid();

    for (;;)
    {
        /*
        Когда сервер готов обслужить очередной запрос, он использует функцию accept. Функция ACCEPT создаёт для общения с клиентом новый сокет и возвращает его дескриптор.
        1й параметр задаёт слушающий сокет. После вызова он остаётся в слушающем состоянии и может принимать другие соединения.
        2й параметр - записывается адрес сокета клиента, который установил соединение с сервером.
        3й параметр - записывается размер структуры; функция accept записывает туда длину, которая реально была использована.
        Если вас не интересует адрес клиента, вы можете просто передать NULL в качестве второго и третьего параметров.
        */
        sock = accept(listener, NULL, NULL);
        if ( sock == -1) {
            perror("accept\n");
            continue;
        };

        close(listener);
        msg.status = WORKER_BUSY;

        /*
        1 - дескриптор созданного ранее коммуникационного узла.
        2 - адрес области памяти, начиная с которого будет браться информация для передачи или размещаться принятая информация.
        3 - количество байт, которое должно быть передано.
        4 - флаги
        5, 6 - структура с адресом сокета получателя информации и ее размер.
        */
        r = sendto(localsock, &msg, sizeof(WORKER_INFO), 0, NULL, 0);
        if (r == -1) {
            perror("sendto\n");
        };

        /*
        Для чтения данных из сокета используется функция RECV.
        1й параметр - принимает дескриптор сокета
        2й - указатель на буфер с данными
        3й - длина буфера в байтах
        4й - набор битовых флагов, управляющих работой функции (если флаги не используются, передайте функции 0)
        Функция recv возвращает количество прочитанных байтов, которое может быть меньше размера буфера. Существует ещё один особый случай, при котором recv возвращает 0. Это означает, что соединение было разорвано.
        */
        bytes_read = recv(sock, buf, MAX_BUF, 0);
        if (bytes_read <= 0) {
            printf("recv\n");
            goto error;
        };
        printf("%s%s\n", "-> ", buf);
        Send_data(sock, buf);

        /*
        r = send(sock, buf, bytes_read, 0);
        if ( r <= 0) {
           printf("send\n");
           goto error;
        };
        */

error:
        close(sock);
        msg.status = WORKER_IDLE;

        r = sendto(localsock, &msg, sizeof(WORKER_INFO), 0, NULL, 0);
        if ( r == -1 ) {
            perror("sendto\n");
        };

        /* Ожидание пока менеджер не уволит */
        while(1)
            sleep(1);
    };
}

int localsocks[2], listener;

/* Нанимаем рабочего */
pid_t hire_worker()
{
    pid_t pid;
    pid = fork();
    switch (pid) {
        case -1:
            perror("fork\n");
            break;
        case 0:/* дочерний процесс */
            worker(listener, localsocks[1]);
            break;
        default: /* родительский процесс */
            printf("worker (pid = %d) hired\n", pid);
            break;
    };
    return pid;
}

/* Увольняем рабочего */
void fire_worker(pid_t pid)
{
    kill(pid, SIGTERM); //SIGTERM - сигнал, для запроса завершения процесса. Этот сигнал может быть обработан или проигнорирован программой.
    printf("worker (pid = %d) fired\n", pid);
}

/* Обрабатываем полученные данные и отправляем ответ */
int Send_data(int client, char *buffer)
{
    FILE *fp;
    int len, i;
    char file_res[MAX_LEN_FILE];

    if(zapros(buffer,file_res)== 1)/* проверяем правильность URL. Если правильно, то формируется путь к файлу*/
    {
        if((fp=fopen(anti_registr(file_res),"r"))!=NULL)/* пытаемся открыть файл, по полученному пути из функции anti_registr*/
        {
            i=0;
            strcpy(buffer,"HTTP/1.1 200 OK \n");
            printf("%s%s","<- ", buffer);
            printf("%s \n", file_res);
            send(client,buffer,strlen(buffer),0);
            while((len=reader(fp,buffer))>0) /* читаем из файла */
            {
                /*
                Функция SEND используется для отправки данных.
                1й параметр - принимает дескриптор сокета
                2й - указатель на буфер с данными
                3й - длина буфера в байтах
                4й - набор битовых флагов, управляющих работой функции (если флаги не используются, передайте функции 0)
                Функция send возвращает число байтов, которое на самом деле было отправлено (или -1 в случае ошибки). Это число может быть меньше указанного размера буфера.
                */
                send(client,buffer,len,MSG_NOSIGNAL);
            }
            fclose(fp);
        }
        else
        {
            bzero(buffer,MAX_BUF);
            strcpy(buffer,"HTTP/1.1 404 ERROR");
            printf("%s%s \n","<- ", buffer);
            send(client,buffer,MAX_BUF,MSG_NOSIGNAL);
            bzero(buffer,MAX_BUF);
            strcpy(buffer,"File not found \n");
            send(client,buffer,MAX_BUF,MSG_NOSIGNAL);
        }
    }
    close(client);
}

/* Получаем строку запроса и извлекаем из нее URL. Если он правильно сформирован, то отправляем 1 и формируем путь к файлу, иначе отправляем 0.*/
int zapros(char * zap,char * file_res)
{
    int i=0, k=0;
    int flag = 0 ;
    char buf[MAX_LEN_FILE];
    char zap_str[256];
    i=0;
    strcat(zap_str, zap);
    while((*(zap_str+i)!='/')&&(*(zap_str+i)!='\n')&&(i<MAX_LEN_ZAP))
        i++;
    if(*(zap_str+i)=='/')
    {
        while((*(zap_str+i)!=' ')&&(*(zap_str+i)!='\n')&&(k<MAX_LEN_FILE))
        {
            *(file_res+k)=*(zap_str+i);
            k++;
            i++;
        }
        *(file_res+k)='\0';
        if(k<MAX_LEN_FILE)
        {
            if(strlen(file_res)==1)
                strcat(file_res,DEFAULT_PAGE);
            flag = 1;
            bzero(buf,sizeof(buf));
            strcat(buf,WWW);
            strcat(buf,file_res);
            strcpy(file_res,buf);
        }
    }
    return flag;
}

/* Получаем путь к файлу. Извлекаем путь к последней папке и пытаемся открыть директорию. Далее просматриваем все файлы в директории и сравниваем их имена с именем нашего файла (в нижнем регистре) */
char* anti_registr(char *file_name)
{
    DIR * d;
    struct dirent * dp;
    int i, k, flag;
    char buf[MAX_LEN_FILE], temp_name[MAX_LEN_FILE];

    bzero(buf,sizeof(buf));
    bzero(temp_name,sizeof(temp_name));
    strcpy(buf,file_name);

    for(i=0;i<strlen(file_name)-1;i++)
    {
        if(buf[strlen(file_name)-1-i]=='/')
        {
            buf[strlen(file_name)-1-i]='\0';
            break;
        };
    }

    if ((d= opendir (buf))!=NULL)
    {
        buf[strlen(file_name)-1-i]='/';
        buf[strlen(file_name)-i]='\0';
        while ((dp=readdir(d))!=NULL)
        {
            strcat(temp_name,buf);
            strcat(temp_name,dp->d_name);

            flag=0;
            if(strlen(file_name)==strlen(temp_name))
            {
                for(k=0;k<strlen(temp_name);k++)
                {
                    if(tolower(temp_name[k])!=tolower(file_name[k]))
                    {
                        flag=1;
                        break;
                    }
                }
                if(flag!=1)
                {
                    strcpy(file_name,temp_name);
                    break;
                }
            }
            bzero(temp_name,sizeof(temp_name));
        }
        closedir(d);
    }
    return file_name;
}

/* Чтение из файла */
int reader(FILE *fp,char *buf)
{
    int ch,len=0;

    while (len<MAX_BUF)
    {
        if ((ch=getc(fp))==EOF)
            break;
        *(buf+len)=ch;
        len++;
    }
    if(ch==EOF)
        *(buf+len)=ch;
    return len;
}


int main()
{
    struct sockaddr_in addr;
    int r, pid, p;
    int total_workers = WORKERS;
    strcpy(WWW, "/home/galya/Files");
    strcpy(DEFAULT_PAGE, "index.html");

    /*
        Для создания сокета используется функция SOCKET. С каждым сокет связываются три атрибута: домен, тип и протокол.
        Домен определяет пространство адресов, в котором располагается сокет, и множество протоколов, которые используются для передачи данных. Чаще других используются домены Unix и Internet, задаваемые константами AF_UNIX и AF_INET.
        Тип сокета определяет способ передачи данных по сети. Чаще других применяются: SOCK_STREAM. Передача потока данных с предварительной установкой соединения. Обеспечивается надёжный канал передачи данных, при котором фрагменты отправленного блока не теряются, не переупорядочиваются и не дублируются.
        Последний атрибут определяет протокол, используемый для передачи данных. Часто протокол однозначно определяется по домену и типу сокета. В этом случае в качестве третьего параметра функции socket можно передать 0
    */
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if(listener < 0)
    {
        perror("socket");
        exit(1);
    }

    /*
        Структура с адресом для передачи в функцию bind (in - обозначает домен internet)
        struct sockaddr_in {
            short int sin_family;  // Семейство адресов
            unsigned short int sin_port;    // Номер порта
            struct in_addr     sin_addr;    // IP-адрес. Если вы готовы соединяться с клиентами через любой интерфейс, задайте в качестве адреса константу INADDR_ANY.
        };
    */
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = INADDR_ANY;

    /*
        Для явного связывания сокета с некоторым адресом используется функция BIND.
        В качестве первого параметра передаётся дескриптор сокета, который мы хотим привязать к заданному адресу.
        Второй параметр содержит указатель на структуру с адресом.
        Третий - длину этой структуры.
    */
    if(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(2);
    }

    /*
        Cоздаётся очередь запросов на соединение. При этом сокет переводится в режим ожидания запросов со стороны клиентов. Всё это выполняет функция LISTEN.
        Первый параметр - дескриптор сокета.
        Второй задаёт размер очереди запросов.
        Каждый раз, когда очередной клиент пытается соединиться с сервером, его запрос ставится в очередь, так как сервер может быть занят обработкой других запросов. Если очередь заполнена, все последующие запросы будут игнорироваться.
    */
    listen(listener, 1);

    /*
        Создаем пару сокетов для внутреннего использования
        1 - область, где будет создана пара сокетов (домен)
        2 - тип сокетов
        3 - протокол (0 - по умолчанию)
    */
    r = socketpair(AF_LOCAL, SOCK_DGRAM, 0 , localsocks);
    if ( r == -1 ) {
            perror("socketpair\n");
            exit(1);
        }

    /*Создаем рабочих */
    for (p = 0; p < WORKERS; p++) {
            pid = hire_worker();
        }

    /* Менеджер */
    signal(SIGCHLD, SIG_IGN); // Обработчик сигнала SIGCHLD. SIGCHLD - ядро посылает сигнал процессу, когда дочерний процесс либо умирает, либо меняет свое состояние. (для борьбы с зомби)
    for (;;) {
        WORKER_INFO msg;
        sleep(1);

        /* Проверяем было ли увольнение */
        if ( total_workers < WORKERS) {
            for (p = 0; p < (WORKERS - total_workers); p++)
                pid = hire_worker();
            total_workers = WORKERS;
        };

        while (1){
            /*
                1 - сокет
                2 - то, что передаем
                3 - размер сообщения
                4 - флаг - MSG_DONTWAIT - функция возвращает значение даже, если сокет заблокирован
                5 - структура с адресом
                6 - размер структуры
                Если сокет не является ориентированным на соединения, то 5 и 6 NULL.
            */
            r = recvfrom(localsocks[0], &msg, sizeof(WORKER_INFO), MSG_DONTWAIT, NULL, NULL);
            if ( r == -1 ) {
                if (errno != EAGAIN) // ошибка в сокете
                    perror("recvfrom");
                    break;
            };

            if (msg.status == WORKER_IDLE) {
                total_workers--;
                fire_worker(msg.pid);
            }

        };

        printf("workers: = %d\n", total_workers);
        };
    close(listener);
    return 0;
}
