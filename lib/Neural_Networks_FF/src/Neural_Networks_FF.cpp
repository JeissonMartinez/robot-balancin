#include "Neural_Networks_FF.h"

Neural_Networks_FF::Neural_Networks_FF()
{
 EPOCHS=0;
 ERROR_C_M=1;
 maximo=0;
 minimo=0;
 LearningRate=0.01;


//Dynamic_Array XI=Dynamic_Array();
//Dynamic_Array D=Dynamic_Array();
//Dynamic_Array N=Dynamic_Array();
//Dynamic_Array W=Dynamic_Array();
//Dynamic_Array er=Dynamic_Array();
//Dynamic_Array y=Dynamic_Array();
//Dynamic_Array E=Dynamic_Array();
//Dynamic_Array G=Dynamic_Array();  
//Dynamic_Array M=Dynamic_Array();
//Dynamic_Array Ysim=Dynamic_Array();
//Dynamic_Array h=Dynamic_Array();
//Dynamic_Array n=Dynamic_Array();
//Dynamic_Array FAC_INT=Dynamic_Array();
//Dynamic_Array FAC_STR=Dynamic_Array();
//Dynamic_Array EST=Dynamic_Array();
}

void Neural_Networks_FF::FEED_FORWARD_NET(Dynamic_Array _STRUCTURE,Dynamic_Array _FUN_ACT_STR){
       STRUCTURE=_STRUCTURE;
       FUN_ACT_STR=_FUN_ACT_STR;
       float NC=STRUCTURE.GetColumns();
       float maxN=STRUCTURE.MaxArray_2D();
       byte L=STRUCTURE.GetColumns();
       N.NewArray_2D(1,L);
       N.ToinitializeArray_2D(0.0);
       for (byte i = 0; i < L; i++)
       {
        N.WriteArray_2D(0,i,STRUCTURE.ReadArray_2D(0,i));
        }
    
        FAC_INT.IntNewArray_1D(FUN_ACT_STR.GetRows());
        FAC_INT.IntToinitializeArray_1D(1);
        STR_to_INT(FUN_ACT_STR,FAC_INT);
    
        W.NewArray_3D(NC,maxN,maxN+1);
        W.ToinitializeArray_3D(0.0);
    
     for (byte capa = 1; capa<L; capa++)
       {for (byte nodo = 0; nodo<N.ReadArray_2D(0,capa); nodo++)
          {for (byte i=0; i<(N.ReadArray_2D(0,capa-1)+1); i++)          
             {W.WriteArray_3D(capa,nodo,i,random(1,10)*0.00000001);
             }//((Entropy.random() & 0xFFFFFF) / 16777216.0)
          }   // random(0,1000)*0.001
        }
    //W.PrintArray_3D();
    y.NewArray_2D(NC,maxN+1);
    y.ToinitializeArray_2D(0.0);
    
    E.NewArray_2D(NC,maxN);
    E.ToinitializeArray_2D(0.0);
    
    G.NewArray_3D(NC,maxN,maxN+1);
    G.ToinitializeArray_3D(0.0);
    
    M.NewArray_3D(NC,maxN,maxN+1);
    M.ToinitializeArray_3D(0.0);
    
    h.NewArray_3D(NC,maxN,maxN+1);
    h.ToinitializeArray_3D(1.0);
    
    n.NewArray_2D(NC,maxN);
    n.ToinitializeArray_2D(0.0);
    
    for (byte capa=1; capa<L; capa++){
    for (byte nodo=0; nodo<N.ReadArray_2D(0,capa); nodo++){
      n.WriteArray_2D(capa,nodo,LearningRate*sqrt(N.ReadArray_2D(0,capa-1)+1));
    }
    }
    }



void Neural_Networks_FF::TRAIN_NET(Dynamic_Array _IN,Dynamic_Array _OUT,float _error_min,unsigned int _epochs_max){
    IN=_IN;
    OUT=_OUT;
    error_min=_error_min;
    epochs_max=_epochs_max;
    byte XC=IN.GetColumns();
    byte DF=OUT.GetRows();
    byte XF=IN.GetRows();  
    byte L=FAC_INT.GetRows();
    er.NewArray_2D(XF,DF);
    er.ToinitializeArray_2D(1.0);
    unsigned int epochs=0;
    float error_t=1;
    
    while ((error_t>error_min)&&(epochs<epochs_max)) {
     
      digitalWrite(13, !digitalRead(13));
      epochs=epochs+1;
      error_t=0;
    for (byte k=0; k<XF; k++) {      
      for (byte i=0; i<N.ReadArray_2D(0,0); i++){
       y.WriteArray_2D(0,i,IN.ReadArray_2D(k,i)); 
       }
       y.WriteArray_2D(0,N.ReadArray_2D(0,0),1);
       
       float s=0.00;
    
    for (byte capa=1; capa<L; capa++)
      {for (byte nodo=0 ;nodo<N.ReadArray_2D(0,capa); nodo++)
         {for (byte i=0; i<(N.ReadArray_2D(0,capa-1)+1); i++)
            { s=s+W.ReadArray_3D(capa,nodo,i)*y.ReadArray_2D(capa-1,i);
             
            }       
    y.WriteArray_2D(capa,nodo,FunAct(capa,s));
    y.WriteArray_2D(capa,nodo+1,1.0);
    s=0.00;
      }
    }
  
    ////Procedimiento COMPUTO-GRADIENTE;
    float su=0.00;
    for (byte capa=L-1; capa>=1;capa--){
    for (byte nodo=0; nodo<N.ReadArray_2D(0,capa); nodo++){
    if (capa==L-1){
      E.WriteArray_2D(L-1,nodo,(OUT.ReadArray_2D(nodo,k)-y.ReadArray_2D(L-1,nodo)));
       }
    else{    
         for (byte m=0; m<N.ReadArray_2D(0,capa+1); m++)
         {
          su=su+(E.ReadArray_2D(capa+1,m)*DFunAct(capa+1,y.ReadArray_2D(capa+1,m))*W.ReadArray_3D(capa+1,m,nodo));
          }
         E.WriteArray_2D(capa,nodo,su);
         su=0;
       }
    }
    }
    
    for (byte i=0; i < DF;i++){
    er.WriteArray_2D(k,i,E.ReadArray_2D(L-1,i));
    }
    
    for (byte capa=1; capa<L; capa++){
    for (byte nodo=0; nodo<N.ReadArray_2D(0,capa); nodo++){
    for (byte i=0; i<(N.ReadArray_2D(0,capa-1)+1); i++){
      
       float aux=E.ReadArray_2D(capa,nodo)*DFunAct(capa,y.ReadArray_2D(capa,nodo))*y.ReadArray_2D(capa-1,i);
       if(epochs!=1&&(G.ReadArray_3D(capa,nodo,i)*aux)>0)
       {h.WriteArray_3D(capa,nodo,i,h.ReadArray_3D(capa,nodo,i)+0.05);}
       else{h.WriteArray_3D(capa,nodo,i,h.ReadArray_3D(capa,nodo,i)*0.95);}
       G.WriteArray_3D(capa,nodo,i,aux); 
    }
    }
    }
    
    for (byte capa=1; capa<L; capa++){
    for (byte nodo=0; nodo<N.ReadArray_2D(0,capa); nodo++){
    for (byte i=0; i<(N.ReadArray_2D(0,capa-1)+1); i++){
      if(epochs==1){
       W.WriteArray_3D(capa,nodo,i,(W.ReadArray_3D(capa,nodo,i)+(n.ReadArray_2D(capa,nodo)*h.ReadArray_3D(capa,nodo,i)*G.ReadArray_3D(capa,nodo,i)))); 
       M.WriteArray_3D(capa,nodo,i,n.ReadArray_2D(capa,nodo)*h.ReadArray_3D(capa,nodo,i)*G.ReadArray_3D(capa,nodo,i)); 
      }
      else{
       W.WriteArray_3D(capa,nodo,i,(W.ReadArray_3D(capa,nodo,i)+(n.ReadArray_2D(capa,nodo)*h.ReadArray_3D(capa,nodo,i)*G.ReadArray_3D(capa,nodo,i))+(0.1*M.ReadArray_3D(capa,nodo,i)))); 
       M.WriteArray_3D(capa,nodo,i,n.ReadArray_2D(capa,nodo)*h.ReadArray_3D(capa,nodo,i)*G.ReadArray_3D(capa,nodo,i)+0.1*M.ReadArray_3D(capa,nodo,i)); 
      }
    }
    }
    }
    
    s=0.00;
    }
    for (byte j=0; j<DF; j++){
    for (byte x=0; x<XF; x++){
    er.WriteArray_2D(x,j,(0.5*pow(er.ReadArray_2D(x,j),2)));
    error_t=error_t+er.ReadArray_2D(x,j);
    } 
    }
    error_t=error_t/(DF*XF);
    }
    EPOCHS=epochs;
    ERROR_C_M =error_t;
    //Serial.println(error_t,6);
    }

void Neural_Networks_FF::TRAIN_NET_ONLINE(Dynamic_Array _IN,Dynamic_Array _OUT,Dynamic_Array _YS){
    IN=_IN;
    OUT=_OUT;
    YS=_YS;
    //error_min=_error_min;
    //epochs_max=_epochs_max;
    byte XC=IN.GetColumns();
    byte DF=OUT.GetRows();
    byte XF=IN.GetRows();  
    byte L=FAC_INT.GetRows();
    //er.NewArray_2D(XF,DF);
    //er.ToinitializeArray_2D(1.0);
    unsigned int epochs=0;
    float error_t=1;
  
      digitalWrite(13, !digitalRead(13));
      epochs=epochs+1;
      error_t=0;
    for (byte k=0; k<XF; k++) {      
      for (byte i=0; i<N.ReadArray_2D(0,0); i++){
       y.WriteArray_2D(0,i,IN.ReadArray_2D(k,i)); 
       }
       y.WriteArray_2D(0,N.ReadArray_2D(0,0),1);
       
       float s=0.00;
    
    for (byte capa=1; capa<L; capa++)
      {for (byte nodo=0 ;nodo<N.ReadArray_2D(0,capa); nodo++)
         {for (byte i=0; i<(N.ReadArray_2D(0,capa-1)+1); i++)
            { s=s+W.ReadArray_3D(capa,nodo,i)*y.ReadArray_2D(capa-1,i);
             
            }   
 
if(E.ReadArray_2D(L-1,nodo)<=0.00&&s>=maximo){s=s-((s-maximo)*1.5);}
if(E.ReadArray_2D(L-1,nodo)>=0.00&&s<=minimo){s=s-((s-minimo)*1.5);}

    y.WriteArray_2D(capa,nodo,FunAct(capa,s));
    y.WriteArray_2D(capa,nodo+1,1.0);

    s=0.00;
      }
    }
  
    ////Procedimiento COMPUTO-GRADIENTE;
    float su=0.00;
    for (byte capa=L-1; capa>=1;capa--){
    for (byte nodo=0; nodo<N.ReadArray_2D(0,capa); nodo++){
    if (capa==L-1){
      E.WriteArray_2D(L-1,nodo,(OUT.ReadArray_2D(nodo,k)-YS.ReadArray_2D(0,nodo)));
       }
    else{    
         for (byte m=0; m<N.ReadArray_2D(0,capa+1); m++)
         {
          su=su+(E.ReadArray_2D(capa+1,m)*DFunAct(capa+1,y.ReadArray_2D(capa+1,m))*W.ReadArray_3D(capa+1,m,nodo));
          }
         E.WriteArray_2D(capa,nodo,su);
         su=0;
       }
    }
    }
    
//    for (byte i=0; i < DF;i++){
//    er.WriteArray_2D(k,i,E.ReadArray_2D(L-1,i));
//    }
    
    for (byte capa=1; capa<L; capa++){
    for (byte nodo=0; nodo<N.ReadArray_2D(0,capa); nodo++){
    for (byte i=0; i<(N.ReadArray_2D(0,capa-1)+1); i++){
      
       float aux=E.ReadArray_2D(capa,nodo)*DFunAct(capa,y.ReadArray_2D(capa,nodo))*y.ReadArray_2D(capa-1,i);
       if(epochs!=1&&(G.ReadArray_3D(capa,nodo,i)*aux)>0)
       {h.WriteArray_3D(capa,nodo,i,h.ReadArray_3D(capa,nodo,i)+0.05);}
       //else{h.WriteArray_3D(capa,nodo,i,h.ReadArray_3D(capa,nodo,i)*0.95); }
       G.WriteArray_3D(capa,nodo,i,aux); 
    }
    }
    }

    for (byte capa=1; capa<L; capa++){
    for (byte nodo=0; nodo<N.ReadArray_2D(0,capa); nodo++){
    for (byte i=0; i<(N.ReadArray_2D(0,capa-1)+1); i++){
      if(epochs==1){
       W.WriteArray_3D(capa,nodo,i,(W.ReadArray_3D(capa,nodo,i)+(n.ReadArray_2D(capa,nodo)*h.ReadArray_3D(capa,nodo,i)*G.ReadArray_3D(capa,nodo,i)))); 
       M.WriteArray_3D(capa,nodo,i,n.ReadArray_2D(capa,nodo)*h.ReadArray_3D(capa,nodo,i)*G.ReadArray_3D(capa,nodo,i)); 
      }
      else{
       W.WriteArray_3D(capa,nodo,i,(W.ReadArray_3D(capa,nodo,i)+(n.ReadArray_2D(capa,nodo)*h.ReadArray_3D(capa,nodo,i)*G.ReadArray_3D(capa,nodo,i))+(0.1*M.ReadArray_3D(capa,nodo,i)))); 
       M.WriteArray_3D(capa,nodo,i,n.ReadArray_2D(capa,nodo)*h.ReadArray_3D(capa,nodo,i)*G.ReadArray_3D(capa,nodo,i)+0.1*M.ReadArray_3D(capa,nodo,i)); 
      }
    }
    }
    }

    
    s=0.00;
    }
//    for (byte j=0; j<DF; j++){
//    for (byte x=0; x<XF; x++){
//    er.WriteArray_2D(x,j,(0.5*pow(er.ReadArray_2D(x,j),2)));
//    error_t=error_t+er.ReadArray_2D(x,j);
//    } 
//     }
//    error_t=error_t/(DF*XF);
    
    EPOCHS=epochs;
    ERROR_C_M =error_t;
    //Serial.println(error_t,6);

    for (byte capa=1; capa<L; capa++){
    for (byte nodo=0; nodo<N.ReadArray_2D(0,capa); nodo++){
      n.WriteArray_2D(capa,nodo,LearningRate*sqrt(N.ReadArray_2D(0,capa-1)+1));
    }
    }

    }


    
void Neural_Networks_FF::SIM_NET(Dynamic_Array _IN){
  IN=_IN;
  byte DF=N.ReadArray_2D(0,N.GetColumns()-1);
  byte XF=IN.GetRows();
  byte L=N.GetColumns();
Ysim.NewArray_2D(DF,XF);
Ysim.ToinitializeArray_2D(0.0);
for (byte k=0; k<XF; k++) {     
  for (byte i=0; i<N.ReadArray_2D(0,0); i++){
   y.WriteArray_2D(0,i,IN.ReadArray_2D(k,i)); 
   }
   y.WriteArray_2D(0,N.ReadArray_2D(0,0),1);
   float s=0.00;
for (byte capa=1; capa<L; capa++)
  {for (byte nodo=0 ;nodo<N.ReadArray_2D(0,capa); nodo++)
     {for (byte i=0; i<(N.ReadArray_2D(0,capa-1)+1); i++)
        { s=s+W.ReadArray_3D(capa,nodo,i)*y.ReadArray_2D(capa-1,i);
        }
y.WriteArray_2D(capa,nodo,FunAct(capa,s));
if(capa==L-1){
Ysim.WriteArray_2D(nodo,k,FunAct(capa,s));
}
y.WriteArray_2D(capa,nodo+1,1.0);
s=0.00;
}
}
}
}

 void Neural_Networks_FF::NORMALIZE_DATA(Dynamic_Array _DATA,float _facN){
  DATA=_DATA;
  facN=_facN;
  for (byte i = 0; i<DATA.GetColumns() ; i++){
  for (byte j = 0; j<DATA.GetRows() ; j++){
    DATA.WriteArray_2D(j,i,DATA.ReadArray_2D(j,i)/facN);
    }
   }
  }

  void Neural_Networks_FF::DATA_CENTERING(Dynamic_Array _DATA){
    DATA=_DATA;
    float media[DATA.GetColumns()];
    for (byte i = 0; i<DATA.GetColumns() ; i++){
      media[i]=0;
      for (byte j = 0; j<DATA.GetRows() ; j++){
        media[i]=media[i]+DATA.ReadArray_2D(j,i);
      }
      media[i]=media[i]/DATA.GetRows();
    }
    
    for (byte i = 0; i<DATA.GetColumns() ; i++){
      for (byte j = 0; j<DATA.GetRows() ; j++){
       DATA.WriteArray_2D(j,i,DATA.ReadArray_2D(j,i)-media[i]);
      }
    }
    }

 void Neural_Networks_FF::STR_to_INT(Dynamic_Array _STRING,Dynamic_Array _INT){
     STRING=_STRING;
     INT=_INT;
     for(int i=0;i<STRING.GetRows();i++){
     if(STRING.CharReadArray_1D(i)=="tansig")
     {INT.IntWriteArray_1D(i,1);}
     else{if(STRING.CharReadArray_1D(i)=="purelin")
        {INT.IntWriteArray_1D(i,2);}
       else{if(STRING.CharReadArray_1D(i)=="logsig")
             {INT.IntWriteArray_1D(i,3);}
             else{if(STRING.CharReadArray_1D(i)=="hardlim")
                   {INT.IntWriteArray_1D(i,4);}
                   else{INT.IntWriteArray_1D(i,5);}
                 }     
            }
       }
}
}

float Neural_Networks_FF::FunAct(byte _a,float _in){
  a=_a;
  in=_in;
float Output;  
switch (FAC_INT.IntReadArray_1D(a)) {
  case 1:
   Output=tansig(in);
    break;
  case 2:
    Output=purelin(in);
    break;
   case 3:
    Output=logsig(in);
    break;
   case 4:
    Output=hardlim(in);
    break;
   default:
//   float maximo;
//   float minimo;
    Output=poslin_lim(in,maximo,minimo);
    break;
}
  return Output;
}


float Neural_Networks_FF::DFunAct(byte _a,float _in){
  a=_a;
  in=_in;
  float Output;
  switch (FAC_INT.IntReadArray_1D(a)) {
  case 1:
   Output=dtansig(in);
    break;
  case 2:
    Output=dpurelin(in);
    break;
   case 3:
    Output=dlogsig(in);
    break;
   case 4:
    Output=dhardlim(in);
    break;
   default:
//   float maximo;
//   float minimo;
    Output=dposlin_lim(in,maximo,minimo);
    break;
  }
  return Output;
}

float Neural_Networks_FF::logsig(float _in){
  in=_in;
  float resultado;
  resultado=1/(1+(pow(2.7183,-in)));
  return resultado;
}

float Neural_Networks_FF::tansig(float _in){
  in=_in;
  float resultado;
  resultado=2/(1+(pow(2.7183,(-2*in))))-1;
  return resultado;
}

float Neural_Networks_FF::purelin(float _in){
  in=_in;
  float resultado;
  resultado=in;
  return resultado;
}

float Neural_Networks_FF::hardlim(float _in){
  in=_in;
  float resultado;
  if(in>=0)
  {resultado=1;}
  else{resultado=0;}
  return resultado;
}

float Neural_Networks_FF::poslin_lim(float _in, float _maximo, float _minimo){
  in=_in;
  maximo=_maximo;
  minimo=_minimo;
  float resultado;
  if(in>=minimo&&in<=maximo)
  {resultado=in;}
  else{if(in>maximo)
       {resultado=maximo;}
       else{resultado=minimo;}
      }
  return resultado;
}

float Neural_Networks_FF::dlogsig(float _in){
  in=_in;
  float resultado;
  resultado=in*(1-in);
  return resultado;
}

float Neural_Networks_FF::dtansig(float _in){
  in=_in;
  float resultado;
  resultado=1-(in*in);
  return resultado;
}

float Neural_Networks_FF::dpurelin(float _in){
  in=_in;
  float resultado;
  resultado=1;
  return resultado;
}

float Neural_Networks_FF::dhardlim(float _in){
  in=_in;
  float resultado;
  resultado=1;
  return resultado;
}

float Neural_Networks_FF::dposlin_lim(float _in, float _maximo, float _minimo){
  in=_in;
  maximo=_maximo;
  minimo=_minimo; 
  float resultado;
  if(in>minimo&&in<maximo){resultado=1;}
  else{resultado=0;}
  return resultado;
}

void Neural_Networks_FF::dposlin_limits(float _sup, float _inf){
 sup=_sup;
 inf=_inf;
 maximo=sup;
 minimo=inf;
}
