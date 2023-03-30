#include<iostream>
#include<fstream>
#include<stdio.h>
#include<time.h>
//#include<string>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>  
#include <sys/time.h>
#include <sys/socket.h> 
#include <arpa/inet.h>  
#include <netinet/in.h>
#include <pthread.h>  
#include <cmath>
//#include <stdint.h>
//#include<WinSock2.h>
#include "UDPPackage.h"
//定时器库
#include "assertions.h"
#include "queue.h"
#include "timer.h"

using namespace std;


typedef unsigned long      DWORD;
//typedef unsigned short     WORD;
//typedef atomic<int>         atomic_int32_t;
//typedef pthread_t           HANDLE;

int sockSrv;
struct sockaddr_in addrClient;
//状态
int state;
//ms
DWORD RTO = 3000;
//thread
//HANDLE hThread;
//HANDLE hCheckFinThread;
//DWORD dwThreadId;
pthread_t dwThreadId;
//DWORD dwCheckFinThread;
pthread_t dwCheckFinThread;
//滑动窗口
int base;//值等于BUFSIZE则重新置0
int old_base;
// int nextseq = 0;
//缓冲区
char sendbuf[BUFSIZE];
//是否传输结束
bool isEnd;
int timerCount;

bool CONNECT = true;
int recvret = -1;
int sendret = -1;
//读取的文件总大小
int file_len = 0;
//rpkg,spkg
UDPPackage *rpkg;
UDPPackage *spkg;
//进入state 3 后，seq始终位于窗口上限的后一位置
uint32_t seq = 1, ack = 0;
// buf索引
int buf_idx = 0;

//读入的文件
char *file_data;
// file data has sent
int sent_offset = 0;

//定时器
//HANDLE hTimerQueue;
pthread_t timer_ppid;
long hTimer[TIMER_MAX];//发送每个报文序号时创建这个序号对应的定时器
bool timer_valid[TIMER_MAX];//false时定时器无效，不起作用
//use for bufferIsEnd
int step_n = 0;



static void* TimerRoutine(void* lpParam);
void*  sendThread(void* lparam);
void*  checkFinThread(void* lparam);

// void *signal_sender_task(void *args){
//   pthread_detach(pthread_self());
//   long time_id[2];
//   for(int i=0;i<2;i++){
//     time_id[i]=timer_start(10, 0, true, NULL, NULL);
//   }
// }
void *signal_receiver_task(void *args){
  pthread_detach(pthread_self());
  int end=0;
  while (end == 0) {
    signal_handle(&end);
  }
  exit(0);
}

int main(){
    //start
    //int end=0;
    //CHECK_INIT_RETURN(timer_init());
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
    sockSrv = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockSrv < 0) {
        printf("[log] socket function failed with error: %s\n", strerror(errno));
        //WSACleanup();
        return 1;
    }else{
        printf("[log] socket function Success\n");
    }

    int addrlen = sizeof(struct sockaddr_in);
    struct sockaddr_in addrSrv;
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_addr.s_addr = inet_addr("10.0.0.1");
    addrSrv.sin_port = htons(40000);

    //bind
    int iResult = bind(sockSrv, (struct sockaddr*)&addrSrv, sizeof(struct sockaddr));
    if (iResult < 0) {
        printf("[log] bind failed with error %s\n", strerror(errno));
        close(sockSrv);
        //WSACleanup();
        return 1;
    }else {
        printf("[log] bind returned success\n");
    }
    printf("==============================================================\n");

    //init
    for (int i = 0; i < TIMER_MAX; ++i){
        timer_valid[i] = true;
    }
    timerCount = 0;
    isEnd = false;
    old_base = 0;
    base = 0;
    state = 0;
    rpkg = new UDPPackage();
    initUDPPackage(rpkg);
    spkg = new UDPPackage();
    initUDPPackage(spkg);
    socklen_t len = sizeof(struct sockaddr);
    memset(sendbuf, 0, sizeof(sendbuf));

    //creat TimerQueue
    //hTimerQueue = CreateTimerQueue();
    CHECK_INIT_RETURN(timer_init());
    //register SIGTIMER handler
    pthread_create(&timer_ppid, NULL, signal_receiver_task, NULL);
    //listening
    printf("[log] listening for client connect\n");
    int retlen = recvfrom(sockSrv, (char*)rpkg, sizeof(*rpkg), 0, (struct sockaddr*)&addrClient, &len); //收到客户端请求建连

    //读文件，file_data读到整个文件
    printf("[input] please input a infilename under dir test/* (such as: test/1.jpg): \n");
    #if debug
        string str = "resource/"+debug_filename;
        strcpy(infilename, str.c_str());
    #else
        scanf("%s", infilename);
    #endif
    ifstream ifile;
    ifile.open(infilename, ifstream::in | ios::binary);
    if (!ifile)
    {
        printf("[log] open file error\n");
        return 1;
    }
    ifile.seekg(0, ifile.end);
    file_len = ifile.tellg();
    ifile.seekg(0, ifile.beg);
    file_data = new char[file_len];
    memset(file_data, 0, sizeof(file_data));
    ifile.read(file_data, file_len);
    ifile.close();
    //bufferInit
    for (int i = 0; i < BUFNUM; ++i){
        initUDPPackage(spkg);
        spkg->seq = seq;
        seq = (seq + 1) % SEQMAX;
        spkg->Length = PACKDATASIZE < (file_len - sent_offset) ? PACKDATASIZE : (file_len - sent_offset); //传剩于文件大小和最大报文的较小值
        memcpy(spkg->data, file_data + sent_offset, spkg->Length);
        sent_offset += (int)spkg->Length;                               // sent_offset仅在这里改变
        spkg->WINDOWSIZE = N;                                           //设置发送窗口当前大小
        spkg->Checksum = checksumFunc(spkg, spkg->Length + UDPHEADLEN); //校验和最后算
        memcpy(sendbuf + i * PACKSIZE, (char *)spkg, sizeof(*spkg));
        //若文件读完了则终止
        if (sent_offset >= file_len){
            ((UDPPackage *)(sendbuf + i%BUFNUM * PACKSIZE))->FLAG = FIN;
            break;
        }
    }
    struct timeval start, end;
    float total_time;
    gettimeofday(&start, NULL);
    //create send thread
    int rc = pthread_create(&dwThreadId, NULL, sendThread, NULL);
    if (rc != 0){
        printf("{---CreateSendThread error: %s}\n", strerror(errno));
        return 1;
    }
    //CloseHandle(hThread);
    // check fin thread
    rc = pthread_create(&dwCheckFinThread, NULL, checkFinThread, NULL);
    if (rc != 0){
        printf("{---CreateCheckFinThread error: %s}\n", strerror(errno));
        return 1;
    }
    //CloseHandle(hCheckFinThread);


    if (retlen && rpkg->FLAG == SYN){
        ack = (rpkg->seq + 1)%SEQMAX;
        printf("[log] client to server SYN, seq=%d, Recv Slide Window Size=%d\n",rpkg->seq, rpkg->WINDOWSIZE);
        while (CONNECT){
            switch (state){
                case 0: //回复确认
                    #if debug
                        printf("state 0:\n");
                    #endif
                    initUDPPackage(spkg);
                    spkg->FLAG = SYNACK;
                    spkg->seq = 0; //seq = (seq+1)%SEQMAX;
                    spkg->ack = ack;
                    sendto(sockSrv, (char*)spkg, sizeof(*spkg), 0, (struct sockaddr*)&addrClient, len);
                    state = 1;
                    printf("[log] server to client SYN,ACK seq=%d,ack=%d\n", spkg->seq, spkg->ack);
                    break;
                case 1: //等待连接
                    #if debug
                        printf("state 1:\n");
                    #endif
                    recvret = recvfrom(sockSrv, (char*)rpkg, sizeof(*rpkg), 0, (struct sockaddr*)&addrClient, &len);
                    if (recvret < 0){
                        printf("[log] server recvfrom fail\n");
                        CONNECT = false;
                        state = 0;
                    }
                    else{
                        //跳转至状态2
                        if (rpkg->FLAG == ACK){
                            ack = (rpkg->seq + 1)%SEQMAX;
                            printf("[log] client to server ACK, seq=%d, ack=%d\n", rpkg->seq, rpkg->ack);
                            state = 2;
                            old_base = -N;
                        }
                    }
                    break;
                case 2://recv
                    #if debug
                        printf("state 2:\n");
                    #endif
                    recvret = recvfrom(sockSrv, (char *)rpkg, sizeof(*rpkg), 0, (struct sockaddr *)&addrClient, &len);
                    if (recvret <= 0){
                        /*do nothing*/
                        continue;
                    }
                    else if(rpkg->FLAG == FINACK){
                        state = 6;
                        continue;
                    }
                    else{
                        if (rpkg->ack >= (base+1)){//已被确认，滑动窗口
                            step_n = (rpkg->ack - base + BUFNUM) % BUFNUM;
                            //销毁 < base的timer
                            for (int del_idx = base; del_idx < (base + step_n); ++del_idx){
                                timer_remove(hTimer[del_idx%BUFNUM], NULL);
                                timer_valid[del_idx%BUFNUM] = false;
                                --timerCount;
                            }
                            printf("[log recvThread] client to server ACK, ack=%d, now base=%d\n", rpkg->ack, (int)base);
                            //read
                            if (sent_offset < file_len){
                                for (int i = 0; i < step_n; ++i){
                                    initUDPPackage(spkg);
                                    spkg->seq = seq;
                                    seq = (seq + 1) % SEQMAX;
                                    spkg->Length = PACKDATASIZE < (file_len - sent_offset) ? PACKDATASIZE : (file_len - sent_offset); //传剩于文件大小和最大报文的较小值
                                    memcpy(spkg->data, file_data + sent_offset, spkg->Length);
                                    sent_offset += (int)spkg->Length;                               // sent_offset仅在这里改变
                                    spkg->WINDOWSIZE = N;                                           //设置发送窗口当前大小
                                    spkg->Checksum = checksumFunc(spkg, spkg->Length + UDPHEADLEN); //校验和最后算
                                    memcpy(sendbuf + (base + i) * PACKSIZE, (char *)spkg, sizeof(*spkg));
                                    printf("seq=%d\n",((UDPPackage*)(sendbuf + (base + i) * PACKSIZE))->seq);
                                    //若文件读完了则终止
                                    if (sent_offset >= file_len)
                                    {
                                        initUDPPackage(spkg);
                                        spkg->FLAG = FIN;
                                        memcpy(sendbuf + ((base + i) % BUFNUM) * PACKSIZE, (char *)spkg, sizeof(*spkg));
                                        break;
                                    }
                                }
                            }
                            //base变化时窗口滑动，发送线程才会捕捉到变化
                            base = rpkg->ack % BUFNUM; // base是下标，本来就比ack和seq小1
                        }
                        else{ /*do nothing 忽略重复ack*/}
                    }
                    break;
                case 4: //传输完毕，单向传输，挥手
                    #if debug
                        printf("state 4:\n");
                    #endif
                    initUDPPackage(spkg);
                    spkg->FLAG = FIN;
                    spkg->seq = seq; seq = (seq+1)%SEQMAX;
                    sendto(sockSrv, (char*)spkg, sizeof(*spkg), 0, (struct sockaddr*)&addrClient, len);
                    printf("[log] server to client file transmit done, FileLength=%fMB, TotalTransLength=%fMB\n[log] server to client FIN, seq=%d\n",
                                (file_len*1.0)/1048576.0, (sent_offset*1.0)/1048576.0, spkg->seq);
                    state = 5;
                    break;
                case 5: //等待客户端FINACK,因为单向传输，收到客户端FINACK后直接跳转到回复ACK阶段
                    #if debug
                        printf("state 5:\n");
                    #endif
                    recvret = recvfrom(sockSrv, (char*)rpkg, sizeof(*rpkg), 0, (struct sockaddr*)&addrClient, &len);
                    if (recvret < 0){
                        printf("[log] server recvfrom fail and exit\n");
                        CONNECT = false;
                        state = 0;
                    }
                    else{
                        ack = (rpkg->seq + 1)%SEQMAX;
                        printf("[log] client to server ACK, seq=%d, ack=%d\n", rpkg->seq, rpkg->ack);
                        state = 6;
                    }
                    break;
                case 6: //回复ACK，关闭
                    #if debug
                        printf("state 6 close:\n");
                    #endif
                    initUDPPackage(spkg);
                    spkg->FLAG = ACK;
                    spkg->seq = seq; seq = (seq+1)%SEQMAX;
                    spkg->ack = ack;
                    sendto(sockSrv, (char*)spkg, sizeof(*spkg), 0, (struct sockaddr*)&addrClient, len);
                    printf("[log] server to client ACK, seq=%d,ack=%d\n", spkg->seq, spkg->ack);
                    CONNECT = false;
                    state = 0;
                    break;
                default:
                    break;
            }//switch
        }//while
    }//if
    gettimeofday(&end, NULL);
    total_time = end.tv_sec - start.tv_sec + (end.tv_usec - start.tv_usec)/1000000.0;
    printf("[log] server to client file transmit done, FileLength=%fMB, TotalTransLength=%fMB\n",
                                (file_len*1.0)/1048576.0, (sent_offset*1.0)/1048576.0);
    printf("transmit time %f s , average rate %f Mb/s\n", total_time, (sent_offset*1.0*8)/1048576.0/total_time);
    //DeleteTimerQueueEx(hTimerQueue, NULL);
    delete[] file_data;
    //system("pause");
    //pause();
    close(sockSrv);
    printf("[log] server socket closed\n");
    //WSACleanup();
    //printf("[log] WSA cleaned\n");
    printf("==============================================================\n");

    return 0;
}



//send thread
void*  sendThread(void* lparam){
    while (true){
        if (old_base != base){//窗口滑动了
            UDPPackage *tmp = (UDPPackage *)(sendbuf + buf_idx * PACKSIZE); //buf_idx start from 0
            //结束
            if (tmp->FLAG == FIN){
                ((UDPPackage *)(sendbuf + (buf_idx-1) * PACKSIZE))->FLAG = 0;
                isEnd = true;
                return 0;
            }
            const int resent_seq = tmp->seq;
            //CreateTimerQueueTimer( &hTimer[buf_idx], hTimerQueue, (WAITORTIMERCALLBACK)TimerRoutine, (Pvoid*)resent_seq, RTO, RTO, 0);
            timer_start(3 ,0, &hTimer[buf_idx], TIMER_PERIODIC ,TimerRoutine, (void *)&resent_seq);
            timer_valid[buf_idx] = true;
            ++timerCount;
            //send
            sendret = sendto(sockSrv, sendbuf + buf_idx * PACKSIZE, sizeof(*spkg), 0, (struct sockaddr *)&addrClient, sizeof(struct sockaddr));
            printf("[log sendThread] server to client file data, seq=%d, checksum=%u, timer_idx=%d, resent_seq=%d\n",
                   tmp->seq, tmp->Checksum, buf_idx, resent_seq);
            if (sendret < 0){
                printf("[log sendThread] send message error\n");
            }
            buf_idx = (buf_idx + 1) % BUFNUM;

            if(resent_seq -base >= 4){
                usSleep(1000*(pow(resent_seq - base + 1, 2.375)));
            } else usSleep(4000);
            //usSleep(2000);
            if(old_base != base){
                old_base = (old_base + 1) % BUFNUM;
            }
        }//if
    }//while
    return NULL;
}



//定时器
static void* TimerRoutine(void* lpParam){
    int t_seq = *(int*)lpParam;
    int t_buf_idx = (t_seq - 1 + BUFNUM) % BUFNUM;

    UDPPackage *tmp = (UDPPackage *)(sendbuf + t_buf_idx % BUFNUM * PACKSIZE);
    if (tmp->FLAG != FIN && timer_valid[t_buf_idx]){
        sendret = sendto(sockSrv, sendbuf + t_buf_idx % BUFNUM * PACKSIZE, sizeof(*spkg), 0, (struct sockaddr *)&addrClient, sizeof(struct sockaddr));
        printf("[TimerRoutine %d] resent %d\n", t_seq, t_seq);
        if (sendret < 0){
            printf("[log TimerRoutine %d] send message error\n", ((UDPPackage *)(sendbuf + t_buf_idx % BUFNUM * PACKSIZE))->seq);
        }
    }
}

//轮询检查，如果定时器全部销毁且传输完毕，说明传输结束，挥手断连
void*  checkFinThread(void* lparam){
    while(true){
        if (isEnd && !timerCount){
            usSleep(RTO*1000);
            if (timerCount==0){
                initUDPPackage(spkg);
                spkg->FLAG = FIN;
                spkg->seq = seq;
                seq = (seq + 1) % SEQMAX;
                sendto(sockSrv, (char *)spkg, sizeof(*spkg), 0, (struct sockaddr *)&addrClient, sizeof(struct sockaddr));
                printf("[log] server to client FIN, seq=%d\n", spkg->seq);
                return 0;
            }
        }
    }
    return NULL;
}
