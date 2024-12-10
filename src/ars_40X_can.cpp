//
// Created by shivesh on 9/13/19.
//

#include "ars_40X/ars_40X_can.hpp"
#include "ICANCmd.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <getopt.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <functional>
#include <string.h>



#define __countof(a)  (sizeof(a)/sizeof(a[0]))

#define  DEF_DEV_INDEX               0
#define  DEF_USE_CAN_NUM             1      // 0-自动选择 1-使用CAN0；2-使用CAN1；3-同时使用CAN0、CAN1
#define  DEF_SEND_TYPE		     2	    // CAN发送类型,0-正常发送;1-单次发送;2-自发自收;3-单次自发自收
#define  DEF_SEND_FRAMES             1      // 每次发送帧数
#define  DEF_SEND_TIMES              1  // 发送次数
#define  DEF_SEND_DELY               5      // 发送前延时,单位秒



typedef struct {
   int Run;
   DWORD ch;
}rcv_thread_arg_t;

typedef struct {
   DWORD ch;
   DWORD sndType;   //0 - 正常发送;1 - 单 次 发送;2 - 自发自收;3 - 单 次 自发自收
   DWORD sndFrames; // 每次发送帧数
   DWORD sndTimes;  // 发送次数
}snd_thread_arg_t;

unsigned long CanSendcount[2] = { 0, 0 };

namespace ars_40X {

int __useCanDevIndex = DEF_DEV_INDEX; // CAN设备索引,从0开始
int __useCanChannel = DEF_USE_CAN_NUM; // 1-使用CAN0；2-使用CAN1；3-使用CAN0、CAN1
int __useCanSendType = DEF_SEND_TYPE; // CAN发送类型,0-正常发送;1-单次发送;2-自发自收;3-单次自发自收
int __useCanSendFrames = DEF_SEND_FRAMES; // 每次发送帧数
int __useCanSendTimes = DEF_SEND_TIMES; // 发送次数
int __useCanSendDely  = DEF_SEND_DELY;  // 发送前延时

bool ARS_40X_CAN::open_can(int argc, char** argv)
{
   char c;
   int helpflg = 0, errflg = 0;

   struct option longopts[] =
   {
      { "CanDevIndex", 1, 0, 'i' },
      { "CanChannel", 1, 0, 'c' },
      { "CanSendType", 1, 0, 'm' },
      { "CanSendFrames", 0, 0, 'l' },
      { "CanSendTimes", 0, 0, 'n' },
      { "CanSendDely", 0, 0, 'd' },
      { "help", 0, &helpflg, 1 },
      { 0, 0, 0, 0 }
   };
   char optstring[] = "i:c:m:l:n:";

   while ( (c = getopt_long(argc, argv, optstring, longopts, NULL)) != EOF ) {
      switch(c)
      {
      case 'i':
         __useCanDevIndex = atoi(optarg);
         break;
      case 'c':
         __useCanChannel = atoi(optarg);
         break;
      case 'm':
         __useCanSendType = atoi(optarg);
         break;
      case 'l':
         __useCanSendFrames = atoi(optarg);
         break;
      case 'n':
         __useCanSendTimes = atoi(optarg);
         break;
      case 'd':
         __useCanSendDely = atoi(optarg);
         break;
      case '?':
         errflg++;
         break;
      default:
         break;
      }
   }

   if ( helpflg || errflg ) {
      fprintf(stderr,
              "Usage:    testLikeCan [OPTION]\n\n%s%s%s%s%s%s%s",
              " like can test .\n",
              "\t-i, --CanDevIndex    CAN设备索引,默认值:0\n ",
              "\t-c, --CanChannel     1-使用CAN0；2-使用CAN1；3-同时使用CAN0、CAN1\n",
              "\t-m, --CanSendType    发送类型:0-正常发送;1-单次发送;2-自发自收;3-单次自发自收\n",
              "\t-l, --CanSendFrames  每次发送帧数\n",
              "\t-n, --CanSendTimes   发送次数\n",
              "\t-d, --CanSendDely    发送前延时,单位:s,默认值:10s\n"
             );
      return false;
   }

   time_t tm1, tm2;
   CAN_InitConfig config;
   Wakeup_Config  wakeup_config;
   int i = 0;
   rcv_thread_arg_t rcv_thread_arg0;
   rcv_thread_arg_t rcv_thread_arg1;
   snd_thread_arg_t snd_thread_arg0;
   snd_thread_arg_t snd_thread_arg1;
   pthread_t rcv_threadid0;
   pthread_t rcv_threadid1;
   pthread_t snd_threadid0;
   pthread_t snd_threadid1;
   std::shared_ptr<std::thread> thd_send0_, thd_send1_;

   int ret;
   // 打开设备
   if ( (dwDeviceHandle = CAN_DeviceOpen(ACUSB_132B, __useCanDevIndex, 0)) == 0 ) {
      printf("open deivce error\n");
      goto ext;
   }
   // 读取设备信息
   CAN_DeviceInformation DevInfo;
   if ( CAN_GetDeviceInfo(dwDeviceHandle, &DevInfo) != CAN_RESULT_OK ) {
      printf("GetDeviceInfo error\n");
      goto ext;
   }

   printf("--%s--\n", DevInfo.szDescription);
   printf("\tSN:%s\n", DevInfo.szSerialNumber);
   printf("\tCAN 通道数:%d\n", DevInfo.bChannelNumber);
   printf("\t硬件  版本:%x\n", DevInfo.uHardWareVersion);
   printf("\t固件  版本:%x\n\n", DevInfo.uFirmWareVersion);
   printf("\t驱动  版本:%x\n", DevInfo.uDriverVersion);
   printf("\t接口库版本:%x\n", DevInfo.uInterfaceVersion);
#if DEF_USE_CAN_NUM==0 
   if(DevInfo.bChannelNumber == 1) {
       __useCanChannel = 1;
   } else {
       __useCanChannel = 3;
   }
#endif
   // 启动CAN通道
   config.dwAccCode = 0;
   config.dwAccMask = 0xffffffff;
   config.nFilter  = 0;       // 滤波方式(0表示未设置滤波功能,1表示双滤波,2表示单滤波)
   config.bMode    = 0;             // 工作模式(0表示正常模式,1表示只听模式)
   config.nBtrType = 1;      // 位定时参数模式(1表示SJA1000,0表示LPC21XX)
   config.dwBtr[0] = 0x00;   // BTR0   0014 -1M 0016-800K 001C-500K 011C-250K 031C-12K 041C-100K 091C-50K 181C-20K 311C-10K BFFF-5K
   config.dwBtr[1] = 0x1c;   // BTR1
   config.dwBtr[2] = 0;
   config.dwBtr[3] = 0;
   if ( (__useCanChannel & 1) == 1 ) {
      if ( CAN_ChannelStart(dwDeviceHandle, 0, &config) != CAN_RESULT_OK ) {
         printf("Start CAN 0 error\n");
         goto ext;
      }
      // 标准帧唤醒(定制型号支持的可选操作)
      wakeup_config.dwAccCode = (0x123 << 21);  // 帧ID为0x123的标准帧 唤醒
      wakeup_config.dwAccMask = 0x001FFFFF;     // 固定
      wakeup_config.nFilter   = 2;              // 滤波方式(0表示未设置滤波功能-收到任意CAN数据都能唤醒,1表示双滤波,2表示单滤波,3-关闭唤醒功能)
      if ( CAN_SetParam(dwDeviceHandle, 0, PARAM_WAKEUP_CFG, &wakeup_config) != CAN_RESULT_OK ) {
         printf("CAN 0 not support Wakeup\n");
      }
   }
   return true;


  // if ( (__useCanChannel & 1) == 1 ) {
  //     rcv_thread_arg0.Run = TRUE;
  //     rcv_thread_arg0.ch = 0;
  //     // ret = pthread_create(&rcv_threadid0, NULL, receive_func, );
  //     thd_rcv0_ = std::make_shared<std::thread>(std::bind(&ARS_40X_CAN::receive_func, this, std::placeholders::_1), &rcv_thread_arg0);
  //  }

  //  if ( (__useCanChannel & 2) == 2 ) {
  //     rcv_thread_arg1.Run = TRUE;
  //     rcv_thread_arg1.ch = 1;
  //     // ret = pthread_create(&rcv_threadid1, NULL, receive_func, &rcv_thread_arg1);
  //     thd_rcv1_ = std::make_shared<std::thread>(std::bind(&ARS_40X_CAN::receive_func, this, std::placeholders::_1), &rcv_thread_arg0);
  //  }
  //  // 延时创建发送线程
  //  printf("\nstart send dley:%d s\n", __useCanSendDely);
  //  for ( i = __useCanSendDely; i >= 0; i-- ) {
  //     sleep(1);
  //     printf(" %d\n", i);
  //  }
  //  printf("\n");
  //  创建发送线程
  //  if ( (__useCanChannel & 1) == 1 ) {
  //     snd_thread_arg0.ch = 0;
      // snd_thread_arg0.sndType = __useCanSendType;
  //     snd_thread_arg0.sndFrames = __useCanSendFrames;
  //     snd_thread_arg0.sndTimes = __useCanSendTimes;
  //     // ret = pthread_create(&snd_threadid0, NULL, send_func, &snd_thread_arg0);
  //     thd_send0_ = std::make_shared<std::thread>(std::bind(&ARS_40X_CAN::send_func, this, std::placeholders::_1), &snd_thread_arg0);

  //  }

  //  if ( (__useCanChannel & 2) == 2 ) {
  //     snd_thread_arg1.ch = 1;
  //     snd_thread_arg1.sndType = __useCanSendType;
  //     snd_thread_arg1.sndFrames = __useCanSendFrames;
  //     snd_thread_arg1.sndTimes = __useCanSendTimes;
  //     // ret = pthread_create(&snd_threadid1, NULL, send_func, &snd_thread_arg1);
  //     thd_send1_ = std::make_shared<std::thread>(std::bind(&ARS_40X_CAN::send_func, this, std::placeholders::_1), &snd_thread_arg1);
  //  }

  //  printf("create thread\n");
  //  // 等待发送线程结束
  //  time(&tm1);
  //  if ( (__useCanChannel & 1) == 1 ) {
  //     // pthread_join(snd_threadid0, NULL);
  //     thd_send0_->join();
  //  }

  //  if ( (__useCanChannel & 2) == 2 ) {
  //     // pthread_join(snd_threadid1, NULL);
  //     thd_send1_->join();
  //  }
  //  time(&tm2);
  //  // 等待接收线程读取完所有帧
  //  printf("recive wait for completion dley:%d s\n", __useCanSendDely);
  //  for ( i = __useCanSendDely; i >= 0; i-- ) {
  //     sleep(1);
  //     printf(" %d\n", i);
  //  }
  //  printf("\n");
  //  // 退出接收线程
  //  rcv_thread_arg0.Run = FALSE;
  //  if ( (__useCanChannel & 1) == 1 ) {
  //     // pthread_join(rcv_threadid0, NULL);
      
  //  }
  //  rcv_thread_arg1.Run = FALSE;
  //  if ( (__useCanChannel & 2) == 2 ) {
  //     // pthread_join(rcv_threadid1, NULL);
  //  }
  //  printf("CAN0 Sendcount:%ld CAN1 Sendcount:%ld With time: %ld minute,%ld second\r\n", CanSendcount[0], CanSendcount[1], (tm2 - tm1) / 60, (tm2 - tm1) % 60);

ext:
   if ( (__useCanChannel & 1) == 1 ) {
      printf("CAN_ChannelStop 0\r\n");
      CAN_ChannelStop(dwDeviceHandle, 0);
   }

   if ( (__useCanChannel & 2) == 2 ) {
      printf("CAN_ChannelStop 1\r\n");
      CAN_ChannelStop(dwDeviceHandle, 1);
   }
  
   CAN_DeviceClose(dwDeviceHandle);
   printf("CAN_DeviceClose\r\n");
  return false;
}


ARS_40X_CAN::ARS_40X_CAN()
    {
  char *argv[] = {""};
  open_can(0, argv);
}

void* ARS_40X_CAN::send_func(void *param, uint8_t* data, uint8_t dlc)
{
   snd_thread_arg_t *thread_arg = (snd_thread_arg_t *)param;
   CAN_DataFrame *send = new CAN_DataFrame[thread_arg->sndFrames];
   int times = thread_arg->sndTimes;  //  26000-12h
   int ch = thread_arg->ch;

   for ( int j = 0; j < thread_arg->sndFrames; j++ ) {
      send[j].uID = ch;         // ID
      send[j].nSendType = thread_arg->sndType;  // 0-正常发送;1-单次发送;2-自发自收;3-单次自发自收
      send[j].bRemoteFlag = 0;  // 0-数据帧；1-远程帧
      send[j].bExternFlag = 1;  // 0-标准帧；1-扩展帧
      send[j].nDataLen = dlc;     // DLC
      for ( int i = 0; i < send[j].nDataLen; i++ ) {
         send[j].arryData[i] = data[i];
      }
   }
  //  while ( times ) {
  //     //printf("CAN%d Send %d\r\n", ch, times);
  //     unsigned long sndCnt = CAN_ChannelSend(dwDeviceHandle, ch, send, thread_arg->sndFrames);
  //     CanSendcount[ch] += sndCnt;
  //     if ( sndCnt )
  //        times--;
  //  }
  CAN_ChannelSend(dwDeviceHandle, ch, send, thread_arg->sndFrames);
   delete[] send;
   printf("CAN%d Send Count:%ld end \r\n", ch, CanSendcount[ch]);
}


void* ARS_40X_CAN::receive_func(void *param)  //接收线程的处理函数
{
   int reclen = 0;
   rcv_thread_arg_t *thread_arg = (rcv_thread_arg_t *)param;
   int ind = thread_arg->ch;
   unsigned long count = 0;
   unsigned long errcount = 0;
   CAN_DataFrame rec[100];
   int i;
   CAN_DataFrame snd;
   CAN_ErrorInformation err;
   printf("receive thread running....%d\n", ind);
   while ( thread_arg->Run ) {
      if ( (reclen = CAN_ChannelReceive(dwDeviceHandle, ind, rec, __countof(rec), 200)) > 0 ) {
         printf("CAN%d Receive: %08X", ind, rec[reclen - 1].uID);
         for ( i = 0; i < rec[reclen - 1].nDataLen; i++ ) {
            printf(" %02X", rec[reclen - 1].arryData[i]);
         }
         printf("\n");
         count += reclen;
         printf("CAN%d rcv count=%ld\n", ind, count);
      } else {
         if ( CAN_GetErrorInfo(dwDeviceHandle, ind, &err) == CAN_RESULT_OK ) {  
            errcount++;
         } else {
            // CAN卡没有收到CAN报文
         }
      }
   }
   printf("CAN%d rcv count=%ld err count:%ld\n", ind, count, errcount);
}

ARS_40X_CAN::ARS_40X_CAN(std::string port) 
     {
}

ARS_40X_CAN::~ARS_40X_CAN() {
}

bool ARS_40X_CAN::receive_radar_data() {
  uint32_t frame_id;
  uint8_t data[8] = {0};
  int ind = 0;
  // bool read_status = can_.read(&frame_id, &dlc, data);
  CAN_DataFrame rec;
  if (CAN_ChannelReceive(dwDeviceHandle, ind, &rec, 1, 200) == 0) {
    printf("failed rcv\r\n");
    return false;
  }
  int dlc = rec.nDataLen;
  // printf("rcv success\r\n");
  frame_id = rec.uID;
  memcpy(data, rec.arryData, dlc);
  switch (frame_id) {
    case RadarState:
      memcpy(radar_state_.get_radar_state()->raw_data, data, dlc);
      send_radar_state();
      break;

    case Cluster_0_Status:memcpy(cluster_0_status_.get_cluster_0_status()->raw_data, data, dlc);
      send_cluster_0_status();
      break;

    case Cluster_1_General:memcpy(cluster_1_general_.get_cluster_1_general()->raw_data, data, dlc);
      send_cluster_1_general();
      break;

    case Cluster_2_Quality:memcpy(cluster_2_quality_.get_cluster_2_quality()->raw_data, data, dlc);
      send_cluster_2_quality();
      break;

    case Object_0_Status:memcpy(object_0_status_.get_object_0_status()->raw_data, data, dlc);
      send_object_0_status();
      break;

    case Object_1_General:memcpy(object_1_general_.get_object_1_general()->raw_data, data, dlc);
      send_object_1_general();
      break;

    case Object_2_Quality:memcpy(object_2_quality_.get_object_2_quality()->raw_data, data, dlc);
      send_object_2_quality();
      break;

    case Object_3_Extended:memcpy(object_3_extended_.get_object_3_extended()->raw_data, data, dlc);
      send_object_3_extended();
      break;

    default: {
// #if DEBUG
      printf("Unidentified Message: %d\n", frame_id);
// #endif
      break;
    }
  }
  return true;
}

bool ARS_40X_CAN::send_radar_data(uint32_t frame_id) {
//   switch (frame_id) {
    // case RadarCfg:can_.write(frame_id, 8, radar_cfg_.get_radar_cfg()->raw_data);
//       break;
//     case SpeedInformation:
//       can_.write(frame_id,
//                  2,
                //  speed_information_.get_speed_information()->raw_data);
//       break;
//     case YawRateInformation:
//       can_.write(frame_id,
//                  2,
//                  yaw_rate_information_.get_yaw_rate_information()->raw_data);
//       break;
// #if DEBUG
//       default: printf("Frame ID not supported\n");
// #endif
//   }
  snd_thread_arg_t snd_thread_arg0;
  snd_thread_arg0.ch = 0;
  snd_thread_arg0.sndType = __useCanSendType;
  snd_thread_arg0.sndFrames = __useCanSendFrames;
  snd_thread_arg0.sndTimes = __useCanSendTimes;
  switch (frame_id)
  {
  case RadarCfg:
    send_func(&snd_thread_arg0, radar_cfg_.get_radar_cfg()->raw_data, 8);
    break;
  case SpeedInformation:
    send_func(&snd_thread_arg0, speed_information_.get_speed_information()->raw_data, 2);
    break;
  case YawRateInformation:
    send_func(&snd_thread_arg0, yaw_rate_information_.get_yaw_rate_information()->raw_data, 2);
  // #if DEBUG
      default: printf("Frame ID not supported\n");
  // #endif
  }

  return true;
}

cluster_list::Cluster_0_Status *ARS_40X_CAN::get_cluster_0_status() {
  return &cluster_0_status_;
}

cluster_list::Cluster_1_General *ARS_40X_CAN::get_cluster_1_general() {
  return &cluster_1_general_;
}

cluster_list::Cluster_2_Quality *ARS_40X_CAN::get_cluster_2_quality() {
  return &cluster_2_quality_;
}

motion_input_signals::SpeedInformation *ARS_40X_CAN::get_speed_information() {
  return &speed_information_;
}

motion_input_signals::YawRateInformation *ARS_40X_CAN::get_yaw_rate_information() {
  return &yaw_rate_information_;
}

object_list::Object_0_Status *ARS_40X_CAN::get_object_0_status() {
  return &object_0_status_;
}

object_list::Object_1_General *ARS_40X_CAN::get_object_1_general() {
  return &object_1_general_;
}

object_list::Object_2_Quality *ARS_40X_CAN::get_object_2_quality() {
  return &object_2_quality_;
}

object_list::Object_3_Extended *ARS_40X_CAN::get_object_3_extended() {
  return &object_3_extended_;
}

radar_state::RadarState *ARS_40X_CAN::get_radar_state() {
  return &radar_state_;
}

radar_cfg::RadarCfg *ARS_40X_CAN::get_radar_cfg() {
  return &radar_cfg_;
}
}
