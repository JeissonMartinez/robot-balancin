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
   XI.NewArray_2D(68,4);
   XI.ToinitializeArray_2D(0.0);
   //Crear e inicializar el array de salidas deseadas
   D.NewArray_2D(2,68);
   D.ToinitializeArray_2D(0.0);
   
   ///Creamos los datos para entrenar la RED//
      for (int i = 0; i<68 ; i++)
      {
      XI.WriteArray_2D(i,2,i*0.1);
      XI.WriteArray_2D(i,3,3.0*sin(i*0.1)+5.0);
      }
      
      for (int i = 1; i<69 ; i++)
     {    
       XI.WriteArray_2D(i-1,0,i*0.1);
       XI.WriteArray_2D(i-1,1,3.0*sin(i*0.1)+5.0);
       D.WriteArray_2D(0,i-1,3.0*sin(i*0.1)+5.0);
       D.WriteArray_2D(1,i-1,3.0*cos(i*0.1)+5.0);
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
    float V[]={XC,8,DF};
    //Copiamos el vector al array
    for (byte i = 0; i < NC; i++)
    {
     EST.WriteArray_2D(0,i,V[i]);
     }
    //creamos array tipo string para pasar
    //las funciones de activacion
    FAC_STR.CharNewArray_1D(L); 
    //Vector que contine las funciones de activacion
    char *myStrings[] = {"tansig", "tansig","purelin"};
    //Copiamos el vector al array
    for (byte i = 0; i < FAC_STR.GetRows(); i++)
    {
      FAC_STR.CharWriteArray_1D(i,myStrings[i]);
     }
  //Creamos la red   
 net.FEED_FORWARD_NET(EST,FAC_STR);
 //Entrenamos la red
 net.TRAIN_NET(XI,D,1e-3,5000);
 //Simulamos la red
 net.SIM_NET(XI);

  //Imprimimos por serial la salida de la red y la salida deseada
   for (byte i = 0; i<XF ; i++)
{
  Serial.print(D.ReadArray_2D(0,i),4);
  Serial.print(" ");
  Serial.print(net.Ysim.ReadArray_2D(0,i),4);
  Serial.print(" ");
  Serial.print(D.ReadArray_2D(1,i),4);
  Serial.print(" ");
  Serial.println(net.Ysim.ReadArray_2D(1,i),4);
}
// imprimimos epochs y error cudratico medio 
//Serial.println(net.EPOCHS);
//Serial.println(net.ERROR_C_M);

}
void loop() {

}
