#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wait.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int MAX_BUF = 1024;
char* message[4]= {"GET /6.html HTTP/1.1 \n",
                   "GET / HTTP/1.1 \n",
                   "GET /one.html HTTP/1.1 \n",
                   "GET /folder/two.html HTTP/1.1\n",
                  };
char buf[1024];

int main()
{
    int i, pid[3];
    int sock;

    for (i=0; i<4; i++)
    {
        struct sockaddr_in addr;
        /*
            Для создания сокета используется функция SOCKET. С каждым сокет связываются три атрибута: домен, тип и протокол.
            Домен определяет пространство адресов, в котором располагается сокет, и множество протоколов, которые используются для передачи данных. Чаще других используются домены Unix и Internet, задаваемые константами AF_UNIX и AF_INET.
            Тип сокета определяет способ передачи данных по сети. Чаще других применяются: SOCK_STREAM. Передача потока данных с предварительной установкой соединения. Обеспечивается надёжный канал передачи данных, при котором фрагменты отправленного блока не теряются, не переупорядочиваются и не дублируются.
            Последний атрибут определяет протокол, используемый для передачи данных. Часто протокол однозначно определяется по домену и типу сокета. В этом случае в качестве третьего параметра функции socket можно передать 0
        */
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0)
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
        addr.sin_port = htons(8080);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);/* Для отладки сетевой программы, если под рукой нет сети, достаточно запустить клиента и сервера на одной машине, а затем использовать для соединения адрес интерфейса внутренней петли (loopback interface). В программе ему соответствует константа INADDR_LOOPBACK (не забудьте применять к ней функцию htonl). Пакеты, направляемые по этому адресу, в сеть не попадают. Вместо этого они передаются стеку протоколов TCP/IP как только что принятые. Таким образом моделируется наличие виртуальной сети, в которой вы можете отлаживать ваши сетевые приложения.*/
   
        /*
            На стороне клиента для установления соединения используется функция CONNECT
            1й параметр -сокет, который будет использоваться для обмена данными с сервером
            2й - содержит указатель на структуру с адресом сервера
            3й - длину этой структуры.
            Обычно сокет не требуется предварительно привязывать к локальному адресу, так как функция connect сделает это за вас, подобрав подходящий свободный порт.
        */
        if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            perror("connect");
            exit(2);
        }

        /*
            Функция SEND используется для отправки данных.
            1й параметр - принимает дескриптор сокета
            2й - указатель на буфер с данными
            3й - длина буфера в байтах
            4й - набор битовых флагов, управляющих работой функции (если флаги не используются, передайте функции 0)
            Функция send возвращает число байтов, которое на самом деле было отправлено (или -1 в случае ошибки). Это число может быть меньше указанного размера буфера.
        */
        pid[i]=fork();
        switch(pid[i])
        {
            case -1:
                perror("fork");
                break;

            case 0:
                send(sock, message[i], MAX_BUF, 0);
                printf("%d%s%s \n",getpid(pid[i]),"<- ", message[i]);

                recv(sock, buf, MAX_BUF, 0);
                printf("%d%s%s \n",getpid(pid[i]),"-> ", buf);

                int read_bytes=1;
                while (read_bytes > 0)
                {
                    bzero(buf,sizeof(buf));
                    read_bytes=recv(sock, buf, MAX_BUF, 0);
                    printf("%s \n", buf);
                }

                close(sock);

                exit(0);
                break;
        }
    }
    int status;

    for (i=0; i<10; i++)
        waitpid(pid[i], &status, 0);
    close(sock);
    return 0;
}
