//Implementacion de un vector dinamico en arduino
//Incluimos la libreria desarrollada
#include<Dynamic_Array.h>
//Creamos el constructor, este no recibe ningun parametro
Dynamic_Array W=Dynamic_Array();
void setup() {
   Serial.begin(9600);
   //Cosultamos la capacidad de memoria SRAM Disponible en bytes
   int cap=W.freeRam();
   //Imprimimos la capacidad de memoria SRAM Disponible en bytes
   Serial.print("Capacidad de memoria SRAM Disponible: ");
   Serial.println(cap);
   //Creamos un nuevo arreglo unidimensional de 10 posiciones [0  2 3...9]
   W.NewArray_1D(10);
   //Cosultamos e imprimimos nuevamente la capacidad de memoria
   //para calcular la memoria consumida por el arreglo creado
   int cap_array=cap-W.freeRam();
   //SRAM Disponible en bytes para calcular la memoria consumida por el arreglo creado
   Serial.print("Capacidad de memoria consumida por el array: ");
   Serial.println(cap_array);
   // Escribimos en las 10 posicines del array creado
   for (double i = 0; i < W.GetRows(); i++)
   {
    W.WriteArray_1D(i,i*i);
    }
    //Leemos e imprimimos el dato guardado el la ultima posicion del array
    Serial.print("Dato guardado el la ultima posicion del array:  ");
    Serial.println(W.ReadArray_1D(9));
    //Imprimimos el array por puerto serial
    Serial.println("Datos del array:  ");
     W.PrintArray_1D();
     //Leemos e imprimimos el numero de filas del array
    Serial.print("Numero de filas del array:  ");
    Serial.println(W.GetRows());
    //Leemos e imprimimos el numero de columnas del array
    Serial.print("Numero de columnas del array:  ");
    Serial.println(W.GetColumns());
    //Leemos e imprimimos la profundidad del array 
    //(para array de 3D)
    Serial.print("Profundidad del array:  ");
    Serial.println(W.GetDepth());
    //eliminamos el arrray para liberar memoria
    W.DeleteArray_1D();
    //Cosultamos e imprimimos la capacidad de memoria SRAM Disponible en bytes
    //Para verificar que la memoria se libero correctamente
    Serial.print("Memoria SRAM Disponible despues de eliminar el array :");
    Serial.println(W.freeRam());
  }

void loop() {
  

}
