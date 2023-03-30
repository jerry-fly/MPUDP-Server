#include<iostream>
#include<stdio.h>
#include<time.h>
// #include<string>
#include<stdint.h>
#include <unistd.h>
#include <sys/types.h>  
#include <sys/socket.h> 
#include <arpa/inet.h>  
#include <netinet/in.h> 
//#include<WinSock2.h>
#include "UDPPackage.h"
#include<fstream>
//#include<windows.h>
using namespace std;

int sockClient;
struct sockaddr_in addrSrv;
//状态
int state = 0;
//滑动窗口
int base = 0;//值等于BUFSIZE则重新置0
//缓冲区
char recvbuf[BUFSIZE];
//统计写入量
float outputLen = 0.0;
//seq, ack
int seq = 0, ack = 0;
//pkg
UDPPackage *rpkg;
UDPPackage *spkg;

bool CONNECT = true;
//file
ofstream outfile;


int main(){
    //start
    // WSADATA wsaData;
    // WORD wVersionRequested;
    // wVersionRequested = MAKEWORD(2, 2);
    // int err = WSAStartup(wVersionRequested, &wsaData);
    // if (err != 0) {
    //     printf("[log] WSAStartup failed with error: %d\n", err);
    //     return 1;
    // }else{
    //     printf("[log] WSAStartup Success\n");
    // }
    //socket
    sockClient = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockClient < 0) {
        printf("[log] socket function failed with error: %s\n", strerror(errno));
        //WSACleanup();
        return 1;
    }else{
        printf("[log] socket function Success\n");
    }

    addrSrv.sin_family = AF_INET;
    addrSrv.sin_addr.s_addr = inet_addr("10.0.0.59");//10.0.0.59 12.1.1.2
    addrSrv.sin_port = htons(40000);

    //init
    rpkg = new UDPPackage(); 
    initUDPPackage(rpkg);//收
    spkg = new UDPPackage(); 
    initUDPPackage(spkg);//发

    socklen_t len = sizeof(struct sockaddr);
    int recvret = -1;
    memset(recvbuf, 0, sizeof(recvbuf));

    //file handle
    printf("==============================================================\n");
    printf("[input] input your output file path(output/*): \n");
    #if debug
        string str = "output/"+debug_filename;
        strcpy(outfilename, str.c_str());
    #else
        scanf("%s", outfilename);
    #endif
    printf("==============================================================\n");
    outfile.open(outfilename, ofstream::out | ios::binary | ios::app);
    if (!outfile){
        printf("[log] open file error, file name:%s\n", outfilename);
        return 1;
    }

    spkg->FLAG = SYN;
    spkg->seq = seq; seq = (seq+1)%SEQMAX;
    spkg->WINDOWSIZE = N;//当前接收窗口大小通告
    sendto(sockClient, (char*)spkg, sizeof(*spkg), 0, (struct sockaddr *)&addrSrv, len);
    printf("[log] client to server SYN, seq=%d\n", spkg->seq);
    while(CONNECT){
        switch (state){
            case 0://等待回复SYNACK，收到发送ACK
                #if debug
                    printf("state 0:\n");
                #endif
                recvret = recvfrom(sockClient, (char*)rpkg, sizeof(*rpkg), 0, (struct sockaddr *)&addrSrv, &len);
                if (recvret < 0){
                    printf("[log] client recvfrom fail\n");
                    CONNECT = false;
                    state = 0;
                }
                else{
                    ack = (rpkg->seq + 1)%SEQMAX;
                    printf("[log] server to client SYN,ACK, seq=%d,ack=%d\n",rpkg->seq,rpkg->ack);
                    initUDPPackage(rpkg);
                    initUDPPackage(spkg);
                    spkg->FLAG = ACK;
                    spkg->seq = seq; //ACK不消耗seq， 下一个仍为当前seq
                    spkg->ack = ack;
                    sendto(sockClient, (char *)spkg, sizeof(*spkg), 0, (struct sockaddr *)&addrSrv, len);
                    printf("[log] client to server ACK, seq=%d,ack=%d\n", spkg->seq, spkg->ack);
                    state = 1;
                }
                break;
            case 1: //接收 初始ack = 1
                #if debug
                    // printf("state 1 recv:\n");
                #endif
                recvret = recvfrom(sockClient, (char *)rpkg, sizeof(*rpkg), 0, (struct sockaddr *)&addrSrv, &len);
                if (recvret <= 0){
                    printf("recvfrom fail\n");
                }
                else if(rpkg->FLAG != FIN){
                    if (rpkg->seq == ack && !checksumFunc(rpkg, rpkg->Length + UDPHEADLEN)){
                        printf("[log] server to client file data, seq=%d, checksum=0x%x, Send Slide Window Size=%d\n",
                                    rpkg->seq, rpkg->Checksum, rpkg->WINDOWSIZE);

                        ack = (rpkg->seq + 1) % SEQMAX;

                        memcpy(recvbuf + base*PACKSIZE, (char*)rpkg, sizeof(*rpkg));
                        outfile.write(rpkg->data, rpkg->Length);
                        outputLen += (float)rpkg->Length;

                        base = (base + 1) % BUFNUM;//base下标值从0开始，ack从1开始

                        //回复ACK
                        initUDPPackage(spkg);
                        spkg->FLAG = ACK;
                        spkg->seq = seq;
                        spkg->ack = ack - 1;//期望的下一个ack-1
                        sendto(sockClient, (char *)spkg, sizeof(*spkg), 0, (struct sockaddr *)&addrSrv, len);
                        printf("[log] client to server ACK, ack=%d\n", spkg->ack);
                    }else if(rpkg->seq > ack){/*ack not change*/}
                }else{
                    state = 2;
                    continue;
                }//if-elif-else
                break;
            case 2: //挥手，回复ACK
                #if debug
                    printf("state 2:\n");
                #endif
                printf("[log] server to client FIN, seq=%d\n", rpkg->seq);
                initUDPPackage(spkg);
                spkg->FLAG = FINACK;
                spkg->seq = seq; seq = (seq+1)%SEQMAX;
                spkg->ack = ack;
                sendto(sockClient, (char *)spkg, sizeof(*spkg), 0, (struct sockaddr *)&addrSrv, len);
                printf("[log] client to server ACK, seq=%d,ack=%d\n", spkg->seq, spkg->ack);

                state = 3;
                break;
            case 3: //收到ACK，断开连接
                #if debug
                    printf("state 3 close:\n");
                #endif
                recvret = recvfrom(sockClient, (char*)rpkg, sizeof(*rpkg), 0, (struct sockaddr*)&addrSrv, &len);
                if (recvret < 0){
                    printf("[log] client recvfrom fail and closed\n");
                    CONNECT = false;
                    state = 0;
                }
                else{
                    printf("[log] server to client ACK, seq=%d, ack=%d\n", rpkg->seq, rpkg->ack);
                    CONNECT = false;
                    state = 0;
                }
                break;
            default:
                break;
        }//switch
    }//while

    printf("[log] outputLen=%fMB\n", outputLen/1048576.0);
    outfile.close();
    //system("pause");
    //pause();
    close(sockClient);
    printf("[log] client socket closed\n");
    //WSACleanup();
    //printf("[log] WSA cleaned\n");
    printf("==============================================================\n");

    return 0;
}
