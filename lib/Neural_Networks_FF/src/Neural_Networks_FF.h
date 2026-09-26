#ifndef NEURAL_NETWORKS_FF_H
#define NEURAL_NETWORKS_FF_H
#include <Arduino.h>
#include<Dynamic_Array.h>
class Neural_Networks_FF
{ 
  public:
   Neural_Networks_FF();
   void SIM_NET(Dynamic_Array _IN);
   void TRAIN_NET(Dynamic_Array _IN,Dynamic_Array _OUT,float _error_min,unsigned int _epochs_max);
   void TRAIN_NET_ONLINE(Dynamic_Array _IN,Dynamic_Array _OUT,Dynamic_Array _YS);
   void FEED_FORWARD_NET(Dynamic_Array _STRUCTURE,Dynamic_Array _FUN_ACT_STR);
   void NORMALIZE_DATA(Dynamic_Array _DATA,float _facN);
   void DATA_CENTERING(Dynamic_Array _DATA);
   void STR_to_INT(Dynamic_Array _STRING,Dynamic_Array _INT);
   float FunAct(byte _a,float _in);
   float DFunAct(byte _a,float _in);
   float logsig(float _in);
   float tansig(float _in);
   float purelin(float _in);
   float hardlim(float _in);
   float poslin_lim(float _in, float _maximo, float _minimo);
   void dposlin_limits(float _sup, float _inf);
   float dlogsig(float _in);
   float dtansig(float _in);
   float dpurelin(float _in);
   float dhardlim(float _in);
   float dposlin_lim(float _in, float _maximo, float _minimo);

  Dynamic_Array Ysim;
  Dynamic_Array y;
  Dynamic_Array W;
  Dynamic_Array E;
  float EPOCHS;
  float ERROR_C_M ;
  float maximo;
  float minimo;
  float LearningRate;



  private:
  Dynamic_Array IN;
  Dynamic_Array OUT;
  Dynamic_Array YS;
  float error_min;
  unsigned int epochs_max;
  Dynamic_Array STRUCTURE;
  Dynamic_Array FUN_ACT_STR;
  Dynamic_Array DATA;
  float facN;
  Dynamic_Array STRING;
  Dynamic_Array INT;
  byte a;
  float in;
  float sup;
  float inf;  
 
Dynamic_Array N;
//Dynamic_Array W;
Dynamic_Array er;
//Dynamic_Array y;
//Dynamic_Array E;
Dynamic_Array G;  
Dynamic_Array M;
//Dynamic_Array Ysim;
Dynamic_Array h;
Dynamic_Array n;
Dynamic_Array FAC_INT;
    
};
#endif
