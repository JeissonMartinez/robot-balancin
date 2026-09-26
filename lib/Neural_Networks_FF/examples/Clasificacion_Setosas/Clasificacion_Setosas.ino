#include<Neural_Networks_FF.h>
//Contructor de la red neuronal
Neural_Networks_FF net=Neural_Networks_FF();
//Array dinamico para ingresar las entradas a la red
Dynamic_Array XI=Dynamic_Array();
//Array dinamico para ingresar las salidas deseadas
Dynamic_Array D=Dynamic_Array();
//Array dinamico tipo string para ingresar las funciones
//de activacion por capa
Dynamic_Array FAC_STR=Dynamic_Array();
//Array dinamico para ingresar la estructura de la red
//como vector
Dynamic_Array EST=Dynamic_Array();

void setup(){
   delay(3000);
   Serial.begin(9600);
   //Pin 13 para visialisar el entrenamiento
   pinMode(13, OUTPUT);
   //randomSeed para los pesos aleatorios iniciales
   randomSeed(analogRead(0));
   //Crear e inicializar el array de los datos de entrada
   XI.NewArray_2D(120,2);
   XI.ToinitializeArray_2D(0.0);
   //Crear e inicializar el array de salidas deseadas
   D.NewArray_2D(1,120);
   D.ToinitializeArray_2D(0.0);
   
   ///Creamos los datos para entrenar la RED//
   float I[120][2]={{3.9000  ,  1.4000},
             { 3.7000  ,  1.0000},
             { 4.5000  ,  1.5000},
             { 1.5000  ,  0.3000},
             { 4.5000  ,  1.5000},
             { 1.5000  ,  0.2000},
             { 6.0000  ,  2.5000},
             { 3.3000  ,  1.0000},
             { 6.7000  ,  2.0000},
             { 4.8000  ,  1.8000},
             { 3.0000  ,  1.1000},
             { 4.4000  ,  1.3000},
             { 1.5000  ,  0.2000},
             { 1.9000  ,  0.2000},
             { 1.3000  ,  0.3000},
             { 1.5000  ,  0.2000},
             { 5.0000  ,  1.9000},
             { 4.0000  ,  1.3000},
             { 1.4000  ,  0.3000},
             { 5.8000  ,  2.2000},
             { 4.3000  ,  1.3000},
             { 5.1000  ,  2.4000},
             { 5.1000  ,  1.6000},
             { 4.8000  ,  1.8000},
             { 6.6000  ,  2.1000},
             { 5.1000  ,  1.9000},
             { 5.6000  ,  1.4000},
             { 5.0000  ,  1.7000},
             { 1.7000  ,  0.5000},
             { 1.5000  ,  0.2000},
             { 1.3000  ,  0.2000},
             { 1.6000  ,  0.2000},
             { 4.5000  ,  1.5000},
             { 1.4000  ,  0.2000},
             { 5.6000  ,  2.4000},
             { 1.6000  ,  0.6000},
             { 3.9000  ,  1.2000},
             { 1.3000  ,  0.2000},
             { 5.5000  ,  1.8000},
             { 3.3000  ,  1.0000},
             { 6.1000  ,  2.5000},
             { 1.5000  ,  0.4000},
             { 5.1000  ,  1.8000},
             { 5.9000  ,  2.1000},
             { 6.0000  ,  1.8000},
             { 4.5000  ,  1.4000},
             { 1.5000  ,  0.1000},
             { 4.8000  ,  1.8000},
             { 5.9000  ,  2.3000},
             { 4.0000  ,  1.3000},
             { 3.5000  ,  1.0000},
             { 5.6000  ,  2.1000},
             { 4.9000  ,  1.4000},
             { 5.1000  ,  2.0000},
             { 1.5000  ,  0.4000},
             { 6.1000  ,  2.3000},
             { 1.6000  ,  0.2000},
             { 5.2000  ,  2.0000},
             { 4.1000  ,  1.3000},
             { 1.5000  ,  0.1000},
             { 6.4000  ,  2.0000},
             { 4.5000  ,  1.5000},
             { 1.4000  ,  0.2000},
             { 1.4000  ,  0.2000},
             { 4.6000  ,  1.4000},
             { 5.5000  ,  1.8000},
             { 1.4000  ,  0.2000},
             { 4.7000  ,  1.4000},
             { 1.7000  ,  0.2000},
             { 4.7000  ,  1.6000},
             { 1.5000  ,  0.2000},
             { 4.8000  ,  1.4000},
             { 3.5000  ,  1.0000},
             { 4.5000  ,  1.7000},
             { 1.4000  ,  0.1000},
             { 6.1000  ,  1.9000},
             { 3.9000  ,  1.1000},
             { 5.5000  ,  2.1000},
             { 1.3000  ,  0.2000},
             { 3.8000  ,  1.1000},
             { 1.5000  ,  0.4000},
             { 1.7000  ,  0.4000},
             { 5.8000  ,  1.8000},
             { 4.7000  ,  1.2000},
             { 1.6000  ,  0.2000},
             { 4.6000  ,  1.5000},
             { 5.1000  ,  1.5000},
             { 1.1000  ,  0.1000},
             { 1.4000  ,  0.2000},
             { 1.7000  ,  0.3000},
             { 1.6000  ,  0.4000},
             { 4.0000  ,  1.0000},
             { 4.0000  ,  1.3000},
             { 5.7000  ,  2.1000},
             { 6.7000  ,  2.2000},
             { 4.4000  ,  1.2000},
             { 4.5000  ,  1.6000},
             { 6.3000  ,  1.8000},
             {5.3000  ,   1.9000},
             { 1.5000  ,  0.2000},
             { 4.1000  ,  1.0000},
             { 4.7000  ,  1.4000},
             { 4.0000  ,  1.2000},
             { 1.4000  ,  0.2000},
             { 1.6000  ,  0.2000},
             { 4.9000  ,  1.5000},
             { 1.9000  ,  0.4000},
             { 5.6000  ,  2.4000},
             { 1.4000  ,  0.2000},
             { 1.2000  ,  0.2000},
             { 5.6000  ,  2.2000},
             { 4.9000  ,  1.8000},
             { 4.7000  ,  1.5000},
             { 4.1000  ,  1.3000},
             { 1.6000  ,  0.2000},
             { 4.9000  ,  1.5000},
             { 4.2000  ,  1.5000},
             { 5.8000  ,  1.6000},
             {5.1000  ,   2.3000},
             { 4.6000  ,  1.3000}};

float T[]={1,1,1,0,1,0,1,1,1,1,1,1,0,0,0,0,1,1,0,1,1,1,1,1,1,1,1,1,0,0,0,0,1,0,1,0,1,0,1,1,
           1,0,1,1,1,1,0,1,1,1,1,1,1,1,0,1,0,1,1,0,1,1,0,0,1,1,0,1,0,1,0,1,1,1,0,1,1,1,0,1,0,
           0,1,1,0,1,1,0,0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,0,0,1,0,1,0,0,1,1,1,1,0,1,1,1,1,1};
      for (byte j = 0; j<2 ; j++){
        for (byte i = 0; i<120 ; i++)
         {
         XI.WriteArray_2D(i,j,I[i][j]);
         }
         }
 
      for (int i = 0; i<120 ; i++)
     {    
      D.WriteArray_2D(0,i,T[i]);
     }
        /// FIN DATOS///
 
     //Columnas de XI   
    byte XC=XI.GetColumns();
    //Filas de D 
    byte DF=D.GetRows();
    //Filas de XI
    byte XF=XI.GetRows();
    
    //Centrado de datos
    net.DATA_CENTERING(XI);
    // Normalizado de datos
    net.NORMALIZE_DATA(XI,XI.MaxArray_2D());
    // Definomos numero de capas
    byte L=3;
    // Creamos e inicializamos el array donde se guardara
    // la estructura de la red
    EST.NewArray_2D(1,L);
    EST.ToinitializeArray_2D(0.0);
    float NC=EST.GetColumns();
    //Vector que contiene la estructura de la red
    float V[]={XC,12,DF};
    //Copiamos el vector al array
    for (byte i = 0; i < NC; i++)
    {
     EST.WriteArray_2D(0,i,V[i]);
     }
    //creamos array tipo string para pasar
    //las funciones de activacion
    FAC_STR.CharNewArray_1D(L); 
    //Vector que contine las funciones de activacion
    char *myStrings[] = {"tansig", "tansig","hardlim"};
    //Copiamos el vector al array
    for (byte i = 0; i < FAC_STR.GetRows(); i++)
    {
      FAC_STR.CharWriteArray_1D(i,myStrings[i]);
     }
  //Creamos la red   
 net.FEED_FORWARD_NET(EST,FAC_STR);
 //Entrenamos la red
 net.TRAIN_NET(XI,D,1e-3,1200);
 //Simulamos la red
 net.SIM_NET(XI);

  //Imprimimos por serial la salida de la red y la salida deseada
  byte correct=0;
   for (byte i = 0; i<XF ; i++)
   {
    if(D.ReadArray_2D(0,i)==round(net.Ysim.ReadArray_2D(0,i)))
      {correct=correct+1;}
     Serial.print(D.ReadArray_2D(0,i),4);
     Serial.print(" ");
     Serial.println(round(net.Ysim.ReadArray_2D(0,i)),4);
   }
   float resultado=correct*100.00/XF;
 Serial.print("porcentaje de clasificacion:" ); 
 Serial.println(resultado);   
// imprimimos epochs y error cudratico medio 
 Serial.println(net.EPOCHS);
 Serial.println(net.ERROR_C_M,6);

}
void loop() {

}
